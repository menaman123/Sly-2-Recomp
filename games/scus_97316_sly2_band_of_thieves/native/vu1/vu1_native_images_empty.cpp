// The native VU1 table of a build WITHOUT a VU1 corpus (NATIVEEE_VU1_CORPUS unset): no image, so
// NativeVu1::Install registers nothing and title.native.vu1 runs the engine's model for every kick.
#include "games/scus_97316_sly2_band_of_thieves/native/vu1/native_vu1_table.h"

namespace ps2r::title::sly2 {

const NativeVu1Image kNativeVu1Images[] = {{0ull, nullptr, 0u, 0ull}};
const size_t kNativeVu1ImageCount = 0;

}  // namespace ps2r::title::sly2
