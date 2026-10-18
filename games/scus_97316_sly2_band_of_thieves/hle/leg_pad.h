// games/scus_97316_sly2_band_of_thieves/hle/leg_pad.h -- LegPad: the pad served to the title at every
// boundary, in the title's play composition -- slot 4 (MaintDuties). First the SIF RPC end callbacks: a
// sceSifCallRpc with an end function in NOWAIT mode (libmc's sceMcXxx: the callback signals the semaphore
// sceMcSync waits on) is queued by the router and runs here, in order, as a guest call on the boundary's
// context -- the engine's rule (rpc_router.h) and the proven composition's slot-4 duty. Then the pad: one
// PadProtocol::Stamp per boundary -- the live host input in play mode, the neutral pad (no buttons,
// centred sticks) otherwise, exactly what the console's padman publishes every frame. Overlays:
//   NATIVEEE_PAD_SCRIPT=<script text | file>  hle/pad_script.h -- press buttons / hold the sticks over the
//       live / neutral pad from the `frame`-th pad stamp for `hold` stamps; a file in the same grammar is
//       what NATIVEEE_PAD_RECORD writes (the replay);
//   NATIVEEE_PAD_RECORD=<file>  hle/pad_recorder.h -- every stamp's SERVED sample (live pad + script
//       overlay) run-length encoded in PadScript's grammar;
//   NATIVEEE_KEY_SCRIPT=<frame>:<sdl_scancode>:<hold>[,...]  (play mode) hold a KEYBOARD key (SDL scancode:
//       A 4, D 7, S 22, W 26, Space 44, Up 82 ...) from the `frame`-th pad stamp for `hold` boundaries
//       through PlatformInput::HoldKey -- the engine's held-key overlay on the kbm backend, so the press
//       goes through the REAL keyboard -> InputMapper (default_bindings.ini) -> DualShock sample path,
//       not the pad script's direct stamp;
//   NATIVEEE_NAV=<items>  hle/nav_probe.h -- the position / camera probe read at every stamp BEFORE the script
//       is applied, so PadScript's closed-loop steps (goto / face / wait_still / jump_when_near) steer from
//       this stamp's sample; nav_trace.csv under the harness out dir.
// run.meta pad_served=: stamps carrying a press or a deflected stick, and the last such sample.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_play.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_services.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/nav_probe.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/pad_recorder.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/pad_script.h"

namespace ps2r::title::sly2 {

class LegPad {
public:
    // `title_dir` = the title directory (the default probe table lives under it, verify/nav/).
    LegPad(const ps2r::Config& cfg, const std::string& title_dir, const LegKnobs& knobs, LegCore& core, LegServices& services, LegPlay& play);
    LegPad(const LegPad&) = delete;
    LegPad& operator=(const LegPad&) = delete;

    struct KeyScriptStep { uint64_t frame; int scancode; uint64_t hold; bool down = false; };

    uint64_t rpc_end_callbacks = 0, pad_stamps = 0;
    uint64_t pad_served_active = 0;
    std::string pad_served_last;
    NavProbe nav_probe;
    PadScript pad_script;
    PadRecorder pad_recorder;
    std::vector<KeyScriptStep> key_script;
};

}  // namespace ps2r::title::sly2
