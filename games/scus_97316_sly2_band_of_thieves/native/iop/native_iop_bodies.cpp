// native_iop_bodies.cpp -- the registry of native bodies: one line per module. Each module's TU adds
// its bodies to its module (`Add(addr, body, name)`); the frame installs them under title.native.iopsys and
// the NATIVEEE_IOP_NATIVE filter. Merge rule: append a line, never reorder.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_iop_frame.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_fileio.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_lgaud.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_native.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989_lock.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/spiop/spiop_native.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/spiop_tray_thread.h"

namespace ps2r::title::sly2 {

void RegisterNativeIopBodies(NativeIopFrame& frame) {
    // iopsys: the 989snd global lock (the shared service; the seam's lane proof).
    Snd989Lock::Register(frame.Snd989());
    // iopsys: spiop's tray-poll thread in the step form (the first coroutine-free native thread; the lane proof).
    SpiopTrayThread::Register(frame.Spiop());
    // snd989: the 989snd_Library bodies (native/iop/snd989/).
    RegisterSnd989Natives(frame.Snd989());
    // spiop (iop/spiop/README.md): the streaming module, 69 of 98 functions.
    RegisterSpiopNatives(frame.Spiop());
    // lgaud: lgAud (native_lgaud.cpp) + FILEIO_service (native_fileio.cpp).
    RegisterLgaudNatives(frame.Lgaud());
    RegisterFileioNatives(frame.Fileio());
}

}  // namespace ps2r::title::sly2
