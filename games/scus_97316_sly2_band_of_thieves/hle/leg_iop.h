// games/scus_97316_sly2_band_of_thieves/hle/leg_iop.h -- LegIop: the IOP arm of the title's play
// composition -- the title's embedded modules hosted on the engine's IOP kernel (hle/iop_host.h) and the
// SIF wiring around them. The EE's sceSifSetDma pushes land in IOP RAM; sceSifAllocIopHeap is the sysmem
// heap; a sceSifLoadModuleBuffer boots the recompiled module whose .iopmod name matches (lgAud, 989snd,
// spiop) or records the row of a natively served one (the loader itself is installed with the manifest,
// leg_image.cpp). The 989snd RPC servers the module registers are put on the RpcRouter by the host, so
// snd_Init's sceSifCallRpc reaches them. The padman face follows the padman the title loads (padman.irx
// 4.10 on this disc: the SDK-3 command table, GET_MODVER 0x410); the row image's .bss is the
// actuator-DMA sink. The boundary's IopDispatch step pumps the IOP one frame quantum and delivers its
// SIF commands (PumpFieldQuantum); the Verify slot's play-mode field pump runs the same quantum once more
// per catch-up field, between the SPU quanta it mixes, so the IOP's frame clock follows the SPU's (one
// quantum per delivered field) and 989snd's stream refills keep pace below 60 boundaries/s; the EeStreamRpc step pumps after the EE's service; the SIF command path IOP -> EE lands in libkernl's
// command buffer (the title's sceSifInitCmd static, iop/iop_modules.h) and runs the EE's handler as a
// guest call. The back-edge yield (recomp/CMakeLists.txt: --yield-backedges): a guest loop that reaches
// no kernel call trips EeYieldPoint every kYieldBudget back-edges -- one field of emulated time, alarms,
// timers, the pending ISRs (the DMAC's INT1 handlers too), the event drain, the IOP pump, the MTVU
// drain, then the thbase preemption point; refused while the boundary sequence is in flight. Unbound
// when this object goes.
#pragma once

#include <cstdint>

#include "engine/core/config/config.h"
#include "engine/ee/sched/ee_yield_point.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/iop_host.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_devices.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_disc.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_interrupts.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_services.h"

namespace ps2r::title::sly2 {

class LegIop {
public:
    static constexpr uint32_t kYieldBudget = EeYieldPoint::kDefaultBudget;   // census ee_yield_budget=

    LegIop(const ps2r::Config& cfg, const LegKnobs& knobs, LegCore& core, LegDevices& devices, LegMtvu& mtvu, LegInterrupts& interrupts,
           LegDisc& disc, LegServices& services);
    LegIop(const LegIop&) = delete;
    LegIop& operator=(const LegIop&) = delete;

    // One IOP frame quantum (IopModuleTable::PumpAtBoundary) + the SIF commands it sent, delivered to the
    // EE. The IopDispatch step runs it once per boundary; the Verify slot's field pump runs it once more
    // per catch-up field, BETWEEN the SPU quanta it mixes (LegAudio::field_quantum) -- IOP quantum k
    // precedes SPU quantum k in both lanes, so the IOP's frame clock follows the SPU's at any frame rate.
    void PumpFieldQuantum(CpuContext& ctx);

    IopHost iop_host;
    struct YieldUnbind {
        ~YieldUnbind() { EeYieldPoint::Instance().Unbind(); }
    } yield_unbind;
};

}  // namespace ps2r::title::sly2
