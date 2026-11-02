#include "games/scus_97316_sly2_band_of_thieves/hle/leg_audio.h"

#include <algorithm>
#include <cstdio>

#include "engine/core/log/log.h"
#include "engine/platform/platform.h"
#include "engine/sound/mixer.h"

namespace ps2r::title::sly2 {

namespace {

constexpr int kFrames = ps2r::sound::Mixer::kFramesPerVsync;
constexpr double kFieldsPerSecond = static_cast<double>(ps2r::sound::Mixer::kSampleRate) / kFrames;

template <typename T>
T Percentile(std::vector<T> v, double p) {
    if (v.empty()) return T{};
    std::sort(v.begin(), v.end());
    const size_t i = static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5);
    return v[std::min(i, v.size() - 1)];
}

std::string Fmt(const char* fmt, double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, fmt, v);
    return buf;
}

}  // namespace

LegAudio::LegAudio(const ps2r::Config& cfg, const LegKnobs& knobs, const LegCore& core, ps2r::sound::SpuCore& spu) : spu_core(spu) {
    if (knobs.spu_trace && spu_trace.Open(knobs.spu_trace->c_str())) spu_core.SetTrace(&spu_trace);
    ps2r::sound::SpuCore::SetFastMix(knobs.mixer_fast);   // NATIVEEE_MIXER_FAST=0 = the sample-major oracle
    rate_legacy = knobs.audio_rate_legacy;
    const bool device = core.play_mode && cfg.audio.enabled;
    dump_mixer = knobs.audio_dump && (knobs.audio_dump_mixer || !device);
    if (dump_mixer && !audio_dump.Open(knobs.audio_dump->c_str())) dump_mixer = false;
    if (knobs.audio_dump_mixer_path && mixer_dump.Open(knobs.audio_dump_mixer_path->c_str())) dump_mixer_second = true;
    if (device) {
        ps2r::platform::AudioSpec spec;   // 48 kHz stereo, 800 frames per quantum
        audio_ring.SetPadWithTail(true);
        audio_ring.Prime();
        if (knobs.audio_dump && !dump_mixer && audio_dump.Open(knobs.audio_dump->c_str())) audio_ring.SetPullTap(&AudioDump::Tap, &audio_dump);
        if (!audio_sink.Open(spec, audio_ring)) PS2R_LOG(HOST, Warn, "audio device not opened: running without sound");
        else PS2R_LOG(HOST, Info, "audio device: %d Hz x%d, %d-frame buffer, rate controller %s", audio_sink.DeviceSampleRate(), audio_sink.DeviceChannels(), audio_sink.DeviceBufferFrames(), rate_legacy ? "legacy" : "windowed");
    }
}

LegAudio::~LegAudio() {
    spu_core.SetTrace(nullptr);   // the mixer has stopped by now
}

void LegAudio::TapMixer(const float* out) {
    if (dump_mixer) AudioDump::Tap(&audio_dump, out, ps2r::sound::Mixer::kFramesPerVsync);
    if (dump_mixer_second) AudioDump::Tap(&mixer_dump, out, ps2r::sound::Mixer::kFramesPerVsync);
}

void LegAudio::PushRing(const float* out, int volume) {
    if (!audio_sink.Running()) return;
    const float gain = static_cast<float>(std::clamp(volume, 0, 100)) / 100.0f;
    ++audio_device_quanta;
    for (int i = 0; i < kFrames * 2; ++i)
        if (out[i] != 0.0f) { ++audio_device_nonsilent; break; }
    const float* src = out;
    if (gain != 1.0f) {
        audio_gain_buf.assign(out, out + kFrames * 2);
        for (float& v : audio_gain_buf) v *= gain;
        src = audio_gain_buf.data();
    }
    const auto now = std::chrono::steady_clock::now();
    double dt = -1.0;
    if (last_push != std::chrono::steady_clock::time_point{}) dt = std::chrono::duration<double>(now - last_push).count();
    else first_push = now;
    last_push = now;
    const int fill = audio_ring.Available();
    const double lead = static_cast<double>(audio_ring.LeadQuanta()) * kFrames;
    if (rate_legacy) {
        // The per-push controller: the smoothed interval to the previous push (ceiling 0.1 s) times 60, times
        // a 0.5 gain on the instantaneous fill error. Every late field moved the ratio by tens of percent.
        constexpr double kSmoothing = 0.1, kLegacyFillGain = 0.5;
        if (dt >= 0.0) push_interval_s += kSmoothing * (std::min(dt, ps2r::sound::StretchRate::kMaxIntervalS) - push_interval_s);
        const double err = std::clamp((lead - (fill + 0.5 * kFrames * stretch_ratio)) / lead, -1.0, 1.0);
        stretch_ratio = std::clamp(push_interval_s * kFieldsPerSecond * (1.0 + kLegacyFillGain * err), ps2r::sound::TimeStretch::kMinRatio,
                                   ps2r::sound::TimeStretch::kMaxRatio);
    } else {
        stretch_ratio = rate.Next(dt, fill, static_cast<int>(lead));
    }
    stretched.clear();
    stretch.Process(src, kFrames, stretch_ratio, stretched);
    const int out_frames = static_cast<int>(stretched.size() / 2);
    if (!stretched.empty()) audio_ring.Push(stretched.data(), out_frames);
    if (push_log.size() < kPushLogMax) {
        push_log.push_back(PushRec{static_cast<float>(std::chrono::duration<double, std::milli>(now - first_push).count()),
                                   static_cast<float>(dt * 1000.0), fill, audio_ring.LeadQuanta(), static_cast<float>(stretch_ratio), out_frames,
                                   static_cast<uint32_t>(audio_ring.padded()), static_cast<uint32_t>(audio_ring.relatches()),
                                   static_cast<uint32_t>(audio_ring.trimmed())});
    }
}

std::string LegAudio::RingCensus() const {
    if (push_log.empty()) return "none";
    std::vector<int32_t> fills;
    std::vector<float> ratios, dts;
    uint64_t stretched_pushes = 0;
    fills.reserve(push_log.size());
    ratios.reserve(push_log.size());
    dts.reserve(push_log.size());
    for (const PushRec& r : push_log) {
        fills.push_back(r.fill);
        ratios.push_back(r.ratio);
        if (r.dt_ms >= 0.0f) dts.push_back(r.dt_ms);
        if (r.ratio != 1.0f) ++stretched_pushes;
    }
    const auto [rmin, rmax] = std::minmax_element(ratios.begin(), ratios.end());
    std::string s = "pushes=" + std::to_string(push_log.size()) + ":controller=" + (rate_legacy ? "legacy" : "windowed") +
                    ":fill_min=" + std::to_string(*std::min_element(fills.begin(), fills.end())) + ":fill_p1=" + std::to_string(Percentile(fills, 0.01)) +
                    ":fill_p50=" + std::to_string(Percentile(fills, 0.5)) + ":fill_p99=" + std::to_string(Percentile(fills, 0.99)) +
                    ":ratio_min=" + Fmt("%.4f", *rmin) + ":ratio_p1=" + Fmt("%.4f", Percentile(ratios, 0.01)) + ":ratio_p50=" + Fmt("%.4f", Percentile(ratios, 0.5)) +
                    ":ratio_p99=" + Fmt("%.4f", Percentile(ratios, 0.99)) + ":ratio_max=" + Fmt("%.4f", *rmax) + ":stretched=" + std::to_string(stretched_pushes);
    if (!dts.empty())
        s += ":dt_p50=" + Fmt("%.2f", Percentile(dts, 0.5)) + ":dt_p99=" + Fmt("%.2f", Percentile(dts, 0.99)) + ":dt_max=" + Fmt("%.2f", *std::max_element(dts.begin(), dts.end()));
    s += ":rate_resets=" + std::to_string(rate.Resets()) + ":guard_pushes=" + std::to_string(rate.GuardPushes()) + ":pad_events=" + std::to_string(audio_ring.relatches()) + ":padded=" + std::to_string(audio_ring.padded()) +
         ":trimmed=" + std::to_string(audio_ring.trimmed()) + ":underruns=" + std::to_string(audio_ring.underruns()) + ":overruns=" + std::to_string(audio_ring.overruns()) +
         ":lead=" + std::to_string(audio_ring.LeadQuanta());
    // TimeStretch's splices: segments that were not the exact continuation (count, >= 2 ms, mean / max offset in
    // frames, mean match score) -- the artefacts a listener can hear.
    const uint64_t sp = stretch.Splices();
    s += ":splices=" + std::to_string(sp) + ":splices_2ms=" + std::to_string(stretch.LargeSplices()) +
         ":splice_mean=" + std::to_string(sp ? stretch.SpliceOffsetSum() / sp : 0) + ":splice_max=" + std::to_string(stretch.SpliceOffsetMax()) +
         ":splice_score=" + Fmt("%.3f", sp ? stretch.SpliceScoreSum() / static_cast<double>(sp) : 0.0);
    return s;
}

bool LegAudio::WriteRingCsv(const std::string& path) const {
    if (push_log.empty()) return false;
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fputs("push,t_ms,dt_ms,fill,lead,ratio,out_frames,padded,relatches,trimmed\n", f);
    for (size_t i = 0; i < push_log.size(); ++i) {
        const PushRec& r = push_log[i];
        std::fprintf(f, "%zu,%.3f,%.3f,%d,%d,%.5f,%d,%u,%u,%u\n", i, static_cast<double>(r.t_ms), static_cast<double>(r.dt_ms), r.fill, r.lead,
                     static_cast<double>(r.ratio), r.out_frames, r.padded, r.relatches, r.trimmed);
    }
    std::fclose(f);
    return true;
}

}  // namespace ps2r::title::sly2
