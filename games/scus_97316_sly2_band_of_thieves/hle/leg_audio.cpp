#include "games/scus_97316_sly2_band_of_thieves/hle/leg_audio.h"

#include <algorithm>

#include "engine/core/log/log.h"
#include "engine/platform/platform.h"
#include "engine/sound/mixer.h"

namespace ps2r::title::sly2 {

LegAudio::LegAudio(const ps2r::Config& cfg, const LegKnobs& knobs, const LegCore& core, ps2r::sound::SpuCore& spu) : spu_core(spu) {
    if (knobs.spu_trace && spu_trace.Open(knobs.spu_trace->c_str())) spu_core.SetTrace(&spu_trace);
    const bool device = core.play_mode && cfg.audio.enabled;
    dump_mixer = knobs.audio_dump && (knobs.audio_dump_mixer || !device);
    if (dump_mixer && !audio_dump.Open(knobs.audio_dump->c_str())) dump_mixer = false;
    if (device) {
        ps2r::platform::AudioSpec spec;   // 48 kHz stereo, 800 frames per quantum
        audio_ring.SetPadWithTail(true);
        audio_ring.Prime();
        if (knobs.audio_dump && !dump_mixer && audio_dump.Open(knobs.audio_dump->c_str())) audio_ring.SetPullTap(&AudioDump::Tap, &audio_dump);
        if (!audio_sink.Open(spec, audio_ring)) PS2R_LOG(HOST, Warn, "audio device not opened: running without sound");
        else PS2R_LOG(HOST, Info, "audio device: %d Hz x%d, %d-frame buffer", audio_sink.DeviceSampleRate(), audio_sink.DeviceChannels(), audio_sink.DeviceBufferFrames());
    }
}

LegAudio::~LegAudio() {
    spu_core.SetTrace(nullptr);   // the mixer has stopped by now
}

void LegAudio::TapMixer(const float* out) {
    if (dump_mixer) AudioDump::Tap(&audio_dump, out, ps2r::sound::Mixer::kFramesPerVsync);
}

void LegAudio::PushRing(const float* out, int volume) {
    if (!audio_sink.Running()) return;
    const float gain = static_cast<float>(std::clamp(volume, 0, 100)) / 100.0f;
    ++audio_device_quanta;
    for (int i = 0; i < ps2r::sound::Mixer::kFramesPerVsync * 2; ++i)
        if (out[i] != 0.0f) { ++audio_device_nonsilent; break; }
    if (gain == 1.0f) {
        audio_ring.Push(out, ps2r::sound::Mixer::kFramesPerVsync);
    } else {
        audio_gain_buf.assign(out, out + ps2r::sound::Mixer::kFramesPerVsync * 2);
        for (float& v : audio_gain_buf) v *= gain;
        audio_ring.Push(audio_gain_buf.data(), ps2r::sound::Mixer::kFramesPerVsync);
    }
}

}  // namespace ps2r::title::sly2
