#include "games/scus_97316_sly2_band_of_thieves/hle/widescreen.h"

#include <cstdio>
#include <cstring>

#include "engine/ee/cpu/dispatch_table.h"

namespace ps2r::title::sly2 {

float Widescreen::LoadF(uint32_t addr) const {
    float v;
    std::memcpy(&v, ram_.Data() + (addr & 0x1ffffff), sizeof v);
    return v;
}

void Widescreen::StoreF(uint32_t addr, float v) { std::memcpy(ram_.Data() + (addr & 0x1ffffff), &v, sizeof v); }

float Widescreen::Factor() const {
    if (!aspect_) return 1.0f;
    return static_cast<float>(aspect_->Ratio() / gs::AspectSelection::kNarrow);
}

bool Widescreen::Install() {
    DispatchTable::Body init = DispatchTable::Capture(kCameraInit);
    DispatchTable::Body project = DispatchTable::Capture(kProjectPoint);
    if (!init || !project) return false;
    // TraceBody keeps each generated body's continuation entries (savestate capture and restore). `entry` is
    // the function's own address (or 0) for a call; anything else resumes inside the generated body, which then
    // runs alone.
    DispatchTable::Install(kCameraInit, DispatchTable::TraceBody(kCameraInit, init, [this, init](CpuContext& ctx, uint32_t entry) {
        const uint32_t camera = ctx.cpuRegs.GPR.r[4].UL[0];
        init(ctx, entry);
        if (entry != 0 && entry != kCameraInit) return;
        camera_ = camera;
        base_ = LoadF(camera + kHorizontalScale);
        written_ = base_;
        Apply(ctx);
    }));
    DispatchTable::Install(kProjectPoint, DispatchTable::TraceBody(kProjectPoint, project, [this, project](CpuContext& ctx, uint32_t entry) {
        const float factor = Factor();
        if (factor == 1.0f || (entry != 0 && entry != kProjectPoint)) {
            project(ctx, entry);
            return;
        }
        const uint32_t divisor = ctx.cpuRegs.GPR.r[4].UL[0] + kProjectDivisor;
        const float own = LoadF(divisor);
        StoreF(divisor, own / factor);
        project(ctx, entry);
        StoreF(divisor, own);
    }));
    return true;
}

std::string Widescreen::Census() const {
    char buf[160];
    std::snprintf(buf, sizeof buf, "camera=0x%08x:base=%.5f:now=%.5f:factor=%.4f", camera_, base_,
                  camera_ ? LoadF(camera_ + kHorizontalScale) : 0.0f, Factor());
    return buf;
}

void Widescreen::OnBoundary(CpuContext& ctx) {
    if (!camera_) return;
    const float now = LoadF(camera_ + kHorizontalScale);
    if (now != written_) {   // the game set its own value since (a camera blend): widen that
        base_ = now;
        written_ = now;
    }
    if (base_ * Factor() != written_) Apply(ctx);
}

void Widescreen::Apply(CpuContext& ctx) {
    const float want = base_ * Factor();
    StoreF(camera_ + kHorizontalScale, want);
    written_ = want;
    const uint32_t sp = ctx.cpuRegs.GPR.r[29].UL[0];
    if (sp < 0x00100000u || sp > 0x02000000u) return;   // no game stack to run on; the next recalc picks it up
    const auto gpr = ctx.cpuRegs.GPR;
    const uint32_t pc = ctx.cpuRegs.pc;
    const auto fpu = ctx.fpuRegs;
    ctx.cpuRegs.GPR.r[4].UD[0] = camera_;
    DispatchTable::Invoke(kRecalc, ctx);
    ctx.cpuRegs.GPR = gpr;
    ctx.cpuRegs.pc = pc;
    ctx.fpuRegs = fpu;
}

}  // namespace ps2r::title::sly2
