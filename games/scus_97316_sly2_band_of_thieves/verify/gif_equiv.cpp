// gif_equiv.cpp -- the native GIF builder's equivalence test against the engine parser.
//
// Two independent GS state machines receive the same packet sequence: A through the engine's
// chain (GifParser -> GsSinkFanout{GsStateSink} -> GsState), B through NativeGif::Intercept with
// the parser as its fallback (exactly the arbiter's rule: the builder is offered a packet only
// while the parser is between streams). Each GsState feeds a recording DrawIrSink that hashes
// every record it receives (the .gsdl DrawRec of a draw + its vertex array, the JobRec of a job
// or frame). The two record sequences must be identical, record for record.
//
//   sly2_gif_equiv --selftest                synthetic packets covering every shape class the builder
//                                            serves and every one it declines (no game data; the ctest row)
//   sly2_gif_equiv <corpus.gifpkt> [...]     recorded packets (NATIVEEE_GIF_PKT_CAPTURE: "GIFPKT01" then
//                                            {u8 path, u8[3], u32 size, bytes}); a missing file is a SKIP
//                                            (exit 77), game data never enters the repository
// A FinishPath3 is applied after every PATH3 packet on both sides (the arbiter's per-pass rule
// approximated; the same on both sides, so the comparison is exact either way).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/hle/gs_sink_fanout.h"
#include "engine/gif/hle/gs_state_sink.h"
#include "engine/gs/hle/ir/draw_ir_sink.h"
#include "engine/gs/state/gs_state.h"
#include "games/scus_97316_sly2_band_of_thieves/native/gif/native_gif.h"

namespace {

using ps2r::gs::DrawIr;
using ps2r::gs::JobRec;

constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ull;
constexpr uint64_t kFnvPrime = 0x100000001b3ull;
uint64_t Fnv(const void* p, size_t n, uint64_t h = kFnvOffset) {
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * kFnvPrime;
    return h;
}

struct Record {
    char kind;       // 'D' draw, 'J' job, 'F' frame
    uint64_t hash;
    uint32_t verts;
    uint32_t seq;
};

class RecordingSink : public ps2r::gs::DrawIrSink {
public:
    void OnDraw(const DrawIr& d) override {
        const ps2r::gs::DrawRec rec = d.ToDrawRec();
        uint64_t h = Fnv(&rec, sizeof rec);
        if (!d.verts.empty()) h = Fnv(d.verts.data(), d.verts.size() * sizeof(ps2r::gs::VertexRec), h);
        h = Fnv(d.bbox, sizeof d.bbox, h);
        records.push_back({'D', h, static_cast<uint32_t>(d.verts.size()), d.seq});
    }
    void OnJob(const JobRec& j) override { records.push_back({'J', Fnv(&j, sizeof j), 0, j.seq}); }
    void OnFrame(const JobRec& j) override { records.push_back({'F', Fnv(&j, sizeof j), 0, j.seq}); }
    std::vector<Record> records;
};

struct Packet {
    GifPath path;
    std::vector<uint8_t> bytes;
};

// Path A: the engine chain. Path B: the builder with the parser as fallback.
struct Side {
    GifRegs regs;
    ps2r::gs::GsState gs;
    GsStateSink sink{gs};
    GsSinkFanout fanout{&sink};
    GifParser parser{fanout, regs};
    RecordingSink rec;
    std::unique_ptr<ps2r::title::sly2::NativeGif> native;
    uint64_t served = 0, declined = 0;

    explicit Side(bool with_native) {
        gs.SetSink(&rec);
        if (with_native) native = std::make_unique<ps2r::title::sly2::NativeGif>(nullptr, gs);
    }
    void Feed(const Packet& p) {
        bool lowered = false;
        if (native && parser.CanAcceptDirect()) {
            lowered = native->Intercept(p.path, p.bytes.data(), p.bytes.size());
            if (lowered) ++served; else ++declined;
        }
        if (!lowered) parser.ReceiveData(p.path, p.bytes.data(), p.bytes.size());
        if (p.path == GifPath::Path3) parser.FinishPath3();
    }
};

int Compare(const std::vector<Packet>& packets, const char* label) {
    Side a(false), b(true);
    for (const Packet& p : packets) {
        a.Feed(p);
        b.Feed(p);
    }
    const auto& ra = a.rec.records;
    const auto& rb = b.rec.records;
    size_t first_diff = ra.size();
    const size_t n = ra.size() < rb.size() ? ra.size() : rb.size();
    for (size_t i = 0; i < n; ++i) {
        if (ra[i].kind != rb[i].kind || ra[i].hash != rb[i].hash) {
            first_diff = i;
            break;
        }
    }
    const bool same = ra.size() == rb.size() && first_diff == ra.size();
    const auto& c = b.native->counters();
    std::printf("%s: packets=%zu served=%llu declined=%llu records A=%zu B=%zu %s\n", label, packets.size(),
                static_cast<unsigned long long>(b.served), static_cast<unsigned long long>(b.declined), ra.size(), rb.size(),
                same ? "IDENTICAL" : "DIFFER");
    std::printf("  builder: %s\n", b.native->Detail().c_str());
    (void)c;
    if (!same) {
        if (first_diff < n)
            std::printf("  first differing record #%zu: A kind=%c seq=%u verts=%u hash=%016llx | B kind=%c seq=%u verts=%u hash=%016llx\n",
                        first_diff, ra[first_diff].kind, ra[first_diff].seq, ra[first_diff].verts,
                        static_cast<unsigned long long>(ra[first_diff].hash), rb[first_diff].kind, rb[first_diff].seq,
                        rb[first_diff].verts, static_cast<unsigned long long>(rb[first_diff].hash));
        else
            std::printf("  record counts differ after %zu identical records\n", n);
        return 1;
    }
    return 0;
}

// ---- synthetic packets (no game data) ----------------------------------------------

struct Builder {
    std::vector<uint8_t> b;
    void Qw(uint64_t lo, uint64_t hi) {
        uint8_t q[16];
        std::memcpy(q, &lo, 8);
        std::memcpy(q + 8, &hi, 8);
        b.insert(b.end(), q, q + 16);
    }
    void Tag(uint32_t nloop, bool eop, bool pre, uint32_t prim, uint32_t flg, const std::vector<uint8_t>& regs) {
        uint64_t lo = (nloop & 0x7FFF) | (static_cast<uint64_t>(eop) << 15) | (static_cast<uint64_t>(pre) << 46) |
                      (static_cast<uint64_t>(prim & 0x7FF) << 47) | (static_cast<uint64_t>(flg & 3) << 58) |
                      (static_cast<uint64_t>(regs.size() & 0xF) << 60);
        uint64_t hi = 0;
        for (size_t i = 0; i < regs.size(); ++i) hi |= static_cast<uint64_t>(regs[i] & 0xF) << (4 * i);
        Qw(lo, hi);
    }
    void Ad(uint8_t addr, uint64_t v) { Qw(v, addr); }
};

uint32_t g_seed = 0x9E3779B9u;
uint32_t Rnd() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}
uint64_t Xy(uint32_t x16, uint32_t y16) { return (static_cast<uint64_t>(x16 & 0xFFFF)) | (static_cast<uint64_t>(y16 & 0xFFFF) << 32); }
uint64_t FloatBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

void AddStateBlock(std::vector<Packet>& out, GifPath path) {
    Builder b;
    b.Tag(9, true, false, 0, 0, {0xE});
    b.Ad(0x4C, 0x00000000000a0000ull | 0x0);              // FRAME_1: fbp 0, fbw 10 (640), psm 0
    b.Ad(0x4E, 0x0000000000000100ull | 0x100);           // ZBUF_1
    b.Ad(0x18, Xy(0x1000, 0x1000));                      // XYOFFSET_1 (256,256)
    b.Ad(0x40, 0x000001df0000027full);                   // SCISSOR_1 0..639 x 0..447
    b.Ad(0x47, 0x0000000000070000ull | 0x51);            // TEST_1
    b.Ad(0x06, 0x0000000000a00000ull | (2u << 26) | 0x1);  // TEX0_1
    b.Ad(0x14, 0x0000000000000160ull);                   // TEX1_1
    b.Ad(0x42, 0x0000000000000044ull);                   // ALPHA_1
    b.Ad(0x3F, 0);                                       // TEXFLUSH
    out.push_back({path, b.b});
}

void AddStrip(std::vector<Packet>& out, GifPath path, bool xyzf, uint32_t n, bool with_adc, uint32_t prim) {
    Builder b;
    b.Tag(n, true, true, prim, 0, {0x2, 0x1, static_cast<uint8_t>(xyzf ? 0x4 : 0x5)});
    for (uint32_t i = 0; i < n; ++i) {
        b.Qw(FloatBits(static_cast<float>(i) * 0.125f) | (FloatBits(0.5f) << 32), FloatBits(i % 3 ? 1.0f : 0.0f));
        b.Qw((Rnd() & 0xFF) | (static_cast<uint64_t>(Rnd() & 0xFF) << 32), (Rnd() & 0xFF) | (static_cast<uint64_t>(0x80) << 32));
        const uint64_t lo = Xy(0x1000 + (i % 7) * 0x200 + (Rnd() & 0xF), 0x1000 + (i % 5) * 0x300);
        uint64_t hi = xyzf ? ((static_cast<uint64_t>(0x1000 + i) << 4) | (static_cast<uint64_t>(i & 0xFF) << 36))
                           : static_cast<uint64_t>(0x00100000u + i);
        if (with_adc && (i % 11 == 0)) hi |= 1ull << 47;
        b.Qw(lo, hi);
    }
    out.push_back({path, b.b});
}

void AddGenericRows(std::vector<Packet>& out, GifPath path) {
    Builder b;
    // RGBAQ UV FOG XYZ2 NOP XYZ3 rows (the generic body), sprite PRIM.
    b.Tag(6, true, true, 0x156, 0, {0x1, 0x3, 0xA, 0x5, 0xF, 0xD});
    for (uint32_t i = 0; i < 6; ++i) {
        b.Qw(0x80 | (0x40ull << 32), 0x20 | (0x80ull << 32));
        b.Qw((i * 64) | (static_cast<uint64_t>(i * 32) << 32), 0);
        b.Qw(0, static_cast<uint64_t>(i % 2 ? 0x7F : 0) << 36);
        b.Qw(Xy(0x1100 + i * 0x100, 0x1200 + i * 0x80), 0x00200000u);
        b.Qw(0xdeadbeef, 0xfeedface);
        b.Qw(Xy(0x1300, 0x1400), 0x00200000u);
    }
    out.push_back({path, b.b});
}

void AddImageUpload(std::vector<Packet>& out, GifPath path, uint32_t qw) {
    Builder b;
    b.Tag(4, false, false, 0, 0, {0xE});
    b.Ad(0x50, 0x0000000a00000000ull | (0x000a0000ull << 32) | 0x0);  // BITBLTBUF: dbp 0x0a0000.. dbw 10 dpsm 0
    b.Ad(0x51, 0);                                                      // TRXPOS
    b.Ad(0x52, 0x0000000800000010ull);                                  // TRXREG 16 x 8
    b.Ad(0x53, 0);                                                      // TRXDIR host -> local
    b.Tag(qw, true, false, 0, 2, {});
    for (uint32_t i = 0; i < qw; ++i) b.Qw(Rnd() | (static_cast<uint64_t>(Rnd()) << 32), Rnd() | (static_cast<uint64_t>(Rnd()) << 32));
    out.push_back({path, b.b});
}

std::vector<Packet> Synthetic() {
    std::vector<Packet> p;
    AddStateBlock(p, GifPath::Path2);
    AddStrip(p, GifPath::Path1, false, 24, true, 0x004 | 0x10 | 0x40);   // strip, IIP, TME... XYZ2
    AddStrip(p, GifPath::Path1, true, 17, false, 0x004 | 0x20);           // XYZF2 with fog
    AddGenericRows(p, GifPath::Path1);
    AddStrip(p, GifPath::Path1, false, 3, false, 0x003);                  // one triangle
    AddImageUpload(p, GifPath::Path3, 8);                                 // 16x8 PSMCT32 = 32 qw? no: 8 qw of 128 pixels
    // NLOOP 0 EOP (a flush-only tag).
    {
        Builder b;
        b.Tag(0, true, false, 0, 0, {0x1});
        p.push_back({GifPath::Path1, b.b});
    }
    // Declined: a REGLIST tag.
    {
        Builder b;
        b.Tag(2, true, true, 0x003, 1, {0x1, 0x5});
        b.Qw(0x80808080ull, Xy(0x1000, 0x1000) | (0x1000ull << 32));
        b.Qw(0x40404040ull, Xy(0x1800, 0x1000) | (0x1000ull << 32));
        p.push_back({GifPath::Path2, b.b});
    }
    // Declined: an A+D block with FINISH.
    {
        Builder b;
        b.Tag(2, true, false, 0, 0, {0xE});
        b.Ad(0x3F, 0);
        b.Ad(0x61, 0);
        p.push_back({GifPath::Path3, b.b});
    }
    // Declined: a split stream (a strip whose payload continues in the next PATH1 packet).
    {
        std::vector<Packet> tmp;
        AddStrip(tmp, GifPath::Path1, false, 9, false, 0x004);
        const size_t cut = 16 + 3 * 16 * 4;   // after 4 vertices
        Packet first{GifPath::Path1, std::vector<uint8_t>(tmp[0].bytes.begin(), tmp[0].bytes.begin() + cut)};
        Packet second{GifPath::Path1, std::vector<uint8_t>(tmp[0].bytes.begin() + cut, tmp[0].bytes.end())};
        p.push_back(first);
        p.push_back(second);
    }
    // Served again after the split (the parser is idle): a strip, then a state block, then a strip.
    AddStrip(p, GifPath::Path1, false, 12, true, 0x004 | 0x10);
    AddStateBlock(p, GifPath::Path2);
    AddStrip(p, GifPath::Path1, true, 30, true, 0x004 | 0x20 | 0x10);
    // Mixed A+D + known descriptors (declined), then a PRIM-only PACKED tag (served, generic).
    {
        Builder b;
        b.Tag(1, true, false, 0, 0, {0xE, 0x1});
        b.Ad(0x3F, 0);
        b.Qw(0x80, 0x80);
        p.push_back({GifPath::Path2, b.b});
        Builder c;
        c.Tag(1, true, false, 0, 0, {0x0});
        c.Qw(0x156, 0);
        p.push_back({GifPath::Path2, c.b});
    }
    return p;
}

bool ReadCorpus(const char* path, std::vector<Packet>& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char magic[8];
    if (std::fread(magic, 1, 8, f) != 8 || std::memcmp(magic, "GIFPKT01", 8) != 0) {
        std::fclose(f);
        return false;
    }
    for (;;) {
        uint8_t head[4];
        uint32_t size;
        if (std::fread(head, 1, 4, f) != 4 || std::fread(&size, 4, 1, f) != 1) break;
        Packet p;
        p.path = static_cast<GifPath>(head[0]);
        p.bytes.resize(size);
        if (size && std::fread(p.bytes.data(), 1, size, f) != size) break;
        out.push_back(std::move(p));
    }
    std::fclose(f);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: sly2_gif_equiv --selftest | <corpus.gifpkt>...\n");
        return 2;
    }
    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--selftest") {
            rc |= Compare(Synthetic(), "selftest");
            continue;
        }
        std::vector<Packet> packets;
        if (!ReadCorpus(arg.c_str(), packets)) {
            std::printf("%s: no corpus (SKIP)\n", arg.c_str());
            if (argc == 2) return 77;
            continue;
        }
        rc |= Compare(packets, arg.c_str());
    }
    return rc;
}
