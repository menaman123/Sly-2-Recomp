// LegWatchdog: after NATIVEEE_WATCHDOG_S seconds (default 60; 0 = off) with no boundary (e.g. an unmodelled
// wait), closes the run as park=watchdog with the main thread's pc, ra, sp, a0, the thread table and the
// run token's owner, so a hang still leaves a park row. NATIVEEE_RAM_DUMP=<file> also writes the 32 MB guest
// RAM (racy against still-running threads). Polls every 200 ms.
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
