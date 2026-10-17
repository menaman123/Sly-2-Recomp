// games/scus_97316_sly2_band_of_thieves/hle/leg_watchdog.h -- LegWatchdog: a guest that never reaches a
// boundary (a raw SIF poll with no IOP, an unmodelled wait) would otherwise only end at the bounded
// runner's wall timeout with no run.meta park row. After NATIVEEE_WATCHDOG_S seconds (default 60; 0 =
// off) without a boundary the leg closes as park=watchdog with the main thread's pc -- the FINDING of a
// boot stage is exactly where the fallbacks stop carrying the title -- plus watchdog_ra / _sp / _a0, the
// thread census and the run token's owner; NATIVEEE_RAM_DUMP=<file> writes the 32 MB guest RAM image at
// the park (boot triage only; racy against still-running guest threads, which is fine for a parked
// title). The thread polls every 200 ms while the leg runs; Join() after the cold boot returned.
#pragma once

#include <thread>

#include "engine/core/verify/verify_host.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_census.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_park.h"

namespace ps2r::title::sly2 {

class LegWatchdog {
public:
    LegWatchdog(ps2r::verify::VerifyHost& host, const LegKnobs& knobs, LegCore& core, LegPark& park, LegCensus& census);
    LegWatchdog(const LegWatchdog&) = delete;
    LegWatchdog& operator=(const LegWatchdog&) = delete;

    void Join();

private:
    void Run(ps2r::verify::VerifyHost& host, const LegKnobs& knobs, LegCore& core, LegPark& park, LegCensus& census);

    std::thread thread_;
};

}  // namespace ps2r::title::sly2
