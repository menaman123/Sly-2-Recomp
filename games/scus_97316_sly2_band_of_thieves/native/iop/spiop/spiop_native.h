// spiop_native.h -- Sly 2's `spiop` module (0x5a000..0x5f700) as host C++, function by function.
//
// THE CONTRACT. Every body here has the recompiled function's exact shape: `void(IopContext&)` on the
// module's own register file, arguments in $a0..$a3 and on the IOP stack, the result in $v0, callee-saved
// registers untouched, THE SAME IOP STACK FRAME (the body adjusts $sp by the original frame size, stores
// the same saved registers / outgoing arguments / parameter blocks at the same offsets -- imports read
// those blocks from IOP RAM, and the RAM image stays byte-comparable to the recompiled run's), the same
// memory writes in the same order, the same imports (`iop_stubs::import_call`, i.e. the engine's HLE
// libraries or the other hosted modules' exports -- a blocking WaitSema switches coroutines inside the
// call exactly as it does for the recompiled body) and calls to sibling functions THROUGH THE RESOLVER
// (`iop_stubs::iop_resolve(addr)`: the module's address -> body map, so a sibling runs native when it is
// installed and recompiled when it is not -- the fallback and the oracle at every call). What is not
// mirrored: the leftover values of caller-saved registers ($t*, $a*, $v1) at return.
//
// INSTALLATION. `RegisterSpiopNatives(NativeIopModule&)` Add()s every body by the address of the recompiled
// function it replaces (native_iop_bodies.cpp); the iopsys frame installs the admitted ones into the module's
// per-function seam slots under title.native.iopsys (NATIVEEE_IOP_NATIVE=spiop[:addr,...] bisects). The census
// reads `native_iop=spiop:<native fns>/<total>:<native calls>/<recompiled calls>`.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/iop/iop_context.h"

namespace ps2r::title::sly2 {
class NativeIopModule;
}

namespace ps2r::title::sly2::spiop {

using Body = void (*)(ps2r::iop::IopContext&);

struct NativeFn {
    uint32_t addr;       // the recompiled function's address (its iop_func_XXXXXXXX)
    uint32_t size;       // bytes of MIPS the body replaces
    Body body;
    const char* name;    // what it is (the port's name; the module has no symbols)
};

// Every ported function, ascending by address.
const NativeFn* Functions(size_t& count);
const NativeFn* Find(uint32_t addr);

// Adds every ported body to the spiop NativeIopModule (the iopsys frame installs them).
void Register(NativeIopModule& spiop);

}  // namespace ps2r::title::sly2::spiop

namespace ps2r::title::sly2 {
// The registry line (native_iop_bodies.cpp).
inline void RegisterSpiopNatives(NativeIopModule& spiop) { spiop::Register(spiop); }
}  // namespace ps2r::title::sly2::spiop
