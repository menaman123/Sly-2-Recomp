// games/scus_97316_sly2_band_of_thieves/hle/leg_saves.h -- LegSaves: the save states (F5 / F8) of the
// title's play composition, over the engine's title-neutral machinery. The section registry
// (StateRestorer over MemorySections: RAM0 / SPAD) carries, in registration = restore order (the first
// title's order, private app/host/ee_bootstrap_sections.cpp RegisterSections): GCLK (SessionClock),
// VUMM (VU0/VU1 memories; a restored VU1 is mirrored into the MTVU worker's unit), THRD (threads +
// parked contexts + shadow chains), SEMA/EVFL, INTC, DMAC (quiesced: the deferred VIF1 kick flushed at
// capture), TIMR, VIFS, GSST/VRAM/GSPR (the VRAM read through the pipelined queue's Sync when the image
// is split), GIFS, PADS, VU1R, SDRX/SNDC (SPU2 voices + block transfers), MCHL (libmc pending +
// card-seen). The IOP (the hosted lgAud / 989snd / spiop on the cooperative kernel, their running
// coroutines and the SIF RPC servers) has NO section in this composition: a restore rewinds the EE while
// the IOP keeps its live state (run.meta f8=live,iop=retained) -- what the first title's StartupOwnership
// contract refused by relaunch; here the live route is taken knowingly and the result is judged by the
// frame compare (saves_notes.md).
//   F5 (SessionCapture, E234): the capture runs on the completed game owner inside the GameBoundary
//   consumer (GameBoundary::FreshThreads sees only that scope -- the session's Boundary() moved from the
//   Verify slot into that consumer), admitted by a Quiescence of owners (the MTVU worker, the GS queue's
//   Sync); the writer publishes <save root>/savestates/slot<save.slot>.ps2st + .meta.
//   F8 (SessionRestore, the LIVE route): preflight on the game owner (the slot's sidecar composition, the
//   registry, the clock, THRD's boundary owner), unwind + join the guest workers, the display's memory
//   forgotten (the consumer's last frame), every section restored on the live composition, the guest
//   threads relaunched from their continuations (DispatchTable::InvokeAt). run.meta restore rows:
//   restore_mhash=, restored_vsync_frame=, restore_route=, restore_sections=.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

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
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_play.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_services.h"

namespace ps2r::title::sly2 {

class LegSaves {
public:
    LegSaves(const ps2r::Config& cfg, ps2r::verify::VerifyHost& host, const TitleProfile& title, LegCore& core, LegGs& gs,
             LegDevices& devices, LegMtvu& mtvu, LegServices& services, LegPlay& play, const char* build_id);
    LegSaves(const LegSaves&) = delete;
    LegSaves& operator=(const LegSaves&) = delete;

    // The Verify slot's owner calls this BEFORE SessionRuntime::SetHooks: the capture / restore hooks
    // (F5 / F8) are bound here with the guest workers and the ticker the restore has to stop / restart.
    // Play mode with a session only; a no-op otherwise.
    void Bind(ps2r::app::SessionRuntime::Hooks& hooks, ps2r::app::SessionWorkers& workers, TickThread& tick_thread,
              LegAudio& audio);
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
    uint64_t dmac_save_unquiesced = 0, gifs_restore_tail_bytes = 0;
    std::atomic<uint64_t> restores{0};
    uint32_t restored_vsync_frame = 0;
    std::string restore_sections;   // the tags of the last restored image, file order
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
    LegPlay& play_;
    std::string build_id_;
};

}  // namespace ps2r::title::sly2
