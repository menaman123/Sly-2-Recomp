#include "games/scus_97316_sly2_band_of_thieves/native/native_census.h"

namespace ps2r::title::sly2 {

NativeCensus& NativeCensus::Instance() {
    static NativeCensus census;
    return census;
}

void NativeCensus::SetDetail(NativeUnit u, const std::string& detail) {
    std::string d;
    d.reserve(detail.size());
    for (char ch : detail) d += (ch == ',' || ch == ';' || ch == '\n') ? '_' : ch;
    detail_[Idx(u)] = d;
}

std::string NativeCensus::Row(const NativeKnobs& knobs) const {
    std::string row;
    for (int i = 0; i < static_cast<int>(NativeUnit::kCount); ++i) {
        const NativeUnit u = static_cast<NativeUnit>(i);
        if (!row.empty()) row += ",";
        row += NativeUnitName(u);
        row += knobs.On(u) ? "=on:" : "=off:";
        row += std::to_string(ServedCount(u));
        row += "/";
        row += std::to_string(FallbackCount(u));
        if (!detail_[i].empty()) {
            row += ":";
            row += detail_[i];
        }
    }
    row += knobs.Any() ? ";any=on" : ";any=off";
    return row;
}

void NativeCensus::Reset() {
    for (int i = 0; i < static_cast<int>(NativeUnit::kCount); ++i) {
        c_[i].served.store(0, std::memory_order_relaxed);
        c_[i].fallback.store(0, std::memory_order_relaxed);
        detail_[i].clear();
    }
}

}  // namespace ps2r::title::sly2
