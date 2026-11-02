// snd989_lock.h -- the 989snd global lock as native bodies: the shared service every native 989snd
// body takes before touching the driver's state (the "two locks" of native/snd/README.md).
//
// The recompiled module keeps the lock in its own RAM (989snd_Library at 0x33000; the words below are
// its .bss as the IRX laid them, read off the generated TU):
//   g[0x5229c]  owner tid (-1 = free)         g[0x52528]  the semaphore id (CreateSema'd at init)
//   g[0x52214]  recursion depth                g[0x52218]  the 32-entry tag stack (one word per Acquire)
//   g[0x52298]  the last Acquire's tag          g[0x523b0]  (printed by the deadlock report)
// Acquire(tag) = fn_42f58 (97 insns): GetThreadId; a re-entrant owner skips the wait; otherwise
// CpuSuspendIntr/CpuResumeIntr then WaitSema(sema) (the park point), the error reporter fn_35140(0xbb8)
// on a failed wait, owner = tid; then the tag stack push, depth + 1 returned.
// Release() = fn_430dc (56 insns): a non-owner reports fn_35140(0x7b); depth 0 is a no-op; depth - 1,
// last tag cleared, the stack slot cleared (fn_35140(0x24) on underflow); at depth 0 owner = -1 and
// SignalSema(sema).
// Both call the kernel through IopServices (the same HLE bodies the recompiled code reaches), so the
// semaphore's waiter order, the coroutine park and the intrman flag are the hosted kernel's; the
// register results (v0) are the recompiled bodies' at every exit.
#pragma once

#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_iop_module.h"

namespace ps2r::title::sly2 {

class Snd989Lock {
public:
    static constexpr uint32_t kAcquire = 0x42f58;   // fn_42f58
    static constexpr uint32_t kRelease = 0x430dc;   // fn_430dc
    static constexpr uint32_t kOwner = 0x5229c;
    static constexpr uint32_t kSema = 0x52528;
    static constexpr uint32_t kDepth = 0x52214;
    static constexpr uint32_t kTagStack = 0x52218;
    static constexpr uint32_t kTagStackEntries = 0x20;
    static constexpr uint32_t kLastTag = 0x52298;
    static constexpr uint32_t kReportWord = 0x523b0;
    static constexpr uint32_t kReportFn = 0x35140;   // fn_35140(code, 0, 0, 0, [sp+0x10] = 0)

    // Registers both bodies on the module (snd989).
    static void Register(NativeIopModule& snd989);

    void Acquire(IopContext& ctx);
    void Release(IopContext& ctx);

private:
    static void Report(IopContext& ctx, uint32_t code);   // fn_35140 with the stack argument
};

}  // namespace ps2r::title::sly2
