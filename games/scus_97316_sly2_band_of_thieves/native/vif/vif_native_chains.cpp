#include "games/scus_97316_sly2_band_of_thieves/native/vif/vif_native_chains.h"

#include <cstring>

#include "engine/ee/mem/scratchpad.h"
#include "engine/vif/vif.h"
#include "engine/vif/vif_regs.h"
#include "engine/vif/vif_unpack.h"
#include "engine/vu/vu_unit.h"
#include "games/scus_97316_sly2_band_of_thieves/native/native_census.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vif/vif_chain_tags.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vif/vif_unpack_native.h"

namespace {

using ps2r::title::sly2::NativeCensus;
using ps2r::title::sly2::NativeUnit;

constexpr int kVif1 = static_cast<int>(DmacChannelId::Vif1);

constexpr uint32_t kCmdStcycl = 0x01;
constexpr uint32_t kCmdStmask = 0x20;
constexpr uint32_t kCmdStrow = 0x30;
constexpr uint32_t kCmdStcol = 0x31;
constexpr uint32_t kCmdMpg = 0x4A;
constexpr uint32_t kCmdDirect = 0x50;
constexpr uint32_t kCmdDirecthl = 0x51;

inline uint32_t Word(const uint8_t* data, size_t w) {
    uint32_t x;
    std::memcpy(&x, data + w * 4, 4);
    return x;
}

// The unit is between commands with nothing buffered: a span starting here parses
// exactly as the engine's whole-span parser would parse it (its admission set).
bool UnitClean(const Vif& vif) {
    const VifUnitState& u = vif.unit(1);
    return u.state == VifState::Idle && u.latch_fill == 0 && u.word_index >= 4 && u.fifo.empty() && !u.tte_feed;
}

}  // namespace

VifNativeChains::VifNativeChains(const MainMemory& ram, Scratchpad& spad, Vif& vif, ChainWalker& walker, bool typed_unpack)
    : ram_(ram), spad_(spad), vif_(vif), walker_(walker), typed_unpack_(typed_unpack) {}

// The admission of one span: every code's payload inside the span and no DIRECT /
// DIRECTHL (their quadword framing and PATH2 stream are the engine's). Reads code
// words and payload sizes only, tracking the STCYCL in force. A span that is not
// admitted goes whole to Vif::ProcessData, exactly as the walker feeds it.
bool VifNativeChains::Admit(const uint8_t* data, size_t bytes, uint32_t cycle) const {
    const size_t nwords = bytes / 4;
    uint32_t cl = cycle & 0xFFu, wl = (cycle >> 8) & 0xFFu;
    size_t w = 0;
    while (w < nwords) {
        const uint32_t code = Word(data, w);
        const uint32_t cmd = (code >> 24) & 0x7F;
        const uint32_t num = (code >> 16) & 0xFF;
        const uint32_t imm = code & 0xFFFF;
        const size_t s = w + 1;
        if (cmd == kCmdDirect || cmd == kCmdDirecthl) return false;
        size_t words = 0;
        if (VifUnpack::IsUnpackCmd(cmd)) {
            words = (VifUnpack::GetUnpackSize(cmd, num, cl, wl) + 3) / 4;
        } else if (cmd == kCmdStcycl) {
            cl = imm & 0xFFu;
            wl = (imm >> 8) & 0xFFu;
        } else if (cmd == kCmdStmask) {
            words = 1;
        } else if (cmd == kCmdStrow || cmd == kCmdStcol) {
            words = 4;
        } else if (cmd == kCmdMpg) {
            words = static_cast<size_t>(num == 0 ? 256 : num) * 2;
        }
        if (words > nwords - s) return false;
        w = s + words;
    }
    return true;
}

// One admitted span: the title's UNPACK loops where they apply, every other code
// through the engine's own executor. A code the admission did not see (rewritten
// under the i-bit handler) hands the remainder to Vif::ProcessData -- exact when
// it starts on a quadword edge (the unit's framing), counted when it does not.
void VifNativeChains::Execute(const uint8_t* data, size_t bytes) {
    const size_t nwords = bytes / 4;
    VifRegs& r = vif_.regs(1);
    uint8_t* dmem = vif_.CtxFor(1).state().data;
    size_t w = 0;
    while (w < nwords) {
        const uint32_t code = Word(data, w);
        const bool irq = (code >> 31) != 0;
        const uint32_t cmd = (code >> 24) & 0x7F;
        const uint32_t num = (code >> 16) & 0xFF;
        const uint32_t imm = code & 0xFFFF;
        const size_t s = w + 1;  // payload start word

        if (cmd == kCmdDirect || cmd == kCmdDirecthl) {
            // Not admitted: the word changed under the i-bit handler. The engine's feed takes the rest.
            rewritten_++;
            if ((w & 3) != 0) rewritten_misaligned_++;
            vif_.ProcessData(1, data + w * 4, bytes - w * 4);
            return;
        }

        if (VifUnpack::IsUnpackCmd(cmd)) {
            const bool use_mask = (cmd & 0x10) != 0;
            uint32_t cl = VifRegsCl(r), wl = VifRegsWl(r);
            const uint32_t size = VifUnpack::GetUnpackSize(cmd, num, cl, wl);
            if (size == 0) {
                // An undefined format: the engine's executor ignores it (and raises the i bit).
                r.code = code;
                vif_.ExecuteCommand(1, cmd, imm, num, nullptr, 0, irq, use_mask);
                codes_engine_++;
                w = s;
                continue;
            }
            const size_t words = (size + 3) / 4;
            if (words > nwords - s) {
                rewritten_++;
                if ((w & 3) != 0) rewritten_misaligned_++;
                vif_.ProcessData(1, data + w * 4, bytes - w * 4);
                return;
            }
            r.code = code;
            vif_.SetUnpackPhase(1, static_cast<uint8_t>(s & 1));
            if (cl == 0) cl = 1;
            if (wl == 0) wl = 1;
            // The payload as the engine's executor sees it: in place, with the lookahead word
            // after it when the payload does not end on a quadword edge, else a copy + a zero word.
            const uint8_t* payload = data + s * 4;
            size_t psize = words * 4;
            const bool edge = ((s + words) & 3) == 0;
            VifUnpackNative::Command c;
            c.format = cmd & 0xF;
            c.masked = use_mask;
            c.usn = (imm & 0x4000) != 0;
            c.addr_qw = imm & 0x3FFu;
            if (imm & 0x8000) c.addr_qw = (c.addr_qw + r.tops) & 0x3FFu;
            c.n = num == 0 ? 256 : num;
            c.cl = cl;
            c.wl = wl;
            c.mode = r.mode;
            c.phase = static_cast<uint8_t>(s & 1);
            c.src = payload;
            c.src_bytes = psize;
            const bool lookahead = VifUnpackNative::NeedsLookahead(c.format);
            if (lookahead || !(typed_unpack_ && VifUnpackNative::Supports(c))) {
                if (!edge) {
                    psize += 4;
                } else {
                    scratch_.assign(payload, payload + words * 4);
                    scratch_.insert(scratch_.end(), 4, 0);
                    payload = scratch_.data();
                    psize = scratch_.size();
                }
                c.src = payload;
                c.src_bytes = psize;
            }
            if (typed_unpack_ && VifUnpackNative::Supports(c)) {
                VifUnpackNative::Run(c, r, dmem);
                unpack_typed_++;
                NativeCensus::Instance().Served(NativeUnit::Vif);
                if (irq) {
                    ibit_raised_++;
                    vif_.RaiseIBit(1);
                }
            } else {
                vif_.ExecuteCommand(1, cmd, imm, num, payload, psize, irq, use_mask);
                unpack_engine_++;
                NativeCensus::Instance().Fallback(NativeUnit::Vif);
                if (irq) ibit_raised_++;
            }
            w = s + words;
            continue;
        }

        size_t words = 0;
        if (cmd == kCmdStmask) words = 1;
        else if (cmd == kCmdStrow || cmd == kCmdStcol) words = 4;
        else if (cmd == kCmdMpg) words = static_cast<size_t>(num == 0 ? 256 : num) * 2;
        if (words > nwords - s) {
            rewritten_++;
            if ((w & 3) != 0) rewritten_misaligned_++;
            vif_.ProcessData(1, data + w * 4, bytes - w * 4);
            return;
        }
        r.code = code;
        if (words) vif_.SetUnpackPhase(1, static_cast<uint8_t>(s & 1));  // the whole-span parser's phase latch, for every payload
        vif_.ExecuteCommand(1, cmd, imm, num, words ? data + s * 4 : nullptr, words * 4, irq, false);
        codes_engine_++;
        if (irq) ibit_raised_++;
        w = s + words;
    }
}

ChainWalker::NativeChainStep VifNativeChains::Step(int ch, DmacChannelRegs& regs, DmacVifWalkResult& res) {
    using namespace DmacChcr;
    ChainWalker::NativeChainStep step;
    if (ch != kVif1) return step;
    chains_++;
    const bool tte = (regs.chcr & kTte) != 0;
    const bool tie = (regs.chcr & kTie) != 0;
    uint32_t tadr = regs.tadr & VifChainTags::kTadrMask;
    bool from_spr = Scratchpad::Contains(regs.tadr);

    // The walker's loop restated: every tag of the chain is this executor's; a span
    // is run natively when the unit is idle and the span is admitted, else it is fed
    // to the engine exactly as the walker feeds it. The chain ends on the walker's
    // rules (REFE / END / empty-stack RET / IRQ+TIE / the tag guard) and on a
    // terminator or unreadable tag, which the walker reads itself after this step.
    while (step.tags < ChainWalker::kMaxTags) {
        const VifChainTag t = VifChainTags::Read(ram_, spad_, tadr, from_spr);
        if (!t.readable || t.terminator) break;

        uint32_t src = 0;
        bool src_spr = false;
        bool transfer = false;
        bool end = false;
        uint32_t next = tadr;
        bool next_spr = from_spr;
        const uint32_t after = tadr + 16 + t.qwc * 16;
        uint32_t chcr = (regs.chcr & 0xFFFFu) | (static_cast<uint32_t>(t.lo) & kTagMask);
        switch (t.id) {
            case DmacTagId::kRefe: src = t.addr; src_spr = t.spr; next = tadr + 16; end = true; transfer = true; break;
            case DmacTagId::kCnt: src = tadr + 16; src_spr = from_spr; next = after; transfer = true; break;
            case DmacTagId::kNext: src = tadr + 16; src_spr = from_spr; next = t.addr; next_spr = t.spr; transfer = t.qwc > 0; break;
            case DmacTagId::kRef:
            case DmacTagId::kRefs: src = t.addr; src_spr = t.spr; next = tadr + 16; transfer = true; break;
            case DmacTagId::kCall: {
                src = tadr + 16;
                src_spr = from_spr;
                const uint32_t asp = Asp(chcr);
                if (asp == 0) { regs.asr0 = after; chcr = WithAsp(chcr, 1); }
                else if (asp == 1) { regs.asr1 = after; chcr = WithAsp(chcr, 2); }
                next = t.addr;
                next_spr = t.spr;
                transfer = true;
                break;
            }
            case DmacTagId::kRet: {
                src = tadr + 16;
                src_spr = from_spr;
                const uint32_t asp = Asp(chcr);
                if (asp == 2) { next = regs.asr1; chcr = WithAsp(chcr, 1); }
                else if (asp == 1) { next = regs.asr0; chcr = WithAsp(chcr, 0); }
                else end = true;
                transfer = true;
                break;
            }
            case DmacTagId::kEnd: src = tadr + 16; src_spr = from_spr; next = after; end = true; transfer = t.qwc > 0; break;
            default: end = true; break;
        }
        res.last_tag_lo = t.lo;
        res.last_tag_tadr = tadr;
        res.last_tag_from_spr = from_spr;
        res.last_id = t.id;
        regs.chcr = chcr;
        regs.madr = src;
        regs.tadr = next;
        regs.qwc = t.qwc;

        if (tte) {
            // The TTE quadword: the tag's upper two words, zero-padded (the unit skips the pad).
            uint8_t qw[16];
            std::memset(qw, 0, sizeof(qw));
            std::memcpy(qw, &t.hi, 8);
            vif_.SetTteFeed(1, true);
            vif_.ProcessData(1, qw, 16);
            vif_.SetTteFeed(1, false);
            tte_quadwords_++;
        }
        if (transfer && t.qwc > 0) {
            const size_t bytes = static_cast<size_t>(t.qwc) * 16;
            const uint8_t* span = VifChainTags::Span(ram_, spad_, src, src_spr, bytes);
            if (span) {
                if (UnitClean(vif_) && Admit(span, bytes, vif_.regs(1).cycle)) {
                    Execute(span, bytes);
                    spans_native_++;
                    NativeCensus::Instance().Served(NativeUnit::Dmac);
                } else {
                    // The engine's feed takes the whole span (the walker's own call).
                    spans_engine_++;
                    NativeCensus::Instance().Fallback(NativeUnit::Dmac);
                    vif_.ProcessData(1, span, bytes);
                }
                regs.madr += static_cast<uint32_t>(bytes);
                regs.qwc = 0;
            } else {
                spans_unreadable_++;  // the walker's rule: nothing fed, MADR/QWC left at the span
            }
        }
        const uint32_t base = from_spr ? Scratchpad::Offset(tadr) : (tadr & VifChainTags::kRamMask);
        if (regs.qwc == 0) walker_.ObserveVif1Tag(base, from_spr, step.tags == 0);
        step.tags++;
        tags_++;
        if (t.irq && tie) end = true;
        tadr = next & VifChainTags::kTadrMask;
        from_spr = next_spr;
        step.next_from_spr = next_spr;
        if (end) {
            step.ended = true;
            chains_full_++;
            return step;
        }
    }
    if (step.tags >= ChainWalker::kMaxTags) {
        step.ended = true;  // the walker's guard: the chain completes here
        chains_full_++;
    }
    return step;
}

std::string VifNativeChains::Row() const {
    return "chains=" + std::to_string(chains_) + ":full=" + std::to_string(chains_full_) + ":tags=" + std::to_string(tags_) + ":spans=" +
           std::to_string(spans_native_) + "/" + std::to_string(spans_engine_) + "/" + std::to_string(spans_unreadable_) + ":tte_qw=" +
           std::to_string(tte_quadwords_) + ":unpack=" + std::to_string(unpack_typed_) + "/" + std::to_string(unpack_engine_) + ":codes_engine=" +
           std::to_string(codes_engine_) + ":ibit=" + std::to_string(ibit_raised_) + ":rewritten=" + std::to_string(rewritten_) + "/" +
           std::to_string(rewritten_misaligned_);
}
