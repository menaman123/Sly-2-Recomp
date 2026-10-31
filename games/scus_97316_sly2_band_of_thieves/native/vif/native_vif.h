#pragma once
// NativeVif: Sly 2's VIF1 stream on the title's native path -- two seams, two knobs:
//   title.native.dmac  the chain executor on the engine's walker (ChainWalker::SetVifChainExecutor):
//                      the tag walk by the walker's rules, every span fed through the walker's own
//                      VIF1 feed (inline, or the MTVU worker's staged copy);
//   title.native.vif   the span executor on the VIF unit (Vif::SetSpanExecutor): every VIF1 span
//                      -- inline on the EE / DMAC thread, or replayed by the MTVU worker on its
//                      thread -- parsed and executed by the title (typed UNPACK loops straight into
//                      VU1 data memory, MPG into micro memory, DIRECT to PATH2, the register codes,
//                      payloads parked and resumed across spans, the TTE quadword), the engine's
//                      parser as the fallback and the oracle.
// NATIVEEE_VIF_CENSUS=<file> = the read-only chain census (through the chain executor seam).
// NativeCensus counts both units (run.meta `native=`: dmac = tags walked here / 0; vif = spans
// executed here / spans left to the engine).
#include <cstdint>
#include <memory>
#include <string>

#include "engine/dmac/chain_walker.h"

class MainMemory;
class Scratchpad;
class Vif;
class VifChainCensus;
class VifNativeChains;
class VifNativeSpans;

class NativeVif {
public:
    struct Options {
        bool dmac = false;             // title.native.dmac: the chain executor walks the VIF1 chains
        bool vif = false;              // title.native.vif: the span executor runs every VIF1 span
        std::string census_path;       // NATIVEEE_VIF_CENSUS=<file>: histogram every VIF1 chain, write the table there at shutdown
    };

    NativeVif(const MainMemory& ram, Scratchpad& spad, Vif& vif, ChainWalker& walker, Options options);
    ~NativeVif();
    NativeVif(const NativeVif&) = delete;
    NativeVif& operator=(const NativeVif&) = delete;

    bool Installed() const { return installed_; }
    // The detail row (the census' `vif=`/`dmac=` detail + the run.meta `native_vif=` row).
    std::string Census() const;

private:
    static ChainWalker::NativeChainStep Thunk(void* user, int ch, DmacChannelRegs& regs, DmacVifWalkResult& res);
    ChainWalker::NativeChainStep Step(int ch, DmacChannelRegs& regs, DmacVifWalkResult& res);

    ChainWalker& walker_;
    Options options_;
    bool installed_ = false;
    std::unique_ptr<VifChainCensus> census_;
    std::unique_ptr<VifNativeChains> native_;
    std::unique_ptr<VifNativeSpans> spans_;
    uint64_t chains_ = 0;
};
