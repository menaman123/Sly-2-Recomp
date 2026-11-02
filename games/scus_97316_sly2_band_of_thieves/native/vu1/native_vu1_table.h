// native_vu1_table.h -- the table the build-time generator (vu1_native_gen) emits into
// <build>/generated/games/scus_97316_sly2_band_of_thieves/native/vu1/vu1_native_images.cpp: one row per
// native VU1 microprogram of the title, keyed by its CONTENT hash (VuImageHash::Fast over the program's
// `size` bytes at VU address 0 -- the bytes the disc's ELF uploads) naming the exports table
// of its generated TU (engine/vu/vu1_aot/vu1_aot_exports.h). A kicked image is identified by hashing its
// first `size` bytes (NativeVu1::Identify) whatever residue the rest of the 16 KB carries. (A row with
// size 16384 is a whole-image row of the legacy per-image mode.) Without a corpus at build time the table
// is empty (vu1_native_images_empty.cpp) and NativeVu1 installs nothing.
#pragma once

#include <cstddef>
#include <cstdint>

struct Vu1AotImageExports;

namespace ps2r::title::sly2 {

struct NativeVu1Image {
    uint64_t hash;                                 // VuImageHash::Fast of the program's `size` bytes (content hash)
    uint32_t size;                                 // program bytes at VU address 0 (16384 = a whole image)
    const Vu1AotImageExports* (*exports)();        // vu1_aot_exports_<hash>() of the generated TU
    uint32_t blocks;                               // block functions the TU holds (the generator's count)
    uint64_t ops;                                  // ops inside them
};

extern const NativeVu1Image kNativeVu1Images[];
extern const size_t kNativeVu1ImageCount;

}  // namespace ps2r::title::sly2
