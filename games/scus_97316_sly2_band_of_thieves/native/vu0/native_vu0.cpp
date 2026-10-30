// NativeVu0 -- rules in the header.
#include "games/scus_97316_sly2_band_of_thieves/native/vu0/native_vu0.h"

#include <cstddef>

#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu_chop_round_guard.h"
#include "engine/vu/vu_unit.h"
#include "games/scus_97316_sly2_band_of_thieves/native/native_census.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vu0/vu0_native_programs.h"

namespace ps2r::title::sly2 {

namespace {

uint64_t Fnv1a64(const uint8_t* p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

const vu0_native::Vu0NativeProgram* Find(uint32_t pc) {
    for (uint32_t i = 0; i < vu0_native::kImage.count; ++i) {
        if (vu0_native::kImage.programs[i].entry == pc) return &vu0_native::kImage.programs[i];
    }
    return nullptr;
}

}  // namespace

void NativeVu0::Install(const NativeKnobs& knobs, VuUnit& vu0) {
    if (!knobs.vu0 || installed_) return;
    vu0.dispatch().SetVu0Hook(&NativeVu0::Hook, this);
    installed_ = true;
    NativeCensus::Instance().SetDetail(NativeUnit::Vu0, "programs=" + std::to_string(vu0_native::kImage.count));
}

bool NativeVu0::Hook(void* user, VuUnit& unit, uint32_t start_pc, uint64_t micro_generation) {
    return static_cast<NativeVu0*>(user)->Run(unit, start_pc, micro_generation);
}

bool NativeVu0::Run(VuUnit& unit, uint32_t start_pc, uint64_t micro_generation) {
    if (micro_generation != memo_generation_) {
        // The image fingerprint: judged once per upload (the EE window / VIF0 MPG bump the generation).
        const Vu1State& st = unit.state();
        const uint32_t n = vu0_native::kImage.size <= st.mem_mask + 1u ? vu0_native::kImage.size : st.mem_mask + 1u;
        memo_match_ = Fnv1a64(st.micro, n) == vu0_native::kImage.fnv1a64;
        memo_generation_ = micro_generation;
    }
    const vu0_native::Vu0NativeProgram* prog = memo_match_ ? Find(start_pc & 0xFFFu) : nullptr;
    if (prog == nullptr) {
        NativeCensus::Instance().Fallback(NativeUnit::Vu0);
        return false;
    }
    VuChopRoundGuard round_guard;  // the tiers' rule: FMAC/FDIV truncate (a no-op on the guest thread, already toward zero)
    Vu1State& st = unit.state();
    st.suspended = false;          // the tier's per-run scratch (a native body never parks)
    prog->fn(unit);
    NativeCensus::Instance().Served(NativeUnit::Vu0);
    return true;
}

}  // namespace ps2r::title::sly2
