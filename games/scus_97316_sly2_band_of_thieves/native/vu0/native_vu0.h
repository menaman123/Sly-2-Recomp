#pragma once
// NativeVu0: Sly 2's VU0 microprograms (the six VCALLMS entries 0x10 / 0x60 / 0xf0 / 0x120 /
// 0x150 / 0x1c0: matrix products, M*v transforms, a normalise, the frustum cull) as host
// code, behind `title.native.vu0`. The bodies are GENERATED AT BUILD TIME from the ELF's
// VU0 program image by native/vu0/vu0_native_gen (vu0_native_programs.h is their table);
// this class is the installer + the engine seam's hook (VuTierDispatch::SetVu0Hook on the
// EE-side VU0 unit): per VCALLMS, if the live micro memory still carries the image (an
// FNV-1a64 over the first `size` bytes, recomputed only when the unit's micro_generation
// changed) and the entry has a body, the body runs and the cached tier is skipped; else
// the call is handed back (NativeCensus::Fallback) and the engine's model runs it.
// Entries the generator refused (control flow it does not fold) simply have no body.
#include <cstdint>

#include "games/scus_97316_sly2_band_of_thieves/native/native_knobs.h"

class VuUnit;

namespace ps2r::title::sly2 {

class NativeVu0 {
public:
    // Installs the hook on `vu0`'s dispatch when `knobs.vu0`; a no-op otherwise.
    void Install(const NativeKnobs& knobs, VuUnit& vu0);
    bool installed() const { return installed_; }

    // The hook body (public for the test): true = the program ran natively.
    bool Run(VuUnit& unit, uint32_t start_pc, uint64_t micro_generation);

private:
    static bool Hook(void* user, VuUnit& unit, uint32_t start_pc, uint64_t micro_generation);

    bool installed_ = false;
    uint64_t memo_generation_ = ~0ull;  // the micro_generation the fingerprint was last judged at
    bool memo_match_ = false;
};

}  // namespace ps2r::title::sly2
