// LegMtvu: VU1 and its VIF1 stream on a worker thread (MTVU), the GIF drain thread, and the VU1 AOT tier.
//
// MTVU: the worker owns its own VU1 unit and receives copies of the VIF1 bytes; the DMAC walk and channel
// completion stay on the EE. The GIF arbiter keeps the worker's PATH1/PATH2 packets at the stream position
// the inline path would produce. On in play mode (engine.mtvu); headless runs stay on the deterministic
// inline path unless NATIVEEE_MTVU=1.
//
// GIF drain thread: decodes GIF -> GsState -> DrawIR off the EE whenever the DrawIR consumer has its own
// thread (never with gs.queue=inline). The EE's own pushes into the chain (the VSync job, the display
// register jobs) then go through the arbiter in stream order. Sly 2's render thread waits for both of its
// GS FINISHes before the vblank wait, so boundaries are not deferred by default.
//
// AOT tier, on the live VU1 unit: a manifest under <user_data>/vu1_aot/<build>/ seeds the verdicts, new
// microprograms are verified in the background, then compiled to native code and installed. The builder
// is declared before the cache so it outlives it.
#pragma once

#include <memory>

#include "app/host/mtvu_stack.h"
#include "app/host/vu1_aot_builder.h"
#include "app/host/vu1_aot_cache.h"
#include "engine/core/config/config.h"
#include "engine/title/title_profile.h"
#include "engine/vu/vu_unit.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_devices.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_gs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"

namespace ps2r::title::sly2 {

class LegMtvu {
public:
    LegMtvu(const ps2r::Config& cfg, const TitleProfile& title, const LegKnobs& knobs, LegCore& core, LegGs& gs,
            LegDevices& devices);
    LegMtvu(const LegMtvu&) = delete;
    LegMtvu& operator=(const LegMtvu&) = delete;

    // The worker's VU1 unit under MTVU, else the inline unit.
    VuUnit& LiveVu1() { return mtvu ? mtvu->WorkerVu1() : devices_.vu1; }

    std::unique_ptr<MtvuStack> mtvu;   // null = inline
    bool early_finish = false;         // engine.mtvu_early_finish (MTVU only)
    bool wait_probe = false;           // engine.mtvu_wait_probe (MTVU only)
    bool deferred_boundary = false;    // no terminal sync at the boundary (paced play only)
    std::unique_ptr<ps2r::app::Vu1AotBuilder> vu1_aot_builder;
    std::unique_ptr<ps2r::app::Vu1AotCache> vu1_aot_cache;

private:
    LegDevices& devices_;
};

}  // namespace ps2r::title::sly2
