// games/scus_97316_sly2_band_of_thieves/hle/leg_interrupts.h -- LegInterrupts: the interrupt plumbing of
// the title's play composition between the DMAC / GS and the kernel: the deferred VIF1 kick's
// observation points at the frame boundary / sceGsSyncPath (GsSync::BindDmac; under MTVU the chase is
// followed by the frame sync), the kernel's DMAC handler records copied into the DMAC's registry before
// every dispatch (InstallDmacHandlers), the GS interrupt observer (SIGNAL / FINISH counted on GsPrivRegs
// as the general register is written; the IMR view is the kernel's GsPutIMR word; masked events counted,
// the rest raised on Intc kGs) and the syscall-entry pre-dispatch hook: every syscall's entry dispatches
// the pending enabled INTC causes (and the DMAC's INT1 handlers) on the calling thread before the
// syscall's body runs, with the BLOCKING-wait rules (a DMA observation point; the ISR deferred to the
// other READY threads; the MTVU idle wait when nothing else is READY). The census counters live here.
#pragma once

#include <cstdint>

#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_devices.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_dma_ipu.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_gs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h"

namespace ps2r::title::sly2 {

class LegInterrupts {
public:
    LegInterrupts(const LegKnobs& knobs, LegCore& core, LegGs& gs, LegDevices& devices, LegMtvu& mtvu);
    LegInterrupts(const LegInterrupts&) = delete;
    LegInterrupts& operator=(const LegInterrupts&) = delete;

    // The kernel's DMAC handler records reach the DMAC's registry before every dispatch.
    void InstallDmacHandlers();

    uint64_t gs_irq_events = 0, gs_irq_masked = 0, gs_irq_raises = 0;              // census gs_irq=
    // E425 hazard census: GS events by kind and the parser's active path at the raise
    // ([0] = FINISH, [1] = SIGNAL; path index 0 = none/unknown, 1..3 = PATH1..3) and the
    // thread they were raised on (the EE holder vs the drain thread).
    uint64_t gs_irq_kind_path[2][4] = {};
    uint64_t gs_irq_on_ee = 0, gs_irq_on_drain = 0;
    uint64_t syscall_entry_isrs = 0, syscall_entry_isrs_deferred = 0;              // census syscall_entry_isrs=
    uint64_t mtvu_idle_wait_entries = 0;                                           // census mtvu= idle_wait_entries
    uint64_t mtvu_idle_wait_skipped = 0;   // E430: observation points whose wait would not have parked (the idle loop skipped)
    uint64_t mtvu_idle_wait_vblank_skipped = 0;   // E430: ... whose block wakes a vblank sleeper (skipped)

private:
    LegDevices& devices_;
};

}  // namespace ps2r::title::sly2
