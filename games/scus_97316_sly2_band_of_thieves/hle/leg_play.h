// LegPlay: the live pad and the session window, on main (play mode only; both null otherwise).
//
// The boundary's pad Sample() posts to main and waits at most 3 ms. SessionRuntime owns the visible window
// and its GL context: Run() moves the game to a worker thread with a 1 GiB stack while main pumps SDL,
// presents posted frames and services the pad. With --dump-frames, presented pictures are written as
// p<n>.ppm keyed by output frame (under frame skip, presents and boundaries no longer pair 1:1).
//
// The player's menu (SessionGui): Escape pauses (Resume / Quit), F1 opens it without pausing. Display and
// Controls pages apply settings live and persist them; the saves page drives F5 / F8 (LegSaves).
#pragma once

#include <memory>

#include "app/host/controls_page_binding.h"
#include "app/host/platform_input.h"
#include "app/host/session_runtime.h"
#include "engine/gs/hle/present/aspect_selection.h"
#include "engine/render/texture_filter_selection.h"
#include "engine/core/config/config.h"
#include "engine/title/title_profile.h"
#include "app/host/clock_census.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/frame_timeline.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_audio.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_gs.h"

namespace ps2r::title::sly2 {

class LegPlay {
public:
    LegPlay(const ps2r::Config& cfg, const TitleProfile& title, LegCore& core, LegGs& gs, LegAudio& audio);
    LegPlay(const LegPlay&) = delete;
    LegPlay& operator=(const LegPlay&) = delete;

    // True if the session window could not be opened; RunLeg then exits with 2.
    bool Failed() const { return failed_; }
    // One mixer quantum to the device ring at the session's live volume (audio.volume without a session).
    void PushAudioRing(const float* out);

    std::unique_ptr<PlatformInput> live_input;
    std::unique_ptr<ps2r::app::SessionRuntime> session;
    // Display selections shared with the session window (aspect) and the GL device (texture filter).
    std::shared_ptr<ps2r::gs::AspectSelection> aspect;
    std::shared_ptr<ps2r::render::TextureFilterSelection> texture_filter;
    std::unique_ptr<ps2r::app::ControlsPageBinding> controls_page;
    uint64_t settings_persisted = 0, settings_persist_failed = 0;   // nativeee.ini writes
    FrameTimeline timeline;
    // Per 1 s wall window: boundaries (game_hz), fields delivered (field_hz) and presents. See the clock=
    // row in leg_census.cpp.
    ps2r::app::ClockCensus clock_census;

private:
    const ps2r::Config& cfg_;
    LegAudio& audio_;
    bool failed_ = false;
};

}  // namespace ps2r::title::sly2
