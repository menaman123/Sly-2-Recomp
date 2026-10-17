// games/scus_97316_sly2_band_of_thieves/hle/gif_trace_sink.h -- GifTraceSink: NATIVEEE_GIF_TRACE=1 (boot
// triage only) -- a GsSink added to the GIF parser's fan-out that prints every GIF tag / register write
// to stderr and counts the kicks / image quadwords of each packet.
#pragma once

#include <cstdint>

#include "engine/gif/gs_sink.h"

namespace ps2r::title::sly2 {

struct GifTraceSink : GsSink {
    void BeginTag(const GifTag& tag, GifPath path) override;
    void WriteRegister(uint8_t reg, uint64_t value) override;
    void PackedRgbaq(uint8_t, uint8_t, uint8_t, uint8_t) override {}
    void PackedSt(float, float, float) override {}
    void PackedUv(uint16_t, uint16_t) override {}
    void PackedFog(uint8_t) override {}
    void KickVertex(float, float, float, uint8_t, bool) override { ++kicks; }
    void ImageData(uint64_t, uint64_t) override { ++image; }
    void EndOfPacket() override;
    void FinishPath3() override;
    unsigned long long kicks = 0, image = 0;
};

}  // namespace ps2r::title::sly2
