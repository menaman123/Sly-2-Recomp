// games/scus_97316_sly2_band_of_thieves/hle/gif_trace_sink.h -- GifTraceSink: NATIVEEE_GIF_TRACE=1 (boot
// triage only) -- a GsSink added to the GIF parser's fan-out that prints every GIF tag / register write
// to stderr and counts the kicks / image quadwords of each packet.
//   NATIVEEE_GIF_TRACE=tbp:<hex>  prints only while the most recent TEX0.TBP0 (64-word units, i.e. the
//                                 byte address / 256) equals <hex>, and then ALSO the packed per-vertex
//                                 values (RGBAQ, ST, UV, the kick) -- the vertex-colour triage of one
//                                 object (solidity stage: the canopy's add-pass colours).
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
    // Parses the knob value ("1" = everything; "tbp:<hex>" = the TEX0.TBP0 filter above).
    void SetFilter(const char* value);
    bool Printing() const { return filter_tbp < 0 || active; }
    unsigned long long kicks = 0, image = 0;
    long filter_tbp = -1;
    bool active = false;
};

}  // namespace ps2r::title::sly2
