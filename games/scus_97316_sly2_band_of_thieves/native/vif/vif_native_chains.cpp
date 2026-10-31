#include "games/scus_97316_sly2_band_of_thieves/native/vif/vif_native_chains.h"

#include <cstring>

#include "engine/ee/mem/scratchpad.h"
#include "games/scus_97316_sly2_band_of_thieves/native/native_census.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vif/vif_chain_tags.h"

namespace {

using ps2r::title::sly2::NativeCensus;
using ps2r::title::sly2::NativeUnit;

constexpr int kVif1 = static_cast<int>(DmacChannelId::Vif1);

}  // namespace

VifNativeChains::VifNativeChains(const MainMemory& ram, Scratchpad& spad, ChainWalker& walker)
    : ram_(ram), spad_(spad), walker_(walker) {}

ChainWalker::NativeChainStep VifNativeChains::Step(int ch, DmacChannelRegs& regs, DmacVifWalkResult& res) {
    if (ch != kVif1) return ChainWalker::NativeChainStep{};
    const uint64_t t0 = tags_;
    const ChainWalker::NativeChainStep step = Walk(regs, res);
    // The per-unit census once per chain (the counters are shared atomics): tags walked here.
    NativeCensus::Instance().Served(NativeUnit::Dmac, tags_ - t0);
    return step;
}

ChainWalker::NativeChainStep VifNativeChains::Walk(DmacChannelRegs& regs, DmacVifWalkResult& res) {
    using namespace DmacChcr;
    ChainWalker::NativeChainStep step;
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
            walker_.FeedVif1(qw, 16, true);
            tte_quadwords_++;
        }
        if (transfer && t.qwc > 0) {
            const size_t bytes = static_cast<size_t>(t.qwc) * 16;
            const uint8_t* span = VifChainTags::Span(ram_, spad_, src, src_spr, bytes);
            if (span) {
                // The walker's own feed: the span executor or the engine's parser inline, the staged copy under MTVU.
                walker_.FeedVif1(span, bytes, false);
                spans_fed_++;
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
    return "chains=" + std::to_string(chains_) + ":full=" + std::to_string(chains_full_) + ":tags=" + std::to_string(tags_) +
           ":spans=" + std::to_string(spans_fed_) + "/" + std::to_string(spans_unreadable_) + ":tte_qw=" + std::to_string(tte_quadwords_);
}
