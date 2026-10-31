// snd989_init2.cpp -- the boot-time path, part 2: the pan law (fn_4ea54), the group ramp parameters (fn_4f224), the
// voice table init (fn_4bf1c), the stop / pause of every handle in a voice mask (fn_41a64 / fn_41bd0), the stream
// idle wait + CD stream init (fn_4b0a4), the bank post-load fix-up (fn_3d674), the movie stop (fn_3e6f4) and its
// voice side (fn_3e7d8). Ported from the recompiled 989snd_Library over the module's RAM.
#include <cstdio>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

namespace {
constexpr uint32_t kPanMode = G::kData + 0x2518;          // s16: 1 = mono, 2 = the rear law
constexpr uint32_t kPanTable = G::kData + 0x1eb8;         // 180 x {s16 L, s16 R}
constexpr uint32_t kGroupRamps = 0x59e28;                 // 4 x 24 bytes
constexpr uint32_t kVoiceTable = 0x593a8, kVoiceOwner = 0x58e38;
constexpr uint32_t kStreamRecList = G::kData + 0x2338;
constexpr uint32_t kRefillCurrent = G::kData + 0x234c, kCdBusy = G::kData + 0x2350, kCdSectors = G::kData + 0x230c;
constexpr uint32_t kReaderWaiter = G::kData + 0x235c;
constexpr uint32_t kBankList = G::kData + 0x1994, kBankList2 = G::kData + 0x1998;
constexpr uint32_t kMovieCallback = G::kData + 0x198c, kMovieBuf = G::kData + 0x19b0, kMovieArg = G::kData + 0x19b4;
constexpr uint32_t kMovieCount = G::kData + 0x19b8, kMovieVoice0 = G::kData + 0x19cc, kMovieVoice1 = G::kData + 0x19d0;
constexpr uint32_t kMovieBusy = G::kData + 0x19d4, kMovieMode = G::kData + 0x19d8, kMovieVol = G::kData + 0x2538;
}  // namespace

// fn_4ea54(vol, pan, out): the pan law. Mono (kPanMode == 1): both sides = vol * 258. Else the angle is wrapped to
// [0, 360), rotated by 90, and the table law applied: for angles < 180 the table pair scaled by vol * 258 / 16383
// with the signs of the previous pair kept when both agree, else the louder side's ratio applied; for angles >= 180
// the pair swapped, and (unless kPanMode == 2) one side negated according to the previous pair's signs.
void fn_4ea54(IopContext& c) {
    const uint32_t out = A2(c);
    const int32_t a3 = S32(A0(c)) * 258;
    if (Lds16(kPanMode) == 1) {
        St16(out, U32(a3));
        St16(out + 2, U32(a3));
        Ret(c, 1);
        return;
    }
    int32_t a1 = S32(A1(c));
    if (!(a1 < 0x168)) {
        a1 -= 0x168;
        bool below;
        do {
            below = a1 < 0x168;
            a1 -= 0x168;
        } while (!below);
        a1 += 0x168;
    }
    if (a1 < 0) {
        a1 += 0x168;
        bool neg;
        do {
            neg = a1 < 0;
            a1 += 0x168;
        } while (neg);
        a1 -= 0x168;
    }
    a1 = (a1 < 0x10e) ? a1 + 0x5a : a1 - 0x10e;
    const int32_t oldL = Lds16(out), oldR = Lds16(out + 2);
    const uint32_t rawL = Ld16(out);
    if (a1 < 0xb4) {
        const uint32_t tab = kPanTable + U32(a1) * 4;
        int32_t a0 = Div16383(MulLo(a3, Lds16(tab)));
        int32_t a2 = Div16383(MulLo(a3, Lds16(tab + 2)));
        if ((oldL < 0) == (oldR < 0)) {
            if (oldL < 0) {
                a0 = -a0;
                a2 = -a2;
            }
            St16(out, U32(a0));
            St16(out + 2, U32(a2));
            Ret(c, rawL << 16);
            return;
        }
        const int32_t absL = oldL < 0 ? -oldL : oldL, absR = oldR < 0 ? -oldR : oldR;
        int32_t q;
        if (absR < absL) {
            if (absL == 0) {
                std::printf("[IOP-RECOMP] BREAK at 0x4ebd0\n");
                return;
            }
            q = MipsDiv(oldL, absL);
        } else {
            if (absR == 0) {
                std::printf("[IOP-RECOMP] BREAK at 0x4ec00\n");
                return;
            }
            q = MipsDiv(oldR, absR);
        }
        a0 = MulLo(a0, q);
        a2 = MulLo(a2, q);
        St16(out, U32(a0));
        St16(out + 2, U32(a2));
        Ret(c, U32(q));
        return;
    }
    const uint32_t tab = kPanTable + U32(a1 - 0xb4) * 4;
    int32_t a2 = Div16383(MulLo(a3, Lds16(tab)));
    int32_t a0 = Div16383(MulLo(a3, Lds16(tab + 2)));
    uint32_t v0 = 2;
    if (Lds16(kPanMode) != 2) {
        if (oldL >= 0) {
            if (oldR >= 0) {
                v0 = a2 < a0;
                if (a2 < a0) a2 = -a2; else a0 = -a0;
            } else {
                v0 = rawL << 16;
                a2 = -a2;
            }
        } else {
            v0 = a0 < a2;
            if (oldR >= 0) {
                a0 = -a0;
            } else if (a0 < a2) {
                a2 = -a2;
            } else {
                a0 = -a0;
            }
        }
    }
    St16(out, U32(a0));
    St16(out + 2, U32(a2));
    Ret(c, v0);
}

// fn_4f224(group, params): the ramp parameters of group 0..3: no params -> the record's first word = -1; else the
// six words copied, words 3 and 4 turned into step counts ((0x10000 - params[2]) / ((w * 30) >> 16), at least 1), word
// 5 = 0x10000.
void fn_4f224(IopContext& c) {
    const uint32_t group = A0(c), params = A1(c);
    if (!(group < 4)) {
        Ret(c, 0);
        return;
    }
    const uint32_t base = kGroupRamps + group * 24;
    if (params == 0) {
        St32(base, 0xffffffffu);
        Ret(c, kGroupRamps);
        return;
    }
    for (uint32_t i = 0; i < 0x18; i += 4) St32(base + i, Ld32(params + i));
    const int32_t a2 = S32(0x10000u - Ld32(base + 8));
    for (uint32_t off = 0xc; off <= 0x10; off += 4) {
        int32_t a0 = S32(Ld32(base + off) * 30u) >> 16;
        if (a0 == 0) a0 = 1;
        const int32_t q = MipsDiv(a2, a0);
        St32(base + off, U32(q));
        if (q == 0) St32(base + off, 1);
    }
    St32(base + 0x14, 0x10000);
    Ret(c, base);
}

// fn_4bf1c(): the voice table: 48 voices deactivated (fn_4dec0), numbered, their owner bytes cleared; the free /
// active lists and counters zeroed; the 16 group entries {0, 0x2f}; the voice-table lock semaphore (1/1, signalled).
void fn_4bf1c(IopContext& c) {
    Frame f(c, 0x48);
    St32(G::kData + 0x23a8, 0);
    St32(G::kData + 0x23a4, 0);
    for (uint32_t i = 0; i < 0x30; ++i) {
        Call(c, 0x4dec0, i);
        St32(kVoiceTable + 4 + i * 0x38, i);
        St8(kVoiceOwner + i, 0);
    }
    for (uint32_t a : {0x2554u, 0x2550u, 0x255cu, 0x2558u, 0x2564u, 0x2560u, 0x256cu, 0x2568u, 0x254cu, 0x2548u, 0x2544u,
                       0x23b4u, 0x2540u, 0x21bcu, 0x23f8u})
        St32(G::kData + a, 0);
    for (uint32_t i = 0; i < 16; ++i) {
        St16(G::kData + 0x23b8 + i * 4, 0);
        St16(G::kData + 0x23ba + i * 4, 0x2f);
    }
    St32(f.At(0x10), 1);
    St32(f.At(0x18), 0);
    St32(f.At(0x1c), 1);
    const uint32_t s = Import(c, "thsemap", 4, f.At(0x10));
    St32(G::kVLockSema, s);
    imp::SignalSema(c, s);
}

// fn_41a64(mask): under the lock (0x4b), every playing handle whose voice-group bit is in `mask` and is not paused:
// a stream (type 4) -> fn_44e70(flags), else fn_4198c(h, 1).
void fn_41a64(IopContext& c) {
    const uint32_t mask = A0(c);
    Frame f(c, 0x28);
    Call(c, 0x42f58, 0x4b);
    for (uint32_t h = Ld32(G::kSoundList); h != 0; h = Ld32(h + 0x24)) {
        if (!(mask & (1u << (U32(Lds8(h + 0x17)) & 31)))) continue;
        if (Ld8(h + 0x16) & 2) continue;
        const uint32_t w = Ld32(h);
        if (((w >> 24) & 0x1f) == 4) Call(c, 0x44e70, w);
        else Call(c, 0x4198c, h, 1);
    }
    Call(c, 0x430dc);
}

// fn_41bd0(mask): under the lock (0x4d), every paused handle whose voice-group bit is in `mask`: a stream ->
// fn_450c4(flags), else fn_419f8(h, 1).
void fn_41bd0(IopContext& c) {
    const uint32_t mask = A0(c);
    Frame f(c, 0x28);
    Call(c, 0x42f58, 0x4d);
    for (uint32_t h = Ld32(G::kSoundList); h != 0; h = Ld32(h + 0x24)) {
        if (!(mask & (1u << (U32(Lds8(h + 0x17)) & 31)))) continue;
        if (!(Ld8(h + 0x16) & 2)) continue;
        const uint32_t w = Ld32(h);
        if (((w >> 24) & 0x1f) == 4) Call(c, 0x450c4, w);
        else Call(c, 0x419f8, h, 1);
    }
    Call(c, 0x430dc);
}

// fn_4b0a4(wait): no stream records -> reported (0x4a), 1. While a refill is current, the CD busy or sectors pending:
// without `wait` -> 0; else this thread registers as the reader waiter and sleeps until woken. Then sceCdStInit(0)
// -> 1.
void fn_4b0a4(IopContext& c) {
    const uint32_t wait = A0(c);
    Frame f(c, 0x28);
    if (Ld32(kStreamRecList) == 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x4a, 0, 0, 0);
        Ret(c, 1);
        return;
    }
    while (Ld32(kRefillCurrent) != 0 || Ld32(kCdBusy) != 0 || Ld32(kCdSectors) != 0) {
        if (wait == 0) {
            Ret(c, 0);
            return;
        }
        St32(kReaderWaiter, imp::GetThreadId(c));
        Import(c, "thbase", 24);
        St32(kReaderWaiter, 0);
    }
    Import(c, "cdvdman", 37, 0);
    Ret(c, 1);
}

// fn_3d674(): after a bank load: every bank's sound entries (0x1c bytes each) get their bank back-pointer; entries of
// type 4 / 5 resolve their referenced block (fn_3db50; a miss is reported, 0x5a); the bank's flags |= 2 -- for both
// bank lists.
void fn_3d674(IopContext& c) {
    Frame f(c, 0x30);
    for (uint32_t b = Ld32(kBankList); b != 0; b = Ld32(b + 0x30)) {
        if (Lds16(b + 0x14) > 0) {
            uint32_t off = 0;
            for (int32_t i = 0; i < Lds16(b + 0x14); ++i, off += 0x1c) {
                St32(off + Ld32(b + 0x1c) + 4, b);
                const uint32_t e = off + Ld32(b + 0x1c);
                if (Ld32(e) - 4 < 2) {
                    const uint32_t r = Call(c, 0x3db50, Ld32(e + 0xc));
                    St32(e + 0x18, r);
                    if (r == 0) {
                        St32(f.At(0x10), 0);
                        Call(c, 0x35140, 0x5a, Ld32(e + 0xc), 0, 0);
                    }
                }
            }
        }
        St32(b + 8, Ld32(b + 8) | 2);
    }
    for (uint32_t b = Ld32(kBankList2); b != 0; b = Ld32(b + 0x30)) St32(b + 8, Ld32(b + 8) | 2);
}

// fn_3e7d8(): the movie's voice side stopped: a channel set (!= -1): in the SPU-DMA mode (kMovieMode != 3) the
// channel's voice silenced (volumes 0, key-off bits cleared) under suspended interrupts and SdBlockTrans(ch, 2 =
// stop); else the two voices keyed off (fn_4e02c(core, voice)). The movie volume pair and the busy word cleared.
void fn_3e7d8(IopContext& c) {
    Frame f(c, 0x28);
    if (Ld32(G::kMovieChan) == 0xffffffffu) {
        Ret(c, 0xffffffffu);
        return;
    }
    if (Ld32(kMovieMode) != 3) {
        const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
        const uint32_t ch = Ld16(G::kMovieChan);
        imp::SdSetParam(c, ch | 0xf80, 0);
        imp::SdSetParam(c, ch | 0x1080, 0);
        const uint32_t p = imp::SdGetParam(c, ch | 0x800);
        imp::SdSetParam(c, ch | 0x800, p & 0xff3f);
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
        Import(c, "libsd", 18, U32(Lds16(G::kMovieChan)), 2, 0, 0);
    } else {
        int32_t v = Lds32(kMovieVoice0);
        int32_t q = Div24(v);
        Call(c, 0x4e02c, U32(q), U32(v - q * 24));
        v = Lds32(kMovieVoice1);
        q = Div24(v);
        Call(c, 0x4e02c, U32(q), U32(v - q * 24));
    }
    St16(kMovieVol, 0);
    St16(kMovieVol + 2, 0);
    St32(kMovieBusy, 0);
}

// fn_3e6f4(): the movie stopped: the voice side (fn_3e7d8), a DMA channel 0 / 1 released (fn_3e1cc), the channel =
// -1, the busy word cleared, the loader callback for the buffer (kMovieCallback(buf)), the buffer freed (fn_42924(arg,
// count * 2)), the two voices released (fn_4dfa4) and set to -1, the buffer word cleared.
void fn_3e6f4(IopContext& c) {
    Frame f(c, 0x28);
    Call(c, 0x3e7d8);
    const uint32_t ch = Ld32(G::kMovieChan);
    if (ch < 2) Call(c, 0x3e1cc, ch);
    const uint32_t buf = Ld32(kMovieBuf);
    St32(G::kMovieChan, 0xffffffffu);
    St32(kMovieBusy, 0);
    if (buf != 0) CallAddr(c, Ld32(kMovieCallback), buf, A1(c), A2(c), A3(c));
    const uint32_t arg = Ld32(kMovieArg);
    if (arg != 0) Call(c, 0x42924, arg, Ld32(kMovieCount) << 1);
    if (Ld32(kMovieVoice0) != 0xffffffffu) {
        Call(c, 0x4dfa4, Ld32(kMovieVoice0));
        St32(kMovieVoice0, 0xffffffffu);
    }
    if (Ld32(kMovieVoice1) != 0xffffffffu) {
        Call(c, 0x4dfa4, Ld32(kMovieVoice1));
        St32(kMovieVoice1, 0xffffffffu);
    }
    St32(kMovieBuf, 0);
}

}  // namespace ps2r::title::sly2::snd989
