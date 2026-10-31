// snd989_bodies.h -- the ported 989snd_Library functions (one declaration per recompiled function, named by
// its address in the module as loaded at 0x33000) and the table the installer puts in the module's seam.
#pragma once

#include <cstdint>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_abi.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_state.h"

namespace ps2r::title::sly2::snd989 {

struct PortedFn {
    uint32_t addr;        // the function's address (iop_snd989::iop_fn_slots[].addr)
    IopFn body;           // the host body
    const char* name;     // the role (OpenGOAL's name where it has one)
};
// Every ported function, in address order. The installer (snd989_native.cpp) installs each unless filtered.
extern const PortedFn kPortedFns[];
extern const uint32_t kPortedFnCount;

// snd989_tick.cpp -- the 240 Hz tick and what it reaches directly.
void fn_35140(IopContext&);   // snd_ReportError(code, a, b, c, d)
void fn_42f58(IopContext&);   // snd_Lock(tag)
void fn_430dc(IopContext&);   // snd_Unlock()
void fn_431bc(IopContext&);   // snd_TryLock() (the tick's)
void fn_4325c(IopContext&);   // snd_Tick(): the 240 Hz step
void fn_43638(IopContext&);   // snd_GetTick()
void fn_4cf5c(IopContext&);   // voice_ApplyKeyOns()
void fn_4d074(IopContext&);   // voice_ApplyKeyOffs()
void fn_4d184(IopContext&);   // voice_Ended(v)
void fn_4d318(IopContext&);   // voice_KeyOff(v)

// snd989_volume.cpp -- master volume, ramps, group fades, the voice-table lock.
void fn_3e1cc(IopContext&);   // ramp_Done(slot)
void fn_369c8(IopContext&);   // master_StepRamps()
void fn_40eb8(IopContext&);   // master_SetVolume(core, l, r)
void fn_40f64(IopContext&);   // master_Tick()
void fn_4c1ac(IopContext&);   // voice_Lock(wait, tag)
void fn_4c2f8(IopContext&);   // voice_Unlock()
void fn_4edbc(IopContext&);   // voice_RefreshGroupVolumes(groups)
void fn_4efb4(IopContext&);   // fade_Step()

// snd989_seq.cpp -- the sound-handle list, the sequencer step, the stop path.
void fn_35a04(IopContext&);   // endhandler_Poll()
void fn_377a4(IopContext&);   // seq_Tick(h)
void fn_3926c(IopContext&);   // seq_Step(h)
void fn_41da0(IopContext&);   // snd_StopHandle(h, force, release, why)
void fn_4218c(IopContext&);   // sndlist_Tick()
void fn_4226c(IopContext&);   // timer_Walk(node, h)

}  // namespace ps2r::title::sly2::snd989
