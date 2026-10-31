// snd989_native.h -- the installer of the native 989snd_Library bodies (STAGE NATIVE-3, track snd989).
//
// The recompiled module (iop/snd989, ps2r_iop_recompiler --override-table) carries one seam slot per function;
// Install() puts every ported body (snd989_bodies.h kPortedFns) into its slot, so from then on each call of
// those functions -- from the module's own recompiled callers, the kernel's thread/timer/RPC entries, or
// another native body -- runs the host body over the module's RAM. The recompiled body stays linked as the
// fallback and the oracle: NATIVEEE_IOP_NATIVE=snd989=off installs nothing (every call recompiled);
// NATIVEEE_IOP_NATIVE=snd989=4325c,431bc installs only those (bisecting a divergence); default = all ported.
// The census (run.meta native_iop=) counts calls that ran native / recompiled per the module and names the
// most-called functions still recompiled (the port's priority list).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps2r::title::sly2::snd989 {

struct NativeOptions {
    bool enabled = true;
    std::vector<uint32_t> only;   // empty = every ported function
    // NATIVEEE_IOP_NATIVE: "<module>=off|on|<addr>[,<addr>...]" entries separated by ';' (other modules' entries ignored).
    static NativeOptions FromEnv(const char* env_value);
};

// Installs the ported bodies per the options; returns how many were installed.
int InstallNative(const NativeOptions& opt);
// Removes every installed body (the module runs recompiled again).
void UninstallNative();
// "snd989:<native calls>/<recompiled calls>:fns=<installed>/<total>:top=<addr>x<calls>,..." (the five most-called
// recompiled functions), the run.meta native_iop= row of this module.
std::string NativeCensusRow();

}  // namespace ps2r::title::sly2::snd989
