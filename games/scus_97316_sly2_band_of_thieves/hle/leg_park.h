// LegPark: LegCloser (run.meta park=, park_pc=, park_tid=, park_detail=, unhandled_mmio= and the
// `census:` block, written once) and the counters every exit path reads: boundaries, the wall clock every
// 100th boundary, the shutdown steps, abandoned workers, the NATIVEEE_VU1_CAP probe and the thread table.
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

    // run.meta threads=: every guest thread's status, wait object, priority, entry and live ra/pc (racy
    // reads; diagnostic only). st: 1 RUN 2 READY 4 WAIT 8 SUSPEND 16 DORMANT; wait: 1 sleep 2 sema 4 flag.
    static std::string ThreadCensus();
    // Every 100th boundary, appends the wall clock since boot to boundary_wall_ms (per-window frame rate).
    void NoteBoundaryWall();

    LegCloser closer;
    uint64_t boundaries = 0;
    // Completed-boundary callbacks (play). Fewer than `boundaries` means some completions never ran.
    uint64_t boundary_consumer_calls = 0;
    const std::chrono::steady_clock::time_point leg_t0;
    std::string boundary_wall;
    std::string shutdown_steps, guest_join_abandoned;   // run.meta shutdown=
    Vu1Probe vu1_probe;
    std::atomic<CpuContext*> main_ctx{nullptr};
};

}  // namespace ps2r::title::sly2
