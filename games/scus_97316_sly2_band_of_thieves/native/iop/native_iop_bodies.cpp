// native_iop_bodies.cpp -- the registry of native bodies: one line per module track. Each track's TU adds
// its bodies to its module (`Add(addr, body, name)`); the frame installs them under title.native.iopsys and
// the NATIVEEE_IOP_NATIVE filter. Merge rule: append a line, never reorder.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_iop_frame.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989_lock.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/spiop/spiop_native.h"

namespace ps2r::title::sly2 {

void RegisterNativeIopBodies(NativeIopFrame& frame) {
    // iopsys: the 989snd global lock (the shared service; the seam's lane proof).
    Snd989Lock::Register(frame.Snd989());
    // snd989 track:  RegisterSnd989Natives(frame.Snd989());
    // spiop track (iop/spiop/README.md): the streaming module, 69 of 98 functions.
    RegisterSpiopNatives(frame.Spiop());
    // lgaud track:   RegisterLgaudNatives(frame.Lgaud()); RegisterFileioNatives(frame.Fileio());
}

}  // namespace ps2r::title::sly2
