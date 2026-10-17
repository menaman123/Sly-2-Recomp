// games/scus_97316_sly2_band_of_thieves/hle/leg_core.cpp -- LegCore (leg_core.h): memory + MMIO, the time
// domain, INTC + timers, the boundary and the kernel seams, in the single-file composition's order. The
// members' constructors run first (every one of them is a plain binding of references / defaults), then
// the arming sequence below in the order the single-file leg ran it.
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"

#include <cstdio>

#include "engine/core/config/config_loader.h"
#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/hle/alarms.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/gs/hle/sce/gs_sync.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_text.h"

namespace ps2r::title::sly2 {

LegCore::LegCore(const ps2r::Config& cfg, const LegKnobs& knobs)
    : map(ram, spad),
      mmio(sink),
      fast_mem(ram, map, mmio),
      intc(Intc::Instance()),
      clocks(domain),
      timers(clocks, intc),
      play_mode(!cfg.harness.headless && !cfg.time.tick_sync),
      dump_frames_dir(ps2r::ConfigLoader::Request().dump_frames_dir),
      want_video(play_mode || !dump_frames_dir.empty()),
      game_boundary(ThreadManager::Instance(), sequencer),
      protocol(tick_source, sequencer.Ticks()) {
    // --- memory + MMIO (the sink answers every unmodelled page idle; INTC/timers/SIF displace it) --
    RecompAbi::BindMemory(&fast_mem);
    // NATIVEEE_WATCH=<hexaddr>,<bytes>[;...][;max=<n>][;val=<hex>]: print every EE store overlapping
    // the guest ranges (store_watch.h; boot triage, non-NDEBUG builds).
    // The `text` mode of the knob is TextWatch's (text_watch.h; armed after the ELF load in RunLeg).
    store_watch.Install(knobs.watch && !TextWatch::IsTextSpec(knobs.watch->c_str()) ? knobs.watch->c_str() : nullptr);

    // --- time domain, boundary owner, INTC + timers, SIF registers -------------------------------
    intc.Register(mmio);
    if (cfg.time.tick_deterministic) domain.SetRegimePin(1);
    clocks.SetRtcFixed(cfg.game.rtc_fixed);
    SetLibscfClocks(&clocks);
    timers.Register(mmio);
    sif_state.Register(mmio);
    timers.SetCtxProvider([] {
        return ThreadManager::Instance().Current() ? ThreadManager::Instance().Current()->ctx : nullptr;
    });
    tick_source.SetPolicy(cfg.time.tick_deterministic ? TickSource::Policy::Deterministic : TickSource::Policy::Play);
    tick_source.SetMode(cfg.harness.headless ? TickSource::Mode::Freerun : TickSource::Mode::Realtime);

    // --- the boundary: sequencer + the game-thread boundary owner + the Phase-3 steps ------------
    attached_ = game_boundary.Attached();
    if (!attached_) {
        std::fprintf(stderr, "nativeee: the game boundary did not attach\n");
        return;
    }
    BoundarySequencer::Phase3Deps deps;
    deps.source = &tick_source;
    deps.domain = &domain;
    deps.rendezvous = &rendezvous;
    deps.intc = &intc;
    deps.alarms = &Alarms::Instance();
    deps.timers = &timers;
    deps.queue = &EeEventQueue::Instance();
    deps.running = &running;
    sequencer.InstallPhase3Steps(deps);   // slots 0-3: rendezvous ticket, tick advance, event drain
    protocol.SetRunahead(static_cast<uint32_t>(cfg.engine.ee_runahead));

    // --- kernel clocks + seams --------------------------------------------------------------------
    clocks.InstallKernelClocks(SyscallTable::Instance(), protocol.WaitHook());
    GsSync::Bind(&sequencer, &clocks);
    guest_call = [this](uint32_t addr, CpuContext& ctx) {
        if (DispatchTable::IdOf(addr) >= DispatchTable::FunctionCount()) {
            // Not a generated function start: Invoke runs nothing. Count + list (the SDK's libkernl kernel
            // patches do this at boot: syscall 0x5b -> 0x8007[456]000; see the title README, overlays).
            if (guest_call_unknown++ < 16) {
                std::lock_guard<std::mutex> lock(guest_call_unknown_mutex);
                guest_call_unknown_list += (guest_call_unknown_list.empty() ? "" : ",") + Hex(addr) + "@ra" +
                                           Hex(ctx.cpuRegs.GPR.r[31].UL[0]);
            }
        }
        DispatchTable::Invoke(addr, ctx);
        return true;
    };
    SyscallTable::Instance().SetGuestCall(guest_call);
    SyscallTable::Instance().SetTrace([this](int num, const char* name) {
        constexpr uint64_t kKeep = 40;
        const std::string entry = std::string(name ? name : "?") + "(" + std::to_string(num) + ")";
        if (syscall_count++ < kKeep) first_syscalls += (first_syscalls.empty() ? "" : ",") + entry;
        if (last_syscalls.size() >= 24) last_syscalls.pop_front();
        last_syscalls.push_back(entry);
    });
    SyscallTable::Instance().BindIntcMask(intc.MaskWord());
    intc.SetGuestCall(guest_call);
    ThreadManager::Instance().InstallEventApplier();
    ThreadManager::Instance().SetGuestBody([](int, CpuContext& ctx) { DispatchTable::Invoke(ctx.cpuRegs.pc, ctx); });
}

}  // namespace ps2r::title::sly2
