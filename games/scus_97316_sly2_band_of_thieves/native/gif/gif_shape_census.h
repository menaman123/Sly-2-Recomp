// gif_shape_census.h -- GifShapeCensus: the histogram of the GIF packet SHAPES this title emits.
//
// A GsSink on the GIF parser's fan-out (NATIVEEE_GIF_CENSUS=<tsv path>): every accepted GIFtag
// opens a row keyed by its TEMPLATE -- the path it arrived on, the tag's low word with NLOOP
// masked out (EOP / PRE / PRIM / FLG / NREG), the descriptor word, the A+D address sequence the
// tag carried (hashed; A+D tags only), the generated function that submitted the packet
// (DrawAttribution::CurrentFn) and, for PATH1, the VU1 microprogram that kicked it (the cached
// tier's current image hash, carried from the arbiter's submit tap to its drain tap through a
// FIFO, one entry per PATH1 packet). Per row: tags, NLOOP sum/min/max, the payload quadwords the
// tag announced, and the sink items the parser delivered (register writes, PACKED items, kicks,
// drawing kicks, IMAGE quadwords, EOPs). The table is written as TSV when the sink is destroyed
// (the leg's end) -- derived facts only (templates, counts, hashes, function addresses): no
// packet bytes.
//
// This is the native GIF path's SPECIFICATION INPUT (step 1 of ~/ra-orch/notes/nat-gif.md): the
// shapes that cover the quadwords are the ones a native builder is written for; everything else
// falls back to the parser.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/gif/gs_sink.h"

struct GifArbiterPacket;
class GifArbiter;
class VuUnit;

namespace ps2r::title::sly2 {

class GifShapeCensus : public GsSink {
public:
    // `path`: the TSV written at destruction. `arbiter` / `vu1`: optional; when both are given the
    // census installs the arbiter's submit and drain observers (the PATH1 program-hash FIFO) --
    // the slots must be free (NATIVEEE_VU1_CAP's probe uses the submit slot).
    GifShapeCensus(std::string path, GifArbiter* arbiter, VuUnit* vu1);
    ~GifShapeCensus() override;

    void BeginTag(const GifTag& tag, GifPath path) override;
    void WriteRegister(uint8_t reg, uint64_t value) override;
    void PackedRgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
    void PackedSt(float s, float t, float q) override;
    void PackedUv(uint16_t u, uint16_t v) override;
    void PackedFog(uint8_t f) override;
    void KickVertex(float x, float y, float z, uint8_t fog, bool draw) override;
    void ImageData(uint64_t lo, uint64_t hi) override;
    void ImageSpan(const uint8_t* data, size_t qwords) override;
    void EndOfPacket() override;
    void FinishPath3() override;

    // Writes the table now (also done by the destructor). Returns false on an I/O failure.
    bool Write() const;
    size_t Rows() const { return rows_.size(); }

    // Record every packet the drain delivers (path + bytes, delivery order) into `file` until
    // `max_bytes` of payload were written: the corpus of the builders' equivalence test. The file
    // holds game data: it lives outside every repository. Format: "GIFPKT01" then records of
    // {u8 path, u8 reserved[3], u32 size, size bytes}. Needs the arbiter taps (ctor with arbiter).
    bool StartPacketCapture(const std::string& file, uint64_t max_bytes);
    uint64_t CapturedPackets() const { return cap_packets_; }

private:
    struct Key {
        uint8_t path;
        uint64_t tag_lo;      // NLOOP (bits 0-14) cleared
        uint64_t tag_hi;
        uint64_t ad_seq;      // FNV of the A+D address sequence (0 = no A+D descriptor)
        uint32_t fn;
        uint64_t vu1_hash;
        bool operator==(const Key& o) const {
            return path == o.path && tag_lo == o.tag_lo && tag_hi == o.tag_hi && ad_seq == o.ad_seq && fn == o.fn &&
                   vu1_hash == o.vu1_hash;
        }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const;
    };
    struct Row {
        uint64_t tags = 0;
        uint64_t nloop_sum = 0;
        uint32_t nloop_min = 0xFFFFFFFFu;
        uint32_t nloop_max = 0;
        uint64_t qw = 0;         // payload quadwords announced by the tags
        uint64_t regs = 0;       // WriteRegister items
        uint64_t packed = 0;     // RGBAQ / ST / UV / FOG PACKED items
        uint64_t kicks = 0;
        uint64_t draw_kicks = 0;
        uint64_t image_qw = 0;
        uint64_t eops = 0;
        std::string ad_regs;     // the A+D address sequence of the FIRST tag of the row (hex, '.'-joined, <= 48)
    };
    // The open tag: the row is created when the tag's A+D sequence is complete (at the next
    // BeginTag / FinishPath3 / destruction), since the sequence is part of the key.
    struct Open {
        bool valid = false;
        Key key{};
        bool has_ad = false;
        uint32_t nloop = 0;
        uint64_t qw = 0;
        std::vector<uint8_t> ad_regs;
        Row items;   // the items delivered under this tag
    };

    static void OnArbiterSubmit(void* user, GifPath path, const uint8_t* data, size_t size);
    static void OnArbiterDeliver(void* user, const GifArbiterPacket& pkt);
    void CloseOpen();

    std::string path_;
    GifArbiter* arbiter_ = nullptr;
    VuUnit* vu1_ = nullptr;
    std::deque<uint64_t> vu1_fifo_;
    uint64_t vu1_current_ = 0;
    uint64_t fifo_underflows_ = 0;
    Open open_;
    std::unordered_map<Key, Row, KeyHash> rows_;
    uint64_t finish_path3_ = 0;
    void* cap_file_ = nullptr;   // std::FILE*
    uint64_t cap_max_bytes_ = 0;
    uint64_t cap_bytes_ = 0;
    uint64_t cap_packets_ = 0;
};

}  // namespace ps2r::title::sly2
