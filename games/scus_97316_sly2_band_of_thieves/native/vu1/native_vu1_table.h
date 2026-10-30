// native_vu1_table.h -- the table the build-time generator (vu1_native_gen) emits into
// <build>/generated/games/scus_97316_sly2_band_of_thieves/native/vu1/vu1_native_images.cpp: one row per
// native VU1 microprogram image of the title, keyed by the engine's image hash (VuImageHash::Fast over
// the unit's 16 KB micro memory, the key every engine table uses), naming the exports table of the
// image's generated TU (engine/vu/vu1_aot/vu1_aot_exports.h). Without a corpus at build time the
// table is empty (vu1_native_images_empty.cpp) and NativeVu1 installs nothing.
#pragma once

#include <cstddef>
#include <cstdint>

struct Vu1AotImageExports;

namespace ps2r::title::sly2 {

struct NativeVu1Image {
    uint64_t hash;                                 // VuImageHash::Fast of the image
    const Vu1AotImageExports* (*exports)();        // vu1_aot_exports_<hash>() of the generated TU
    uint32_t blocks;                               // block functions the TU holds (the generator's count)
    uint64_t ops;                                  // ops inside them
};

extern const NativeVu1Image kNativeVu1Images[];
extern const size_t kNativeVu1ImageCount;

}  // namespace ps2r::title::sly2
