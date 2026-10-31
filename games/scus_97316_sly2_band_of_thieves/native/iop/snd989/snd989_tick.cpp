// snd989_tick.cpp -- the 240 Hz tick (fn_4325c) and the voice-mask / lock / error-reporter functions it reaches,
// ported from the recompiled 989snd_Library over the module's own RAM (snd989_state.h; the ABI: snd989_abi.h).
// Each body follows its recompiled original statement by statement; the libsd / kernel imports are made in the
// original order with the original arguments (the op stream is the oracle).
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

#include <cstdio>

namespace ps2r::title::sly2::snd989 {

// fn_35140: the error reporter. snd_ReportError(code, a, b, c, d): the registered hook (g[kErrorHook]) when set,
// else printf(kFmtError, code, a, b, c, d) unless quiet. `d` is the caller's first stack argument.
void fn_35140(IopContext& c) {
    const uint32_t code = A0(c), a = A1(c), b = A2(c), cc = A3(c);
    const uint32_t d = StackArg(c, 0);
    Frame f(c, 0x20);
    const uint32_t hook = Ld32(G::kErrorHook);
    if (hook != 0) {
        St32(f.At(0x10), d);
        CallAddr(c, hook, code, a, b, cc);
    } else if (Ld32(G::kQuiet) == 0) {
        St32(f.At(0x10), cc);
        St32(f.At(0x14), d);
        imp::Printf(c, G::kFmtError, code, a, b);
    }
}

// The lock diagnostics both the deadlocked lock (fn_42f58) and the stuck tick (fn_4325c) print after their own header.
static void PrintLockStack(IopContext& c, Frame& f) {
    imp::Printf(c, G::kFmtLockHdr);
    imp::Printf(c, G::kFmtLockTag, Ld32(G::kLockTag));
    for (uint32_t i = 0; i < 32; ++i) imp::Printf(c, G::kFmtLockStack, i, Ld32(G::kLockTags + 4 * i));
    imp::Printf(c, G::kFmtLockDiag, Ld32(G::kLockDiag));
    (void)f;
}

// fn_42f58: snd_Lock(tag) -- take the global lock (recursive by owner thread), push the tag; returns the new depth.
void fn_42f58(IopContext& c) {
    const uint32_t tag = A0(c);
    Frame f(c, 0x38);
    const uint32_t tid = imp::GetThreadId(c);
    if (tid != Ld32(G::kLockOwner)) {
        const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x18));
        if (suspended != 0) {
            // Interrupts were already off: the module reports a deadlock, then `break` (the recompiled body
            // prints its BREAK line and returns).
            imp::Printf(c, G::kFmtLockDeadlock, tag);
            imp::Printf(c, G::kFmtLockOwner, Ld32(G::kLockOwner));
            const uint32_t me = imp::GetThreadId(c);
            imp::Printf(c, G::kFmtLockMe, me);
            PrintLockStack(c, f);
            std::printf("[IOP-RECOMP] BREAK at 0x43034\n");
            return;
        }
        imp::CpuResumeIntr(c, Ld32(f.At(0x18)));
        if (imp::WaitSema(c, Ld32(G::kLockSema)) != 0) {
            St32(f.At(0x10), 0);
            Call(c, iop_snd989::iop_func_00035140, 0xbb8, 0, 0, 0);
        }
        St32(G::kLockOwner, imp::GetThreadId(c));
    }
    const int32_t depth = Lds32(G::kLockDepth);
    if (depth < 32) St32(G::kLockTags + 4 * static_cast<uint32_t>(depth), tag);
    St32(G::kLockTag, tag);
    St32(G::kLockDepth, static_cast<uint32_t>(depth + 1));
    Ret(c, static_cast<uint32_t>(depth + 1));
}

// fn_430dc: snd_Unlock() -- pop one level; the last level frees the owner and signals the semaphore.
void fn_430dc(IopContext& c) {
    Frame f(c, 0x28);
    const uint32_t tid = imp::GetThreadId(c);
    if (tid != Ld32(G::kLockOwner)) {
        St32(f.At(0x10), 0);
        Call(c, iop_snd989::iop_func_00035140, 0x7b, 0, 0, 0);
        return;
    }
    const int32_t depth = Lds32(G::kLockDepth);
    if (depth == 0) return;
    const int32_t nd = depth - 1;
    St32(G::kLockDepth, static_cast<uint32_t>(nd));
    St32(G::kLockTag, 0);
    if (nd < 0) {
        St32(f.At(0x10), 0);
        Call(c, iop_snd989::iop_func_00035140, 0x24, 0, 0, 0);
    } else if (nd < 32) {
        St32(G::kLockTags + 4 * static_cast<uint32_t>(nd), 0);
    }
    if (Ld32(G::kLockDepth) != 0) return;
    St32(G::kLockOwner, 0xffffffffu);
    imp::SignalSema(c, Ld32(G::kLockSema));
}

// fn_431bc: snd_TryLock() for the tick -- PollSema when the lock is free; returns 1 when taken (tag 0xffff), 0 if not.
void fn_431bc(IopContext& c) {
    Frame f(c, 0x18);
    if (Ld32(G::kLockOwner) == 0xffffffffu) {
        const uint32_t r = imp::PollSema(c, Ld32(G::kLockSema));
        if (r == static_cast<uint32_t>(-0x1a3)) {   // KE_SEMA_ZERO: someone holds it
            Ret(c, 0);
            return;
        }
    } else {
        Ret(c, 0);
        return;
    }
    const uint32_t tid = imp::GetThreadId(c);
    St32(G::kLockOwner, tid);
    const int32_t depth = Lds32(G::kLockDepth);
    if (depth < 32) St32(G::kLockTags + 4 * static_cast<uint32_t>(depth), 0xffff);
    St32(G::kLockTag, 0xffff);
    St32(G::kLockDepth, static_cast<uint32_t>(Lds32(G::kLockDepth) + 1));
    Ret(c, 1);
}

// fn_43638: snd_GetTick() = g[kTickCount].
void fn_43638(IopContext& c) { Ret(c, Ld32(G::kTickCount)); }

// fn_4cf5c: apply the pending key-ons -- VMIXEL/VMIXER with the mix masks, KON with the pending masks (core 0
// then 1), then move the voices from `ending` to `on`. Under CpuSuspendIntr when anything is pending.
void fn_4cf5c(IopContext& c) {
    Frame f(c, 0x30);
    if (Ld32(G::kKonPending) == 0 && Ld32(G::kKonPending + 4) == 0) return;
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    imp::SdSetSwitch(c, 0x1900, Ld32(G::kVmix));
    imp::SdSetSwitch(c, 0x1b00, Ld32(G::kVmix));
    imp::SdSetSwitch(c, 0x1901, Ld32(G::kVmix + 4));
    imp::SdSetSwitch(c, 0x1b01, Ld32(G::kVmix + 4));
    imp::SdSetSwitch(c, 0x1500, Ld32(G::kKonPending));
    imp::SdSetSwitch(c, 0x1501, Ld32(G::kKonPending + 4));
    const uint32_t k0 = Ld32(G::kKonPending), k1 = Ld32(G::kKonPending + 4);
    St32(G::kKonPending + 4, 0);
    St32(G::kKonPending, 0);
    St32(G::kVoicesEnding, Ld32(G::kVoicesEnding) & ~k0);
    St32(G::kVoicesEnding + 4, Ld32(G::kVoicesEnding + 4) & ~k1);
    St32(G::kVoicesOn, Ld32(G::kVoicesOn) | k0);
    St32(G::kVoicesOn + 4, Ld32(G::kVoicesOn + 4) | k1);
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
}

// fn_4d074: apply the pending key-offs -- KOFF per core, then move the voices from `on` to `ending`.
void fn_4d074(IopContext& c) {
    Frame f(c, 0x28);
    if (Ld32(G::kKoffPending) == 0 && Ld32(G::kKoffPending + 4) == 0) return;
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    imp::SdSetSwitch(c, 0x1600, Ld32(G::kKoffPending));
    imp::SdSetSwitch(c, 0x1601, Ld32(G::kKoffPending + 4));
    const uint32_t k0 = Ld32(G::kKoffPending), k1 = Ld32(G::kKoffPending + 4);
    St32(G::kKoffPending + 4, 0);
    St32(G::kKoffPending, 0);
    St32(G::kVoicesOn, Ld32(G::kVoicesOn) & ~k0);
    St32(G::kVoicesOn + 4, Ld32(G::kVoicesOn + 4) & ~k1);
    St32(G::kVoicesEnding, Ld32(G::kVoicesEnding) | k0);
    St32(G::kVoicesEnding + 4, Ld32(G::kVoicesEnding + 4) | k1);
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
}

// fn_4d318(voice): key the voice off -- the owner's callback (why = 3) once, then under CpuSuspendIntr: drop a
// pending key-on, queue the key-off.
void fn_4d318(IopContext& c) {
    const int v = static_cast<int32_t>(A0(c));
    Frame f(c, 0x28);
    const uint32_t rec = G::Voice(v);
    if (Ld32(rec + G::kVoiceState) == 1) {
        const uint32_t cb = Ld32(rec + G::kVoiceCb);
        if (cb != 0) CallAddr(c, cb, static_cast<uint32_t>(v), Ld32(rec + G::kVoiceCbArg), 3, G::kVoiceTable);
    }
    St32(rec + G::kVoiceCb, 0);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    const uint32_t core = static_cast<uint32_t>(VoiceCore(v)) * 4;
    const uint32_t bit = 1u << VoiceBit(v);
    const uint32_t kon = Ld32(G::kKonPending + core);
    if (kon & bit) St32(G::kKonPending + core, kon & ~bit);
    St32(G::kKoffPending + core, Ld32(G::kKoffPending + core) | bit);
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
}

// fn_4d184(voice): the voice has finished releasing -- free it (fn_4dec0 unless priority 0x7f: the movie / stream
// voices keep theirs), the owner's callback (why = 2) once, clear the `ending` bit.
void fn_4d184(IopContext& c) {
    const int v = static_cast<int32_t>(A0(c));
    Frame f(c, 0x28);
    const uint32_t rec = G::Voice(v);
    if (Ld8(rec + G::kVoicePrio) != 0x7f) Call(c, iop_snd989::iop_func_0004dec0, static_cast<uint32_t>(v));
    const uint32_t cb = Ld32(rec + G::kVoiceCb);
    if (cb != 0) CallAddr(c, cb, static_cast<uint32_t>(v), Ld32(rec + G::kVoiceCbArg), 2);
    St32(rec + G::kVoiceCb, 0);
    const uint32_t core = static_cast<uint32_t>(VoiceCore(v)) * 4;
    const uint32_t bit = 1u << VoiceBit(v);
    St32(G::kVoicesEnding + core, Ld32(G::kVoicesEnding + core) & ~bit);
}

// fn_4325c: THE TICK (the hard-timer beat's thread body calls it). Returns 0.
//   1. the re-entrancy guard; the trylock (unless kTickNoLock) -- a failed trylock counts a pending beat and, every
//      480th, prints the lock diagnostics;
//   2. the voice scan: for each active voice not pending key-on, ENVX == 0 ends it (ending -> fn_4d184 free; on ->
//      after 4 silent steps fn_4d318 key-off), ENVX != 0 rearms the 4-step countdown;
//   3. one step per pending beat: tick count, fn_4218c (the sound list), fn_4d074 (KOFF), fn_4cf5c (KON),
//      fn_40f64, fn_35a04 (sound-end handlers), fn_4efb4 (the fades) every 4th step;
//   4. the stream reader's CD watch; unlock.
void fn_4325c(IopContext& c) {
    Frame f(c, 0x38);
    if (Ld32(G::kTickBusy) != 0) {
        Ret(c, 0);
        return;
    }
    St32(G::kTickBusy, 1);
    if (Ld32(G::kTickNoLock) == 0) {
        if (Call(c, iop_snd989::iop_func_000431bc) == 0) {
            const int32_t n = Lds32(G::kTickPending) + 1;
            St32(G::kTickPending, static_cast<uint32_t>(n));
            if (n == (n / 480) * 480) {
                imp::Printf(c, G::kFmtTickStuck);
                imp::Printf(c, G::kFmtTickDepth, Ld32(G::kLockDepth));
                PrintLockStack(c, f);
            }
            St32(G::kTickBusy, 0);
            Ret(c, 0);
            return;
        }
    }
    St32(G::kTickPending, static_cast<uint32_t>(Lds32(G::kTickPending) + 1));
    for (uint32_t node = Ld32(G::kActiveVoices); node != 0; node = Ld32(node)) {
        const int v = Lds32(node + 4);
        const uint32_t core = static_cast<uint32_t>(VoiceCore(v));
        const uint32_t bit = static_cast<uint32_t>(VoiceBit(v));
        const uint32_t mask = 1u << bit;
        if (Ld32(G::kKonPending + core * 4) & mask) continue;
        const uint32_t envx = imp::SdGetParam(c, ((core | (bit << 1)) | 0x500) & 0xffff);
        const uint32_t silent = G::kVoiceSilentTicks + static_cast<uint32_t>(v);
        if ((envx << 16) != 0) {
            St8(silent, 4);
            continue;
        }
        if (Ld32(G::kVoicesEnding + core * 4) & mask) {
            Call(c, iop_snd989::iop_func_0004d184, static_cast<uint32_t>(v));
            continue;
        }
        if (Ld32(G::kVoicesOn + core * 4) & mask) {
            St8(silent, Ld8(silent) - 1);
            const int32_t left = Lds8(silent);
            if (left == 0) Call(c, iop_snd989::iop_func_0004d318, static_cast<uint32_t>(v));
            else if (left < 0) St8(silent, 4);
        }
    }
    if (Lds32(G::kTickPending) > 0) {
        do {
            St32(G::kTickCount, Ld32(G::kTickCount) + 1);
            Call(c, iop_snd989::iop_func_0004218c);
            Call(c, iop_snd989::iop_func_0004d074);
            Call(c, iop_snd989::iop_func_0004cf5c);
            Call(c, iop_snd989::iop_func_00040f64);
            Call(c, iop_snd989::iop_func_00035a04);
            if ((Ld32(G::kTickCount) & 3) == 0) Call(c, iop_snd989::iop_func_0004efb4);
            St32(G::kTickPending, static_cast<uint32_t>(Lds32(G::kTickPending) - 1));
        } while (Lds32(G::kTickPending) > 0);
    }
    const uint32_t watch = Ld32(G::kCdWatch);
    St32(G::kTickPending, 0);
    if (watch != 0) {
        const int32_t t = Lds32(G::kCdWatchTicks) + 1;
        St32(G::kCdWatchTicks, static_cast<uint32_t>(t));
        if (t >= 0x961) {
            if (Ld32(G::kQuiet) == 0) imp::Printf(c, G::kFmtTickCd, watch == 1 ? G::kFmtTickCdA : G::kFmtTickCdB);
            imp::SceCdReadIOPm(c);
            St32(G::kCdWatchTicks, 0);
        }
    }
    St32(G::kTickBusy, 0);
    Call(c, iop_snd989::iop_func_000430dc);
    Ret(c, 0);
}

}  // namespace ps2r::title::sly2::snd989
