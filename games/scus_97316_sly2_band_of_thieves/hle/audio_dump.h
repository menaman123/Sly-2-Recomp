// games/scus_97316_sly2_band_of_thieves/hle/audio_dump.h -- AudioDump: NATIVEEE_AUDIO_DUMP=<file.wav>
// (triage) -- the DEVICE stream (what the listener hears, pads and trims included) written as 48 kHz
// stereo int16 WAV from the audio ring's consumer tap (the SDL device thread; the 44-byte header is
// patched at Close). Not the hashed audio lane.
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

namespace ps2r::title::sly2 {

struct AudioDump {
    std::FILE* f = nullptr;
    uint64_t frames = 0;
    std::vector<int16_t> buf;
    // Opens `path`, writes the zero header the Close patches; false = not opened (no tap installed).
    bool Open(const char* path);
    // The AudioPullRing pull tap: n stereo frames of float samples, clamped and widened to int16.
    static void Tap(void* ctx, const float* in, int n);
    void Close();
};

}  // namespace ps2r::title::sly2
