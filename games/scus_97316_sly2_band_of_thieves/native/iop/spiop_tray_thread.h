// spiop_tray_thread.h -- spiop's tray-poll thread (entry fn_5b438) in the STEP form: the first native IOP
// thread that runs without a coroutine (IopKernel::StartStepThread through thbase's step provider).
//
// The recompiled thread (45 insns; a0 = the module context g[0x5f524]):
//     while (ctx[0x00] == 0) {                       // the module's shutdown flag
//         sema = ctx[0x30];                          // the disc semaphore (-1 = none)
//         if (sema >= 0) WaitSema(sema);             // park 1
//         tray = 0; if (sceCdTrayReq(2, &tray)) ctx[0x2c] = tray;   // op 0x0c's reply word (nat2-snd.md)
//         if (sema >= 0) SignalSema(sema);
//         DelayThread(100000);                       // park 2: every 100 ms
//     }
//     ChangeThreadPriority(0, 9); SignalSema(ctx[0x24]); return;
// As a step body the two parks are RETURNED (Park::Sema / Park::Delay) and the loop is a phase machine
// over the same IOP RAM words and the same non-blocking services (sceCdTrayReq, SignalSema through
// IopServices: the hosted cdvdman / kernel). The thread's stack frame (sp - 0x20; tray at sp+0x14, the
// sema copy at sp+0x10) is kept as the recompiled one lays it. The function body fn_5b438 itself is not
// replaced: the coroutine form (knob off / not admitted) is the recompiled body.
#pragma once

#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_iop_module.h"

namespace ps2r::title::sly2 {

class SpiopTrayThread {
public:
    static constexpr uint32_t kEntry = 0x5b438;
    static void Register(NativeIopModule& spiop);
    static iop::IopKernel::StepBody Make(const IopContext& initial);
};

}  // namespace ps2r::title::sly2
