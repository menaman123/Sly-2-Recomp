// games/scus_97316_sly2_band_of_thieves/hle/leg_iop.cpp -- LegIop (leg_iop.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_iop.h"

#include <cstdio>

#include "engine/core/threads/park.h"
#include "engine/gif/gif_arbiter.h"

#include <utility>

#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/hle/alarms.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/sif/hle/sce_sif.h"
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

// The embedded modules' expected SHA-256 (TITLE_INPUTS rows, via TitleInputs.cmake -> hle/CMakeLists.txt
// compile definitions): the host refuses a pushed image that is not the one the build recompiled.
IopHost::ExpectedHashes ExpectedIopHashes() {
    IopHost::ExpectedHashes h;
    h.fileio = PS2R_SLY2_SHA_FILEIO;
    h.lgaud = PS2R_SLY2_SHA_LGAUD;
    h.snd989 = PS2R_SLY2_SHA_SND989;
    h.spiop = PS2R_SLY2_SHA_SPIOP;
    return h;
}

}  // namespace

LegIop::LegIop(const ps2r::Config& cfg, LegCore& core, LegDevices& devices, LegMtvu& mtvu, LegInterrupts& interrupts,
               LegDisc& disc, LegServices& services)
    : iop_host(cfg, core.ram, services.rpc_router, services.spu_core, disc.Reader(), ExpectedIopHashes()) {
    SyscallTable::Instance().SetSifDmaCopy([this](uint32_t ee_src, uint32_t iop_dst, uint32_t size) {
        iop_host.SifDmaCopy(ee_src, iop_dst, size);
    });
    // The padman face follows the padman the title loads.
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
    core.sequencer.SetStep(BoundarySequencer::Step::IopDispatch, [this](CpuContext& ctx) {
        PumpFieldQuantum(ctx);   // the boundary's own field; the Verify slot's field pump runs the catch-up fields' quanta
        return true;
    });
    // The SIF command path IOP -> EE: a hosted module's sceSifSendCmd lands in libkernl's command
    // buffer (the title's sceSifInitCmd static) and runs the handler the EE registered with
    // sceSifAddCmdHandler, as a guest call on the EE's context.
    SceSif::SetCmdBuffer(SceSif::CmdBuffer{IopModules::kEeSifCmdBuffer, IopModules::kEeSifCmdSlots, IopModules::kEeSifCmdSlotBytes});
    SceSif::SetGuestCall([](uint32_t addr, CpuContext& ctx) { DispatchTable::Invoke(addr, ctx); });
    core.sequencer.SetStep(BoundarySequencer::Step::EeStreamRpc, [this](CpuContext&) {
        iop_host.Table().PumpAfterEeService();
        return true;
    });
    // The back-edge yield.
    {
        Dmac& dmac = devices.dmac;
        std::unique_ptr<MtvuStack>& mtvu_stack = mtvu.mtvu;
        EeYieldPoint::Deps yd;
        yd.advance_field = [hook = core.protocol.WaitHook()] { hook(0); };
        // The wall vblank landing on a running thread (E421): under Play a pacer tick nobody has
        // fired yet readies the vsync thread at the holder's next syscall entry (leg_interrupts).
        yd.wall_field_due = [&protocol = core.protocol] { return protocol.WallFieldDue(); };
        yd.domain = &core.domain;
        yd.alarms = &Alarms::Instance();
        yd.timers = &core.timers;
        yd.intc = &core.intc;
        // The DMAC's INT1 handlers too: a guest loop spinning on a counter its channel-3
        // completion handler bumps (libmpeg's CSC chunks) has no kernel call to deliver it at.
        yd.dispatch_dmac = [&dmac, &interrupts](CpuContext& ctx) {
            if (!dmac.CheckInterrupt()) return;
            interrupts.InstallDmacHandlers();
            dmac.DispatchInterrupt(ctx);
        };
        yd.queue = &EeEventQueue::Instance();
        yd.inhibit = [&sequencer = core.sequencer] { return sequencer.InFlight() != 0; };
        // The idle runner never passes a field over GS deliveries in flight (the worker's spans,
        // the drain thread's parser): it parks on their progress as the pre-dispatch's idle wait does.
        yd.pipeline_wait = [&mtvu_stack] { return mtvu_stack && !mtvu_stack->sync().WaitProgress(); };
        yd.pump_devices = [this, &mtvu_stack](CpuContext& ctx) {
            // The IOP's threads run on their own clock while the EE spins (an RPC status
            // poll into a hosted module needs the module to progress); its SIF commands land.
            iop_host.Table().PumpFromParkLoop();
            SceSif::DeliverPendingCommands(ctx);
            // A guest loop spinning on a GS flag the worker's packets set (CSR.FINISH, a
            // SIGNAL) has no kernel call to drain at: the sealed prefix lands here.
            if (mtvu_stack) mtvu_stack->sync().TryDrainPending();
        };
        yd.budget = kYieldBudget;
        EeYieldPoint::Instance().Bind(std::move(yd));
    }
}

void LegIop::PumpFieldQuantum(CpuContext& ctx) {
    iop_host.Table().PumpAtBoundary();
    SceSif::DeliverPendingCommands(ctx);   // SIF commands the IOP sent since the last quantum
}

}  // namespace ps2r::title::sly2
