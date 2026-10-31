// Sly2_NativeVifChains: the title's VIF1 chain executor against the engine's walk.
//
// Two identical rigs (RAM, scratchpad, VU0/VU1, VIF, GIF arbiter -> parser -> op
// stream, the DMAC chain walker); the same chains written into both RAMs; one rig
// walks them through ChainWalker alone (the oracle), the other with NativeVif
// installed (title.native.dmac + title.native.vif). After every chain: VU1 data
// memory, the VIF1 register file, the VIF1 unit state, the channel registers, the
// walk result, the GIF op count and the i-bit hook census must be identical.
//
// Chains: the colours grammar (REF'd V4-32 + V2-16 geometry under STCYCL 4/1, an
// inline CNT of V4-5 colours + STMASK + masked S-8 alphas, MSCAL, FLUSHE with the
// i bit whose handler WRITES the payload of a later span), then every shape the
// executor must hand back (DIRECT, MPG, a payload crossing the span end, the
// filling write, V3-32, TTE, CALL/RET, NEXT, REFE, IRQ+TIE, an unreadable span,
// a terminator), and a seeded random walk over the grammar.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "engine/dmac/chain_walker.h"
#include "engine/dmac/dmac_channel.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vif/vif.h"
#include "engine/vu/vu_unit.h"
#include "games/scus_97316_sly2_band_of_thieves/native/native_census.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vif/native_vif.h"

namespace {

int g_fail = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

constexpr uint32_t U_NOP = 0x000002FFu;
constexpr uint32_t L_NOP = 0x8000033Cu;
constexpr uint32_t U_EBIT = 0x40000000u;

// The i-bit handler of the rig: counts, and (when armed) writes `words` at RAM `addr`
// -- the game's VIF1 interrupt handler filling a colour payload further down the chain.
struct IrqSink {
    MainMemory* ram = nullptr;
    int irqs = 0;
    int stcs = 0;
    uint32_t write_addr = 0;
    std::vector<uint32_t> write_words;
    static void OnIrq(void* user, int vif) {
        auto* s = static_cast<IrqSink*>(user);
        if (vif != 1) return;
        s->irqs++;
        if (!s->write_words.empty()) {
            uint8_t* p = s->ram->Pointer(s->write_addr, s->write_words.size() * 4);
            if (p) std::memcpy(p, s->write_words.data(), s->write_words.size() * 4);
        }
    }
    static void OnStc(void* user, int vif) {
        if (vif == 1) static_cast<IrqSink*>(user)->stcs++;
    }
};

struct Rig {
    MainMemory ram;
    Scratchpad spad;
    RegisterOpStream ops;
    GifRegs gif_regs;
    GifParser parser;
    GifArbiter arbiter;
    VuUnit vu0;
    VuUnit vu1;
    Vif vif;
    ChainWalker walker;
    IrqSink irq;
    DmacChannelRegs ch{};
    Rig() : parser(ops, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter), vif(vu0, vu1, arbiter, gif_regs), walker(ram, spad, vif, arbiter) {
        irq.ram = &ram;
        vif.SetIrqHooks(&IrqSink::OnIrq, &IrqSink::OnStc, &irq);
        // A one-bundle program (E bit) at every MSCAL entry the chains use.
        for (uint32_t pc = 0; pc < 0x4000; pc += 8) {
            uint8_t b[8];
            std::memcpy(b, &L_NOP, 4);
            const uint32_t up = U_NOP | U_EBIT;
            std::memcpy(b + 4, &up, 4);
            vu1.WriteMicroMem(pc, b, 8);
        }
    }
};

// The DMAtag low word: QWC, ID, IRQ, ADDR, SPR.
struct Builder {
    static uint64_t Tag(uint32_t id, uint32_t qwc, uint32_t addr, bool irq = false, bool spr = false) {
        return static_cast<uint64_t>(qwc & 0xFFFFu) | (static_cast<uint64_t>(id & 7u) << 28) | (irq ? (1ull << 31) : 0) |
               (static_cast<uint64_t>(addr & 0x7FFFFFF0u) << 32) | (spr ? (1ull << 63) : 0);
    }
};

uint32_t Code(uint32_t cmd, uint32_t num, uint32_t imm, bool irq = false) {
    return (irq ? 0x80000000u : 0u) | ((cmd & 0x7Fu) << 24) | ((num & 0xFFu) << 16) | (imm & 0xFFFFu);
}
void PadQw(std::vector<uint32_t>& w) {
    while (w.size() % 4) w.push_back(0);
}

// Runs the channel registers `regs` (TADR, CHCR) as one VIF1 chain kick on the rig.
DmacVifWalkResult Walk(Rig& r, uint32_t tadr, uint32_t chcr_extra = 0) {
    using namespace DmacChcr;
    r.ch = DmacChannelRegs{};
    r.ch.tadr = tadr;
    r.ch.chcr = kStr | (kModChain << kModShift) | chcr_extra;
    return r.walker.WalkVifChain(static_cast<int>(DmacChannelId::Vif1), r.ch);
}

bool Same(const Rig& a, const Rig& b, const DmacVifWalkResult& ra, const DmacVifWalkResult& rb, const char* what) {
    bool ok = true;
    if (std::memcmp(a.vu1.state().data, b.vu1.state().data, Vu1State::kMemSize) != 0) {
        std::printf("  %s: VU1 data memory differs\n", what);
        ok = false;
    }
    if (std::memcmp(&a.vif.regs(1), &b.vif.regs(1), sizeof(VifRegs)) != 0) {
        std::printf("  %s: VIF1 registers differ (stat %08x/%08x code %08x/%08x cycle %08x/%08x tops %x/%x)\n", what, a.vif.regs(1).stat,
                    b.vif.regs(1).stat, a.vif.regs(1).code, b.vif.regs(1).code, a.vif.regs(1).cycle, b.vif.regs(1).cycle, a.vif.regs(1).tops,
                    b.vif.regs(1).tops);
        ok = false;
    }
    const VifUnitState& ua = a.vif.unit(1);
    const VifUnitState& ub = b.vif.unit(1);
    // pending_cmd is live only in WaitData: the engine's word latch rewrites it at every command it parks
    // mid-span while it decodes word by word (a span after a TTE park), the whole-span parsers never do.
    const bool pending_live = ua.state == VifState::WaitData || ub.state == VifState::WaitData;
    if (ua.state != ub.state || ua.latch_fill != ub.latch_fill || ua.word_index != ub.word_index || ua.fifo != ub.fifo ||
        ua.pending_bytes != ub.pending_bytes || (pending_live && ua.pending_cmd != ub.pending_cmd) || ua.unpack_phase != ub.unpack_phase ||
        ua.direct_buffer != ub.direct_buffer) {
        std::printf("  %s: VIF1 unit state differs (state %d/%d latch %u/%u wi %d/%d pending %u/%u cmd %02x/%02x fifo %zu/%zu phase %u/%u direct %zu/%zu)\n",
                    what, static_cast<int>(ua.state), static_cast<int>(ub.state), ua.latch_fill, ub.latch_fill, ua.word_index, ub.word_index,
                    ua.pending_bytes, ub.pending_bytes, ua.pending_cmd, ub.pending_cmd, ua.fifo.size(), ub.fifo.size(), ua.unpack_phase,
                    ub.unpack_phase, ua.direct_buffer.size(), ub.direct_buffer.size());
        ok = false;
    }
    if (std::memcmp(&a.ch, &b.ch, sizeof(DmacChannelRegs)) != 0) {
        std::printf("  %s: channel registers differ (chcr %08x/%08x madr %08x/%08x tadr %08x/%08x qwc %u/%u asr %08x,%08x/%08x,%08x)\n", what,
                    a.ch.chcr, b.ch.chcr, a.ch.madr, b.ch.madr, a.ch.tadr, b.ch.tadr, a.ch.qwc, b.ch.qwc, a.ch.asr0, a.ch.asr1, b.ch.asr0,
                    b.ch.asr1);
        ok = false;
    }
    if (ra.completed != rb.completed || ra.tag_count != rb.tag_count || ra.last_id != rb.last_id || ra.last_tag_lo != rb.last_tag_lo ||
        ra.last_tag_tadr != rb.last_tag_tadr || ra.last_tag_from_spr != rb.last_tag_from_spr) {
        std::printf("  %s: walk result differs (tags %d/%d last_id %u/%u last_tadr %08x/%08x)\n", what, ra.tag_count, rb.tag_count, ra.last_id,
                    rb.last_id, ra.last_tag_tadr, rb.last_tag_tadr);
        ok = false;
    }
    if (a.ops.Count() != b.ops.Count()) {
        std::printf("  %s: GIF op count differs (%zu/%zu)\n", what, a.ops.Count(), b.ops.Count());
        ok = false;
    }
    if (a.irq.irqs != b.irq.irqs) {
        std::printf("  %s: i-bit raises differ (%d/%d)\n", what, a.irq.irqs, b.irq.irqs);
        ok = false;
    }
    if (std::memcmp(a.ram.Data(), b.ram.Data(), MainMemory::kSize) != 0) {
        std::printf("  %s: RAM differs\n", what);
        ok = false;
    }
    return ok;
}

// Both rigs' RAM get the same bytes.
struct Pair {
    Rig oracle;
    Rig native;
    NativeVif nv;
    Pair()
        : nv(native.ram, native.spad, native.vif, native.walker, NativeVif::Options{true, true, std::string()}) {
        ps2r::title::sly2::NativeCensus::Instance().Reset();
    }
    void Mirror() { std::memcpy(native.ram.Data(), oracle.ram.Data(), MainMemory::kSize); }
    bool Run(uint32_t tadr, const char* what, uint32_t chcr_extra = 0) {
        Mirror();
        const DmacVifWalkResult ra = Walk(oracle, tadr, chcr_extra);
        const DmacVifWalkResult rb = Walk(native, tadr, chcr_extra);
        const bool ok = Same(oracle, native, ra, rb, what);
        CHECK(ok);
        return ok;
    }
};

// ---- the colours grammar -------------------------------------------------------------------

// Fills the geometry block (V4-32 positions, V2-16 ST under STCYCL 4/1) at `geo`, the chain at `chain`.
// Returns the TADR.
uint32_t BuildColourChain(Rig& r, uint32_t chain, uint32_t geo, uint32_t n, bool ibit, uint32_t mscal_pc, std::mt19937& rng) {
    // Geometry span: STCYCL 4/1, V4-32 x n at TOPS+0, V2-16 x n at TOPS+3 (the slots the colours interleave with).
    std::vector<uint32_t> g;
    g.push_back(Code(0x01, 0, 0x0104));
    g.push_back(Code(0x6C, n, 0x8000 | 0x000));
    for (uint32_t i = 0; i < n; i++) {
        for (int k = 0; k < 3; k++) g.push_back(rng());
        g.push_back((i % 2) ? 0x00008000u : 0u);  // the ADC flag in W
    }
    g.push_back(Code(0x65, n, 0x8000 | 0x4000 | 0x003));
    for (uint32_t i = 0; i < n; i++) g.push_back(rng());
    PadQw(g);
    std::memcpy(r.ram.Data() + geo, g.data(), g.size() * 4);
    const uint32_t geo_qwc = static_cast<uint32_t>(g.size() / 4);

    // The colour block: V4-5 x n at +1 and +2 (unsigned), STMASK 3f3f3f3f, S-8 masked x n at +1 and +2.
    std::vector<uint32_t> c;
    c.push_back(Code(0x6F, n, 0x8000 | 0x4000 | 0x001));
    for (uint32_t i = 0; i < (n + 1) / 2; i++) c.push_back(rng());
    c.push_back(Code(0x6F, n, 0x8000 | 0x4000 | 0x002));
    for (uint32_t i = 0; i < (n + 1) / 2; i++) c.push_back(rng());
    c.push_back(Code(0x20, 0, 0));
    c.push_back(0x3f3f3f3fu);
    c.push_back(Code(0x72, n, 0x8000 | 0x4000 | 0x001));
    for (uint32_t i = 0; i < (n + 3) / 4; i++) c.push_back(rng());
    c.push_back(Code(0x72, n, 0x8000 | 0x4000 | 0x002));
    for (uint32_t i = 0; i < (n + 3) / 4; i++) c.push_back(rng());
    PadQw(c);

    // The kick span: MSCAL + FLUSHE (i bit) + NOPs to the quadword.
    std::vector<uint32_t> k;
    k.push_back(Code(0x14, 0, mscal_pc / 8));
    k.push_back(Code(0x10, 0, 0, ibit));
    PadQw(k);

    uint32_t cur = chain;
    auto put = [&](const void* p, size_t n_bytes) {
        std::memcpy(r.ram.Data() + cur, p, n_bytes);
        cur += static_cast<uint32_t>(n_bytes);
    };
    auto tag = [&](uint32_t id, uint32_t qwc, uint32_t addr, bool irq = false) {
        const uint64_t lo = Builder::Tag(id, qwc, addr, irq);
        put(&lo, 8);
        const uint64_t zero = 0;
        put(&zero, 8);
    };
    tag(DmacTagId::kRef, geo_qwc, geo);
    tag(DmacTagId::kCnt, static_cast<uint32_t>(c.size() / 4), 0);
    put(c.data(), c.size() * 4);
    tag(DmacTagId::kCnt, static_cast<uint32_t>(k.size() / 4), 0);
    put(k.data(), k.size() * 4);
    tag(DmacTagId::kEnd, 0, 0);
    const uint64_t ones = ~0ull;
    put(&ones, 8);
    put(&ones, 8);
    return chain;
}

void TestColourChains() {
    std::printf("colour chains\n");
    Pair p;
    std::mt19937 rng(1234);
    for (int i = 0; i < 40; i++) {
        const uint32_t n = 1 + static_cast<uint32_t>(rng() % 60);
        const uint32_t chain = 0x100000 + static_cast<uint32_t>(i) * 0x4000;
        const uint32_t geo = chain + 0x2000;
        p.oracle.vif.regs(1).base = 0x100;
        p.oracle.vif.regs(1).ofst = 0x200;
        p.native.vif.regs(1).base = 0x100;
        p.native.vif.regs(1).ofst = 0x200;
        BuildColourChain(p.oracle, chain, geo, n, (i % 3) != 0, 0x100, rng);
        char what[64];
        std::snprintf(what, sizeof(what), "colour chain %d n=%u", i, n);
        p.Run(chain, what);
    }
    CHECK(p.native.irq.irqs > 0);
    std::printf("  irqs %d, native census: %s\n", p.native.irq.irqs, p.nv.Census().c_str());
}

// The handler writes the colour payload of the NEXT chain segment during the FLUSHE(i) stall.
void TestIbitHandlerWritesPayload() {
    std::printf("i-bit handler writes a later span's payload\n");
    Pair p;
    std::mt19937 rng(99);
    const uint32_t chain = 0x300000;
    // Span A: UNPACK normals (V4-32 x 4 at 0x10) + MSCAL + FLUSHE(i). Span B (CNT): V4-5 x 8 at 0x20 whose payload the handler fills.
    std::vector<uint32_t> a;
    a.push_back(Code(0x01, 0, 0x0101));
    a.push_back(Code(0x6C, 4, 0x010));
    for (int i = 0; i < 16; i++) a.push_back(rng());
    a.push_back(Code(0x14, 0, 0x100 / 8));
    a.push_back(Code(0x10, 0, 0, true));
    PadQw(a);
    std::vector<uint32_t> bsp;
    bsp.push_back(Code(0x6F, 8, 0x4000 | 0x020));
    for (int i = 0; i < 4; i++) bsp.push_back(0xDEADBEEFu);  // stale words the handler overwrites
    bsp.push_back(Code(0x72, 8, 0x4000 | 0x021));
    for (int i = 0; i < 2; i++) bsp.push_back(0xCAFEF00Du);
    PadQw(bsp);
    uint32_t cur = chain;
    auto put = [&](const void* q, size_t nb) {
        std::memcpy(p.oracle.ram.Data() + cur, q, nb);
        cur += static_cast<uint32_t>(nb);
    };
    auto tag = [&](uint32_t id, uint32_t qwc, uint32_t addr) {
        const uint64_t lo = Builder::Tag(id, qwc, addr);
        put(&lo, 8);
        const uint64_t z = 0;
        put(&z, 8);
    };
    tag(DmacTagId::kCnt, static_cast<uint32_t>(a.size() / 4), 0);
    put(a.data(), a.size() * 4);
    const uint32_t b_tag = cur;
    tag(DmacTagId::kCnt, static_cast<uint32_t>(bsp.size() / 4), 0);
    put(bsp.data(), bsp.size() * 4);
    tag(DmacTagId::kEnd, 0, 0);
    const uint64_t ones = ~0ull;
    put(&ones, 8);
    put(&ones, 8);
    // Both handlers write the same new payload words at the V4-5 payload (b_tag + 16 + 4).
    for (Rig* r : {&p.oracle, &p.native}) {
        r->irq.write_addr = b_tag + 16 + 4;
        r->irq.write_words = {0x12345678u, 0x9abcdef0u, 0x0f0f0f0fu, 0x7c1f83e0u};
    }
    p.Run(chain, "handler payload");
    CHECK(p.oracle.irq.irqs == 1);
    // The VU1 memory at 0x20 holds the handler's colours, not the stale words, on both rigs.
    uint32_t v[4];
    std::memcpy(v, p.native.vu1.state().data + 0x20 * 16, 16);
    CHECK(v[0] == ((0x5678u & 0x1F) << 3));
}

// Every shape the executor hands back, each as its own chain, compared after the walk.
void TestHandBack() {
    std::printf("hand-back shapes\n");
    Pair p;
    std::mt19937 rng(7);
    uint32_t base = 0x400000;
    auto chain_of = [&](const std::vector<std::vector<uint32_t>>& spans, const std::vector<uint32_t>& ids, const char* what,
                        uint32_t chcr_extra = 0, bool terminator = true) {
        uint32_t cur = base;
        auto put = [&](const void* q, size_t nb) {
            std::memcpy(p.oracle.ram.Data() + cur, q, nb);
            cur += static_cast<uint32_t>(nb);
        };
        for (size_t i = 0; i < spans.size(); i++) {
            std::vector<uint32_t> s = spans[i];
            PadQw(s);
            const uint64_t lo = Builder::Tag(ids[i], static_cast<uint32_t>(s.size() / 4), 0);
            put(&lo, 8);
            const uint32_t hi0 = Code(0x01, 0, 0x0101), hi1 = Code(0x00, 0, 0);  // TTE words: STCYCL + NOP
            put(&hi0, 4);
            put(&hi1, 4);
            put(s.data(), s.size() * 4);
        }
        if (terminator) {
            const uint64_t ones = ~0ull;
            put(&ones, 8);
            put(&ones, 8);
        }
        p.Run(base, what, chcr_extra);
        base += 0x10000;
    };
    auto unpack = [&](uint32_t cmd, uint32_t n, uint32_t addr, uint32_t words) {
        std::vector<uint32_t> s;
        s.push_back(Code(cmd, n, addr));
        for (uint32_t i = 0; i < words; i++) s.push_back(rng());
        return s;
    };
    // DIRECT in a span (PATH2 packet of 2 quadwords: a GIF tag with NLOOP 1, A+D of one register).
    {
        std::vector<uint32_t> s = unpack(0x6C, 2, 0x40, 8);
        s.push_back(Code(0x50, 0, 2));
        PadQw(s);
        s.insert(s.end(), {0x00008001u, 0x10000000u, 0x0000000Eu, 0u, 0x11223344u, 0u, 0x0000003Fu, 0u});  // tag + A+D 0x3F (TEXFLUSH)
        chain_of({s, unpack(0x6C, 1, 0x50, 4)}, {DmacTagId::kCnt, DmacTagId::kEnd}, "DIRECT in span");
    }
    // MPG (8 bytes x 2) then an UNPACK.
    {
        std::vector<uint32_t> s;
        s.push_back(Code(0x4A, 2, 0x200));
        s.insert(s.end(), {L_NOP, U_NOP | U_EBIT, L_NOP, U_NOP | U_EBIT});
        std::vector<uint32_t> u = unpack(0x6C, 3, 0x60, 12);
        s.insert(s.end(), u.begin(), u.end());
        chain_of({s}, {DmacTagId::kEnd}, "MPG");
    }
    // A payload crossing the span end: V4-32 x 4 with only 2 vectors in the first CNT, the rest in the next.
    {
        std::vector<uint32_t> s1 = unpack(0x6C, 4, 0x70, 8);  // 8 words of 16: the span ends mid-payload
        std::vector<uint32_t> s2;
        for (int i = 0; i < 8; i++) s2.push_back(rng());
        std::vector<uint32_t> s3 = unpack(0x6F, 5, 0x4000 | 0x80, 3);
        s2.insert(s2.end(), s3.begin(), s3.end());
        chain_of({s1, s2}, {DmacTagId::kCnt, DmacTagId::kEnd}, "payload crossing the span end");
    }
    // The filling write (CL 1 / WL 2) and V3-32 (W from the next word), V4-16, V3-8, V4-8, S-32: the engine's loops.
    {
        std::vector<uint32_t> s;
        s.push_back(Code(0x01, 0, 0x0201));
        std::vector<uint32_t> u = unpack(0x6C, 6, 0x90, 12);
        s.insert(s.end(), u.begin(), u.end());
        s.push_back(Code(0x01, 0, 0x0101));
        u = unpack(0x68, 3, 0xA0, 9);
        s.insert(s.end(), u.begin(), u.end());
        u = unpack(0x6D, 2, 0xB0, 4);
        s.insert(s.end(), u.begin(), u.end());
        u = unpack(0x6A, 3, 0xC0, 3);
        s.insert(s.end(), u.begin(), u.end());
        u = unpack(0x6E, 2, 0xD0, 2);
        s.insert(s.end(), u.begin(), u.end());
        u = unpack(0x60, 2, 0xE0, 2);
        s.insert(s.end(), u.begin(), u.end());
        chain_of({s}, {DmacTagId::kEnd}, "filling write + other formats");
    }
    // MODE 1/2/3 with STROW/STCOL, masked V4-32 and V2-16 under CL 3 / WL 2.
    {
        std::vector<uint32_t> s;
        s.push_back(Code(0x30, 0, 0));
        s.insert(s.end(), {1u, 2u, 3u, 4u});
        s.push_back(Code(0x31, 0, 0));
        s.insert(s.end(), {10u, 20u, 30u, 40u});
        s.push_back(Code(0x01, 0, 0x0203));
        s.push_back(Code(0x05, 0, 1));
        std::vector<uint32_t> u = unpack(0x7C, 5, 0x100, 20);
        s.insert(s.end(), u.begin(), u.end());
        s.push_back(Code(0x20, 0, 0));
        s.push_back(0x5a5a5a5au);
        s.push_back(Code(0x05, 0, 2));
        u = unpack(0x75, 7, 0x120, 7);
        s.insert(s.end(), u.begin(), u.end());
        s.push_back(Code(0x05, 0, 3));
        u = unpack(0x72, 9, 0x130, 3);
        s.insert(s.end(), u.begin(), u.end());
        s.push_back(Code(0x05, 0, 0));
        s.push_back(Code(0x01, 0, 0x0101));
        chain_of({s}, {DmacTagId::kEnd}, "MODE + masked + CL3/WL2");
    }
    // TTE: the tag's upper words are fed (STCYCL + NOP), the whole chain is the walker's.
    chain_of({unpack(0x6C, 2, 0x140, 8)}, {DmacTagId::kEnd}, "TTE", DmacChcr::kTte);
    // CALL / RET: CNT -> CALL(sub) -> RET -> END; NEXT.
    {
        uint32_t cur = base;
        auto put = [&](const void* q, size_t nb) {
            std::memcpy(p.oracle.ram.Data() + cur, q, nb);
            cur += static_cast<uint32_t>(nb);
        };
        auto tag = [&](uint32_t id, uint32_t qwc, uint32_t addr, bool irq = false) {
            const uint64_t lo = Builder::Tag(id, qwc, addr, irq);
            put(&lo, 8);
            const uint64_t z = 0;
            put(&z, 8);
        };
        std::vector<uint32_t> s1 = unpack(0x6C, 1, 0x150, 4);
        std::vector<uint32_t> s2 = unpack(0x6F, 2, 0x4000 | 0x160, 1);
        PadQw(s2);
        std::vector<uint32_t> s3 = unpack(0x72, 4, 0x4000 | 0x170, 1);
        PadQw(s3);
        const uint32_t sub = base + 0x1000;
        const uint32_t nxt = base + 0x2000;
        tag(DmacTagId::kCnt, 2, 0);
        put(s1.data(), 16);  // one quadword + one NOP quadword
        const uint32_t nops[4] = {0, 0, 0, 0};
        put(nops, 16);
        tag(DmacTagId::kCall, 1, sub);
        put(s2.data(), 16);
        tag(DmacTagId::kNext, 1, nxt);
        put(s3.data(), 16);
        // the subroutine
        cur = sub;
        tag(DmacTagId::kRet, 1, 0);
        put(s3.data(), 16);
        // the NEXT target: REFE to the geometry-like block, IRQ+TIE
        cur = nxt;
        tag(DmacTagId::kRefe, 1, base + 0x3000, true);
        cur = base + 0x3000;
        put(s2.data(), 16);
        p.Run(base, "CALL/RET/NEXT/REFE", DmacChcr::kTie);
        base += 0x10000;
    }
    // An unreadable REF span (address past RAM) -- the walker keeps QWC; then END.
    {
        uint32_t cur = base;
        auto put = [&](const void* q, size_t nb) {
            std::memcpy(p.oracle.ram.Data() + cur, q, nb);
            cur += static_cast<uint32_t>(nb);
        };
        auto tag = [&](uint32_t id, uint32_t qwc, uint32_t addr) {
            const uint64_t lo = Builder::Tag(id, qwc, addr);
            put(&lo, 8);
            const uint64_t z = 0;
            put(&z, 8);
        };
        tag(DmacTagId::kRef, 4, 0x01FFFFF0u);
        tag(DmacTagId::kEnd, 0, 0);
        p.Run(base, "unreadable REF span");
        base += 0x10000;
    }
    // An END with data, no terminator after it (the walk ends on END).
    chain_of({unpack(0x6C, 2, 0x180, 8)}, {DmacTagId::kEnd}, "END with data, no terminator", 0, false);
    // An undefined format (0x63) and an undefined command (0x3F), both with the i bit.
    {
        std::vector<uint32_t> s;
        s.push_back(Code(0x63, 3, 0x190, true));
        s.push_back(Code(0x3F, 0, 0, true));
        std::vector<uint32_t> u = unpack(0x6C, 1, 0x1A0, 4);
        s.insert(s.end(), u.begin(), u.end());
        chain_of({s}, {DmacTagId::kEnd}, "undefined format / command");
    }
    // Scratchpad-prefixed TADR: the chain lives in the scratchpad.
    {
        std::vector<uint32_t> s = unpack(0x6C, 2, 0x1B0, 8);
        PadQw(s);
        for (Rig* r : {&p.oracle, &p.native}) {
            uint8_t* sp = r->spad.Pointer(0x100, 16 + s.size() * 4 + 32);
            const uint64_t lo = Builder::Tag(DmacTagId::kEnd, static_cast<uint32_t>(s.size() / 4), 0);
            std::memcpy(sp, &lo, 8);
            std::memset(sp + 8, 0, 8);
            std::memcpy(sp + 16, s.data(), s.size() * 4);
            std::memset(sp + 16 + s.size() * 4, 0xFF, 16);
        }
        p.Run(0x70000100u, "scratchpad chain");
    }
    std::printf("  native census: %s\n", p.nv.Census().c_str());
}

// Random chains over the grammar: tags CNT/REF/END with random spans of the title's codes,
// random nums, VU addresses (wrapping), CL/WL, masks, modes, formats, i bits.
void TestRandomChains() {
    std::printf("random chains\n");
    Pair p;
    std::mt19937 rng(2026);
    int chains = 0;
    for (int it = 0; it < 800; it++) {
        const uint32_t base = 0x800000 + static_cast<uint32_t>(it % 64) * 0x8000;
        const uint32_t geo = base + 0x4000;
        uint32_t cur = base;
        uint32_t gcur = geo;
        auto put = [&](const void* q, size_t nb) {
            std::memcpy(p.oracle.ram.Data() + cur, q, nb);
            cur += static_cast<uint32_t>(nb);
        };
        auto tag = [&](uint32_t id, uint32_t qwc, uint32_t addr, bool irq) {
            const uint64_t lo = Builder::Tag(id, qwc, addr, irq);
            put(&lo, 8);
            const uint64_t z = 0;
            put(&z, 8);
        };
        const int ntags = 1 + static_cast<int>(rng() % 5);
        for (int t = 0; t < ntags; t++) {
            std::vector<uint32_t> s;
            const int ncodes = 1 + static_cast<int>(rng() % 5);
            for (int c = 0; c < ncodes; c++) {
                const uint32_t pick = rng() % 16;
                if (pick < 9) {
                    static const uint32_t kFormats[] = {0x6C, 0x65, 0x6F, 0x72, 0x7C, 0x75, 0x7F, 0x62, 0x68, 0x6D, 0x6E, 0x6A, 0x60, 0x66, 0x7A};
                    const uint32_t cmd = kFormats[rng() % 15];
                    const uint32_t n = (rng() % 20 == 0) ? 0 : 1 + rng() % 40;  // 0 = 256 vectors
                    const uint32_t imm = (rng() & 0x3FF) | ((rng() & 1) ? 0x4000 : 0) | ((rng() & 1) ? 0x8000 : 0);
                    const uint32_t cl = 1 + rng() % 4, wl = 1 + rng() % 4;
                    s.push_back(Code(0x01, 0, cl | (wl << 8)));
                    s.push_back(Code(cmd, n, imm, (rng() % 7) == 0));
                    const uint32_t bytes = VifUnpack::GetUnpackSize(cmd, n, cl, wl);
                    for (uint32_t w = 0; w < (bytes + 3) / 4; w++) s.push_back(rng());
                } else if (pick == 9) {
                    s.push_back(Code(0x20, 0, 0));
                    s.push_back(rng());
                } else if (pick == 10) {
                    s.push_back(Code(0x05, 0, rng() % 4));
                } else if (pick == 11) {
                    s.push_back(Code(0x30 + (rng() & 1), 0, 0));
                    for (int k = 0; k < 4; k++) s.push_back(rng());
                } else if (pick == 12) {
                    s.push_back(Code(0x14, 0, 0x100 / 8));
                    s.push_back(Code(0x10, 0, 0, (rng() & 1) != 0));
                } else if (pick == 13) {
                    s.push_back(Code(0x03, 0, rng() & 0x3FF));
                    s.push_back(Code(0x02, 0, rng() & 0x3FF));
                } else if (pick == 14) {
                    s.push_back(Code(0x00, 0, 0, (rng() % 5) == 0));
                    s.push_back(Code(0x07, 0, rng() & 0xFFFF));
                } else {
                    s.push_back(Code(0x04, 0, rng() & 0x3FF));
                }
            }
            if ((rng() % 10) == 0) {
                // A DIRECT (PATH2) packet: one GIF tag (NLOOP 1, A+D) + one register write.
                s.push_back(Code(0x50, 0, 2));
                PadQw(s);
                s.insert(s.end(), {0x00008001u, 0x10000000u, 0x0000000Eu, 0u, 0x11223344u, 0u, 0x0000003Fu, 0u});
            }
            PadQw(s);
            const bool last = (t == ntags - 1);
            const uint32_t id = last ? ((rng() & 1) ? DmacTagId::kEnd : DmacTagId::kRefe) : ((rng() & 1) ? DmacTagId::kCnt : DmacTagId::kRef);
            // One span in eight is split over two CNT tags at a random quadword edge: a payload crossing the span end.
            size_t split = s.size();
            if (!last && id == DmacTagId::kCnt && s.size() >= 8 && (rng() % 8) == 0) split = 4 * (1 + rng() % (s.size() / 4 - 1));
            if (id == DmacTagId::kCnt || id == DmacTagId::kEnd) {
                tag(id, static_cast<uint32_t>(split / 4), 0, (rng() % 9) == 0);
                put(s.data(), split * 4);
                if (split < s.size()) {
                    tag(DmacTagId::kCnt, static_cast<uint32_t>((s.size() - split) / 4), 0, false);
                    put(s.data() + split, (s.size() - split) * 4);
                }
            } else {
                std::memcpy(p.oracle.ram.Data() + gcur, s.data(), s.size() * 4);
                tag(id, static_cast<uint32_t>(s.size() / 4), gcur, (rng() % 9) == 0);
                gcur += static_cast<uint32_t>(s.size() * 4);
            }
        }
        const uint64_t ones = ~0ull;
        put(&ones, 8);
        put(&ones, 8);
        char what[48];
        std::snprintf(what, sizeof(what), "random chain %d", it);
        const uint32_t extra = ((rng() & 1) ? DmacChcr::kTie : 0) | ((rng() % 6 == 0) ? DmacChcr::kTte : 0);
        if (!p.Run(base, what, extra)) break;
        chains++;
    }
    std::printf("  %d chains, native census: %s\n", chains, p.nv.Census().c_str());
}

}  // namespace

int main() {
    TestColourChains();
    TestIbitHandlerWritesPayload();
    TestHandBack();
    TestRandomChains();
    if (g_fail) {
        std::printf("%d FAILURES\n", g_fail);
        return 1;
    }
    std::printf("Sly2_NativeVifChains: all passed\n");
    return 0;
}
