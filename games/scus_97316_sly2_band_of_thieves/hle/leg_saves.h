// LegSaves: save states.
//
// Sections, in registration order (which is also restore order): RAM0/SPAD, GCLK, VUMM, THRD, SEMA/EVFL,
// INTC, DMAC, TIMR, VIFS, GSST/VRAM/GSPR, GIFS, PADS, VU1R, IOPH, SDRX/SNDC, MCHL. IOPH (the hosted IOP)
// comes before the SPU sections so the saved SPU image wins over anything a re-armed IOP thread touches.
// Restoring IOPH re-runs each hosted IOP thread to its first park on the restored RAM and re-registers
// the RPC servers.
//
//   F5            capture on the game thread at a completed boundary, once the MTVU worker and GS queue
//                 are quiet; writes <save root>/savestates/slot<N>.ps2st and .meta.
//   F8            live restore: preflight, unwind and join the guest workers, restore every section in
//                 this process, relaunch the threads from their continuations.
//   --load-state  cold restore: boot everything, then before the first guest instruction restore the
//                 slot and relaunch its threads in place of the ELF entry.
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

    // Binds the F5 / F8 hooks; must run before SessionRuntime::SetHooks. Play mode only.
    void Bind(ps2r::app::SessionRuntime::Hooks& hooks, ps2r::app::SessionWorkers& workers, TickThread& tick_thread,
              LegAudio& audio);
    // --load-state, on the game thread before any guest instruction. Returns 0, or 2 if the preflight
    // refused (nothing has been touched yet).
    struct ColdDeps {
        ps2r::app::SessionWorkers& workers;
        LegAudio& audio;
        LegPark& park;
        LegCensus& census;
        std::function<void()> join_ladder;   // headless: run after the workers end
    };
    int ColdBoot(const std::string& path, const ColdDeps& deps);
    void JoinWriter();
    // run.meta rows: savestate_capture=, f8=, restore_*=.
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
    std::unique_ptr<ps2r::app::ColdRestore> cold_restore;   // outlives the relaunched workers
    uint64_t dmac_save_unquiesced = 0, gifs_restore_tail_bytes = 0;
    std::atomic<uint64_t> restores{0};
    uint32_t restored_vsync_frame = 0;
    std::string restore_sections;   // section tags of the last restored image, in file order
    std::string restore_route;      // "live" | "cold"
    std::string restore_mc_bound;   // cold restore: libmc readiness read from the image
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
