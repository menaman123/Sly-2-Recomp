// games/scus_97316_sly2_band_of_thieves/hle/leg_play.cpp -- LegPlay (leg_play.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_play.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "app/host/user_data.h"
#include "engine/core/config/settings_file.h"
#include "engine/core/log/log.h"
#include "engine/platform/host/platform.h"

namespace ps2r::title::sly2 {

namespace {

// Config's enums onto the engine's selections (the same order; pinned here).
ps2r::gs::AspectSelection::Shape ShapeOf(ps2r::AspectSel a) {
    static_assert(static_cast<int>(ps2r::gs::AspectSelection::Shape::Auto) == static_cast<int>(ps2r::AspectSel::Auto) &&
                  static_cast<int>(ps2r::gs::AspectSelection::Shape::Narrow) == static_cast<int>(ps2r::AspectSel::R4_3) &&
                  static_cast<int>(ps2r::gs::AspectSelection::Shape::Wide16_9) == static_cast<int>(ps2r::AspectSel::R16_9) &&
                  static_cast<int>(ps2r::gs::AspectSelection::Shape::Wide16_10) == static_cast<int>(ps2r::AspectSel::R16_10) &&
                  static_cast<int>(ps2r::gs::AspectSelection::Shape::Wide21_9) == static_cast<int>(ps2r::AspectSel::R21_9));
    return static_cast<ps2r::gs::AspectSelection::Shape>(a);
}

ps2r::render::TextureFilter FilterOf(ps2r::TextureFilterSel f) {
    static_assert(static_cast<int>(ps2r::render::TextureFilter::Title) == static_cast<int>(ps2r::TextureFilterSel::Title) &&
                  static_cast<int>(ps2r::render::TextureFilter::Point) == static_cast<int>(ps2r::TextureFilterSel::Point) &&
                  static_cast<int>(ps2r::render::TextureFilter::Bilinear) == static_cast<int>(ps2r::TextureFilterSel::Bilinear) &&
                  static_cast<int>(ps2r::render::TextureFilter::Trilinear) == static_cast<int>(ps2r::TextureFilterSel::Trilinear));
    return static_cast<ps2r::render::TextureFilter>(f);
}

}  // namespace

LegPlay::LegPlay(const ps2r::Config& cfg, const TitleProfile& title, LegCore& core, LegGs& gs, LegAudio& audio)
    : cfg_(cfg), audio_(audio) {
    if (core.play_mode && gs.video_live) {
        std::string exe_err;
        const std::string exe = ps2r::platform::host::ExecutablePath(&exe_err);
        const size_t slash = exe.find_last_of('/');
        const std::string exe_dir = slash == std::string::npos ? std::string(".") : exe.substr(0, slash);
        live_input = std::make_unique<PlatformInput>(PlatformInput::SettingsFrom(cfg, exe_dir));
        session = std::make_unique<ps2r::app::SessionRuntime>(live_input.get());
        std::string err;
        if (!session->Open(cfg.audio.volume, audio.audio_sink.Running(), /*recording=*/false, std::string(), err, cfg.paths.mc_dir)) {
            std::fprintf(stderr, "nativeee: session window: %s\n", err.c_str());
            failed_ = true;
            return;
        }
        session->ConfigurePresentation(cfg.video.vsync, cfg.video.fps_cap);
        session->SetWindowTitle(title.display_name.empty() ? title.serial : title.display_name);
        session->SetEventTrace(cfg.dev.log_level == ps2r::LogLevelSetting::Narrate);
        // The player's settings: seeded from Config, persisted to <user data>/ps2r.ini by the one
        // writer (SettingsFile), never to the memory card. A write failure is loud, never silent.
        const std::string settings_file = ps2r::app::UserData::SettingsFile(cfg);
        session->ConfigureSettings(cfg.ui.perf_overlay, [this, settings_file](const std::string& key, const std::string& value) {
            std::string err;
            if (ps2r::SettingsFile::Set(settings_file, key, value, &err)) {
                ++settings_persisted;
                PS2R_LOG(HOST, Info, "settings: %s=%s -> %s", key.c_str(), value.c_str(), settings_file.c_str());
            } else {
                ++settings_persist_failed;
                std::fprintf(stderr, "nativeee: settings: %s=%s not saved: %s\n", key.c_str(), value.c_str(), err.c_str());
            }
        });
        // The Display page: the presenter's rows (mode, aspect, vsync, render rate, frame cap) seeded from
        // video.*; the aspect selection fits the picture in the session window; the texture
        // filter selection is the GL device's sampler choice (video.texture_filter / anisotropy).
        // video.scale is persisted by the row and read at the next start (this video stack builds
        // its ScalePolicy once, at Create).
        // video.frame_cap seeds the Frame cap row (the presented-frame cadence: off | 30 | 60; the
        // engine's FramePacer order == the Config enum's).
        static_assert(static_cast<int>(ps2r::FrameCap::Off) == 0 && static_cast<int>(ps2r::FrameCap::Hz30) == 1 &&
                      static_cast<int>(ps2r::FrameCap::Hz60) == 2);
        session->ConfigureDisplayRows({cfg.video.scale, static_cast<int>(cfg.video.aspect), static_cast<int>(cfg.video.render_rate),
                                       static_cast<int>(cfg.video.fullscreen), cfg.harness.display_hz, static_cast<int>(cfg.video.frame_cap)});
        aspect = std::make_shared<ps2r::gs::AspectSelection>(ShapeOf(cfg.video.aspect));
        session->SetAspect(aspect);
        texture_filter = std::make_shared<ps2r::render::TextureFilterSelection>(FilterOf(cfg.video.texture_filter), cfg.video.anisotropy);
        gs.video.Device().SetTextureFilter(texture_filter);
        session->SetTextureFilter(texture_filter);
        // The pause menu (Escape) and the Controls page (press-to-bind over <user data>/bindings.ini,
        // applied to the live pad at once).
        session->ConfigurePauseMenu(true);
        // --dump-frames <dir> in play: the presented WINDOW image the session ends on (<dir>/w_final.ppm:
        // the picture plus the menu overlays -- what the user saw), the pin for the menu pages.
        if (gs.frame_dump.Enabled()) session->ConfigureWindowDump(gs.frame_dump.Dir());
        // The F1 panel's Renderer line: the backend in force and how it was chosen.
        if (gs.video_live)
            session->SetRenderer(std::string(gs.video.ActiveBackend()) + (gs.video.BackendAuto() ? " (auto): " : ": ") +
                                 gs.video.Renderer());
        controls_page = std::make_unique<ps2r::app::ControlsPageBinding>(cfg.paths.user_data, settings_file, live_input.get());
        session->ConfigureControlsPage(controls_page->Model());
        // The presenter's draws: the ones that showed a NEW picture are the shown lane of the record.
        session->SetPresentObserver([this](std::chrono::steady_clock::time_point at, bool new_picture) {
            if (new_picture) timeline.NoteAt(FrameTimeline::Lane::Shown, at);
        });
        // The consumer thread's presented image goes to the session window and, with --dump-frames,
        // every dump_every-th one is written as p<n>.ppm (n = the OUTPUT frame ordinal).
        gs.video.SetOutput([this, &gs](std::vector<uint8_t> rgba, int w, int h) {
            timeline.Note(FrameTimeline::Lane::Output);
            clock_census.NotePresent();
            const uint32_t n = static_cast<uint32_t>(gs.video.OutputFrames());
            if (gs.frame_dump.Enabled() && gs.DumpWanted(n)) gs.frame_dump.Write("p", n, w, h, rgba.data(), /*flip=*/true);
            session->PostFrame(std::move(rgba), w, h);
        });
    }
}

void LegPlay::PushAudioRing(const float* out) {
    if (!audio_.audio_sink.Running()) return;
    audio_.PushRing(out, session ? session->Volume() : cfg_.audio.volume);
}

}  // namespace ps2r::title::sly2
