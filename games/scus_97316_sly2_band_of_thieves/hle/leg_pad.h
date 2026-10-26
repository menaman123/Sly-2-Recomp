// LegPad: boundary slot 4. First the queued SIF RPC end callbacks (e.g. libmc's NOWAIT calls, whose
// callback signals the semaphore sceMcSync waits on) run in order as guest calls. Then one pad sample per
// boundary: live input in play mode, otherwise a neutral pad, as the console's padman publishes each frame.
//
// Overlays:
//   NATIVEEE_PAD_SCRIPT=<text | file>   press buttons / hold sticks for set stamps (pad_script.h); the same
//                                       grammar NATIVEEE_PAD_RECORD writes. `path:<name>` steps expand
//                                       verify/nav/ep1_waypoints.json.
//   NATIVEEE_PAD_RECORD=<file>          record every served sample, run-length encoded (pad_recorder.h)
//   NATIVEEE_KEY_SCRIPT=<frame>:<sdl_scancode>:<hold>[,...]   (play) hold a keyboard key via
//                                       PlatformInput::HoldKey, through the real key -> InputMapper ->
//                                       DualShock path. Scancodes: A 4, D 7, S 22, W 26, Space 44, Up 82.
//   NATIVEEE_LAT_PULSE=<sdl_scancode>:<period_ms>:<hold_ms>   (play) press a key every period_ms at wall
//                                       instants, as input edges for the latency census.
//   NATIVEEE_NAV=<items>                read the position / camera probe before the script applies, so its
//                                       closed-loop steps steer from this sample (nav_probe.h).
// run.meta pad_served=: stamps with a press or a deflected stick, and the last such sample.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
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
    // `title_dir` holds the default probe table (verify/nav/).
    LegPad(const ps2r::Config& cfg, const std::string& title_dir, const LegKnobs& knobs, LegCore& core, LegServices& services, LegPlay& play);
    ~LegPad();
    LegPad(const LegPad&) = delete;
    LegPad& operator=(const LegPad&) = delete;
    // "off" | "scancode=N:period_ms=N:hold_ms=N:pulses=N"
    std::string PulseCensus() const;

    struct KeyScriptStep { uint64_t frame; int scancode; uint64_t hold; bool down = false; };

    uint64_t rpc_end_callbacks = 0, pad_stamps = 0;
    uint64_t pad_served_active = 0;
    std::string pad_served_last;
    NavProbe nav_probe;
    PadScript pad_script;
    PadRecorder pad_recorder;
    std::vector<KeyScriptStep> key_script;

private:
    // NATIVEEE_LAT_PULSE's thread (play mode only).
    struct Pulse { int scancode = 0; uint64_t period_ms = 0, hold_ms = 0; };
    Pulse pulse_;
    std::atomic<bool> pulse_run_{false};
    std::atomic<uint64_t> pulses_{0};
    std::thread pulse_thread_;
};

}  // namespace ps2r::title::sly2
