// snd989_play.cpp -- the play request (fn_33770 -> fn_3f820 -> fn_37000 for a sequence bank 'SBlk'), the handle
// pool (fn_4111c alloc, fn_41334 the sequence form, fn_413a8 link into the sound list, fn_4144c release), fn_464e0
// (is the handle started), the timer tree insert (fn_42d3c), the stream voice record setup (fn_44a58 / fn_4a8a8 /
// fn_4b24c), the group volume (fn_4e100, fn_3eda0 the movie group), the key-off request (fn_4e02c), and the CD side
// (fn_46ed0 the stream CD read, fn_46d8c the CD wait, fn_3dcb8 the loader status push). Ported from the recompiled
// 989snd_Library over the module's RAM.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

#include <cstdio>
#include <thread>

namespace ps2r::title::sly2::snd989 {

namespace {
constexpr uint32_t kSoundListTail = G::kData + 0x21ec;
constexpr uint32_t kFmtSoundListTailSelf = G::kData + 0xc;
constexpr uint32_t kFmtSoundListPrev = G::kData + 0x38, kFmtSoundListNext = G::kData + 0x4c;
constexpr uint32_t kFmtNoBank = G::kData - 0x44, kFmtNoSound = G::kData - 0x1c;
constexpr uint32_t kFmtCdReadFailed = G::kData + 0x5bc;
constexpr uint32_t kVerbose = G::kData + 0x1910;
constexpr uint32_t kGroupHook = G::kData + 0x618;          // -> {.., fn(group, vol)} notified on a group volume change
constexpr uint32_t kTimerRoot = G::kData + 0x2208, kTimerCount = G::kData + 0x220c;
constexpr uint32_t kStreamRecList = G::kData + 0x2338;     // the stream voice records (rec[0xcc] = next)
constexpr uint32_t kStreamRecRanges = G::kData + 0x22a4;   // per kind: {s16 first, s16 last} record index
constexpr uint32_t kSeqHandleTable = G::kData + 0x2fb8;    // 64 x 0x124 (type 5)
// The movie (type 3) voices: 0x519ac the SPU DMA channel, 0x519c0 / 0x519c4 the level words, 0x519c8 its group,
// 0x519cc / 0x519d0 its voices, 0x519d4 active, 0x519d8 the mode (3 = voices), 0x519dc the channel count, 0x52538 the
// L/R words fn_4ea54 fills.
constexpr uint32_t kMovieLevelA = G::kData + 0x19c0, kMovieLevelB = G::kData + 0x19c4, kMovieGroup = G::kData + 0x19c8;
constexpr uint32_t kMovieVoice1 = G::kData + 0x19d0, kMovieActive = G::kData + 0x19d4, kMovieMode = G::kData + 0x19d8;
constexpr uint32_t kMovieChannels = G::kData + 0x19dc, kMovieVol = G::kData + 0x2538;
// The stream CD read (fn_46ed0 / fn_46d8c): 0x522f0 the read mode byte, 0x522f4 bytes requested, 0x522f8, 0x522fc /
// 0x52300 first / last sector, 0x52304 the big buffer, 0x52308 the read buffer, 0x5230c max sectors (0 = idle),
// 0x52310 sectors, 0x52314 the sector, 0x52320 the error, 0x52324 the waiting thread, 0x52328 the big-buffer mode,
// 0x5232c.. the last read's {lsn, sectors, buf}, 0x52338 the record list, 0x52350 busy, 0x527b8 the small buffer.
constexpr uint32_t kCd = G::kData + 0x22f0;
constexpr uint32_t kCdSmallBuffer = G::kData + 0x27b8;
// fn_3dcb8: three 16-byte status records at 0x52768 / 0x52778 DMA'd to the EE address 0x51990 (+0x10).
constexpr uint32_t kStatusRecA = G::kData + 0x2768, kStatusRecB = G::kData + 0x2778, kStatusEeAddr = G::kData + 0x1990;
constexpr uint32_t kPositionDmaSema = G::kData + 0x62c;
constexpr uint32_t kPositionDmaCallback = 0x351c8;
}  // namespace

// fn_4111c(table, count, type, stride): the first free handle record (rec[4] == 0) of a pool: its link / list words
// and flags cleared, rec[0] = ((rec[0] + 1) & 0xffff) | index << 16 | type << 24 (the handle id). Interrupts
// suspended. Returns the record or 0.
void fn_4111c(IopContext& c) {
    uint32_t rec = A0(c);
    const int32_t count = S32(A1(c));
    const uint32_t type = A2(c), stride = A3(c);
    Frame f(c, 0x30);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    for (int32_t i = 0; i < count; ++i, rec += stride) {
        if (Ld32(rec + 4) != 0) continue;
        const uint32_t id = Ld16(rec);
        St32(rec + 0x24, 0);
        St32(rec + 0x20, 0);
        St32(rec + 0x28, 0);
        St32(rec + 0x2c, 0);
        St32(rec + 0x30, 0);
        St8(rec + 0x16, 0);
        St32(rec, ((id + 1) & 0xffff) | (U32(i) << 16) | (type << 24));
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
        Ret(c, rec);
        return;
    }
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
    Ret(c, 0);
}

// fn_41334(def, ..): a sequence handle for definition `def`: a definition flagged 8 asks fn_411f8 (a handle to
// supersede -> fn_41da0(h, 1, 0, 1)); refused -> 0. Then a record from the 64 x 0x124 pool (type 5).
void fn_41334(IopContext& c) {
    const uint32_t def = A0(c), a1_in = A1(c), a2_in = A2(c);
    Frame f(c, 0x20);
    if (def != 0 && (Ld16(def + 6) & 8)) {
        if (Call(c, 0x411f8, def, a1_in, a2_in, f.At(0x10)) == 0) {
            Ret(c, 0);
            return;
        }
        const uint32_t h = Ld32(f.At(0x10));
        if (h != 0) Call(c, 0x41da0, h, 1, 0, 1);
    }
    Call(c, 0x4111c, kSeqHandleTable, 0x40, 5, 0x124);
}

// fn_413a8(rec): append the handle to the sound list (kSoundList head / kSoundListTail; rec[0x20] prev, rec[0x24]
// next), flag bit 31 set; a tail that is the record itself prints and hangs (the module's own guard).
void fn_413a8(IopContext& c) {
    const uint32_t rec = A0(c);
    Frame f(c, 0x20);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    St32(rec + 0x20, 0);
    St32(rec + 0x24, 0);
    St32(rec, Ld32(rec) | 0x80000000u);
    const uint32_t tail = Ld32(kSoundListTail);
    if (tail == 0) {
        St32(G::kSoundList, rec);
    } else if (tail == rec) {
        imp::Printf(c, kFmtSoundListTailSelf);
        for (;;) std::this_thread::yield();   // 0x41410: `j 0x41410` -- the recompiled body spins here too
    } else {
        St32(tail + 0x24, rec);
        St32(rec + 0x20, tail);
    }
    St32(kSoundListTail, rec);
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
}

// fn_4144c(rec, force): release a handle: a root with children and no `force` is an error (0x78) and forces; the
// children are released first; its sound-list nodes freed (fn_42310); it is unlinked from the sound list (a broken
// link prints and dumps: fn_424f8) and from its parent's child list (the children re-parented; a broken list ->
// error 0x74, the record left as is); rec[4] = 0, bit 31 cleared. Interrupts suspended.
void fn_4144c(IopContext& c) {
    const uint32_t rec = A0(c);
    uint32_t force = A1(c);
    Frame f(c, 0x30);
    if (force == 0 && Ld32(rec + 0x28) == 0 && Ld32(rec + 0x2c) != 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x78, 0, 0, 0);
        force = 1;
    }
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x18));
    if (force != 0) {
        while (Ld32(rec + 0x2c) != 0) Call(c, 0x4144c, Ld32(rec + 0x2c), force);
    }
    if (Ld32(rec + 8) != 0) {
        Call(c, 0x42310, Ld32(rec + 8));
        St32(rec + 8, 0);
    }
    const uint32_t prev = Ld32(rec + 0x20);
    if (prev != 0) {
        if (Ld32(prev + 0x24) != rec) {
            imp::Printf(c, kFmtSoundListPrev);
            Call(c, 0x424f8, rec);
        } else {
            St32(prev + 0x24, Ld32(rec + 0x24));
        }
    } else {
        St32(G::kSoundList, Ld32(rec + 0x24));
    }
    const uint32_t next = Ld32(rec + 0x24);
    if (next != 0) {
        if (Ld32(next + 0x20) != rec) {
            imp::Printf(c, kFmtSoundListNext);
            Call(c, 0x424f8, rec);
        } else {
            St32(next + 0x20, Ld32(rec + 0x20));
        }
    } else {
        St32(kSoundListTail, Ld32(rec + 0x20));
    }
    const uint32_t parent = Ld32(rec + 0x28);
    if (parent != 0) {
        auto broken = [&]() {
            if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x18)));
            St32(f.At(0x10), 0);
            Call(c, 0x35140, 0x74, 0, 0, 0);
        };
        if (Ld32(parent + 0x2c) == rec) {
            St32(parent + 0x2c, Ld32(rec + 0x30));
        } else {
            uint32_t p = Ld32(parent + 0x2c);
            while (p != 0 && Ld32(p + 0x30) != rec) p = Ld32(p + 0x30);
            if (p == 0) {
                broken();
                return;
            }
            St32(p + 0x30, Ld32(rec + 0x30));
        }
        for (uint32_t ch = Ld32(rec + 0x2c); ch != 0; ch = Ld32(ch + 0x30)) St32(ch + 0x28, Ld32(rec + 0x28));
        const uint32_t par = Ld32(rec + 0x28);
        const uint32_t first = Ld32(par + 0x2c);
        if (first == 0) {
            St32(par + 0x2c, Ld32(rec + 0x2c));
        } else {
            uint32_t tail = first;
            while (Ld32(tail + 0x30) != 0) {
                tail = Ld32(tail + 0x30);
                if (tail == 0) {
                    broken();
                    return;
                }
            }
            St32(tail + 0x30, Ld32(rec + 0x2c));
        }
    }
    const uint32_t w = Ld32(rec);
    St32(rec + 4, 0);
    St32(rec, w & 0x7fffffffu);
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x18)));
}

// fn_464e0(h): 1 when the handle has started (rec[0x16] & 8), 0 when not, -1 for an unknown handle.
void fn_464e0(IopContext& c) {
    const uint32_t h = A0(c);
    Frame f(c, 0x18);
    const uint32_t rec = Call(c, 0x417f8, h);
    if (rec == 0) {
        Ret(c, 0xffffffffu);
        return;
    }
    Ret(c, (Ld8(rec + 0x16) & 8) != 0 ? 1u : 0u);
}

// fn_42d3c(node): insert a timer node (node[4] its due tick, node[0xc] parent, node[0x10] / node[0x14] earlier /
// later child) into the timer tree by due tick; node[8] = 1 (armed).
void fn_42d3c(IopContext& c) {
    const uint32_t node = A0(c);
    if (node == 0) return;
    uint32_t p = Ld32(kTimerRoot);
    St32(node + 8, 1);
    St32(kTimerCount, Ld32(kTimerCount) + 1);
    if (p == 0) {
        St32(kTimerRoot, node);
        St32(node + 0xc, 0);
        return;
    }
    const uint32_t due = Ld32(node + 4);
    for (;;) {
        if (due < Ld32(p + 4)) {
            const uint32_t l = Ld32(p + 0x10);
            if (l == 0) {
                St32(p + 0x10, node);
                St32(node + 0xc, p);
                return;
            }
            p = l;
        } else {
            const uint32_t r = Ld32(p + 0x14);
            if (r != 0) {
                p = r;
            } else {
                St32(p + 0x14, node);
                St32(node + 0xc, p);
                return;
            }
        }
    }
}

// fn_4b24c(h, st): append a stream voice record to the handle's chain (h[4] head, st[0xc8] next); a record already
// chained is an error (0x4b).
void fn_4b24c(IopContext& c) {
    const uint32_t h = A0(c), st = A1(c);
    Frame f(c, 0x20);
    uint32_t p = Ld32(h + 4);
    if (p == 0) {
        St32(h + 4, st);
        return;
    }
    bool dup = false;
    for (;;) {
        if (Ld32(p + 0xc8) == 0) break;
        if (p == st) {
            dup = true;
            break;
        }
        p = Ld32(p + 0xc8);
    }
    if (!dup && p != st) {
        St32(p + 0xc8, st);
        return;
    }
    St32(f.At(0x10), 0);
    Call(c, 0x35140, 0x4b, 0, 0, 0);
}

// fn_4a8a8(kind): a free stream voice record (flags bit 0) within kind's index range of the record list (records
// before `first` skipped, none past `last`): taken (flags = 2, chain link cleared) under suspended interrupts.
// Returns the record or 0.
void fn_4a8a8(IopContext& c) {
    const uint32_t kind = A0(c);
    Frame f(c, 0x28);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    uint32_t rec = Ld32(kStreamRecList);
    const uint32_t range = kStreamRecRanges + kind * 4;
    const int32_t first = Lds16(range);
    int32_t i = 0;
    auto fail = [&]() {
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
        Ret(c, 0);
    };
    if (first > 0) {
        if (rec == 0) {
            fail();
            return;
        }
        do {
            rec = Ld32(rec + 0xcc);
            ++i;
            if (!(i < first)) break;
        } while (rec != 0);
    }
    if (rec == 0 || !(i < Lds16(range + 2) + 1)) {
        fail();
        return;
    }
    for (;;) {
        if (Ld32(rec) & 1) {
            St32(rec, 2);
            St32(rec + 0xc8, 0);
            if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
            Ret(c, rec);
            return;
        }
        rec = Ld32(rec + 0xcc);
        ++i;
        if (rec == 0 || !(i < Lds16(range + 2) + 1)) break;
    }
    fail();
}

// fn_44a58(h, st, source, a3, pair, flags): set up a stream voice record for a chunk: the voice's owner, the source
// (flags & 0xc000: an EE descriptor fetched by sceSifGetOtherData into st[0x98..0xa0] -- a failure reports 0x72 and
// marks the source -1; flags & 0x10000: an IOP descriptor {size, base, position}; else none), the counters reset,
// st[0x8c] = a3, st flags |= flags, the chunk's volume byte / pan from the handle (unless h[0x16] & 0x10), then the
// record joins the handle's chain (fn_4b24c, or fn_4b2e0 for the pair's second record).
void fn_44a58(IopContext& c) {
    const uint32_t h = A0(c), st = A1(c), a3 = A3(c);
    uint32_t src = A2(c);
    const uint32_t pair = StackArg(c, 0), flags = StackArg(c, 1);
    Frame f(c, 0x90);
    const int32_t v = Lds32(st + 0x7c);
    if (v >= 0) St32(G::Voice(v) + 0xc, h);
    if (flags & 0xc000) {
        St32(st + 0x94, src);
        St32(f.At(0x10), 0);
        if (Import(c, "sifcmd", 23, f.At(0x58), src, f.At(0x18), 0x40) != 0) {
            St32(f.At(0x10), 0);
            Call(c, 0x35140, 0x72, 0, 0, 0);
            src = 0xffffffffu;
        } else {
            src = Ld32(f.At(0x1c));
            St32(st + 0xa4, 0);
            St32(st + 0x98, Ld32(f.At(0x18)));
            St32(st + 0x9c, src);
            St32(st + 0xa0, Ld32(f.At(0x20)));
        }
    } else if (flags & 0x10000) {
        St32(st + 0x94, src);
        const uint32_t d = src;
        St32(st + 0x98, Ld32(d));
        src = Ld32(d + 4);
        St32(st + 0x9c, Ld32(d + 4));
        St32(st + 0xa4, 0);
        St32(st + 0xa0, Ld32(d + 8));
    } else {
        St32(st + 0x94, 0);
        St32(st + 0x90, 0);
    }
    const uint32_t w38 = Ld32(st + 0x38), w64 = Ld32(st + 0x64);
    St32(st + 0x10, 0x10);
    const uint32_t fl = Ld32(st);
    St32(st + 0xac, 0x800);
    St32(st + 0x84, src);
    St32(st + 0x88, src);
    St32(st + 0x8c, a3);
    St32(st + 0x3c, 0);
    St32(st + 0x24, 0);
    St32(st + 0x50, 0);
    St32(st + 0x68, 0);
    St32(st + 0x6c, 0);
    St32(st + 0x74, 0);
    St32(st + 0x70, 0);
    St32(st + 8, 0);
    St32(st + 0xd0, h);
    St32(st + 0xb8, 0xffffffffu);
    St32(st + 0xb0, 0);
    St32(st + 0xb4, 0);
    St32(st + 0xc8, 0);
    St8(st + 5, 0);
    St8(st + 0xc5, 0);
    St32(st + 0x78, 0);
    St32(st + 0x14, w38);
    St32(st + 0x40, w64);
    St32(st, fl | flags);
    if (!(Ld8(h + 0x16) & 0x10)) {
        St8(st + 0xc4, Ld8(h + 0x10));
        St16(st + 0xc6, Ld16(h + 0x12));
    } else {
        St8(st + 0xc4, 0);
        St16(st + 0xc6, 0);
    }
    if (pair == 0) Call(c, 0x4b24c, h, st);
    else Call(c, 0x4b2e0, h, st);
}

// fn_4e02c(core, bit): with interrupts suspended, drop the voice's pending key-on and set its pending key-off.
void fn_4e02c(IopContext& c) {
    const uint32_t core4 = A0(c) * 4, bit = A1(c);
    Frame f(c, 0x28);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    const uint32_t m = 1u << bit;
    const uint32_t kon = Ld32(G::kKonPending + core4) & ~m;
    const uint32_t koff = Ld32(G::kKoffPending + core4) | m;
    St32(G::kKonPending + core4, kon);
    St32(G::kKoffPending + core4, koff);
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
}

// fn_3eda0(group): the movie voices' volume when `group` is theirs (and a movie is active): fn_4ea54 recomputes the
// L/R words, then (mode != 3) the DMA channel's 0xf80 / 0x1080 parameters, or (voices) VOLL/VOLR of the voice(s):
// one voice gets both, two voices get L on the first and R on the second. Interrupts suspended.
void fn_3eda0(IopContext& c) {
    const uint32_t group = A0(c), a3_in = A3(c);
    Frame f(c, 0x38);
    if (Ld32(kMovieActive) == 0) return;
    if (group != Ld32(kMovieGroup)) return;
    Call(c, 0x4ea54, Ld32(kMovieLevelA), Ld32(kMovieLevelB), kMovieVol, a3_in);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    if (Ld32(kMovieMode) != 3) {
        uint32_t s = Call(c, 0x4ed08, U32(Lds16(kMovieVol)), Ld32(kMovieGroup));
        imp::SdSetParam(c, Ld16(G::kMovieChan) | 0xf80, s & 0xffff);
        s = Call(c, 0x4ed08, U32(Lds16(kMovieVol + 2)), Ld32(kMovieGroup));
        imp::SdSetParam(c, Ld16(G::kMovieChan) | 0x1080, s & 0xffff);
    } else {
        const int32_t v0 = Lds32(G::kMovieVoice0);
        const uint32_t e0 = VoiceEntry(v0);
        uint32_t s = Call(c, 0x4ed08, U32(Lds16(kMovieVol)), Ld32(kMovieGroup));
        imp::SdSetParam(c, e0 & 0xffff, (s & 0xffff) >> 1);
        if (!(Lds32(kMovieChannels) < 2)) {
            imp::SdSetParam(c, (e0 | 0x100) & 0xffff, 0);
            const uint32_t e1 = VoiceEntry(Lds32(kMovieVoice1));
            imp::SdSetParam(c, e1 & 0xffff, 0);
            s = Call(c, 0x4ed08, U32(Lds16(kMovieVol + 2)), Ld32(kMovieGroup));
            imp::SdSetParam(c, (e1 | 0x100) & 0xffff, (s & 0xffff) >> 1);
        } else {
            s = Call(c, 0x4ed08, U32(Lds16(kMovieVol + 2)), Ld32(kMovieGroup));
            imp::SdSetParam(c, (e0 | 0x100) & 0xffff, (s & 0xffff) >> 1);
        }
    }
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
}

// fn_4e100(group, volume): fno 9's body: the group's level (0..0x400; group 15 is an error 0x53), the hook notified,
// then the voices of the group refreshed (fn_4edbc with the group's bit, fn_3eda0); group 16 = the master: both
// cores' MVOLL/MVOLR (0x981 / 0xa81) = volume * 16383 / 1024, interrupts suspended.
void fn_4e100(IopContext& c) {
    const uint32_t group = A0(c), a2_in = A2(c), a3_in = A3(c);
    int32_t vol = S32(A1(c));
    Frame f(c, 0x30);
    if (!(vol < 0x401)) vol = 0x400;
    if (vol < 0) vol = 0;
    if (group == 0xf) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x53, 0, 0, 0);
        return;
    }
    St32(G::kParamTable + group * 4, U32(vol));
    const uint32_t hook = Ld32(kGroupHook);
    if (hook != 0 && Ld32(hook + 4) != 0) CallAddr(c, Ld32(hook + 4), group, U32(vol), a2_in, a3_in);
    if (group != 0x10) {
        Call(c, 0x4edbc, 1u << group);
        Call(c, 0x3eda0, group);
        return;
    }
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x18));
    const int32_t scaled = S32(U32(vol) << 14) - vol;
    const uint32_t mv = U32((scaled < 0 ? scaled + 0x3ff : scaled) >> 10) & 0xffff;
    imp::SdSetParam(c, 0x981, mv);
    imp::SdSetParam(c, 0xa81, mv);
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x18)));
}

// fn_3f820(req): the play request: the bank (req[0x28] & 1: req[0] is the bank; else looked up by name through
// fn_40464 -- unknown: a verbose print, 0), the sound (req[0x28] & 2: the index req[0x10]; else fn_404d8 by name --
// unknown: print, 0); a sequence bank ('SBlk') starts through fn_37000(bank, index, -1, -1, req), else a stream
// through fn_3f9d0(bank, index, volume, pan, a, b) with the request's optional words. Returns the handle.
void fn_3f820(IopContext& c) {
    const uint32_t req = A0(c);
    Frame f(c, 0x30);
    const uint32_t bank_slot = f.At(0x18);
    if (Ld32(req + 0x28) & 1) {
        St32(bank_slot, Ld32(req));
    } else if (Lds8(req) == 0) {
        St32(bank_slot, 0);
    } else {
        const uint32_t b = Call(c, 0x40464, req);
        St32(bank_slot, b);
        if (b == 0) {
            if (Ld32(G::kQuiet) == 0 && Ld32(kVerbose) != 0) imp::Printf(c, kFmtNoBank, req);
            Ret(c, 0);
            return;
        }
    }
    uint32_t index;
    if (Ld32(req + 0x28) & 2) {
        index = U32(Lds16(req + 0x10));
    } else {
        const uint32_t name = req + 0x10;
        index = Call(c, 0x404d8, Ld32(bank_slot), name, bank_slot);
        if (S32(index) < 0) {
            if (Ld32(G::kQuiet) == 0 && Ld32(kVerbose) != 0) imp::Printf(c, kFmtNoSound, name);
            Ret(c, 0);
            return;
        }
    }
    const uint32_t bank = Ld32(bank_slot);
    if (Ld32(bank) == 0x6b6c4253u) {
        St32(f.At(0x10), req);
        Call(c, 0x37000, bank, index, 0xffffffffu, 0xffffffffu);
        return;
    }
    const uint32_t fl = Ld32(req + 0x28);
    const uint32_t vol = (fl & 4) ? Ld32(req + 8) : 0x400;
    const uint32_t pan = (fl & 8) ? Ld32(req + 0xc) : 0xffffffffu;
    const uint32_t a = (fl & 0x10) ? U32(Lds16(req + 0x20)) : 0;
    const uint32_t b = (fl & 0x20) ? U32(Lds16(req + 0x22)) : 0;
    St32(f.At(0x10), a);
    St32(f.At(0x14), b);
    Call(c, 0x3f9d0, bank, index, vol, pan);
}

// fn_37000(bank, index, volume, pan, req): start sequence `index` of a sequence bank: the entry (12 bytes at
// bank[0x1c]) is flagged 0x4000 and must have a track (entry[4]); -1 volume / pan take the entry's; the request's
// optional words (volume req[8], pan req[0xc], the two pitch halves req[0x20]/req[0x22]) default 0x400 / -1 / 0;
// entries flagged 2 preempt (fn_41f90); under the lock a handle (fn_41334) is filled (definition, tick, the
// parameter words, the four volume bytes from req[0x24..] under flag 0x40, the four modulators), stepped until it
// waits (fn_3926c), linked (fn_413a8); a sequence that ended at once is stopped (fn_41da0) and 0 returned, else the
// handle id.
void fn_37000(IopContext& c) {
    const uint32_t bank = A0(c), a3_in = A3(c);
    const int32_t index = S32(A1(c));
    int32_t vol = static_cast<int16_t>(A2(c)), pan = static_cast<int16_t>(A3(c));
    const uint32_t req = StackArg(c, 0);
    Frame f(c, 0x38);
    if (index < 0 || !(index < Lds16(bank + 0x16))) {
        Ret(c, 0);
        return;
    }
    const uint32_t entry = Ld32(bank + 0x1c) + U32(index) * 12;
    const uint32_t ef = Ld16(entry + 6) | 0x4000;
    const int32_t track = Lds8(entry + 4);
    St16(entry + 6, ef);
    if (track == 0) {
        Ret(c, 0);
        return;
    }
    if (vol == -1) vol = Lds8(entry);
    if (pan == -1) pan = Lds16(entry + 2);
    const uint32_t rf = Ld32(req + 0x28);
    int32_t s5 = (rf & 4) ? Lds32(req + 8) : 0x400;
    int32_t s6 = (rf & 8) ? Lds32(req + 0xc) : -1;
    const int32_t fp = (rf & 0x10) ? Lds16(req + 0x20) : 0;
    const int32_t s7 = (rf & 0x20) ? Lds16(req + 0x22) : 0;
    if (s5 == 0x7fffffff) s5 = 0x400;
    if (U32(s6 + 2) < 2) s6 = pan;
    if (Ld16(entry + 6) & 2) Call(c, 0x41f90, entry, 0, 1, a3_in);
    Call(c, 0x42f58, 0x1a);
    const uint32_t rec = Call(c, 0x41334, entry, U32(s5), (Ld32(req + 0x28) >> 31) ^ 1, a3_in);
    if (rec == 0) {
        Call(c, 0x430dc);
        Ret(c, 0);
        return;
    }
    St32(rec + 4, entry);
    St32(rec + 0x120, entry);
    St32(rec + 0x11c, Call(c, 0x43638));
    Call(c, 0x35440, rec);
    St32(rec + 8, 0);
    St16(rec + 0xc, U32(vol));
    St16(rec + 0xe, U32(pan));
    St16(rec + 0x10, 0x7f);
    St16(rec + 0x12, U32(s6));
    St16(rec + 0x14, U32(fp));
    St8(rec + 0x16, 0);
    St32(rec + 0x18, 0);
    St32(rec + 0x1c, 0);
    St8(rec + 0x34, 0x3c);
    St8(rec + 0x35, 0);
    St16(rec + 0x36, U32(s7));
    St16(rec + 0x38, 0);
    St16(rec + 0x3a, U32(s5));
    St16(rec + 0x3c, 0);
    St16(rec + 0x3e, U32(s6));
    St16(rec + 0x40, 0);
    St16(rec + 0x42, U32(s7));
    St16(rec + 0x44, 0);
    St16(rec + 0x46, U32(fp));
    St8(rec + 0x17, Ld8(entry + 1));
    St16(rec + 0x4a, 0);
    St16(rec + 0x48, Ld16(Ld32(entry + 8) + 4));
    if (Ld32(req + 0x28) & 0x40) {
        St8(rec + 0x4c, Ld8(req + 0x24));
        St8(rec + 0x4d, Ld8(req + 0x25));
        St8(rec + 0x4e, Ld8(req + 0x26));
        St8(rec + 0x4f, Ld8(req + 0x27));
    } else {
        St8(rec + 0x4c, 0);
        St8(rec + 0x4d, 0);
        St8(rec + 0x4e, 0);
        St8(rec + 0x4f, 0);
    }
    St8(rec + 0x50, 0);
    St16(rec + 0x52, 0);
    St16(rec + 0x54, 0);
    St32(rec + 0x58, bank);
    for (uint32_t i = 0, node = rec + 0x5c; i < 4; ++i, node += 0x30) {
        St16(node, 0);
        St32(node + 0xc, 0);
        St8(node + 2, 0);
        St8(node + 4, 0);
        St8(node + 5, 0);
        St32(node + 0x28, rec);
    }
    St8(rec + 0x16, Ld8(rec + 0x16) & 0xbf);
    uint32_t ended = 0;
    if (Lds16(rec + 0x4a) != -1 && Lds16(rec + 0x48) == 0) {
        for (;;) {
            ended = Call(c, 0x3926c, rec);
            if (ended != 0) break;
            if (Lds16(rec + 0x4a) == -1) break;
            if (Lds16(rec + 0x48) != 0) break;
        }
    }
    Call(c, 0x413a8, rec);
    bool stop = ended != 0;
    if (!stop && Lds16(rec + 0x48) == 0 && Ld32(rec + 0x18) == 0 && Ld32(rec + 0x1c) == 0 && Ld32(rec + 0x2c) == 0) stop = true;
    if (stop) {
        Call(c, 0x41da0, rec, 1, 0, 0);
        Call(c, 0x430dc);
        Ret(c, 0);
        return;
    }
    Call(c, 0x430dc);
    Ret(c, Ld32(rec));
}

// fn_33770(buff): the play shim: fn_3f820(buff) -> the reply word.
void fn_33770(IopContext& c) {
    const uint32_t b = A0(c);
    Frame f(c, 0x18);
    const uint32_t r = Call(c, 0x3f820, b);
    St32(Ld32(G::kResultPtr), r);
}

// fn_46ed0(): issue the stream reader's CD read: the sector count from the bytes requested (capped by the max), the
// first/last sector words, sceCdSync(16), the callback fn_3a9dc armed, then sceCdRead(sector, count, buffer, mode
// {0x32, kCd[0], 0}) into the small or the big buffer (0x52328 selects); two or more sectors into the small buffer
// is an error (0x66: the reader is released and its waiter woken). A refused read records sceCdGetError (-1 when
// none, with a print unless quiet), reports 0x3d, releases the reader (wakes the waiter) and, in big-buffer mode,
// pushes the loader status (fn_3dcb8(0, error)).
void fn_46ed0(IopContext& c) {
    Frame f(c, 0x38);
    const int32_t bytes = Lds32(kCd + 4);
    const uint32_t max_sectors = Ld32(kCd + 0x1c);
    St32(kCd + 0x20, max_sectors);
    const uint32_t sectors = U32((bytes < 0 ? bytes + 0x7ff : bytes) >> 11);
    if (sectors < max_sectors) St32(kCd + 0x20, sectors);
    const uint32_t lsn = Ld32(kCd + 0x24);
    St32(kCd + 0xc, lsn);
    St32(kCd + 0x10, lsn - 1 + Ld32(kCd + 0x20));
    Import(c, "cdvdman", 11, 0x10);
    Import(c, "cdvdman", 37, 0x3a9dc);
    St8(f.At(0x18), 0x32);
    St8(f.At(0x1a), 0);
    St8(f.At(0x19), Ld8(kCd));
    auto release = [&]() {
        const int32_t waiter = Lds32(kCd + 0x34);
        St32(kCd + 0x60, 0);
        St32(kCd + 0x1c, 0);
        if (waiter != -1) {
            St32(kCd + 0x34, 0xffffffffu);
            Import(c, "thbase", 25, U32(waiter));
        }
    };
    uint32_t ok;
    if (Ld32(kCd + 0x38) == 0) {
        const uint32_t n = Ld32(kCd + 0x20), buf = Ld32(kCd + 0x18);
        St32(kCd + 0x3c, Ld32(kCd + 0x24));
        St32(kCd + 0x40, n);
        St32(kCd + 0x44, buf);
        if (buf == kCdSmallBuffer && !(n < 2)) {
            St32(kCd + 0x30, 0xffffffffu);
            St32(f.At(0x10), 0);
            Call(c, 0x35140, 0x66, n, 0, 0);
            release();
            return;
        }
        St32(G::kCdWatch, 2);
        St32(G::kCdWatchTicks, 0);
        ok = Import(c, "cdvdman", 6, Ld32(kCd + 0x24), Ld32(kCd + 0x20), Ld32(kCd + 0x18), f.At(0x18));
    } else {
        St32(G::kCdWatch, 2);
        St32(G::kCdWatchTicks, 0);
        const uint32_t buf = Ld32(kCd + 0x14), l = Ld32(kCd + 0x24), n = Ld32(kCd + 0x20);
        St32(kCd + 0x3c, l);
        St32(kCd + 0x40, n);
        St32(kCd + 0x44, buf);
        ok = Import(c, "cdvdman", 6, l, n, buf, f.At(0x18));
    }
    if (ok != 0) return;
    St32(G::kCdWatch, 0);
    const uint32_t err = Import(c, "cdvdman", 8);
    St32(kCd + 0x30, err);
    if (err == 0) {
        St32(kCd + 0x30, 0xffffffffu);
        if (Ld32(G::kQuiet) == 0) imp::Printf(c, kFmtCdReadFailed);
    }
    if (Ld32(kCd + 0x38) == 0) {
        St32(f.At(0x10), Ld32(kCd + 0x18));
        Call(c, 0x35140, 0x3d, Ld32(kCd + 0x30), Ld32(kCd + 0x24), Ld32(kCd + 0x20));
    } else {
        St32(f.At(0x10), Ld32(kCd + 0x14));
        Call(c, 0x35140, 0x3d, Ld32(kCd + 0x30), Ld32(kCd + 0x24), Ld32(kCd + 8));
    }
    release();
    if (Ld32(kCd + 0x38) != 0) Call(c, 0x3dcb8, 0, Ld32(kCd + 0x30));
}

// fn_46d8c(mode): wait for the stream reader's CD read: no record list -> sceCdSync(mode); idle (no read, not busy)
// -> 0; mode 1 / 0x11 -> 1 (poll: busy); another thread already waiting and mode 0 / 0x10 -> error 0x3c, 1; else
// sleep (the waiter word = this thread) until the read and the busy flag clear; 0.
void fn_46d8c(IopContext& c) {
    const uint32_t mode = A0(c);
    Frame f(c, 0x28);
    if (Ld32(kCd + 0x48) == 0) {
        Ret(c, Import(c, "cdvdman", 11, mode));
        return;
    }
    if (Ld32(kCd + 0x1c) == 0 && Ld32(kCd + 0x60) == 0) {
        Ret(c, 0);
        return;
    }
    if (mode == 1 || mode == 0x11) {
        Ret(c, 1);
        return;
    }
    if (Lds32(kCd + 0x34) != -1 && (mode == 0 || mode == 0x10)) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x3c, 0, 0, 0);
        Ret(c, 1);
        return;
    }
    bool sleep = Ld32(kCd + 0x1c) != 0;
    for (;;) {
        if (!sleep) {
            if (Ld32(kCd + 0x60) == 0) {
                Ret(c, 0);
                return;
            }
        }
        uint32_t pending;
        do {
            const uint32_t tid = Import(c, "thbase", 20);
            St32(kCd + 0x34, tid);
            Import(c, "thbase", 24);
            pending = Ld32(kCd + 0x1c);
            St32(kCd + 0x34, 0xffffffffu);
        } while (pending != 0);
        sleep = false;
    }
}

// fn_3dcb8(which, status): push the loader status to the EE: kStatusRecA[0] = which, kStatusRecB[0] = status; each
// 16-byte record DMA'd (sceSifSetDmaIntr + the completion semaphore, polled by sceSifDmaStat) to kStatusEeAddr (A)
// and + 0x10 (B): A first when `which` is set, else B then A. Nothing without an EE address.
void fn_3dcb8(IopContext& c) {
    const uint32_t which = A0(c), status = A1(c);
    Frame f(c, 0x58);
    St32(kStatusRecA, which);
    St32(kStatusRecB, status);
    const uint32_t ee = Ld32(kStatusEeAddr);
    if (ee == 0) return;
    St32(f.At(0x20), 0x10);
    St32(f.At(0x24), 0);
    auto push = [&](uint32_t src, uint32_t dst, uint32_t code) {
        St32(f.At(0x18), src);
        St32(f.At(0x1c), dst);
        const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x28));
        const uint32_t id = Import(c, "sifman", 32, f.At(0x18), 1, kPositionDmaCallback, kPositionDmaSema);
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x28)));
        for (;;) {
            if (S32(Import(c, "sifman", 8, id)) < 0) break;
            if (imp::WaitSema(c, Ld32(kPositionDmaSema)) != 0) {
                St32(f.At(0x10), 0);
                Call(c, 0x35140, code, 0, 0, 0);
            }
        }
    };
    if (which != 0) push(kStatusRecA, ee, 0x7d2);
    push(kStatusRecB, Ld32(kStatusEeAddr) + 0x10, 0x7d3);
    if (which == 0) push(kStatusRecA, Ld32(kStatusEeAddr), 0x7d4);
}

}  // namespace ps2r::title::sly2::snd989
