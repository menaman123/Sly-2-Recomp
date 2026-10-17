// games/scus_97316_sly2_band_of_thieves/hle/leg_play.cpp -- LegPlay (leg_play.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_play.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "engine/platform/host/platform.h"

namespace ps2r::title::sly2 {

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
        // The consumer thread's presented image goes to the session window and, with --dump-frames,
        // every dump_every-th one is written as p<n>.ppm (n = the OUTPUT frame ordinal).
        gs.video.SetOutput([this, &gs](std::vector<uint8_t> rgba, int w, int h) {
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
