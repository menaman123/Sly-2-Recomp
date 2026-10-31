// spiop_threads.h -- spiop's headset threads (B fn_5b0cc monitor, A fn_5b2a0 feeder) in the STEP form; see the
// .cpp for what is and is not in the step form and why.
#pragma once

#include <cstdint>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_iop_module.h"

namespace ps2r::title::sly2::spiop {

class SpiopThreads {
public:
    static constexpr uint32_t kMonitor = 0x5b0cc;   // thread B: the headset monitor (op 0x00 starts it)
    static constexpr uint32_t kFeeder = 0x5b2a0;    // thread A: the headset feeder (fn_5ad80 starts it; never on the lanes)
    static void Register(NativeIopModule& spiop);
    static iop::IopKernel::StepBody MakeMonitor(const IopContext& initial);
    static iop::IopKernel::StepBody MakeFeeder(const IopContext& initial);
};

}  // namespace ps2r::title::sly2::spiop
