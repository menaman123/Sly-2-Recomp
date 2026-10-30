#include "games/scus_97316_sly2_band_of_thieves/hle/motion_blur.h"

#include "engine/ee/cpu/dispatch_table.h"

namespace ps2r::title::sly2 {

bool MotionBlur::Install() {
    DispatchTable::Body blend = DispatchTable::Capture(kBlend);
    if (!blend) return false;
    // TraceBody keeps the generated body's continuation entries (savestate capture and restore). `entry` is the
    // function's own address (or 0) for a call; anything else resumes inside the generated body.
    return DispatchTable::Install(kBlend, DispatchTable::TraceBody(kBlend, blend, [this, blend](CpuContext& ctx, uint32_t entry) {
        if (On() || (entry != 0 && entry != kBlend)) {
            blend(ctx, entry);
            return;
        }
        ++skipped_;
        ctx.cpuRegs.GPR.r[2].SD[0] = static_cast<int32_t>((kSkipFlag + 0x8000u) & 0xffff0000u);   // the lui half
        ctx.cpuRegs.GPR.r[3].SD[0] = 1;
    }));
}

}  // namespace ps2r::title::sly2
