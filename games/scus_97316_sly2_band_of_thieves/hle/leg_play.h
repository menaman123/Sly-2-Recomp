// games/scus_97316_sly2_band_of_thieves/hle/leg_play.h -- LegPlay: play mode's live pad + the session window
// on main, in the title's play composition. PlatformInput is constructed on main (its owner); the
// boundary's Sample() posts to main and waits a bounded 3 ms. SessionRuntime owns the VISIBLE window +
// its own GL context on main (opened after the video stack's context was released from main): Run()
// moves the cold boot to a 1 GiB-stack game worker while main pumps SDL, presents the posted frames at
// video.fps_cap and services the pad. The GS pipeline's presented image (VideoStack::Output, on the
// consumer thread) is posted to it and, with --dump-frames, every dump_every-th one is written as
// p<n>.ppm with n = the OUTPUT frame ordinal (under frame skip the presents and the EE boundaries no
// longer pair 1:1, so the play dump is keyed by the picture, not the boundary). Both objects stay null
// outside play mode / without a live GL stack.
//
// The player's menu (the engine's SessionGui, all title-neutral): Escape = the pause menu (the game
// pauses; Resume / Quit), F1 the same menu without the pause; its Display page (display mode, aspect,
// vsync, render rate, texture filter + anisotropy, perf overlay -- each applied live by the presenter /
// the GL device and persisted to <user data>/ps2r.ini through SettingsFile, the one writer), its
// Controls page (the keyboard rows' press-to-bind over <user data>/bindings.ini + the live mapper
// reload: ControlsPageBinding), the Reproduction saves page (F5 / F8, LegSaves). The seeds are the
// Config's (video.*, ui.perf_overlay); the aspect / texture-filter selections are shared with the
// session window (the picture's fit) and the video stack's GL device (the samplers).
#pragma once

#include <memory>

#include "app/host/controls_page_binding.h"
#include "app/host/platform_input.h"
#include "app/host/session_runtime.h"
#include "engine/gs/hle/present/aspect_selection.h"
#include "engine/render/texture_filter_selection.h"
#include "engine/core/config/config.h"
#include "engine/title/title_profile.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_audio.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_gs.h"

namespace ps2r::title::sly2 {

class LegPlay {
public:
    LegPlay(const ps2r::Config& cfg, const TitleProfile& title, LegCore& core, LegGs& gs, LegAudio& audio);
    LegPlay(const LegPlay&) = delete;
    LegPlay& operator=(const LegPlay&) = delete;

    // True when the session window could not be opened: the leg ends with exit 2.
    bool Failed() const { return failed_; }
    // One mixer quantum to the device ring at the session's live volume (audio.volume without a session).
    void PushAudioRing(const float* out);

    std::unique_ptr<PlatformInput> live_input;
    std::unique_ptr<ps2r::app::SessionRuntime> session;
    // The Display page's shared selections (the session window fits the picture at `aspect`;
    // the GL device samples through `texture_filter`), the Controls page's binding model.
    std::shared_ptr<ps2r::gs::AspectSelection> aspect;
    std::shared_ptr<ps2r::render::TextureFilterSelection> texture_filter;
    std::unique_ptr<ps2r::app::ControlsPageBinding> controls_page;
    uint64_t settings_persisted = 0, settings_persist_failed = 0;   // census: ps2r.ini writes

private:
    const ps2r::Config& cfg_;
    LegAudio& audio_;
    bool failed_ = false;
};

}  // namespace ps2r::title::sly2
