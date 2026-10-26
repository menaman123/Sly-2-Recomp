#include "games/scus_97316_sly2_band_of_thieves/hle/leg_iop.h"

#include <cstdio>

#include "engine/core/threads/park.h"
#include "engine/gif/gif_arbiter.h"

#include <utility>

#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/hle/alarms.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/sif/hle/iop_rpc_server.h"
#include "engine/sif/hle/sce_sif.h"
#include "engine/vu/tiers/mtvu_sync.h"
#include "games/scus_97316_sly2_band_of_thieves/iop/iop_modules.h"

#ifndef PS2R_SLY2_SHA_FILEIO
#define PS2R_SLY2_SHA_FILEIO ""
#endif
#ifndef PS2R_SLY2_SHA_LGAUD
#define PS2R_SLY2_SHA_LGAUD ""
#endif
#ifndef PS2R_SLY2_SHA_SND989
#define PS2R_SLY2_SHA_SND989 ""
#endif
#ifndef PS2R_SLY2_SHA_SPIOP
#define PS2R_SLY2_SHA_SPIOP ""
#endif

namespace ps2r::title::sly2 {

namespace {

// The embedded modules' expected SHA-256 (from TITLE_INPUTS.sha256 via compile definitions): the host
// refuses a pushed image that is not the one the build recompiled.
IopHost::ExpectedHashes ExpectedIopHashes() {
    IopHost::ExpectedHashes h;
    h.fileio = PS2R_SLY2_SHA_FILEIO;
    h.lgaud = PS2R_SLY2_SHA_LGAUD;
    h.snd989 = PS2R_SLY2_SHA_SND989;
    h.spiop = PS2R_SLY2_SHA_SPIOP;
    return h;
}

}  // namespace

LegIop::LegIop(const ps2r::Config& cfg, const LegKnobs& knobs, LegCore& core, LegDevices& devices, LegMtvu& mtvu, LegInterrupts& interrupts,
               LegDisc& disc, LegServices& services)
    : iop_host(cfg, core.ram, services.rpc_router, services.spu_core, disc.Reader(), ExpectedIopHashes()) {
    SyscallTable::Instance().SetSifDmaCopy([this](uint32_t ee_src, uint32_t iop_dst, uint32_t size) {
        iop_host.SifDmaCopy(ee_src, iop_dst, size);
    });
    services.padman.SetModuleFacts([this] {
        PadmanModule::ModuleFacts f;
        if (const auto* m = iop_host.Table().FindModule("padman")) {
            f.version = static_cast<uint16_t>(m->version);
            if (m->text_addr) {
                f.act_sink = m->text_addr + m->text_size + m->data_size;
                f.act_sink_size = m->bss_size;
            }
        }
        return f;
    });
    if (knobs.rpc_trace) {
        if (FILE* f = std::fopen(knobs.rpc_trace->c_str(), "w")) {
            std::fprintf(f, "# rpc trace: iop_frame sid fno ssize send_hex -> reply rsize recv_hex (first 64 bytes each)\n");
            IopRpcServer::SetTracer([this, f](const IopRpcServer::TraceEvent& ev) {
                auto hex = [f](const uint8_t* p, uint32_t n) {
                    if (!p) { std::fputs("-", f); return; }
                    for (uint32_t i = 0; i < n && i < 64; ++i) std::fprintf(f, "%02x", p[i]);
                };
                std::fprintf(f, "%llu %08x %u %u ", (unsigned long long)iop_host.Table().Kernel().Clock().Frames(), ev.sid, ev.fno, ev.send_size);
                hex(ev.send, ev.send_size);
                std::fprintf(f, " -> %08x %u ", ev.reply, ev.recv_size);
                hex(ev.recv, ev.recv_size);
                std::fputc('\n', f);
            });
            iop_host.EeSink().SetTracer([this, f](uint32_t ee_addr, const uint8_t* src, size_t size) {
                std::fprintf(f, "%llu DMA> %08x %zu ", (unsigned long long)iop_host.Table().Kernel().Clock().Frames(), ee_addr, size);
                for (size_t i = 0; i < size && i < 64; ++i) std::fprintf(f, "%02x", src[i]);
                std::fputc('\n', f);
            });
        }
    }
    core.sequencer.SetStep(BoundarySequencer::Step::IopDispatch, [this](CpuContext& ctx) {
        PumpFieldQuantum(ctx);   // the boundary's own field; catch-up fields are pumped by the Verify slot
        return true;
    });
    // IOP -> EE: sceSifSendCmd lands in libkernl's command buffer and runs the EE's sceSifAddCmdHandler
    // handler as a guest call.
    SceSif::SetCmdBuffer(SceSif::CmdBuffer{IopModules::kEeSifCmdBuffer, IopModules::kEeSifCmdSlots, IopModules::kEeSifCmdSlotBytes});
    SceSif::SetGuestCall([](uint32_t addr, CpuContext& ctx) { DispatchTable::Invoke(addr, ctx); });
    core.sequencer.SetStep(BoundarySequencer::Step::EeStreamRpc, [this](CpuContext&) {
        iop_host.Table().PumpAfterEeService();
        return true;
    });
    {
        Dmac& dmac = devices.dmac;
        std::unique_ptr<MtvuStack>& mtvu_stack = mtvu.mtvu;
        EeYieldPoint::Deps yd;
        yd.advance_field = [hook = core.protocol.WaitHook()] { hook(0); };
        // A due pacer tick readies the vsync thread at the holder's next syscall entry.
        yd.wall_field_due = [&protocol = core.protocol] { return protocol.WallFieldDue(); };
        yd.domain = &core.domain;
        yd.alarms = &Alarms::Instance();
        yd.timers = &core.timers;
        yd.intc = &core.intc;
        // DMAC INT1 handlers too: libmpeg spins on a counter its channel-3 completion handler bumps.
        yd.dispatch_dmac = [&dmac, &interrupts](CpuContext& ctx) {
            if (!dmac.CheckInterrupt()) return;
            interrupts.InstallDmacHandlers();
            dmac.DispatchInterrupt(ctx);
        };
        yd.queue = &EeEventQueue::Instance();
        yd.inhibit = [&sequencer = core.sequencer] { return sequencer.InFlight() != 0; };
        // Never pass a field while GS deliveries are in flight: wait on their progress instead.
        yd.pipeline_wait = [&mtvu_stack] { return mtvu_stack && !mtvu_stack->sync().WaitProgress(WaitCause::IdleWaitField); };
        if (knobs.idle_trace) {
            yd.on_idle_field = [](uint64_t tick, int tid) {
                std::fprintf(stderr, "idle-trace: field at tick %llu on tid %d:", (unsigned long long)tick, tid);
                for (int t = 0; t < 32; ++t) {
                    ThreadStatus st;
                    if (ThreadManager::Instance().ReferThreadStatus(t, st) < 0 || st.status == THS_DORMANT) continue;
                    std::fprintf(stderr, " tid%d{st=%d prio=%d wait=%u id=%u}", t, st.status, st.current_priority, st.wait_type, st.wait_id);
                }
                std::fprintf(stderr, "\n");
            };
        }
        yd.pump_devices = [this, &mtvu_stack](CpuContext& ctx) {
            // The IOP keeps running while the EE spins (an RPC status poll needs the module to progress).
            iop_host.Table().PumpFromParkLoop();
            SceSif::DeliverPendingCommands(ctx);
            // A loop spinning on a GS flag (CSR.FINISH, SIGNAL) has no kernel call to drain at: drain here.
            if (mtvu_stack) mtvu_stack->sync().TryDrainPending();
        };
        yd.budget = kYieldBudget;
        // engine.ee_yield_spin_gate: a trip advances a field only if its thread truly spun (no token
        // acquire and no boundary since the last trip); otherwise it fires only fields the pacer already
        // noted. Census ee_yield= spin_trips/sched_trips/due_fields.
        yd.spin_gate = knobs.yield_spin_gate_set ? knobs.yield_spin_gate_on : cfg.engine.ee_yield_spin_gate;
        yd.fire_due_fields = core.protocol.FireDueFieldsHook();
        EeYieldPoint::Instance().Bind(std::move(yd));
    }
}

void LegIop::PumpFieldQuantum(CpuContext& ctx) {
    iop_host.Table().PumpAtBoundary();
    SceSif::DeliverPendingCommands(ctx);   // SIF commands the IOP sent since the last quantum
}

}  // namespace ps2r::title::sly2
