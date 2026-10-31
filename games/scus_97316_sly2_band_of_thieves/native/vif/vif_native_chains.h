#pragma once
// VifNativeChains: Sly 2's VIF1 DMA chains walked by the title. The engine's walker
// offers every VIF1 chain at its head (ChainWalker::SetVifChainExecutor) -- the inline
// walk on the lane, the staged walk under the MTVU worker -- and this executor walks
// the WHOLE chain with the walker's own tag rules (REFE/CNT/NEXT/REF/REFS/CALL/RET/END,
// the ASR stack, TTE, IRQ+TIE, the tag guard), leaving the channel registers exactly as
// the engine's walk would. Every span and TTE quadword is fed through the walker's own
// VIF1 feed (ChainWalker::FeedVif1: Vif::ProcessData inline, the worker's staged
// copy under MTVU), where the title's span executor (VifNativeSpans, title.native.vif)
// or the engine's parser takes it -- so the two walks differ only in who reads the tags.
#include <cstddef>
#include <cstdint>
#include <string>

#include "engine/dmac/chain_walker.h"
#include "engine/dmac/dmac_channel.h"

class MainMemory;
class Scratchpad;

class VifNativeChains {
public:
    VifNativeChains(const MainMemory& ram, Scratchpad& spad, ChainWalker& walker);

    // The executor body (see ChainWalker::ChainExecutor). VIF1 only.
    ChainWalker::NativeChainStep Step(int ch, DmacChannelRegs& regs, DmacVifWalkResult& res);

    std::string Row() const;

    uint64_t chains() const { return chains_; }
    uint64_t tags() const { return tags_; }
    uint64_t chains_full() const { return chains_full_; }
    uint64_t spans_fed() const { return spans_fed_; }

private:
    ChainWalker::NativeChainStep Walk(DmacChannelRegs& regs, DmacVifWalkResult& res);

    const MainMemory& ram_;
    Scratchpad& spad_;
    ChainWalker& walker_;

    uint64_t chains_ = 0;
    uint64_t chains_full_ = 0;       // chains this executor ran to their end (the rest ended on a terminator the walker read)
    uint64_t tags_ = 0;
    uint64_t spans_fed_ = 0;         // spans fed (the span executor or the engine's parser took them)
    uint64_t spans_unreadable_ = 0;  // spans outside the memory named (nothing fed, the walker's rule)
    uint64_t tte_quadwords_ = 0;
};
