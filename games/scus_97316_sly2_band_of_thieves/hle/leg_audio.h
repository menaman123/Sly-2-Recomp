// games/scus_97316_sly2_band_of_thieves/hle/leg_audio.h -- LegAudio: play mode's audio device in the title's
// play composition. The mixer's 800-frame output of every boundary is ALSO pushed into the SPSC ring the
// SDL device callback pulls from (engine/platform/sdl/audio_pull_ring.h: nothing flows back, so the
// audio lane is byte-identical with or without a device). The device opens in play mode only, and only
// when audio.enabled (headless / deterministic legs stay muted, the rule in sdl_audio_sink.h);
// audio.volume (0..100, the session's live volume) is the gain applied to the ring's copy, never to the
// hashed buffer. Pad-with-tail (the clock contract, rule 4): a late game tick repeats the previous
// quantum's tail -- never a gap, never a pitch change. NATIVEEE_AUDIO_DUMP taps the device stream, or
// (NATIVEEE_AUDIO_DUMP_SOURCE=mixer, and always when no device runs) the mixer's boundary output
// (audio_dump.h). The census counters (audio_device=) and the field pump's state live here; the field pump
// also runs the IOP one frame quantum per catch-up field (field_quantum), so IOP time follows SPU time.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "engine/core/config/config.h"
#include "engine/platform/sdl/audio_pull_ring.h"
#include "engine/platform/sdl/sdl_audio_sink.h"
#include "engine/sound/spu_core.h"
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

    // One mixer quantum (Mixer::kFramesPerVsync stereo frames) to the device ring at `volume` (0..100);
    // nothing when no device runs.
    void PushRing(const float* out, int volume);
    // The mixer's boundary quantum (Mixer::kFramesPerVsync frames) into the dump when it taps the mixer.
    void TapMixer(const float* out);
    bool DumpsMixer() const { return dump_mixer; }
    // The field pump's IOP side (bound by the composition to LegIop::PumpFieldQuantum): one IOP frame
    // quantum per catch-up field, run after that field's SPU quantum and before the next -- the IOP's
    // frame clock advances once per delivered field, as the SPU's does (the IopDispatch step covers the
    // boundary's own field). Unbound: the IOP stays on the boundary clock.
    using FieldQuantum = std::function<void(CpuContext&)>;
    FieldQuantum field_quantum;

    ps2r::platform::sdl::AudioPullRing audio_ring;
    ps2r::platform::sdl::SdlAudioSink audio_sink;
    std::vector<float> audio_gain_buf;
    uint64_t audio_device_quanta = 0, audio_device_nonsilent = 0;   // census audio_device=
    uint64_t audio_pumped_tick = 0, audio_field_pumps = 0;           // the field pump (Verify slot)
    AudioDump audio_dump;
    bool dump_mixer = false;
    ps2r::sound::VoiceTrace spu_trace;   // NATIVEEE_SPU_TRACE (attached to the core while open)
    ps2r::sound::SpuCore& spu_core;
};

}  // namespace ps2r::title::sly2
