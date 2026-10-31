// snd989_stream2.cpp -- the stream start (fn_43f30: a new stream handle or chunks appended to one), its voice
// records (fn_43d2c) and handle (fn_4b164), the key-on programming of a stream's voices (fn_4a050), the CD read
// request of the stream reader (fn_46840) and its status readers (fn_493b0 / fn_46e94), the bank loader's read
// (fn_3b080), the module heap allocation (fn_3deec), the timer tree (fn_42c84 unlink / fn_42dd0 find), the SPU
// transfer handler reset (fn_3e110), the record range setter (fn_43d00) and the fno 54 shim (fn_3384c). Ported from
// the recompiled 989snd_Library over the module's RAM.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

namespace {
constexpr uint32_t kStreamHandleTable = G::kData + 0x22e4;   // -> the type-4 records (0x78 each)
constexpr uint32_t kStreamHandleCount = G::kData + 0x22e8;
constexpr uint32_t kStreamRecList = G::kData + 0x2338;
constexpr uint32_t kStreamRecRanges = G::kData + 0x22a4;
constexpr uint32_t kFmtHelperBusyA = G::kData + 0x4f4, kFmtHelperBusyB = G::kData + 0x51c;
constexpr uint32_t kCd = G::kData + 0x22f0;                   // snd989_play.cpp's CD block
constexpr uint32_t kCdReadDepth = G::kData + 0x2394;         // fn_46840 re-entrancy counter
constexpr uint32_t kCdReadLocked = G::kData + 0x238c;        // non-zero: no reads (error 0x13)
constexpr uint32_t kTimerRoot = G::kData + 0x2208, kTimerCount = G::kData + 0x220c;
constexpr uint32_t kTransBusy = G::kData + 0x1940;
// The bank loader (fn_3b080): 0x51958 the file descriptor (-1 = none), 0x5195c cached-sector mode, 0x51964 / 0x51968
// sector / offset, 0x51974 the error, 0x51978 memory mode, 0x5197c the memory position.
constexpr uint32_t kLoadFd = G::kData + 0x1958, kLoadFromCd = G::kData + 0x195c, kLoadSector = G::kData + 0x1964;
constexpr uint32_t kLoadOffset = G::kData + 0x1968, kLoadError = G::kData + 0x1974, kLoadFromMem = G::kData + 0x1978;
constexpr uint32_t kLoadMemPos = G::kData + 0x197c;
constexpr uint32_t kHeapReserve = G::kData + 0x1954;         // fn_3deec: reserve 6 MiB of a large free heap
constexpr uint32_t kFmtHeapFree = G::kData - 0xd8, kFmtHeapReserve = G::kData - 0xa4, kFmtHeapLeft = G::kData - 0x84;
constexpr uint32_t kFmtHeapNone = G::kData - 0x64;
}  // namespace

// fn_43d00(kind, first, last): the stream voice record range of `kind` (< 16).
void fn_43d00(IopContext& c) {
    const int32_t kind = S32(A0(c));
    if (!(kind < 0x10)) {
        Ret(c, 0);   // the original's leftover $v0 (spiop calls this export: snd989:121)
        return;
    }
    const uint32_t r = kStreamRecRanges + U32(kind) * 4;
    St16(r, A1(c));
    St16(r + 2, A2(c));
    Ret(c, r);
}

// fn_43d2c(*rec0, *rec1, two, with_voices, kind): one or two stream voice records of `kind` (fn_4a8a8), each with a
// voice (fn_4c750 group `kind`, priority 0x7f, kept; state 4) when `with_voices`, else voice -1. A failure frees what
// was taken (record flags = 1, the first voice deactivated) and returns 0; 1 on success.
void fn_43d2c(IopContext& c) {
    const uint32_t p0 = A0(c), p1 = A1(c), two = A2(c), with_voices = A3(c);
    const uint32_t kind = StackArg(c, 0);
    Frame f(c, 0x30);
    const uint32_t r0 = Call(c, 0x4a8a8, kind);
    St32(p0, r0);
    if (r0 == 0) {
        Ret(c, 0);
        return;
    }
    St32(r0 + 0xc8, 0);
    if (two != 0) {
        const uint32_t r1 = Call(c, 0x4a8a8, kind);
        St32(p1, r1);
        if (r1 == 0) {
            St32(Ld32(p0), 1);
            Ret(c, 0);
            return;
        }
        St32(r1 + 0xc8, 0);
    }
    if (with_voices == 0) {
        St32(Ld32(p0) + 0x7c, 0xffffffffu);
        if (two != 0) St32(Ld32(p1) + 0x7c, 0xffffffffu);
        Ret(c, 1);
        return;
    }
    Call(c, 0x4c1ac, 1, 0x5a);
    const int32_t v0 = S32(Call(c, 0x4c750, kind, 0x7f, 1));
    St32(Ld32(p0) + 0x7c, U32(v0));
    if (v0 == -1) {
        St32(Ld32(p0), 1);
        if (two != 0) St32(Ld32(p1), 1);
        Call(c, 0x4c2f8);
        Ret(c, 0);
        return;
    }
    St32(G::Voice(v0) + 8, 4);
    if (two != 0) {
        const int32_t v1 = S32(Call(c, 0x4c750, kind, 0x7f, 1));
        St32(Ld32(p1) + 0x7c, U32(v1));
        if (v1 == -1) {
            St32(Ld32(p0), 1);
            St32(Ld32(p1), 1);
            Call(c, 0x4dec0, Ld32(Ld32(p0) + 0x7c));
            Call(c, 0x4c2f8);
            Ret(c, 0);
            return;
        }
        St32(G::Voice(v1) + 8, 4);
    }
    Call(c, 0x4c2f8);
    Ret(c, 1);
}

// fn_4b164(): a free stream handle record (type 4) from the 0x78-stride table, as fn_4111c does.
void fn_4b164(IopContext& c) {
    Frame f(c, 0x28);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    const int32_t count = Lds32(kStreamHandleCount);
    uint32_t off = 0;
    for (int32_t i = 0; i < count; ++i, off += 0x78) {
        const uint32_t rec = Ld32(kStreamHandleTable) + off;
        if (Ld32(rec + 4) != 0) continue;
        const uint32_t id = Ld16(rec);
        St32(rec + 0x20, 0);
        St32(rec + 0x24, 0);
        St32(rec + 0x28, 0);
        St32(rec + 0x2c, 0);
        St32(rec + 0x30, 0);
        St8(rec + 0x16, 0);
        St32(rec, ((id + 1) & 0xffff) | (U32(i) << 16) | 0x04000000u);
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
        Ret(c, Ld32(kStreamHandleTable) + off);
        return;
    }
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
    Ret(c, 0);
}

// fn_43f30(src0, src1, arg0, arg1, volume, pan, kind, h, chunk_id, flags): start a stream or append to one.
// With a handle `h` (flags & 0x10: fail when unknown, else retry as a new stream): the chunk node(s) (fn_4ad0c; two
// for a stereo handle with src1) carry the sources and args, the chunk id, the volume (volume * 127 / 1024) and pan
// under flags & 0x40 (else -1), flags 2 / 4, and are queued (fn_4ac90); returns 1. Else a new handle: one or two
// voice records with voices (fn_43d2c), a handle record (fn_4b164; none: error 0x30), its parameter words, the
// voice mask, a helper voice (flags & 0x20, mono; fn_4c750 kept, state 4), the per-chunk volume/pan tables, the
// records set up (fn_44a58 with the source flags 0x1f800) and flagged (1 -> 0x10, 2 -> 0x100, 4 -> 0x400, chunk id
// at st[0xc5]), the first record queued to start, the stream semaphore signalled, the handle linked (fn_413a8).
// Returns the handle id (0 on failure). Under the lock.
void fn_43f30(IopContext& c) {
    const uint32_t src0 = A0(c), src1 = A1(c), arg0 = A2(c), arg1 = A3(c);
    int32_t vol = S32(StackArg(c, 0));
    int32_t pan = S32(StackArg(c, 1));
    const uint32_t kind = StackArg(c, 2);
    uint32_t h = StackArg(c, 3);
    const uint32_t chunk_id = StackArg(c, 4), flags = StackArg(c, 5);
    Frame f(c, 0x48);
    const uint32_t st0_slot = f.At(0x18), st1_slot = f.At(0x1c);
    for (;;) {
        St32(st0_slot, 0);
        St32(st1_slot, 0);
        if (Ld32(kStreamRecList) == 0) {
            St32(f.At(0x10), 0);
            Call(c, 0x35140, 0x2f, 0, 0, 0);
            Ret(c, 0);
            return;
        }
        Call(c, 0x42f58, 0x50);
        if (h == 0) break;
        const uint32_t rec = Call(c, 0x417f8, h);
        if (rec == 0) {
            Call(c, 0x430dc);
            if (flags & 0x10) {
                Ret(c, 0);
                return;
            }
            h = 0;
            continue;
        }
        if (flags & 8) St32(rec + 0x74, 0);
        if (U32(pan + 2) < 2) pan = Lds16(rec + 0x12);
        if (vol == 0x7fffffff) vol = Div127(S32(U32(Lds16(rec + 0x10)) << 10));
        const uint32_t first = Ld32(rec + 4);
        St32(st0_slot, first);
        auto fill = [&](uint32_t n, uint32_t src, uint32_t arg) {
            St32(n + 4, src);
            St32(n + 8, arg);
            St8(n + 0xd, chunk_id);
        };
        auto volpan = [&](uint32_t n) {
            if (flags & 0x40) {
                St8(n + 0xc, U32((S32(U32(vol) << 7) - vol) >> 10));
                St16(n + 0xe, U32(pan));
            } else {
                St8(n + 0xc, 0xff);
                St16(n + 0xe, 0xffff);
            }
        };
        auto bits = [&](uint32_t n) {
            if (flags & 2) St32(n, Ld32(n) | 2);
            if (flags & 4) St32(n, Ld32(n) | 4);
        };
        if (Ld32(first + 0xc8) != 0 && src1 != 0) {
            const uint32_t n0 = Call(c, 0x4ad0c);
            const uint32_t n1 = Call(c, 0x4ad0c);
            if (n0 == 0 || n1 == 0) {
                if (n0 != 0 && n1 == 0) St32(n0, 0);
                if (n1 != 0) St32(n1, 0);
                Call(c, 0x430dc);
                Ret(c, 0);
                return;
            }
            fill(n0, src0, arg0);
            fill(n1, src1, arg1);
            if (flags & 0x40) {
                const uint32_t v = U32((S32(U32(vol) << 7) - vol) >> 10);
                St8(rec + 0x16, Ld8(rec + 0x16) | 0x10);
                St8(n1 + 0xc, v);
                St8(n0 + 0xc, v);
                St16(n1 + 0xe, U32(pan));
                St16(n0 + 0xe, U32(pan));
            } else {
                St8(n1 + 0xc, 0xff);
                St8(n0 + 0xc, 0xff);
                St16(n1 + 0xe, 0xffff);
                St16(n0 + 0xe, 0xffff);
            }
            if (flags & 2) {
                St32(n0, Ld32(n0) | 2);
                St32(n1, Ld32(n1) | 2);
            }
            if (flags & 4) {
                St32(n0, Ld32(n0) | 4);
                St32(n1, Ld32(n1) | 4);
            }
            Call(c, 0x4ac90, rec, n0);
            Call(c, 0x4ac90, rec, n1);
        } else {
            const uint32_t n0 = Call(c, 0x4ad0c);
            if (n0 == 0) {
                Call(c, 0x430dc);
                Ret(c, 0);
                return;
            }
            fill(n0, src0, arg0);
            if (flags & 0x40) St8(rec + 0x16, Ld8(rec + 0x16) | 0x10);
            volpan(n0);
            bits(n0);
            Call(c, 0x4ac90, rec, n0);
        }
        Call(c, 0x430dc);
        Ret(c, 1);
        return;
    }
    // A new stream handle.
    St32(f.At(0x10), kind);
    if (Call(c, 0x43d2c, st0_slot, st1_slot, src1 != 0 ? 1u : 0u, 1) == 0) {
        Call(c, 0x430dc);
        Ret(c, 0);
        return;
    }
    const uint32_t rec = Call(c, 0x4b164);
    if (rec == 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x30, 0, 0, 0);
        Call(c, 0x430dc);
        Ret(c, 0);
        return;
    }
    St16(rec + 0x10, U32((S32(U32(vol) << 7) - vol) >> 10));
    St16(rec + 0xc, 0x7f);
    if (U32(pan + 2) < 2) pan = 0;
    const uint32_t fl_play = flags & 1;
    St16(rec + 0x12, U32(pan));
    St16(rec + 0xe, U32(pan));
    St16(rec + 0x14, 0);
    St8(rec + 0x16, 0);
    if (fl_play != 0) St8(rec + 0x16, 2);
    St8(rec + 0x17, kind);
    St32(rec + 0x18, 0);
    St32(rec + 0x1c, 0);
    const uint32_t mask = rec + 0x18;
    auto set_mask = [&](int32_t v) {
        const int32_t core = Div24(v), bit = v - core * 24;
        St32(mask + U32(core) * 4, Ld32(mask + U32(core) * 4) | (1u << bit));
    };
    set_mask(Lds32(Ld32(st0_slot) + 0x7c));
    if ((flags & 0x20) && src1 == 0) {
        if (Lds32(rec + 0x38) != -1) imp::Printf(c, kFmtHelperBusyA, mask);
        Call(c, 0x4c1ac, 1, 0x5b);
        const int32_t hv = S32(Call(c, 0x4c750, kind, 0x7f, 1));
        St32(rec + 0x38, U32(hv));
        if (hv != -1) {
            St32(G::Voice(hv) + 8, 4);
            St32(G::Voice(Lds32(rec + 0x38)) + 0xc, rec);
            set_mask(Lds32(rec + 0x38));
        }
        Call(c, 0x4c2f8);
    } else if (Lds32(rec + 0x38) != -1) {
        imp::Printf(c, kFmtHelperBusyB, mask);
    }
    St32(rec + 0x34, 1);
    St32(rec + 0x70, 0);
    St32(rec + 0x6c, 0);
    St32(rec + 0x74, (flags & 8) ? 0u : 1u);
    const uint32_t fl_vol = flags & 0x40, fl_2 = flags & 2, fl_4 = flags & 4;
    for (uint32_t i = 0; i < 0x10; ++i) {
        St8(rec + 0x3c + i, 0x7f);
        St16(rec + 0x4c + i * 2, 0);
    }
    if (src1 != 0) {
        set_mask(Lds32(Ld32(st1_slot) + 0x7c));
        St32(rec + 0x34, Ld32(rec + 0x34) + 1);
    }
    const uint32_t src_flags = flags & 0x1f800;
    auto setup = [&](uint32_t slot, uint32_t src, uint32_t arg) {
        St32(f.At(0x10), 0);
        St32(f.At(0x14), src_flags);
        Call(c, 0x44a58, rec, Ld32(slot), src, arg);
        const uint32_t st = Ld32(slot);
        if (fl_play != 0) St32(st, Ld32(st) | 0x10);
        if (fl_2 != 0) St32(st, Ld32(st) | 0x100);
        if (fl_4 != 0) St32(st, Ld32(st) | 0x400);
        St8(Ld32(slot) + 0xc5, chunk_id);
    };
    setup(st0_slot, src0, arg0);
    if (src1 != 0) setup(st1_slot, src1, arg1);
    if (fl_vol != 0) St8(rec + 0x16, Ld8(rec + 0x16) | 0x10);
    Call(c, 0x4aa70, Ld32(st0_slot) + 0x10);
    imp::SignalSema(c, Ld32(G::kStreamSema));
    Call(c, 0x413a8, rec);
    Call(c, 0x430dc);
    Ret(c, Ld32(rec));
}

// fn_4a050(st, all): program a stream handle's voice record(s) for key-on (`all`: the whole chain from `st`): the
// handle's volume/pan taken from the first record's chunk words (h[0x16] & 0x10), the voice's group/owner, the
// helper voice (h[0x38], first record only: pan word 0x10e), the law (fn_4e628), the pending loop-address flag
// (0x200) when the buffer is larger than the data, then for the helper and the voice: VOLL/VOLR (zero when the
// record was already playing), the 0x1800 / 0x1a00 switches, PITCH (fn_4be14 of the handle's rate; 0 when
// playing), LSAX = 0x5000, SSA = the buffer + st[0x8c] (+ 0x30 for the voice unless a pair), ADSR1/2, the key-on
// pending bit and the effects-send mask word; st flags |= 4. A record without a voice (-1) stops the walk (st
// flags |= 0x80). Interrupts suspended throughout.
void fn_4a050(IopContext& c) {
    uint32_t st = A0(c);
    const uint32_t all = A1(c);
    Frame f(c, 0x60);
    bool first = true;
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x28));
    auto done = [&]() {
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x28)));
    };
    while (st != 0) {
        int32_t v = Lds32(st + 0x7c);
        if (v == -1) {
            St32(st, Ld32(st) | 0x80);
            done();
            return;
        }
        int32_t helper = -1;
        if (first) {
            uint32_t h = Ld32(st + 0xd0);
            if (Ld8(h + 0x16) & 0x10) {
                St16(h + 0x10, U32(Lds8(st + 0xc4)));
                h = Ld32(st + 0xd0);
                St16(h + 0x12, Ld16(st + 0xc6));
            }
            first = false;
            h = Ld32(st + 0xd0);
            v = Lds32(st + 0x7c);
            helper = Lds32(h + 0x38);
        }
        const uint32_t voice = G::Voice(v);
        const uint32_t stf = Ld32(st);
        St16(voice + 0x22, 0);
        St16(voice + 0x20, 0);
        St32(G::kStreamVoiceCount, Ld32(G::kStreamVoiceCount) + 1);
        uint32_t h = Ld32(st + 0xd0);
        St8(voice + 0x1b, Ld8(h + 0x17));
        St32(voice + 0xc, Ld32(st + 0xd0));
        const int32_t core = Div24(v), bit = v - core * 24;
        const uint32_t playing = stf & 0x10;
        if (helper != -1) St16(st + 6, 0x10e);
        h = Ld32(st + 0xd0);
        St32(f.At(0x10), U32(Lds16(st + 6)));
        St32(f.At(0x14), 0x7f);
        St32(f.At(0x18), U32(Lds16(h + 0x4c + U32(Lds8(st + 5)) * 2)));
        St32(f.At(0x20), 4);
        St32(f.At(0x1c), voice + 0x20);
        Call(c, 0x4e628, U32(Lds8(h + 0x3c + U32(Lds8(st + 5)))), U32(Lds16(h + 0x10)), U32(Lds16(h + 0x12)), U32(Lds8(st + 4)));
        if (!(Ld32(st + 0x10) & 0x800) && (Ld32(st + 0xc) >> 1) < Ld32(st + 0xa8)) St32(st, Ld32(st) | 0x200);
        int32_t hcore = 0, hbit = 0;
        if (helper != -1) {
            const uint32_t hv = G::Voice(helper);
            St16(hv + 0x22, Ld16(voice + 0x20));
            St16(hv + 0x20, Ld16(voice + 0x22));
            St8(hv + 0x1b, Ld8(voice + 0x1b));
            St32(hv + 0xc, Ld32(st + 0xd0));
            hcore = Div24(helper);
            hbit = helper - hcore * 24;
            const uint32_t he = U32(hcore) | (U32(hbit) << 1);
            uint32_t a1 = 0;
            if (playing == 0) a1 = (Call(c, 0x4ed08, U32(Lds16(hv + 0x20)), U32(Lds8(hv + 0x1b))) & 0xffff) >> 1;
            imp::SdSetParam(c, he & 0xffff, a1);
            a1 = 0;
            if (playing == 0) a1 = (Call(c, 0x4ed08, U32(Lds16(hv + 0x22)), U32(Lds8(hv + 0x1b))) & 0xffff) >> 1;
            imp::SdSetParam(c, (he | 0x100) & 0xffff, a1);
            const uint32_t hm = 1u << hbit;
            uint32_t sw = (U32(hcore) | 0x1800) & 0xffff;
            imp::SdSetSwitch(c, sw, imp::SdGetSwitch(c, sw) | hm);
            sw = (U32(hcore) | 0x1a00) & 0xffff;
            imp::SdSetSwitch(c, sw, imp::SdGetSwitch(c, sw) | hm);
            uint32_t pitch = 0;
            if (playing == 0) pitch = Call(c, 0x4be14, U32(Lds16(Ld32(st + 0xd0) + 0x14)), Ld32(st + 8));
            imp::SdSetParam(c, (he | 0x200) & 0xffff, pitch);
            imp::SdSetAddr(c, (he | 0x2140) & 0xffff, 0x5000);
            const uint32_t ssa = Ld32(st + 0x18) + Ld32(st + 0x8c) + 0x30;
            St32(st + 0xbc, ssa);
            imp::SdSetAddr(c, (he | 0x2040) & 0xffff, ssa);
            imp::SdSetParam(c, (he | 0x300) & 0xffff, 0x80f);
            imp::SdSetParam(c, (he | 0x400) & 0xffff, 0x1fc0);
        }
        const uint32_t e = U32(core) | (U32(bit) << 1);
        uint32_t a1 = 0;
        if (playing == 0) a1 = (Call(c, 0x4ed08, U32(Lds16(voice + 0x20)), U32(Lds8(voice + 0x1b))) & 0xffff) >> 1;
        imp::SdSetParam(c, e & 0xffff, a1);
        a1 = 0;
        if (playing == 0) a1 = (Call(c, 0x4ed08, U32(Lds16(voice + 0x22)), U32(Lds8(voice + 0x1b))) & 0xffff) >> 1;
        imp::SdSetParam(c, (e | 0x100) & 0xffff, a1);
        const uint32_t m = 1u << bit;
        uint32_t sw = (U32(core) | 0x1800) & 0xffff;
        imp::SdSetSwitch(c, sw, imp::SdGetSwitch(c, sw) | m);
        sw = (U32(core) | 0x1a00) & 0xffff;
        imp::SdSetSwitch(c, sw, imp::SdGetSwitch(c, sw) | m);
        uint32_t pitch = 0;
        if (playing == 0) pitch = Call(c, 0x4be14, U32(Lds16(Ld32(st + 0xd0) + 0x14)), Ld32(st + 8));
        imp::SdSetParam(c, (e | 0x200) & 0xffff, pitch);
        imp::SdSetAddr(c, (e | 0x2140) & 0xffff, 0x5000);
        const uint32_t base = Ld32(st + 0x18) + Ld32(st + 0x8c);
        St32(st + 0xbc, base);
        if (!(Ld32(st + 0x10) & 0x100)) St32(st + 0xbc, base + 0x30);
        imp::SdSetAddr(c, (e | 0x2040) & 0xffff, Ld32(st + 0xbc));
        imp::SdSetParam(c, (e | 0x300) & 0xffff, 0x80f);
        imp::SdSetParam(c, (e | 0x400) & 0xffff, 0x1fc0);
        const uint32_t core4 = U32(core) * 4;
        St32(st, Ld32(st) | 4);
        St32(G::kKonPending + core4, Ld32(G::kKonPending + core4) | m);
        if (Ld32(st) & 0x100) St32(G::kVmix + core4, Ld32(G::kVmix + core4) | m);
        else St32(G::kVmix + core4, Ld32(G::kVmix + core4) & ~m);
        if (helper != -1) {
            const uint32_t hcore4 = U32(hcore) * 4, hm = 1u << hbit;
            St32(G::kKonPending + hcore4, Ld32(G::kKonPending + hcore4) | hm);
            if (Ld32(st) & 0x100) St32(G::kVmix + hcore4, Ld32(G::kVmix + hcore4) | hm);
            else St32(G::kVmix + hcore4, Ld32(G::kVmix + hcore4) & ~hm);
        }
        st = (all != 0) ? Ld32(st + 0xc8) : 0;
    }
    done();
}

// fn_46840(sector, sectors, buf): the stream reader's CD read request: nested calls refused (0); kCdReadLocked ->
// error 0x13 (0); no record list -> a direct sceCdRead(sector, sectors, buf, {0x64, kCd[0], 0}); a read in progress
// (fn_46d8c(0x11) == 1) -> error 0x13 (0); else the request words are set under suspended interrupts and the
// stream semaphore signalled; 1.
void fn_46840(IopContext& c) {
    const uint32_t sector = A0(c), sectors = A1(c), buf = A2(c);
    Frame f(c, 0x30);
    const uint32_t depth = Ld32(kCdReadDepth);
    St32(kCdReadDepth, depth + 1);
    if (!(S32(depth + 1) < 2)) {
        St32(kCdReadDepth, depth);
        Ret(c, 0);
        return;
    }
    if (Ld32(kCdReadLocked) != 0) {
        St32(kCd + 0x30, 0x13);
        St32(kCdReadDepth, depth);
        Ret(c, 0);
        return;
    }
    if (Ld32(kStreamRecList) == 0) {
        St8(f.At(0x10), 0x64);
        St8(f.At(0x12), 0);
        St8(f.At(0x11), Ld8(kCd));
        St32(kCdReadDepth, depth);
        Ret(c, Import(c, "cdvdman", 6, sector, sectors, buf, f.At(0x10)));
        return;
    }
    if (Call(c, 0x46d8c, 0x11) == 1) {
        St32(kCd + 0x30, 0x13);
        St32(kCdReadDepth, Ld32(kCdReadDepth) - 1);
        Ret(c, 0);
        return;
    }
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x14));
    St32(kCd + 0x34, 0xffffffffu);
    St32(kCd + 0x18, buf);
    St32(kCd + 0x24, sector);
    St32(kCd + 0x38, 0);
    St32(kCd + 0x1c, sectors);
    St32(kCd + 0x28, 0);
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x14)));
    imp::SignalSema(c, Ld32(G::kStreamSema));
    St32(kCdReadDepth, Ld32(kCdReadDepth) - 1);
    Ret(c, 1);
}

// fn_493b0(): the stream reader's CD read completed badly (kCd + 0x28 set -> error 1): the reader released, its
// waiter woken, the loader status pushed in big-buffer mode.
void fn_493b0(IopContext& c) {
    Frame f(c, 0x18);
    if (Ld32(kCd + 0x28) != 0) St32(kCd + 0x30, 1);
    St32(kCd + 0x28, 0);
    const int32_t waiter = Lds32(kCd + 0x34);
    St32(kCd + 0x1c, 0);
    if (waiter != -1) {
        St32(kCd + 0x34, 0xffffffffu);
        Import(c, "thbase", 25, U32(waiter));
    }
    if (Ld32(kCd + 0x38) != 0) Call(c, 0x3dcb8, 0, Ld32(kCd + 0x30));
}

// fn_46e94(): the reader's last error (with a record list), else sceCdGetError().
void fn_46e94(IopContext& c) {
    Frame f(c, 0x18);
    if (Ld32(kStreamRecList) != 0) {
        Ret(c, Ld32(kCd + 0x30));
        return;
    }
    Ret(c, Import(c, "cdvdman", 8));
}

// fn_42c84(node): unlink a timer node from the tree (its later child re-inserted), count down, fields cleared.
void fn_42c84(IopContext& c) {
    const uint32_t node = A0(c);
    if (node == 0) return;
    Frame f(c, 0x18);
    if (node == Ld32(kTimerRoot)) {
        St32(kTimerRoot, Ld32(node + 0x14));
    } else {
        const uint32_t parent = Ld32(node + 0xc);
        if (Ld32(parent + 0x10) == node) St32(parent + 0x10, Ld32(node + 0x14));
        else St32(parent + 0x14, Ld32(node + 0x14));
    }
    const uint32_t later = Ld32(node + 0x14);
    if (later != 0) St32(later + 0xc, Ld32(node + 0xc));
    Call(c, 0x42d3c, Ld32(node + 0x10));
    const uint32_t n = Ld32(kTimerCount);
    St32(node + 8, 0);
    St32(node + 0x10, 0);
    St32(node + 0x14, 0);
    St32(kTimerCount, n - 1);
}

// fn_42dd0(node, tick): the first node of the subtree whose due tick is >= `tick` (walking later children), or 0.
void fn_42dd0(IopContext& c) {
    uint32_t node = A0(c);
    const uint32_t tick = A1(c);
    while (node != 0) {
        if (!(Ld32(node + 4) < tick)) break;
        node = Ld32(node + 0x14);
    }
    Ret(c, node);
}

// fn_3e110(): take the SPU transfer channel: channel 0 when free (kTransBusy[0] = 1, its interrupt handler cleared:
// libsd 26 (0, 0, 0)) -> 0; else channel 1 likewise -> 1; both busy -> -1. Interrupts suspended.
void fn_3e110(IopContext& c) {
    Frame f(c, 0x20);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    if (Ld32(kTransBusy) == 0) {
        St32(kTransBusy, 1);
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
        Import(c, "libsd", 26, 0, 0, 0);
        Ret(c, 0);
        return;
    }
    if (Ld32(kTransBusy + 4) == 0) {
        St32(kTransBusy + 4, 1);
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
        Import(c, "libsd", 26, 1, 0, 0);
        Ret(c, 1);
        return;
    }
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
    Ret(c, 0xffffffffu);
}

// fn_3b080(buf, size): the bank loader's read of `size` bytes: no file (-1) -> 0; cached-sector mode -> fn_3c308(sector,
// offset, size, buf) and the sector/offset advance; memory mode -> fn_3af2c(pos, buf, size) (short: error 0x101, 0);
// else ioman read(fd, buf, size) (short: error 0x101). Returns the bytes read.
void fn_3b080(IopContext& c) {
    const uint32_t buf = A0(c), size = A1(c);
    Frame f(c, 0x20);
    const uint32_t fd = Ld32(kLoadFd);
    if (fd == 0xffffffffu) {
        Ret(c, 0);
        return;
    }
    if (Ld32(kLoadFromCd) != 0) {
        if (Call(c, 0x3c308, Ld32(kLoadSector), Ld32(kLoadOffset), size, buf) == 0) {
            Ret(c, 0);
            return;
        }
        const int32_t off = Lds32(kLoadOffset) + S32(size);
        St32(kLoadOffset, U32(off));
        if (!(off < 0x800)) {
            const int32_t sectors = (off < 0 ? off + 0x7ff : off) >> 11;
            St32(kLoadOffset, U32(off - S32(U32(sectors) << 11)));
            St32(kLoadSector, Ld32(kLoadSector) + U32(sectors));
        }
        Ret(c, size);
        return;
    }
    if (Ld32(kLoadFromMem) != 0) {
        if (Call(c, 0x3af2c, Ld32(kLoadMemPos), buf, size) != size) {
            St32(kLoadError, 0x101);
            Ret(c, 0);
            return;
        }
        St32(kLoadMemPos, Ld32(kLoadMemPos) + size);
        Ret(c, size);
        return;
    }
    const uint32_t r = Import(c, "ioman", 6, fd, buf, size);
    if (r != size) St32(kLoadError, 0x101);
    Ret(c, r);
}

// fn_3deec(size, kind, *out_size): the module's heap allocation: kind 8 sizes itself from the free heap
// (QueryMaxFreeMemSize; above 2 MiB with the reserve word set: 6 MiB kept back, prints unless quiet; none left ->
// 0x2800), rounded to 16, capped by `size`; 0 -> nothing. AllocSysMemory(mode, size, 0) with mode 1 for kinds 1..5
// and 7, else 0; *out_size = the size on success. Interrupts suspended. Returns the block.
void fn_3deec(IopContext& c) {
    int32_t size = S32(A0(c));
    const int32_t kind = S32(A1(c));
    const uint32_t out = A2(c);
    Frame f(c, 0x38);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    if (kind == 8) {
        int32_t have = S32(Import(c, "sysmem", 7));
        if (Ld32(kHeapReserve) != 0 && 0x200000 < have) {
            if (Ld32(G::kQuiet) == 0) imp::Printf(c, kFmtHeapFree, U32(have));
            have = S32(U32(have) + 0xffa00000u);
            if (have < 0) have = 0;
            if (Ld32(G::kQuiet) == 0) {
                imp::Printf(c, kFmtHeapReserve, 0x600000);
                imp::Printf(c, kFmtHeapLeft, U32(have));
            }
            if (have == 0) {
                if (Ld32(G::kQuiet) == 0) imp::Printf(c, kFmtHeapNone);
                have = 0x2800;
            }
        }
        if (have & 0xf) have = S32(U32((have < 0 ? have + 0xf : have) >> 4) << 4);
        if (have < size) size = have;
        if (size == 0) {
            if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
            Ret(c, 0);
            return;
        }
    }
    const uint32_t mode = (kind == 7 || (kind > 0 && kind < 6)) ? 1u : 0u;
    const uint32_t p = Import(c, "sysmem", 4, mode, U32(size), 0);
    if (p != 0 && out != 0) St32(out, U32(size));
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
    Ret(c, p);
}

// fn_3384c(buff): the fno 54 shim: fn_41c9c(buff ..) (no reply word).
void fn_3384c(IopContext& c) {
    const uint32_t a0 = A0(c), a1 = A1(c), a2 = A2(c), a3 = A3(c);
    Frame f(c, 0x18);
    Call(c, 0x41c9c, a0, a1, a2, a3);
}

}  // namespace ps2r::title::sly2::snd989
