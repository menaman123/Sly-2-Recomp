// snd989_control.cpp -- the control plane the boot reaches once or a few times: stop-all (fn_41c9c) and the voice
// mask key-off (fn_4d5f0), the periodic timer pool (fn_42924 add, fn_4272c step), the loader seek (fn_3ad30), the
// ramp-done callback (fn_3a964) and its semaphore waits (fn_3d0d8 / fn_3d118), the heap free (fn_3e0c0), the key-on
// request (fn_4dfc4), the group volume read (fn_4e220), the pan mode (fn_4e23c), the external command with arguments
// (fn_33250), the handle pool / sound list / timer resets (fn_41074 / fn_410c0 / fn_426dc / fn_4e0b0), the bank
// load (fn_3c5d8), the movie buffer clear (fn_3e678), and the RPC shims that only forward (fno 6 / 8 / 10 / 11 / 19 /
// 20 / 21 / 22 / 23 / 25 / 34 / 59 / 60). Ported from the recompiled 989snd_Library over the module's RAM.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

namespace {
constexpr uint32_t kFmtSoundListUnlinked = G::kData + 0x60;
constexpr uint32_t kTimerRoot = G::kData + 0x2208;
constexpr uint32_t kTimerPool = 0x581b8;                      // 0x81 nodes of 0x18 (lui 6, -0x7e48)
constexpr uint32_t kRampDonePending = G::kData + 0x1920;     // the ramp whose end the tick reported (+ 1), 0 = none
constexpr uint32_t kRampDoneHook = G::kData + 0x1924;        // a function pointer called at the end, or 0
constexpr uint32_t kRampDoneSema = G::kData + 0x1928;        // signalled at the end (-1 = none)
constexpr uint32_t kLoadFd = G::kData + 0x1958, kLoadFromCd = G::kData + 0x195c, kLoadBase = G::kData + 0x1960;
constexpr uint32_t kLoadSector = G::kData + 0x1964, kLoadSector2 = G::kData + 0x196c, kLoadBase2 = G::kData + 0x1970;
constexpr uint32_t kLoadOffset = G::kData + 0x1968, kLoadError = G::kData + 0x1974, kLoadFromMem = G::kData + 0x1978;
constexpr uint32_t kLoadMemPos = G::kData + 0x197c, kLoadMemBase2 = G::kData + 0x1980;
constexpr uint32_t kStatusEeAddr = G::kData + 0x1990;
constexpr uint32_t kGroupHook = G::kData + 0x618;
constexpr uint32_t kSeqHandleTable = G::kData + 0x2fb8;
constexpr uint32_t kNodePool = G::kData + 0x78b8;
constexpr uint32_t kSoundListTail = G::kData + 0x21ec;
constexpr uint32_t kActiveSlotTable = 0x59e28;                // 4 words of 0x18 cleared to -1 (lui 6, -0x61d8)
constexpr uint32_t kMovieMode = G::kData + 0x19d8, kMovieBufSize = G::kData + 0x19bc;
}  // namespace

// fn_41c9c(): stop every handle: under the lock each sound-list root not already stopping is stopped (fn_41da0(rec,
// 1, 1, 0); an unflagged record prints), then every voice keyed off (fn_4d5f0 with both masks -1, notifying); the
// lock released; then the thread sleeps 33 ms at a time until the list is empty.
void fn_41c9c(IopContext& c) {
    Frame f(c, 0x30);
    Call(c, 0x42f58, 0x4e);
    for (uint32_t rec = Ld32(G::kSoundList); rec != 0; rec = Ld32(rec + 0x24)) {
        if (!(Ld32(rec) & 0x80000000u)) imp::Printf(c, kFmtSoundListUnlinked);
        if (Ld32(rec + 0x28) == 0 && !(Ld8(rec + 0x16) & 4)) Call(c, 0x41da0, rec, 1, 1, 0);
    }
    St32(f.At(0x10), 0xffffffffu);
    St32(f.At(0x14), 0xffffffffu);
    Call(c, 0x4d5f0, f.At(0x10), 1);
    Call(c, 0x430dc);
    while (Ld32(G::kSoundList) != 0) Import(c, "thbase", 33, 0x80e8);
}

// fn_4d5f0(masks, notify): key off every voice whose bit is set in masks[core]: an active (state 1) voice's end
// callback runs (v, owner, 3) when notifying; the callback is cleared; a non-stream voice (priority != 0x7f) leaves
// the active list; VOLL/VOLR zeroed under suspended interrupts; the key-off requested (fn_4e02c).
void fn_4d5f0(IopContext& c) {
    const uint32_t masks = A0(c), notify = A1(c), a3_in = A3(c);
    Frame f(c, 0x40);
    for (int32_t v = 0; v < 0x30; ++v) {
        const int32_t core = Div24(v), bit = v - core * 24;
        if (!(Ld32(masks + U32(core) * 4) & (1u << bit))) continue;
        const uint32_t voice = G::Voice(v);
        if (notify != 0 && Ld32(voice + 8) == 1 && Ld32(voice + 0x10) != 0)
            CallAddr(c, Ld32(voice + 0x10), U32(v), Ld32(voice + 0xc), 3, a3_in);
        St32(voice + 0x10, 0);
        if (Ld8(voice + 0x1a) != 0x7f) Call(c, 0x4dec0, U32(v));
        const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
        const uint32_t e = U32(core) | (U32(bit) << 1);
        imp::SdSetParam(c, e & 0xffff, 0);
        imp::SdSetParam(c, (e | 0x100) & 0xffff, 0);
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
        Call(c, 0x4e02c, U32(core), U32(bit));
    }
}

// fn_42924(when, period): a periodic timer: `period` rounded up to 64; a pool node due at `when` (its due + period
// == when, or its tick == when + period) is merged (unlinked, the periods added); the first free pool slot (or the
// merged one) is programmed {tick = when, period, no links} and inserted (fn_42d3c). Errors 0x5d (period 0) / 0x21
// (pool full).
void fn_42924(IopContext& c) {
    uint32_t when = A0(c);
    uint32_t period = A1(c);
    Frame f(c, 0x38);
    if (period == 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x5d, when, 0, 0);
        return;
    }
    if (period & 0x3f) period = (period + 0x40) - (period & 0x3f);
    int32_t slot = 0x81;
    uint32_t node = kTimerPool;
    for (int32_t i = 0; i < 0x81; ++i, node += 0x18) {
        if (Ld32(node + 8) == 0) {
            if (i < slot) slot = i;
            continue;
        }
        if (Ld32(node) + Ld32(node + 4) == when) {
            Call(c, 0x42c84, node);
            const bool better = i < slot;
            when = Ld32(node);
            period += Ld32(node + 4);
            if (better) slot = i;
        } else if (Ld32(node) == when + period) {
            Call(c, 0x42c84, node);
            const bool better = i < slot;
            period += Ld32(node + 4);
            if (better) slot = i;
        }
    }
    if (slot < 0x81) {
        const uint32_t n = kTimerPool + U32(slot) * 0x18;
        St32(n + 4, period);
        St32(n, when);
        St32(n + 0xc, 0);
        St32(n + 0x10, 0);
        St32(n + 0x14, 0);
        Call(c, 0x42d3c, n);
        return;
    }
    St32(f.At(0x10), 0);
    Call(c, 0x35140, 0x21, 0, 0, 0);
}

// fn_4272c(ticks): the timer tree step: `ticks` (rounded up to 64) elapse; the earliest node due (fn_42dd0 down the
// earlier children) is unlinked, its remaining due reduced (re-inserted at tick + ticks when some remains). Returns
// its tick word (0 for nothing / none due: error 0x20).
void fn_4272c(IopContext& c) {
    uint32_t ticks = A0(c);
    Frame f(c, 0x28);
    if (ticks == 0) {
        Ret(c, 0);
        return;
    }
    if (ticks & 0x3f) ticks = (ticks + 0x40) - (ticks & 0x3f);
    uint32_t node = Call(c, 0x42dd0, Ld32(kTimerRoot), ticks);
    if (node == 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x20, ticks, 0, 0);
        Ret(c, 0);
        return;
    }
    for (;;) {
        const uint32_t earlier = Call(c, 0x42dd0, Ld32(node + 0x10), ticks);
        if (earlier == 0) break;
        node = earlier;
    }
    const uint32_t tick = Ld32(node);
    Call(c, 0x42c84, node);
    const uint32_t left = Ld32(node + 4) - ticks;
    St32(node + 4, left);
    if (left != 0) {
        St32(node, Ld32(node) + ticks);
        Call(c, 0x42d3c, node);
    }
    Ret(c, tick);
}

// fn_3ad30(offset, whence): the loader seek: no file -> 0. From the CD: the sector / offset from the base (whence 0:
// kLoadFromCd's sector + kLoadBase; else kLoadSector2 + kLoadBase2) plus `offset`, the offset folded into whole
// sectors; returns `offset`. From memory: the position = base (kLoadFromMem / kLoadMemBase2) + offset; returns it.
// Else ioman lseek(fd, offset + base, 0).
void fn_3ad30(IopContext& c) {
    const uint32_t offset = A0(c), whence = A1(c);
    Frame f(c, 0x18);
    const uint32_t fd = Ld32(kLoadFd);
    if (fd == 0xffffffffu) {
        Ret(c, 0);
        return;
    }
    const uint32_t from_cd = Ld32(kLoadFromCd);
    if (from_cd == 0) {
        const uint32_t from_mem = Ld32(kLoadFromMem);
        if (from_mem == 0) {
            const uint32_t base = whence != 0 ? Ld32(kLoadBase2) : Ld32(kLoadBase);
            Ret(c, Import(c, "ioman", 8, fd, offset + base, 0));
            return;
        }
        if (whence == 0) St32(kLoadMemPos, from_mem + offset);
        else St32(kLoadMemPos, Ld32(kLoadMemBase2) + offset);
        Ret(c, Ld32(kLoadMemPos));
        return;
    }
    if (whence == 0) {
        St32(kLoadSector, from_cd);
        St32(kLoadOffset, Ld32(kLoadBase) + offset);
    } else {
        St32(kLoadSector, Ld32(kLoadSector2));
        St32(kLoadOffset, Ld32(kLoadBase2) + offset);
    }
    const int32_t off = Lds32(kLoadOffset);
    if (!(off < 0x800)) {
        const int32_t sectors = (off < 0 ? off + 0x7ff : off) >> 11;
        St32(kLoadOffset, U32(off - S32(U32(sectors) << 11)));
        St32(kLoadSector, Ld32(kLoadSector) + U32(sectors));
    }
    Ret(c, offset);
}

// fn_3a964(): the pending ramp end (kRampDonePending - 1) is finished (fn_3e1cc), the hook called, the semaphore
// signalled (from the interrupt). Returns 1 when nothing pended, else 0.
void fn_3a964(IopContext& c) {
    Frame f(c, 0x18);
    const uint32_t pending = Ld32(kRampDonePending);
    if (pending == 0) {
        Ret(c, 1);
        return;
    }
    Call(c, 0x3e1cc, pending - 1);
    const uint32_t hook = Ld32(kRampDoneHook);
    St32(kRampDonePending, 0);
    if (hook != 0) CallAddr(c, hook, c.gpr[4], c.gpr[5], c.gpr[6], c.gpr[7]);
    const int32_t sema = Lds32(kRampDoneSema);
    if (sema >= 0) imp::iSignalSema(c, U32(sema));
    Ret(c, 0);
}

// fn_3d0d8(): drain the ramp-done semaphore (PollSema until SemaZero).
void fn_3d0d8(IopContext& c) {
    Frame f(c, 0x20);
    uint32_t r;
    do {
        r = imp::PollSema(c, Ld32(kRampDoneSema));
    } while (r != 0xfffffe5du);
}

// fn_3d118(wait): wait for the ramp end (WaitSema; a failure reports 0x7d1). Returns 1 when no ramp end pends.
void fn_3d118(IopContext& c) {
    const uint32_t wait = A0(c);
    Frame f(c, 0x20);
    if (wait != 0 && imp::WaitSema(c, Ld32(kRampDoneSema)) != 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x7d1, 0, 0, 0);
    }
    Ret(c, Ld32(kRampDonePending) == 0 ? 1u : 0u);
}

// fn_3e0c0(p): FreeSysMemory(p) with interrupts suspended.
void fn_3e0c0(IopContext& c) {
    const uint32_t p = A0(c);
    Frame f(c, 0x28);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    Import(c, "sysmem", 5, p);
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
}

// fn_4dfa4(v): fn_4dec0(v).
void fn_4dfa4(IopContext& c) {
    const uint32_t a0 = A0(c), a1 = A1(c), a2 = A2(c), a3 = A3(c);
    Frame f(c, 0x18);
    Call(c, 0x4dec0, a0, a1, a2, a3);
}

// fn_4dfc4(core, bit, fx): the voice's key-on pending bit; its effects-send bit set / cleared by `fx`.
void fn_4dfc4(IopContext& c) {
    const uint32_t core4 = A0(c) * 4, bit = A1(c), fx = A2(c);
    const uint32_t m = 1u << bit;
    St32(G::kKonPending + core4, Ld32(G::kKonPending + core4) | m);
    if (fx != 0) St32(G::kVmix + core4, Ld32(G::kVmix + core4) | m);
    else St32(G::kVmix + core4, Ld32(G::kVmix + core4) & ~m);
}

// fn_4e220(group): the group's volume word.
void fn_4e220(IopContext& c) { Ret(c, Ld32(G::kParamTable + A0(c) * 4)); }

// fn_4e23c(mode): the pan mode half-word.
void fn_4e23c(IopContext& c) { St16(G::kData + 0x2518, A0(c)); }

// fn_33250(name, op, arg0, arg1): an external module's command `op` with two arguments (the module record found by
// name: fn_33148; unknown -> error 3 (name twice); op past its count -> error 4). Returns the handler's result, 0 on
// error.
void fn_33250(IopContext& c) {
    const uint32_t name = A0(c), arg0 = A2(c), arg1 = A3(c);
    const int32_t op = S32(A1(c));
    Frame f(c, 0x30);
    const uint32_t mod = Call(c, 0x33148, name);
    if (mod == 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 3, name, name, 0);
        Ret(c, 0);
        return;
    }
    if (op < Lds32(mod + 4)) {
        St32(f.At(0x10), 0);
        CallAddr(c, Ld32(mod + 0x10 + U32(op) * 4), arg0, arg1, 0, 0);
        return;
    }
    St32(f.At(0x10), 0);
    Call(c, 0x35140, 4, name, 0, 0);
    Ret(c, 0);
}

// fn_41074(table, count, type, stride): reset a handle pool: rec[0] = index << 16 | type << 24 | 1, the owner /
// list / flag words cleared.
void fn_41074(IopContext& c) {
    uint32_t rec = A0(c);
    const int32_t count = S32(A1(c));
    const uint32_t type = A2(c) << 24, stride = A3(c);
    for (int32_t i = 0; i < count; ++i, rec += stride) {
        St32(rec, (U32(i) << 16) | type | 1);
        St32(rec + 4, 0);
        St32(rec + 8, 0);
        St8(rec + 0x16, 0);
        St8(rec + 0x17, 0);
        St32(rec + 0x1c, 0);
        St32(rec + 0x18, 0);
    }
}

// fn_410c0(): reset the sequence handle pool (fn_41074), the sound-list node pool and the sound list.
void fn_410c0(IopContext& c) {
    Frame f(c, 0x18);
    Call(c, 0x41074, kSeqHandleTable, 0x40, 5, 0x124);
    uint32_t n = kNodePool;
    for (int i = 0x3f; i >= 0; --i, n += 0x24) {
        St32(n, 0);
        St32(n + 0xc, 0);
    }
    St32(kSoundListTail, 0);
    St32(G::kSoundList, 0);
}

// fn_426dc(): reset the timer tree (root 0, every pool node's tick 0) and add the 0x5040-tick timer of period
// 0x1fafbf (fn_42924).
void fn_426dc(IopContext& c) {
    Frame f(c, 0x18);
    St32(kTimerRoot, 0);
    uint32_t n = kTimerPool + 0xc08;
    for (int i = 0x80; i >= 0; --i, n -= 0x18) St32(n, 0);
    Call(c, 0x42924, 0x5040, 0x1fafbfu);
}

// fn_4e0b0(): reset the per-core single-voice slots (4 words of 0x18 = -1) and the 15 group levels to 0x10000.
void fn_4e0b0(IopContext& c) {
    (void)c;
    uint32_t p = kActiveSlotTable + 0x48;
    for (int i = 3; i >= 0; --i, p -= 0x18) St32(p, 0xffffffffu);
    uint32_t g = G::kGroupFade + 0x38;
    for (int i = 0xe; i >= 0; --i, g -= 4) St32(g, 0x10000);
}

// fn_3c5d8(a0, a1, size, kind): a bank load: the loader opened (fn_3ac34(a0, a1 ..); failure -> 0), the error word
// cleared, the bank read (fn_3be20(size, kind) -> the bank), the loader closed (fn_3aca0); a loaded bank is announced to the
// group hook's first function (bank, 1). Returns the bank.
void fn_3c5d8(IopContext& c) {
    const uint32_t a0 = A0(c), a1 = A1(c), size = A2(c), kind = A3(c);
    Frame f(c, 0x20);
    if (Call(c, 0x3ac34, a0, a1, size, kind) == 0) {
        Ret(c, 0);
        return;
    }
    St32(kLoadError, 0);
    const uint32_t bank = Call(c, 0x3be20, size, kind);   // `jal 3aca0; move s0, v0`: the bank is fn_3be20's result
    Call(c, 0x3aca0, c.gpr[4], c.gpr[5], c.gpr[6], c.gpr[7]);
    if (bank != 0) {
        const uint32_t hook = Ld32(kGroupHook);
        if (hook != 0 && Ld32(hook) != 0) CallAddr(c, Ld32(hook), bank, 1, c.gpr[6], c.gpr[7]);
    }
    Ret(c, bank);
}

// fn_3e678(): clear the movie IOP buffer (kMovieBufSize / 4 words) and start a 0x400-byte block transfer to its
// SPU address (SdBlockTrans(chan, 0, buf, 0x400)) unless the movie mode is 3 (voices).
void fn_3e678(IopContext& c) {
    Frame f(c, 0x18);
    if (Ld32(kMovieMode) == 3) return;
    int32_t n = Lds32(kMovieBufSize);
    if (n < 0) n += 3;
    n >>= 2;
    uint32_t p = Ld32(G::kMovieIopBuf);
    for (; n > 0; --n, p += 4) St32(p, 0);
    Import(c, "libsd", 18, U32(Lds16(G::kMovieChan)), 0, Ld32(G::kMovieIopBuf), 0x400);
}

// The forwarding shims: fno 6 (fn_33310: kStatusEeAddr = buff[0]; fn_3a250(buff[4])), fno 8 (fn_33514 -> fn_3d674),
// fno 19 / 20 (fn_33804 / fn_33828 -> fn_41a64 / fn_41bd0 of buff[0]), fno 10 (fn_33560: the group volume),
// fno 34 (fn_34514: the external command), fno 25 (fn_33e5c -> fn_4b0a4(buff[0]) -> reply), fno 59 (fn_34044 ->
// fn_3e284 with 6 arguments -> reply), fno 60 (fn_3408c -> fn_3e6f4), fno 61 (fn_340ac -> fn_3e7d8), fno 62
// (fn_340cc -> fn_3e920 with 5 arguments).
void fn_33310(IopContext& c) {
    const uint32_t b = A0(c);
    Frame f(c, 0x18);
    St32(kStatusEeAddr, Ld32(b));
    Call(c, 0x3a250, Ld32(b + 4));
}
void fn_33514(IopContext& c) {
    const uint32_t a0 = A0(c), a1 = A1(c), a2 = A2(c), a3 = A3(c);
    Frame f(c, 0x18);
    Call(c, 0x3d674, a0, a1, a2, a3);
}
void fn_33560(IopContext& c) {
    const uint32_t b = A0(c);
    Frame f(c, 0x18);
    const uint32_t r = Call(c, 0x4e220, Ld32(b));
    St32(Ld32(G::kResultPtr), r);
}
void fn_33804(IopContext& c) {
    const uint32_t b = A0(c), a1 = A1(c), a2 = A2(c), a3 = A3(c);
    Frame f(c, 0x18);
    Call(c, 0x41a64, Ld32(b), a1, a2, a3);
}
void fn_33828(IopContext& c) {
    const uint32_t b = A0(c), a1 = A1(c), a2 = A2(c), a3 = A3(c);
    Frame f(c, 0x18);
    Call(c, 0x41bd0, Ld32(b), a1, a2, a3);
}
void fn_33e5c(IopContext& c) {
    const uint32_t b = A0(c), a1 = A1(c), a2 = A2(c), a3 = A3(c);
    Frame f(c, 0x18);
    const uint32_t r = Call(c, 0x4b0a4, Ld32(b), a1, a2, a3);
    St32(Ld32(G::kResultPtr), r);
}
void fn_34044(IopContext& c) {
    const uint32_t b = A0(c);
    Frame f(c, 0x20);
    St32(f.At(0x10), Ld32(b + 0x10));
    St32(f.At(0x14), Ld32(b + 0x14));
    const uint32_t r = Call(c, 0x3e284, Ld32(b), Ld32(b + 4), Ld32(b + 8), Ld32(b + 0xc));
    St32(Ld32(G::kResultPtr), r);
}
void fn_3408c(IopContext& c) {
    const uint32_t a0 = A0(c), a1 = A1(c), a2 = A2(c), a3 = A3(c);
    Frame f(c, 0x18);
    Call(c, 0x3e6f4, a0, a1, a2, a3);
}
void fn_340ac(IopContext& c) {
    const uint32_t a0 = A0(c), a1 = A1(c), a2 = A2(c), a3 = A3(c);
    Frame f(c, 0x18);
    Call(c, 0x3e7d8, a0, a1, a2, a3);
}
void fn_340cc(IopContext& c) {
    const uint32_t b = A0(c);
    Frame f(c, 0x20);
    St32(f.At(0x10), Ld32(b + 0x10));
    Call(c, 0x3e920, Ld32(b), Ld32(b + 4), Ld32(b + 8), Ld32(b + 0xc));
}
void fn_34514(IopContext& c) {
    const uint32_t b = A0(c);
    Frame f(c, 0x18);
    const uint32_t r = Call(c, 0x33250, Ld32(b), Ld32(b + 4), Ld32(b + 8), b + 0xc);
    St32(Ld32(G::kResultPtr), r);
}

// fn_3a8d0(): the tick rate constant (240).
void fn_3a8d0(IopContext& c) { Ret(c, 0xf0); }

}  // namespace ps2r::title::sly2::snd989
