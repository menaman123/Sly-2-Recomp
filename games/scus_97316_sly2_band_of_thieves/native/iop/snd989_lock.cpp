// snd989_lock.cpp -- fn_42f58 / fn_430dc as native bodies over the module's RAM words.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989_lock.h"

#include <cstdio>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_services.h"

namespace ps2r::title::sly2 {

namespace {
Snd989Lock g_lock;
}  // namespace

void Snd989Lock::Register(NativeIopModule& snd989) {
    snd989.Add(kAcquire, &NativeIopModule::Thunk<Snd989Lock, &Snd989Lock::Acquire>, "snd_lock_acquire", &g_lock);
    snd989.Add(kRelease, &NativeIopModule::Thunk<Snd989Lock, &Snd989Lock::Release>, "snd_lock_release", &g_lock);
}

void Snd989Lock::Report(IopContext& ctx, uint32_t code) {
    // fn_35140(code, 0, 0, 0) with the fifth argument (0) in the caller's frame at $sp+0x10.
    IopRam::W32(ctx.gpr[29] + 0x10, 0);
    ctx.gpr[4] = code;
    ctx.gpr[5] = 0;
    ctx.gpr[6] = 0;
    ctx.gpr[7] = 0;
    IopServices::CallGuest(ctx, kReportFn);
}

// fn_42f58: a0 = the tag of this acquisition (the caller's id). Returns depth + 1 (v0).
void Snd989Lock::Acquire(IopContext& ctx) {
    const uint32_t tag = ctx.gpr[4];
    IopStackFrame frame(ctx, 0x38);
    const uint32_t tid = static_cast<uint32_t>(IopServices::GetThreadId(ctx));
    if (tid != IopRam::R32(kOwner)) {
        // Not the owner: take the semaphore (the park point of every 989snd entry).
        if (IopServices::CpuSuspendIntr(ctx, frame.Sp() + 0x18) != 0) {
            // Interrupts were already disabled: the module's deadlock report, then its `break`
            // (the recompiled body prints the BREAK line and returns without taking the lock).
            IopServices::Printf(ctx, 0x5036c, tag);
            IopServices::Printf(ctx, 0x503b4, IopRam::R32(kOwner));
            IopServices::Printf(ctx, 0x503d4, static_cast<uint32_t>(IopServices::GetThreadId(ctx)));
            IopServices::Printf(ctx, 0x503f4);
            IopServices::Printf(ctx, 0x5040c, IopRam::R32(kLastTag));
            for (uint32_t i = 0; i < kTagStackEntries; ++i) IopServices::Printf(ctx, 0x50424, i, IopRam::R32(kTagStack + i * 4));
            IopServices::Printf(ctx, 0x50440, IopRam::R32(kReportWord));
            std::printf("[IOP-RECOMP] BREAK at 0x43034\n");
            return;
        }
        IopServices::CpuResumeIntr(ctx, frame.Word(0x18));
        if (IopServices::WaitSema(ctx, static_cast<int32_t>(IopRam::R32(kSema))) != 0) Report(ctx, 0xbb8);
        IopRam::W32(kOwner, static_cast<uint32_t>(IopServices::GetThreadId(ctx)));
    }
    // The tag stack push and the depth count (the recompiled body's 0x43084 tail).
    const uint32_t depth = IopRam::R32(kDepth);
    if (static_cast<int32_t>(depth) < static_cast<int32_t>(kTagStackEntries)) IopRam::W32(kTagStack + depth * 4, tag);
    IopRam::W32(kLastTag, tag);
    IopRam::W32(kDepth, depth + 1);
    ctx.gpr[2] = depth + 1;
}

// fn_430dc: no arguments. v0 is what the recompiled body leaves at each exit (callers ignore it).
void Snd989Lock::Release(IopContext& ctx) {
    IopStackFrame frame(ctx, 0x28);
    const uint32_t tid = static_cast<uint32_t>(IopServices::GetThreadId(ctx));
    if (tid != IopRam::R32(kOwner)) {
        Report(ctx, 0x7b);   // v0 = the reporter's
        return;
    }
    const uint32_t depth = IopRam::R32(kDepth);
    ctx.gpr[2] = 0x50000u;   // the body's v0 at the depth-0 exit (the .bss base in $v0 after `lui`)
    if (depth == 0) return;
    const uint32_t d = depth - 1;
    IopRam::W32(kDepth, d);
    IopRam::W32(kLastTag, 0);
    if (static_cast<int32_t>(d) < 0) {
        Report(ctx, 0x24);   // underflow
    } else if (d < kTagStackEntries) {
        IopRam::W32(kTagStack + d * 4, 0);
    }
    const uint32_t remaining = IopRam::R32(kDepth);
    ctx.gpr[2] = 0x50000u;   // the delay slot's `lui $v0, 5` runs on both arms of the branch
    if (remaining != 0) return;
    const int32_t sid = static_cast<int32_t>(IopRam::R32(kSema));
    IopRam::W32(kOwner, 0xffffffffu);
    IopServices::SignalSema(ctx, sid);   // v0 = SignalSema's
}

}  // namespace ps2r::title::sly2
