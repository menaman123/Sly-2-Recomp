// spiop_libc.h -- spiop's libc half (fp-bit soft float, sqrtf, the allocation-failure exit) as native bodies;
// see spiop_libc.cpp for what is live, what is dead, and the differential test.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/iop/iop_context.h"

namespace ps2r::title::sly2 {
class NativeIopModule;
}

namespace ps2r::title::sly2::spiop {

struct LibcFunction {
    uint32_t addr;
    void (*body)(ps2r::iop::IopContext&);
};
// The ported libc functions, ascending by address (the differential test's list).
const LibcFunction* LibcFunctions(size_t& count);
// Adds them to the spiop module (called by spiop::Register).
void RegisterLibc(NativeIopModule& spiop);

}  // namespace ps2r::title::sly2::spiop
