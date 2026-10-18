// games/scus_97316_sly2_band_of_thieves/hle/leg_saves.h -- LegSaves: the save states (F5 / F8) of the
// title's play composition, over the engine's title-neutral machinery. The section registry
// (StateRestorer over MemorySections: RAM0 / SPAD) carries, in registration = restore order (the first
// title's order, private app/host/ee_bootstrap_sections.cpp RegisterSections): GCLK (SessionClock),
// VUMM (VU0/VU1 memories; a restored VU1 is mirrored into the MTVU worker's unit), THRD (threads +
// parked contexts + shadow chains), SEMA/EVFL, INTC, DMAC (quiesced: the deferred VIF1 kick flushed at
// capture), TIMR, VIFS, GSST/VRAM/GSPR (the VRAM read through the pipelined queue's Sync when the image
// is split), GIFS, PADS, VU1R, IOPH (the hosted IOP: RAM, the kernel tables, the module rows / libraries /
// RPC servers / timers of lgAud, 989snd, spiop and FILEIO -- engine/iop/savestate/iop_host_section.h,
// registered BEFORE the SPU sections so the restored SPU image wins over anything a re-armed IOP thread's
// entry touched), SDRX/SNDC (SPU2 voices + block transfers), MCHL (libmc pending + card-seen). A restore
// re-arms every hosted IOP thread from its record: a fresh coroutine runs the module's thread entry to its
// first park on the restored RAM (the Crash contract), the RPC servers are re-registered on the RpcRouter,
// the hard timer re-armed (run.meta f8=live,iop=section).
//   F5 (SessionCapture, E234): the capture runs on the completed game owner inside the GameBoundary
//   consumer (GameBoundary::FreshThreads sees only that scope -- the session's Boundary() moved from the
//   Verify slot into that consumer), admitted by a Quiescence of owners (the MTVU worker, the GS queue's
//   Sync); the writer publishes <save root>/savestates/slot<save.slot>.ps2st + .meta.
//   F8 (SessionRestore, the LIVE route): preflight on the game owner (the slot's sidecar composition, the
//   registry, the clock, THRD's boundary owner), unwind + join the guest workers, the display's memory
//   forgotten (the consumer's last frame), every section restored on the live composition, the guest
//   threads relaunched from their continuations (DispatchTable::InvokeAt). run.meta restore rows:
//   restore_mhash=, restored_vsync_frame=, restore_route=, restore_sections=.
//   --load-state <slot.ps2st> (ColdBoot, the COLD route, app/host/cold_restore.h): the composition boots
//   and arms everything, then -- before the first guest instruction -- the slot is preflighted (registry,
//   sidecar, THRD boundary owner), every section restored in registration order, and the THRD records
//   relaunched (ThreadRelauncher) in place of the ELF entry; the session's supervisor keeps the window
//   while the boundary owner lives; headless, the relaunched workers are joined. restore_route=cold.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "app/host/cold_restore.h"
#include "app/host/live_restore_gate.h"
#include "app/host/session_capture.h"
#include "app/host/session_clock.h"
#include "app/host/session_files.h"
#include "app/host/session_restore.h"
#include "app/host/session_runtime.h"
#include "app/host/session_workers.h"
#include "app/host/tick_thread.h"
#include "engine/core/config/config.h"
#include "engine/core/verify/verify_host.h"
#include "engine/ee/savestate/intc_section.h"
#include "engine/ee/savestate/kernel_sections.h"
#include "engine/ee/savestate/memory_sections.h"
#include "engine/ee/savestate/state_restorer.h"
#include "engine/ee/savestate/thread_section.h"
#include "engine/ee/savestate/timer_section.h"
#include "engine/gif/savestate/gifs_section.h"
#include "engine/gs/savestate/gs_sections.h"
#include "engine/mc/savestate/mchl_section.h"
#include "engine/pad/savestate/pads_section.h"
#include "engine/sound/sound_state_codec.h"
#include "engine/title/title_profile.h"
#include "engine/vif/savestate/vifs_section.h"
#include "engine/vu/savestate/vu1_register_section.h"
#include "engine/vu/savestate/vu_memory_section.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_audio.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_devices.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_gs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_iop.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_play.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_services.h"

namespace ps2r::title::sly2 {

class LegCensus;
class LegPark;

class LegSaves {
public:
    LegSaves(const ps2r::Config& cfg, ps2r::verify::VerifyHost& host, const TitleProfile& title, LegCore& core, LegGs& gs,
             LegDevices& devices, LegMtvu& mtvu, LegServices& services, LegIop& iop, LegPlay& play, const char* build_id);
    LegSaves(const LegSaves&) = delete;
    LegSaves& operator=(const LegSaves&) = delete;

    // The Verify slot's owner calls this BEFORE SessionRuntime::SetHooks: the capture / restore hooks
    // (F5 / F8) are bound here with the guest workers and the ticker the restore has to stop / restart.
    // Play mode with a session only; a no-op otherwise.
    void Bind(ps2r::app::SessionRuntime::Hooks& hooks, ps2r::app::SessionWorkers& workers, TickThread& tick_thread,
              LegAudio& audio);
    // The COLD route (--load-state): on the game owner's thread before any guest instruction -- the
    // session's worker in play (returns 0 and the supervisor keeps the session while the relaunched
    // boundary owner lives), main headless (the relaunched workers are joined here, then the ladder).
    // Returns the leg's result: 0 ran, 2 refused (nothing of the live composition touched when the
    // preflight refuses, so the leg ends through its destructors).
    struct ColdDeps {
        ps2r::app::SessionWorkers& workers;
        LegAudio& audio;
        LegPark& park;
        LegCensus& census;
        std::function<void()> join_ladder;   // headless: the Verify slot's ladder after the workers ended
    };
    int ColdBoot(const std::string& path, const ColdDeps& deps);
    // The supervisor's shutdown ladder step: the writer joined.
    void JoinWriter();
    // run.meta rows (LegCensus): savestate_capture=, f8=, restore_*=.
    void WriteCensus(ps2r::verify::RunMeta& meta);

    MemorySections memory_sections;
    StateRestorer restorer;
    ps2r::app::SessionClock session_clock;
    VuMemorySection vu_memory_section;
    ThreadSection thread_section;
    KernelSections kernel_sections;
    IntcSection intc_section;
    TimerSection timer_section;
    VifsSection vifs_section;
    ps2r::gs::GsSections gs_sections;
    GifsSection gifs_section;
    PadsSection pads_section;
    Vu1RegisterSection vu1_register_section;
    ps2r::sound::SoundStateCodec sound_state_codec;
    MchlSection mchl_section;
    ps2r::app::SessionFiles session_files;
    std::unique_ptr<ps2r::app::SessionCapture> capture;
    std::unique_ptr<ps2r::app::SessionRestore> restore;
    std::unique_ptr<ps2r::app::ColdRestore> cold_restore;   // the --load-state route's owner (outlives the relaunched workers)
    uint64_t dmac_save_unquiesced = 0, gifs_restore_tail_bytes = 0;
    std::atomic<uint64_t> restores{0};
    uint32_t restored_vsync_frame = 0;
    std::string restore_sections;   // the tags of the last restored image, file order
    std::string restore_route;      // "live" | "cold"
    std::string restore_mc_bound;   // cold: libmc readiness derived from the image (run.meta restore_mc_bound=)
    std::string f8_route = "none";
    ps2r::app::LiveRestoreGate::Census live_refusals;   // run.meta live_restore_refusals=
    ps2r::app::LiveRestoreGate::Probes live_probes;

private:
    const ps2r::Config& cfg_;
    ps2r::verify::VerifyHost& host_;
    LegCore& core_;
    LegGs& gs_;
    LegMtvu& mtvu_;
    LegServices& services_;
    LegIop& iop_;
    LegPlay& play_;
    const uint32_t title_mc_client_addr_;   // TitleProfile::mc.client_addr (0 = unknown)
    std::string build_id_;
};

}  // namespace ps2r::title::sly2
