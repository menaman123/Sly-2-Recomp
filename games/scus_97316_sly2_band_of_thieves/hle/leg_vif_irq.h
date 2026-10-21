// games/scus_97316_sly2_band_of_thieves/hle/leg_vif_irq.h -- LegVifIrq: the VIFcode i-bit interrupt
// stall (EE User's Manual: a VIFcode with the i bit set raises VIFn_STAT.INT, INTC cause VIF0/VIF1,
// and STALLS the unit (STAT.VIS) after the command until the EE writes VIFn_FBRST.STC).
// Sly 2 lights its dynamic meshes (cars, searchlight cones, the title portal) with it: per lit object
// the VIF1 chain unpacks normals + base colours to TOPS+0x307.., MSCAL 2 (the VU1 lighting routine),
// then FLUSHE with the i bit (0x90000000); the game's VIF1 handler (0x167f78 -> 0x167e58) reads the
// lit rows through the VU1 data window (0x1100c320 + 0x3070..) and packs them into the V4-5 / S-8
// colour payload of the geometry packets further down the SAME chain, then STC. Without the stall
// the geometry unpacks whatever the payload held (stale pool bytes: diag_colors/REPORT.md).
// Composition (Vif::SetIrqHooks):
//  * MTVU: the worker parks on the i bit (MtvuWorker::EnterIrqStall) after latching the cause
//    (Intc::RaiseDeferred -- the pipeline's route); the EE serves it at its next dispatch point, or
//    inside an MtvuSync wait that finds the worker stalled (MtvuSync::SetIrqStallService: collect +
//    Intc::Dispatch on the running guest thread's context); the handler's STC releases the worker.
//  * Inline: the chain walk runs on the guest thread -- the cause is raised in place and the handler
//    runs right there (the stall IS the nested call); no context = no raise (counted).
// VIF0 i bits take the inline route (VIF0 chains walk on the EE). The census counts both.
#pragma once

#include <cstdint>

#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_devices.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h"

namespace ps2r::title::sly2 {

class LegVifIrq {
public:
    LegVifIrq(LegCore& core, LegDevices& devices, LegMtvu& mtvu);
    ~LegVifIrq();
    LegVifIrq(const LegVifIrq&) = delete;
    LegVifIrq& operator=(const LegVifIrq&) = delete;

    // census vif_irq=
    uint64_t stalls = 0;            // worker parks (MTVU)
    uint64_t inline_isrs = 0;       // handlers run in place (inline walks / VIF0)
    uint64_t inline_refused = 0;    // i bits with no guest context to run the handler on (not raised)
    uint64_t sync_isrs = 0;         // handlers run inside an MtvuSync wait
    uint64_t sync_not_pending = 0;  // a wait found the worker stalled before the cause landed / masked
    uint64_t sync_refused = 0;      // a wait inside an ISR (the handler's own FBRST write)

private:
    static void OnIrq(void* user, int vif);
    static void OnStc(void* user, int vif);
    static int Service(void* user);
    bool DispatchHere(int cause);

    LegCore& core_;
    LegDevices& devices_;
    LegMtvu& mtvu_;
};

}  // namespace ps2r::title::sly2
