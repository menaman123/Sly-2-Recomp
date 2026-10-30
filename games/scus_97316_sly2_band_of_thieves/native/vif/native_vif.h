#pragma once
// NativeVif: Sly 2's VIF1 DMA chains on the title's native path. Installs ONE
// chain executor on the engine's walker (ChainWalker::SetVifChainExecutor), which
// runs, per VIF1 chain kick: the read-only census (NATIVEEE_VIF_CENSUS=<file>),
// then the native executor (title.native.dmac: the chain walk by tag shape;
// title.native.vif: the typed UNPACK loops straight into VU1 data memory) that
// hands everything it does not recognise back to the engine's walk +
// Vif::ExecuteCommand (the fallback and the oracle). NativeCensus counts both
// units (run.meta `native=`).
//
// Only the inline feed is taken (the lane: MTVU off); under the MTVU staged feed
// the walker never consults the executor and the engine's model runs unchanged.
#include <cstdint>
#include <memory>
#include <string>

#include "engine/dmac/chain_walker.h"

class MainMemory;
class Scratchpad;
class Vif;
class VifChainCensus;
class VifNativeChains;

class NativeVif {
public:
    struct Options {
        bool dmac = false;             // title.native.dmac: the chain executor takes the tags it recognises
        bool vif = false;              // title.native.vif: the typed UNPACK loops (implies the chain executor)
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
    uint64_t chains_ = 0;
};
