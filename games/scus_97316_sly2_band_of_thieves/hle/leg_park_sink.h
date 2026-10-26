// LegParkSink: the terminate hook (an uncaught exception: census, then park=exception) and the GuestPark
// sink (an unmodelled wait, unresolved import or unrecompiled pc: census, then park=<reason>). In play the
// session closes cleanly around a park; headless, the park ends the run.
//
// Then boots the IOP reboot image's FILEIO module from IOPRP300.IMG. That has to happen after the sink
// exists, so an unresolved import in it parks visibly.
#pragma once

#include "engine/core/triage/triage_bundle.h"
#include "engine/core/verify/verify_host.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_census.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_disc.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_iop.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_park.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_play.h"

namespace ps2r::title::sly2 {

class LegParkSink {
public:
    LegParkSink(ps2r::verify::VerifyHost& host, LegCensus& census, LegPark& park, LegPlay& play, LegDisc& disc, LegIop& iop);
    LegParkSink(const LegParkSink&) = delete;
    LegParkSink& operator=(const LegParkSink&) = delete;

    struct TerminateHookReset {
        ~TerminateHookReset() { ps2r::triage::TriageBundle::ClearTerminateHook(); }
    } terminate_hook_reset;
};

}  // namespace ps2r::title::sly2
