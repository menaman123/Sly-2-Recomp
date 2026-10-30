// LegAudio: the audio device in play mode. Each boundary's 800-frame mixer output is also pushed into
// the ring the SDL callback pulls from; nothing flows back, so the hashed audio is identical with or
// without a device. The device opens only in play mode with audio.enabled. audio.volume scales the
// device copy only. When boundaries arrive slower than 60 a second, each quantum is time-stretched
// (pitch kept) so the ring holds its standing lead; a stall longer than the lead repeats the previous
// quantum's tail, fading to silence after 100 ms. NATIVEEE_AUDIO_DUMP taps the device stream, or the mixer output
// (NATIVEEE_AUDIO_DUMP_SOURCE=mixer, or when no device runs).
#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "engine/core/config/config.h"
#include "engine/platform/sdl/audio_pull_ring.h"
#include "engine/platform/sdl/sdl_audio_sink.h"
#include "engine/sound/spu_core.h"
#include "engine/sound/time_stretch.h"
#include "engine/sound/voice_trace.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/audio_dump.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"

namespace ps2r::title::sly2 {

class LegAudio {
public:
    LegAudio(const ps2r::Config& cfg, const LegKnobs& knobs, const LegCore& core, ps2r::sound::SpuCore& spu);
    ~LegAudio();
    LegAudio(const LegAudio&) = delete;
    LegAudio& operator=(const LegAudio&) = delete;

    // One mixer quantum to the device ring at `volume` (0..100); nothing when no device runs.
    void PushRing(const float* out, int volume);
    // The mixer's boundary quantum into the dump, when the dump taps the mixer.
    void TapMixer(const float* out);

    ps2r::platform::sdl::AudioPullRing audio_ring;
    ps2r::platform::sdl::SdlAudioSink audio_sink;
    std::vector<float> audio_gain_buf;
    uint64_t audio_device_quanta = 0, audio_device_nonsilent = 0;   // census audio_device=
    // Rate match to the device: output frames per mixed frame. The smoothed wall time between pushes
    // gives the game's rate; a fill term holds the ring near its standing lead.
    ps2r::sound::TimeStretch stretch;
    std::vector<float> stretched;
    double stretch_ratio = 1.0;
    double push_interval_s = 1.0 / 60.0;
    std::chrono::steady_clock::time_point last_push;
    AudioDump audio_dump;
    bool dump_mixer = false;
    ps2r::sound::VoiceTrace spu_trace;   // NATIVEEE_SPU_TRACE
    ps2r::sound::SpuCore& spu_core;
};

}  // namespace ps2r::title::sly2
