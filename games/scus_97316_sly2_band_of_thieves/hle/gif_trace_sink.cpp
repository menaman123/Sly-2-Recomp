// games/scus_97316_sly2_band_of_thieves/hle/gif_trace_sink.cpp -- GifTraceSink (gif_trace_sink.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/gif_trace_sink.h"

#include <cstdio>

namespace ps2r::title::sly2 {

void GifTraceSink::BeginTag(const GifTag& tag, GifPath path) {
    std::fprintf(stderr, "gif: tag path=%u nloop=%u flg=%u nreg=%u eop=%u prim=%u pre=%u\n", unsigned(path),
                 unsigned(tag.nloop), unsigned(tag.flg), unsigned(tag.nregs), unsigned(tag.eop), unsigned(tag.prim), unsigned(tag.pre));
}

void GifTraceSink::WriteRegister(uint8_t reg, uint64_t value) {
    std::fprintf(stderr, "gif:   reg 0x%02x = 0x%016llx\n", reg, (unsigned long long)value);
}

void GifTraceSink::EndOfPacket() {
    std::fprintf(stderr, "gif:   eop (kicks=%llu image=%llu)\n", (unsigned long long)kicks, (unsigned long long)image);
    kicks = image = 0;
}

void GifTraceSink::FinishPath3() { std::fprintf(stderr, "gif:   path3 finish\n"); }

}  // namespace ps2r::title::sly2
