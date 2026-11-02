// LegAudio: the audio device in play mode. Each boundary's 800-frame mixer output is also pushed into
// the ring the SDL callback pulls from; nothing flows back, so the hashed audio is identical with or
// without a device. The device opens only in play mode with audio.enabled. audio.volume scales the
// device copy only.
//
// Rate: each quantum goes through TimeStretch at the ratio StretchRate (engine/sound/stretch_rate.h) derives from
// the wall interval since the previous push and the ring's fill against its standing lead: the game's boundary
// RATE over two seconds, the fill held at the lead by a slow drift term and a dead-banded deficit term, exactly 1
// (a pass-through) when the game holds 60 boundaries a second. A late field followed by a catch-up (a two-field
// movie picture) is jitter the ring's lead absorbs, not a tempo change. A game running slower than 60 boundaries
// a second gets each quantum lengthened (pitch kept) at that steady ratio; a stall longer than the lead repeats the
// previous quantum's tail, fading to silence after 100 ms (AudioPullRing).
// NATIVEEE_AUDIO_RATE=legacy: the per-push controller (ratio = the smoothed interval to the previous
// push x 60, times a 0.5 gain on the instantaneous fill error) -- the A/B arm.
// NATIVEEE_AUDIO_DUMP taps the device stream, or the mixer output (NATIVEEE_AUDIO_DUMP_SOURCE=mixer, or when
// no device runs); NATIVEEE_AUDIO_DUMP_MIXER=<file> writes the mixer output as a second wav alongside.
// Census: audio_ring= (the per-push fill / ratio / interval distributions) and, with a harness out dir,
// audio_ring.csv (one row per push).
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/config/config.h"
#include "engine/platform/sdl/audio_pull_ring.h"
#include "engine/platform/sdl/sdl_audio_sink.h"
#include "engine/sound/spu_core.h"
#include "engine/sound/stretch_rate.h"
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

    // Census (run.meta audio_ring=): pushes, the fill before each push (frames: min / p1 / p50 / p99), the
    // ratio applied (min / p1 / p50 / p99 / max), pushes stretched (ratio != 1), the push interval (p50 / p99 /
    // max ms), rate-window resets, and the ring's pad events / padded / trimmed / underruns / overruns.
    std::string RingCensus() const;
    // One row per push: push, t_ms, dt_ms, fill, lead, ratio, out_frames, padded, relatches, trimmed.
    bool WriteRingCsv(const std::string& path) const;

    ps2r::platform::sdl::AudioPullRing audio_ring;
    ps2r::platform::sdl::SdlAudioSink audio_sink;
    std::vector<float> audio_gain_buf;
    uint64_t audio_device_quanta = 0, audio_device_nonsilent = 0;   // census audio_device=
    ps2r::sound::TimeStretch stretch;
    std::vector<float> stretched;
    double stretch_ratio = 1.0;
    std::chrono::steady_clock::time_point first_push, last_push;
    ps2r::sound::StretchRate rate;               // the ratio per quantum (engine/sound/stretch_rate.h)
    bool rate_legacy = false;                    // NATIVEEE_AUDIO_RATE=legacy
    double push_interval_s = 1.0 / 60.0;         // legacy: the smoothed interval
    struct PushRec {
        float t_ms, dt_ms;
        int32_t fill, lead;
        float ratio;
        int32_t out_frames;
        uint32_t padded, relatches, trimmed;
    };
    std::vector<PushRec> push_log;
    static constexpr size_t kPushLogMax = 1u << 20;
    AudioDump audio_dump;
    AudioDump mixer_dump;   // NATIVEEE_AUDIO_DUMP_MIXER
    bool dump_mixer = false, dump_mixer_second = false;
    ps2r::sound::VoiceTrace spu_trace;   // NATIVEEE_SPU_TRACE
    ps2r::sound::SpuCore& spu_core;
};

}  // namespace ps2r::title::sly2
