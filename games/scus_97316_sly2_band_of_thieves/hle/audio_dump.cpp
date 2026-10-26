#include "games/scus_97316_sly2_band_of_thieves/hle/audio_dump.h"

#include <algorithm>

namespace ps2r::title::sly2 {

bool AudioDump::Open(const char* path) {
    f = std::fopen(path, "wb");
    if (!f) return false;
    static const uint8_t zero_header[44] = {};
    std::fwrite(zero_header, 1, sizeof(zero_header), f);   // patched at close
    return true;
}

void AudioDump::Tap(void* ctx, const float* in, int n) {
    AudioDump* d = static_cast<AudioDump*>(ctx);
    d->buf.resize(static_cast<size_t>(n) * 2);
    for (size_t i = 0; i < d->buf.size(); ++i) {
        const float v = std::clamp(in[i], -1.0f, 1.0f);
        d->buf[i] = static_cast<int16_t>(v < 0 ? v * 32768.0f : v * 32767.0f);
    }
    std::fwrite(d->buf.data(), sizeof(int16_t), d->buf.size(), d->f);
    d->frames += static_cast<uint64_t>(n);
}

void AudioDump::Close() {
    if (!f) return;
    const uint32_t data_bytes = static_cast<uint32_t>(frames * 4), riff = 36 + data_bytes;
    const uint32_t rate = 48000, byte_rate = rate * 4;
    const uint16_t fmt_tag = 1, ch = 2, align = 4, bits = 16;
    const uint32_t fmt_len = 16;
    std::fseek(f, 0, SEEK_SET);
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmt_len, 4, 1, f); std::fwrite(&fmt_tag, 2, 1, f); std::fwrite(&ch, 2, 1, f);
    std::fwrite(&rate, 4, 1, f); std::fwrite(&byte_rate, 4, 1, f); std::fwrite(&align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data_bytes, 4, 1, f);
    std::fclose(f);
    f = nullptr;
}

}  // namespace ps2r::title::sly2
