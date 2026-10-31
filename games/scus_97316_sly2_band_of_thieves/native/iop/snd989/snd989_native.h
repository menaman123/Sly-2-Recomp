// snd989_native.h -- the native 989snd_Library bodies' registration on the frame (STAGE NATIVE-3, track snd989).
//
// The recompiled module (iop/snd989, ps2r_iop_recompiler --seam) carries one seam slot per function; the frame
// (native/iop/native_iop_frame.h, knob title.native.iopsys, admission NATIVEEE_IOP_NATIVE=all|none|snd989[:addr,..])
// installs every body registered here into its slot, so from then on each call of those functions -- from the
// module's own recompiled callers, the kernel's thread/timer/RPC entries, or another native body -- runs the host
// body over the module's RAM. The recompiled body stays linked as the fallback and the oracle. The census is the
// frame's (run.meta native_iop=snd989:<native fns>/<total>:<native calls>/<recompiled calls>, native_iop_remaining=,
// NATIVEEE_IOP_SLOT_REPORT=<file> for every slot).
#pragma once

#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_iop_module.h"

namespace ps2r::title::sly2 {

// Add()s every ported body (snd989_bodies.h kPortedFns) to the snd989 module (native_iop_bodies.cpp's line).
void RegisterSnd989Natives(NativeIopModule& snd989);

namespace snd989 {
// AddThread()s the module's thread entries that have a step form (snd989_steps.cpp: the 'SND ' RPC server, the tick,
// transfer, refill and reader threads). Called by RegisterSnd989Natives.
void RegisterSnd989Threads(NativeIopModule& snd989);
}  // namespace snd989

}  // namespace ps2r::title::sly2
