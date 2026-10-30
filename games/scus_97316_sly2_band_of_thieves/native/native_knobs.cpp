#include "games/scus_97316_sly2_band_of_thieves/native/native_knobs.h"

namespace ps2r::title::sly2 {

const char* NativeUnitName(NativeUnit u) {
    static const char* const kNames[static_cast<int>(NativeUnit::kCount)] = {"vu1", "gif", "vif", "dmac",
                                                                              "iop", "spu", "gs",  "vu0"};
    const int i = static_cast<int>(u);
    return (i >= 0 && i < static_cast<int>(NativeUnit::kCount)) ? kNames[i] : "?";
}

NativeKnobs NativeKnobs::From(const Config& cfg) {
    NativeKnobs k;
    k.vu1 = cfg.title_native.vu1;
    k.gif = cfg.title_native.gif;
    k.vif = cfg.title_native.vif;
    k.dmac = cfg.title_native.dmac;
    k.iop = cfg.title_native.iop;
    k.spu = cfg.title_native.spu;
    k.gs = cfg.title_native.gs;
    k.vu0 = cfg.title_native.vu0;
    return k;
}

bool NativeKnobs::On(NativeUnit u) const {
    switch (u) {
        case NativeUnit::Vu1: return vu1;
        case NativeUnit::Gif: return gif;
        case NativeUnit::Vif: return vif;
        case NativeUnit::Dmac: return dmac;
        case NativeUnit::Iop: return iop;
        case NativeUnit::Spu: return spu;
        case NativeUnit::Gs: return gs;
        case NativeUnit::Vu0: return vu0;
        case NativeUnit::kCount: break;
    }
    return false;
}

bool NativeKnobs::Any() const {
    for (int i = 0; i < static_cast<int>(NativeUnit::kCount); ++i)
        if (On(static_cast<NativeUnit>(i))) return true;
    return false;
}

std::string NativeKnobs::OnList() const {
    std::string s;
    for (int i = 0; i < static_cast<int>(NativeUnit::kCount); ++i) {
        if (!On(static_cast<NativeUnit>(i))) continue;
        if (!s.empty()) s += ",";
        s += NativeUnitName(static_cast<NativeUnit>(i));
    }
    return s.empty() ? std::string("none") : s;
}

}  // namespace ps2r::title::sly2
