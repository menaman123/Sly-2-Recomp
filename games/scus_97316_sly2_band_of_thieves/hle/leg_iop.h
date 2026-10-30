// LegIop: the IOP host (iop_host.h) and the SIF wiring around it.
//
//   * The padman model follows the padman.irx this disc loads (4.10: the SDK-3 command table).
//   * Each boundary pumps the IOP one frame quantum and delivers its SIF commands, in step with the
//     SPU's one quantum per boundary (LegVerifySlot), so 989snd's refills follow the voices they feed.
//   * IOP -> EE SIF commands land in libkernl's command buffer and run the EE's handler as a guest call.
//   * Back-edge yield (recomp --yield-backedges): a guest loop that makes no kernel call trips EeYieldPoint
//     every kYieldBudget back-edges, which advances a field and delivers timers, ISRs, events, the IOP
//     pump and the MTVU drain. Not taken while a boundary is in flight.
#pragma once

#include <cstdint>
#include <functional>

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

    // One IOP frame quantum plus delivery of the SIF commands it sent; once per boundary.
    void PumpQuantum(CpuContext& ctx);
    // Whether this boundary advances the IOP (FrameLimit::TakeQuantum); unset = every boundary.
    std::function<bool()> quantum_gate;

    IopHost iop_host;
    struct YieldUnbind {
        ~YieldUnbind() { EeYieldPoint::Instance().Unbind(); }
    } yield_unbind;
};

}  // namespace ps2r::title::sly2
