#include "games/scus_97316_sly2_band_of_thieves/native/vif/native_vif.h"

#include <cstdio>

#include "games/scus_97316_sly2_band_of_thieves/native/native_census.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vif/vif_census.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vif/vif_native_chains.h"

using ps2r::title::sly2::NativeCensus;
using ps2r::title::sly2::NativeUnit;

NativeVif::NativeVif(const MainMemory& ram, Scratchpad& spad, Vif& vif, ChainWalker& walker, Options options)
    : walker_(walker), options_(std::move(options)) {
    if (!options_.census_path.empty()) census_ = std::make_unique<VifChainCensus>(ram, spad, vif);
    if (options_.dmac || options_.vif) native_ = std::make_unique<VifNativeChains>(ram, spad, vif, walker, options_.vif);
    if (census_ || native_) {
        walker_.SetVifChainExecutor(&NativeVif::Thunk, this);
        installed_ = true;
    }
}

NativeVif::~NativeVif() {
    if (installed_) walker_.SetVifChainExecutor(nullptr, nullptr);
    if (census_) {
        // The table: counts and shapes only (tag ids, sizes, VIF codes, VU addresses), never payload bytes.
        if (std::FILE* f = std::fopen(options_.census_path.c_str(), "wb")) {
            const std::string table = census_->Table();
            std::fwrite(table.data(), 1, table.size(), f);
            std::fclose(f);
        } else {
            std::fprintf(stderr, "native_vif: cannot write the census to %s\n", options_.census_path.c_str());
        }
    }
}

ChainWalker::NativeChainStep NativeVif::Thunk(void* user, int ch, DmacChannelRegs& regs, DmacVifWalkResult& res) {
    return static_cast<NativeVif*>(user)->Step(ch, regs, res);
}

ChainWalker::NativeChainStep NativeVif::Step(int ch, DmacChannelRegs& regs, DmacVifWalkResult& res) {
    if (ch == static_cast<int>(DmacChannelId::Vif1)) {
        chains_++;
        if (census_) census_->Observe(regs);
    }
    if (native_) return native_->Step(ch, regs, res);
    return ChainWalker::NativeChainStep{};
}

std::string NativeVif::Census() const {
    if (native_) {
        // The per-unit details of the native= row (counts only; no ',' / ';').
        NativeCensus::Instance().SetDetail(NativeUnit::Dmac, "chains=" + std::to_string(native_->chains()) + ":tags=" +
                                                                 std::to_string(native_->tags()));
        NativeCensus::Instance().SetDetail(NativeUnit::Vif, "typed=" + std::to_string(native_->unpack_typed()) + ":engine=" +
                                                                std::to_string(native_->unpack_engine()));
    }
    std::string row = (options_.dmac || options_.vif) ? "on" : "off";
    row += ":chains=" + std::to_string(chains_);
    if (native_) row += ":" + native_->Row();
    if (census_) row += ":census=" + census_->Row();
    return row;
}
