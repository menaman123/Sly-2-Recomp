// NATIVEEE_GIF_TRACE: a GsSink on the GIF parser's fan-out that prints every tag and register write
// to stderr and counts each packet's kicks and image quadwords.
//   =1           everything
//   =tbp:<hex>   only while the latest TEX0.TBP0 (byte address / 256) equals <hex>, plus the packed
//                per-vertex values (RGBAQ, ST, UV, kick)
#pragma once

#include <cstdint>

#include "engine/gif/gs_sink.h"

namespace ps2r::title::sly2 {

struct GifTraceSink : GsSink {
    void BeginTag(const GifTag& tag, GifPath path) override;
    void WriteRegister(uint8_t reg, uint64_t value) override;
    void PackedRgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
    void PackedSt(float s, float t, float q) override;
    void PackedUv(uint16_t u, uint16_t v) override;
    void PackedFog(uint8_t) override {}
    void KickVertex(float x, float y, float z, uint8_t fog, bool draw) override;
    void ImageData(uint64_t, uint64_t) override { ++image; }
    void EndOfPacket() override;
    void FinishPath3() override;
    void SetFilter(const char* value);
    bool Printing() const { return filter_tbp < 0 || active; }
    unsigned long long kicks = 0, image = 0;
    long filter_tbp = -1;
    bool active = false;
};

}  // namespace ps2r::title::sly2
