// games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h -- LegMtvu: VU1 (and the VIF1 stream that drives
// it) on its own host thread, and the VU1 AOT tier's persistent side.
// MTVU (app/host/mtvu_stack.h, the first title's bundle): the worker's own VU1 unit, the staged VIF1
// feed on the DMAC's chain walker (the walk, the deferred-kick model and the channel's completion stay
// on the EE; the VIF1 bytes cross to the worker, copied), the GIF arbiter's order markers (the worker's
// PATH1/PATH2 packets land at the stream position the inline path would have produced them at; the EE's
// PATH3 behind), MtvuSync on the inline dispatch (an EE kick of VU1 syncs and runs on the worker's unit),
// the VIF1 register window that syncs first. Play mode: Config engine.mtvu (default on). Headless /
// --dump-frames: the lockstep inline path (the frames lane's determinism) unless NATIVEEE_MTVU=1 asks
// for the worker. The GIF DRAIN THREAD (the GIF -> GsState -> DrawIR decode off the EE): on whenever the
// DrawIR chain's consumer runs on its own thread (gs.queue=pipelined / thread) or no video stack exists
// -- never with gs.queue=inline (the consumer draws on the pushing thread) -- NATIVEEE_GIF_DRAIN=1|0
// forces it. With the thread up the EE's two pushes into the DrawIR chain travel as ordered arbiter
// slots: the boundary's VSync job (LegGs::ordered_submit, fenced) and the privileged display registers'
// jobs (GsPrivRegs::SetJobDeferrer); the FINISH / SIGNAL events rise when the drain thread's parser
// reaches them (Intc's foreign-thread route). Boundaries are NOT deferred (Sly 2's render thread waits
// for both of its GS FINISHes before the vblank wait); kick drop OFF (Sly's FINISH rides a DIRECT packet
// of the same chain). Started before any producer traffic (the ELF is not loaded yet); stopped in the
// join ladder / destroyed before the DMAC (this object sits after LegDevices on RunLeg's frame).
// The AOT tier (app/host/vu1_aot_cache.h, app/host/vu1_aot_builder.h) on the LIVE VU1 unit (the worker's
// under MTVU): the manifest under <user_data>/vu1_aot/<build>/ seeds the verdicts, the corpus is
// verified on a background thread, the kick observer collects new images; the code generator emits a
// verified image's blocks as C++, compiles them with the host toolchain, re-verifies and hands them to
// the tier. The builder is declared BEFORE the cache (destroyed after it: the tier may hold its
// functions until the VU is quiet).
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
    bool DrainThread() const { return mtvu && mtvu->drain().Running(); }
    LegMtvu(const LegMtvu&) = delete;
    LegMtvu& operator=(const LegMtvu&) = delete;

    // The live VU1 unit: the worker's under MTVU, else the inline unit.
    VuUnit& LiveVu1() { return mtvu ? mtvu->WorkerVu1() : devices_.vu1; }

    std::unique_ptr<MtvuStack> mtvu;   // null = the inline path
    std::unique_ptr<ps2r::app::Vu1AotBuilder> vu1_aot_builder;
    std::unique_ptr<ps2r::app::Vu1AotCache> vu1_aot_cache;

private:
    LegDevices& devices_;
};

}  // namespace ps2r::title::sly2
