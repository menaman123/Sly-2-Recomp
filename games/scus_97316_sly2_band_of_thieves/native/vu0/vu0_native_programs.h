#pragma once
// The generated VU0 program table's shape (vu0_native_gen emits the definitions into the
// build dir: <build>/generated/games/scus_97316_sly2_band_of_thieves/native/vu0/
// vu0_programs_gen.cpp, pinned by MANIFEST.sha256 beside this file). No game bytes here.
#include <cstdint>

class VuUnit;

namespace ps2r::title::sly2::vu0_native {

struct Vu0NativeProgram {
    uint32_t entry;           // micro byte address the VCALLMS starts at
    void (*fn)(VuUnit& unit); // runs the program on the unit's register file (regs, Q, flags, TPC, clock)
};

struct Vu0NativeImage {
    uint32_t vaddr;           // where the image lives in the ELF (an address: title data that may be named)
    uint32_t size;            // bytes of micro memory the fingerprint covers, from micro address 0
    uint64_t fnv1a64;         // FNV-1a64 of those bytes: the live micro memory must carry them for a body to run
    const Vu0NativeProgram* programs;
    uint32_t count;
};

extern const Vu0NativeImage kImage;

}  // namespace ps2r::title::sly2::vu0_native
