// native_knobs.h -- the title.native.* switches as the title reads them.
//
// One bool per PS2 unit whose per-frame MODEL this title may replace by native host code for
// its specific use of the unit (README.md). The engine's Config carries the keys as a generic
// `title.native.*` namespace (engine/core/config/config.h TitleNativeConfig) and reads none of
// them itself; this is the ONE place the title interprets them. Default off = the modelled path.
#pragma once

#include <string>

#include "engine/core/config/config.h"

namespace ps2r::title::sly2 {

// The eight units, in the census order (NativeCensus::Row / run.meta `native=`).
enum class NativeUnit : int { Vu1 = 0, Gif, Vif, Dmac, Iop, Spu, Gs, Vu0, kCount };

const char* NativeUnitName(NativeUnit u);

struct NativeKnobs {
    bool vu1 = false;
    bool gif = false;
    bool vif = false;
    bool dmac = false;
    bool iop = false;
    bool spu = false;
    bool gs = false;
    bool vu0 = false;

    // The switches as the loaded Config carries them (title.native.<unit>).
    static NativeKnobs From(const Config& cfg);
    bool On(NativeUnit u) const;
    bool Any() const;
    // "vu1,gif" -- the units that are on, in census order; "none" when none is.
    std::string OnList() const;
};

}  // namespace ps2r::title::sly2
