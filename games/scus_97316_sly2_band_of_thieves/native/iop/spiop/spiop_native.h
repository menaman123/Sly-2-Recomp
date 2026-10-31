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
// INSTALLATION. `Functions()` lists {address, size, body}; the composition installs them either through
// the iopsys track's per-function seam (`iop_spiop::iop_seam_install`) or, until that lands, by replacing
// the entries of `iop_spiop::iop_recompiled_functions` (`InstallInto`): every way a module function is
// reached from outside -- the 'SUKR' handlers 989snd jumps to, the thread bodies thbase starts, the
// module entry -- resolves through that map, so the roots run native and the whole call tree follows.
// A filter (title.native.spiop=<csv of addresses>, or `all`) selects which bodies are installed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "engine/iop/iop_context.h"

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

// Replaces the map entries (the pre-seam install). `filter`: "" or "all" = every ported function; else a
// comma-separated list of hex addresses. Returns the number installed; `installed` (optional) collects them.
size_t InstallInto(std::map<uint32_t, std::function<void(ps2r::iop::IopContext&)>>& functions, const std::string& filter,
                   std::string* installed = nullptr);

// Per-body call counters (the census: native_iop=spiop:<native>/<total ported>).
uint64_t NativeCalls();
uint64_t NativeCalls(uint32_t addr);
// Entries through the map that reached a recompiled body (a ported function not selected by the filter).
uint64_t RecompiledRootCalls();
void ResetCounters();
// The run.meta row: "spiop:<native calls>/<native + recompiled root entries>:fns=<ported>".
std::string CensusRow();

}  // namespace ps2r::title::sly2::spiop
