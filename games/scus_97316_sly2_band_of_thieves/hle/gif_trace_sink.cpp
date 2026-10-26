#include "games/scus_97316_sly2_band_of_thieves/hle/gif_trace_sink.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ps2r::title::sly2 {

void GifTraceSink::SetFilter(const char* value) {
    filter_tbp = -1;
    active = false;
    if (value && std::strncmp(value, "tbp:", 4) == 0) filter_tbp = std::strtol(value + 4, nullptr, 16);
}

void GifTraceSink::BeginTag(const GifTag& tag, GifPath path) {
    if (!Printing()) return;
    std::fprintf(stderr, "gif: tag path=%u nloop=%u flg=%u nreg=%u eop=%u prim=%u pre=%u\n", unsigned(path),
                 unsigned(tag.nloop), unsigned(tag.flg), unsigned(tag.nregs), unsigned(tag.eop), unsigned(tag.prim), unsigned(tag.pre));
}

void GifTraceSink::WriteRegister(uint8_t reg, uint64_t value) {
    // TEX0_1 / TEX0_2: TBP0 is bits 0..13.
    if (filter_tbp >= 0 && (reg == 0x06 || reg == 0x07)) active = static_cast<long>(value & 0x3fffu) == filter_tbp;
    if (!Printing()) return;
    std::fprintf(stderr, "gif:   reg 0x%02x = 0x%016llx\n", reg, (unsigned long long)value);
}

void GifTraceSink::PackedRgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (filter_tbp >= 0 && active) std::fprintf(stderr, "gif:   rgbaq %02x%02x%02x%02x\n", r, g, b, a);
}

void GifTraceSink::PackedSt(float s, float t, float q) {
    if (filter_tbp >= 0 && active) std::fprintf(stderr, "gif:   st %g %g %g\n", s, t, q);
}

void GifTraceSink::PackedUv(uint16_t u, uint16_t v) {
    if (filter_tbp >= 0 && active) std::fprintf(stderr, "gif:   uv %u %u\n", u, v);
}

void GifTraceSink::KickVertex(float x, float y, float z, uint8_t fog, bool draw) {
    ++kicks;
    if (filter_tbp >= 0 && active) std::fprintf(stderr, "gif:   kick %.3f %.3f %.0f f=%u draw=%d\n", x, y, z, fog, draw ? 1 : 0);
}

void GifTraceSink::EndOfPacket() {
    if (Printing()) std::fprintf(stderr, "gif:   eop (kicks=%llu image=%llu)\n", (unsigned long long)kicks, (unsigned long long)image);
    kicks = image = 0;
}

void GifTraceSink::FinishPath3() {
    if (Printing()) std::fprintf(stderr, "gif:   path3 finish\n");
}

}  // namespace ps2r::title::sly2
