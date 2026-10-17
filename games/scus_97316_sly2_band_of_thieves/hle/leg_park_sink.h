// games/scus_97316_sly2_band_of_thieves/hle/leg_park_sink.h -- LegParkSink: the park sink of the title's
// play composition and what is booted once it exists. The TriageBundle terminate hook (an uncaught
// exception on any thread: the census, then park=exception) and the GuestPark sink (a guest park -- an
// unmodelled wait, an unresolved import, an unrecompiled pc: the census, then park=<reason>; in play the
// session closes cleanly around the park (the window goes, the workers are joined) through
// ThreadExitUnwind; a headless leg's park is its result -- leave at once, census written). Then the IOP
// reboot image (IOPRP300.IMG, read off the disc): the stock modules the engine serves natively need no
// bytes; FILEIO (the EE's libfileio server) is hosted from the image's own module -- booted here, after
// the park sink exists, so an unresolved import of a hosted module parks visibly (run.meta
// iop_reboot_image_hosted=). The hook is cleared when this object goes.
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
