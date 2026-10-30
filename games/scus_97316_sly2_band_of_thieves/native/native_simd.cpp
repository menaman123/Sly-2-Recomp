#include "games/scus_97316_sly2_band_of_thieves/native/native_simd.h"

namespace ps2r::title::sly2::simd {

const char* ArmName() {
#if PS2R_NATIVE_SIMD == 1
    return "neon";
#elif PS2R_NATIVE_SIMD == 2
    return "sse2";
#else
    return "scalar";
#endif
}

}  // namespace ps2r::title::sly2::simd
