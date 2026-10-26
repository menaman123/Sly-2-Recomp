// LegVifIrq: the VIFcode i-bit interrupt stall. A VIFcode with the i bit raises VIFn_STAT.INT (INTC cause
// VIF0/VIF1) and stalls the unit (STAT.VIS) until the EE writes VIFn_FBRST.STC.
//
// Sly 2 lights its dynamic meshes (cars, searchlight cones, the title portal) this way: per lit object the
// VIF1 chain unpacks normals and base colours to TOPS+0x307.., runs MSCAL 2 (the VU1 lighting routine),
// then FLUSHE with the i bit. The game's VIF1 handler (0x167f78 -> 0x167e58) reads the lit rows through the
// VU1 data window (0x1100c320 + 0x3070..), packs them into the colour payload of geometry packets further
// down the same chain, then writes STC. Without the stall the geometry unpacks stale colours.
//
//  * MTVU: the worker latches the cause and parks on the i bit; the EE serves it at its next dispatch
//    point, or inside an MtvuSync wait that finds the worker stalled. The handler's STC releases it.
//  * Inline (and every VIF0 i bit): the chain walk runs on the guest thread, so the handler runs right
//    there as a nested call. With no guest context nothing is raised (counted).
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
