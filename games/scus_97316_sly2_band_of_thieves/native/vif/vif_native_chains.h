#pragma once
// VifNativeChains: Sly 2's VIF1 DMA chains executed by the title. The engine's
// walker offers every inline VIF1 chain at its head (ChainWalker::
// SetVifChainExecutor); this executor walks the WHOLE chain with the walker's own
// tag rules (REFE/CNT/NEXT/REF/REFS/CALL/RET/END, the ASR stack, TTE, IRQ+TIE, the
// tag guard) and leaves the channel registers, the VIF unit and VU1 data memory
// exactly as the engine's walk + Vif::ProcessData would have. Each tag's span is
// executed here when the unit is idle at the tag and the span is admitted; every
// other span is fed to the engine's Vif::ProcessData -- the walker's own call, the
// fallback and the oracle -- so the two paths differ only in who parses a span.
//
// A span is ADMITTED when every payload lies inside it and it holds no DIRECT
// (the PATH2 stream and its quadword framing are the engine's). UNPACK V4-32 / V2-16 / V4-5 / S-8 under CL >= WL go through the title's
// typed loops (VifUnpackNative) straight into VU1 data memory; every other code
// -- and every other UNPACK shape -- is replayed through Vif::ExecuteCommand, the
// engine's own command executor. The i bit is preserved: a code carrying it
// raises INT|VIS and runs the composition's stall hook (Vif::RaiseIBit) right
// after its effect, before the next word is read -- the VIF1 interrupt handler
// that fills the colour payloads further down the chain runs then, and the
// later spans are read from memory after it (never cached).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/dmac/chain_walker.h"
#include "engine/dmac/dmac_channel.h"

class MainMemory;
class Scratchpad;
class Vif;

class VifNativeChains {
public:
    // `typed_unpack` = title.native.vif: the typed loops; off = every UNPACK through Vif::ExecuteCommand.
    VifNativeChains(const MainMemory& ram, Scratchpad& spad, Vif& vif, ChainWalker& walker, bool typed_unpack);

    // The executor body (see ChainWalker::ChainExecutor). VIF1 only.
    ChainWalker::NativeChainStep Step(int ch, DmacChannelRegs& regs, DmacVifWalkResult& res);

    std::string Row() const;

    uint64_t chains() const { return chains_; }
    uint64_t tags() const { return tags_; }
    uint64_t chains_full() const { return chains_full_; }
    uint64_t spans_native() const { return spans_native_; }
    uint64_t spans_engine() const { return spans_engine_; }
    uint64_t unpack_typed() const { return unpack_typed_; }
    uint64_t unpack_engine() const { return unpack_engine_; }

private:
    // One span (a whole-quadword VIF stream) is executed here only when admitted:
    // every payload inside the span, no DIRECT; else the engine's feed takes it whole.
    ChainWalker::NativeChainStep Walk(DmacChannelRegs& regs, DmacVifWalkResult& res);
    bool Admit(const uint8_t* data, size_t bytes, uint32_t cycle) const;
    void Execute(const uint8_t* data, size_t bytes);

    const MainMemory& ram_;
    Scratchpad& spad_;
    Vif& vif_;
    ChainWalker& walker_;
    bool typed_unpack_ = false;

    uint64_t chains_ = 0;
    uint64_t chains_full_ = 0;       // chains this executor ran to their end (the rest ended on a terminator the walker read)
    uint64_t tags_ = 0;
    uint64_t spans_native_ = 0;      // spans executed here
    uint64_t spans_engine_ = 0;      // spans fed whole to the engine (unit not idle at the tag / DIRECT / a payload crossing the end)
    uint64_t spans_unreadable_ = 0;  // spans outside the memory named (nothing fed, the walker's rule)
    uint64_t tte_quadwords_ = 0;
    uint64_t unpack_typed_ = 0;      // UNPACKs through the typed loops
    uint64_t unpack_engine_ = 0;     // UNPACKs replayed through Vif::ExecuteCommand
    uint64_t codes_native_ = 0;      // NOP / STCYCL / STMOD / STMASK written here
    uint64_t codes_engine_ = 0;      // other non-UNPACK codes replayed through Vif::ExecuteCommand
    uint64_t ibit_raised_ = 0;
    uint64_t rewritten_ = 0;         // a code word the admission did not see (changed under the i-bit handler)
    uint64_t rewritten_misaligned_ = 0;  // ... and the hand-back started off a quadword edge (framing not exact)
    std::vector<uint8_t> scratch_;   // an UNPACK payload + zero lookahead for the engine's executor
};
