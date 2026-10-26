// LegInterrupts: interrupt plumbing between the DMAC/GS and the kernel. The VIF1 kick's observation
// points at the frame boundary and sceGsSyncPath, the kernel's DMAC handlers, the GS SIGNAL/FINISH
// observer, and the syscall-entry hook that dispatches pending INTC causes before a syscall runs.
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

    void InstallDmacHandlers();

    uint64_t gs_irq_events = 0, gs_irq_masked = 0, gs_irq_raises = 0;              // census gs_irq=
    // GS events by kind ([0] FINISH, [1] SIGNAL), the parser's active path at the raise (0 = unknown,
    // 1..3 = PATH1..3), and the raising thread (EE holder vs drain thread).
    uint64_t gs_irq_kind_path[2][4] = {};
    uint64_t gs_irq_on_ee = 0, gs_irq_on_drain = 0;
    uint64_t syscall_entry_isrs = 0, syscall_entry_isrs_deferred = 0;              // census syscall_entry_isrs=
    uint64_t mtvu_idle_wait_entries = 0;                                           // census mtvu= idle_wait_entries
    uint64_t mtvu_idle_wait_skipped = 0;          // idle waits skipped: the wait would not have parked
    uint64_t mtvu_idle_wait_vblank_skipped = 0;   // idle waits skipped: the block wakes a vblank sleeper

private:
    LegDevices& devices_;
};

}  // namespace ps2r::title::sly2
