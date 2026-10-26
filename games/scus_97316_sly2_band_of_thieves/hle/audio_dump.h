// NATIVEEE_AUDIO_DUMP=<file.wav>: writes the stream the audio device plays as 48 kHz stereo int16 WAV,
// from the audio ring's consumer tap on the SDL device thread.
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

namespace ps2r::title::sly2 {

struct AudioDump {
    std::FILE* f = nullptr;
    uint64_t frames = 0;
    std::vector<int16_t> buf;
    // Writes a placeholder header that Close() fills in. False if the file cannot be opened.
    bool Open(const char* path);
    // AudioPullRing tap: n stereo float frames, clamped and converted to int16.
    static void Tap(void* ctx, const float* in, int n);
    void Close();
};

}  // namespace ps2r::title::sly2
