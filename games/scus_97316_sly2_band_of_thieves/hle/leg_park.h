// games/scus_97316_sly2_band_of_thieves/hle/leg_park.h -- LegPark: the leg closer and the leg-wide counters
// every exit path of the title's play composition reads: LegCloser (run.meta park= / park_pc= /
// park_tid= / park_detail= / unhandled_mmio= + the `census:` block, written exactly once), the boundary
// count, the wall clock at every 100th boundary (run.meta boundary_wall_ms= -- the frame rate of a WINDOW
// of the run, where wall_ms / frames averages the FMV, the loads and the level together), the shutdown's
// step census and the abandoned guest workers, the NATIVEEE_VU1_CAP probe (hle/vu1_probe.h; reads
// `boundaries` at every kick), the main thread's context pointer for the watchdog's park_pc, and the
// thread census (every guest thread: status, what it waits on, priority, entry, live ra/pc -- racy reads
// of live contexts; a diagnostic. st: 1 RUN 2 READY 4 WAIT 8 SUSPEND 16 DORMANT; wait: 1 sleep 2 sema 4
// event flag).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include "app/host/leg_closer.h"
#include "engine/core/verify/verify_host.h"
#include "engine/ee/cpu/cpu_state.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_devices.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/vu1_probe.h"

namespace ps2r::title::sly2 {

class LegPark {
public:
    LegPark(ps2r::verify::VerifyHost& host, const LegKnobs& knobs, LegCore& core, LegDevices& devices, LegMtvu& mtvu);
    LegPark(const LegPark&) = delete;
    LegPark& operator=(const LegPark&) = delete;

    // run.meta threads=: the guest thread table as described above.
    static std::string ThreadCensus();
    // Appends the wall clock since the leg's cold boot to boundary_wall_ms at every 100th boundary.
    void NoteBoundaryWall();

    LegCloser closer;
    uint64_t boundaries = 0;
    const std::chrono::steady_clock::time_point leg_t0;
    std::string boundary_wall;
    std::string shutdown_steps, guest_join_abandoned;   // run.meta shutdown= ("" = no ladder ran / none abandoned)
    Vu1Probe vu1_probe;
    std::atomic<CpuContext*> main_ctx{nullptr};
};

}  // namespace ps2r::title::sly2
