// snd989_loader.cpp -- the functions reached only on the loader route (cold boot -> save resume -> level load): the
// bank unload family (fno 1: fn_334c4 -> fn_3c77c / fn_3c8c8 / fn_3c958 / fn_3d514 / fn_40344 / fn_41f90, the voice-mask
// key-off with callbacks fn_4d438), the stream stop / pause / resume (fn_4555c, fn_402a4, fn_450c4, fn_453fc, fn_3fa2c,
// fno 7 fn_337e0), the queue unlinks (fn_4ab38 / fn_4abe4), the refill / upload membership tests (fn_45470 / fn_454cc),
// the pitch ratio (fn_4be14), the sequence restart (fn_37870 / fn_379cc) and the chain clear (fn_4ad9c). Ported from the
// recompiled 989snd_Library over the module's RAM.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

namespace {
constexpr uint32_t kStartQueueHead = G::kData + 0x233c, kStartQueueTail = G::kData + 0x2340;
constexpr uint32_t kRefillQueueHead = G::kData + 0x2344, kRefillQueueTail = G::kData + 0x2348;
constexpr uint32_t kRefillCurrent = G::kData + 0x234c, kTransSlot = G::kData + 0x2354;
constexpr uint32_t kActiveStreamVoices = G::kData + 0x2388, kStopBusy = G::kData + 0x2390;
constexpr uint32_t kBankList2 = G::kData + 0x1998, kUnloadCallback = G::kData + 0x618, kHeapFree = G::kData + 0x198c;
constexpr uint32_t kVoiceTable = 0x593a8;
constexpr uint32_t kKeyOffMask = G::kData + 0x2558, kKeyOnMask = G::kData + 0x2550;
constexpr uint32_t kSblk = 0x6b6c4253u;
inline uint32_t VoiceEntryOf(int32_t v, int32_t& core, int32_t& voice) {
    core = Div24(v);
    voice = v - core * 24;
    return U32(core) | (U32(voice) << 1);
}
}  // namespace

// fn_41f90(def, a1, a2): every playing handle of definition `def` -> fn_41910(h[0], 1, a1, a2).
void fn_41f90(IopContext& c) {
    const uint32_t def = A0(c), a1 = A1(c), a2 = A2(c);
    Frame f(c, 0x28);
    for (uint32_t h = Ld32(G::kSoundList); h != 0; h = Ld32(h + 0x24))
        if (Ld32(h + 4) == def) Call(c, 0x41910, Ld32(h), 1, a1, a2);
}

// fn_4be14(delta, base): the pitch ratio: `base` scaled by 2^(delta / 1536) in semitone steps of 128 (x 69432 / 65536 up,
// x 0x1e3453a7 / 2^45 down) plus the linear remainder. Returns the 16-bit result.
void fn_4be14(IopContext& c) {
    int32_t a0 = S32(A0(c));
    uint32_t a1 = A1(c);
    int32_t steps = (a0 < 0 ? a0 + 0x7f : a0) >> 7;
    if (steps < 0) steps = -steps;
    if (a0 > 0) {
        for (int32_t i = steps; i > 0; --i) {
            a1 = (a1 * 69432u) >> 16;
            a0 -= 0x80;
        }
        if (a0 > 0) {
            const uint32_t up = (a1 * 69432u) >> 16;
            a1 += U32(MulLo(S32(up - a1), a0)) >> 7;
        }
    } else if (a0 < 0) {
        for (int32_t i = steps; i > 0; --i) {
            a1 = MulHiU(a1 << 16, 0x1e3453a7u) >> 13;
            a0 += 0x80;
        }
        if (a0 < 0) {
            const uint32_t down = MulHiU(a1 << 16, 0x1e3453a7u) >> 13;
            a1 -= U32(MulLo(S32(down - a1), a0)) >> 7;
        }
    }
    Ret(c, a1 & 0xffff);
}

namespace {
// The unlink of `rec` from a singly linked half-record queue (rec[0xc] = next) with head / tail words, under suspended
// interrupts (fn_4ab38: the start queue; fn_4abe4: the refill queue).
void Unlink(IopContext& c, uint32_t rec, uint32_t head_addr, uint32_t tail_addr) {
    Frame f(c, 0x20);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    const uint32_t head = Ld32(head_addr);
    if (head == rec) {
        const uint32_t next = Ld32(rec + 0xc);
        St32(head_addr, next);
        if (next == 0) St32(tail_addr, 0);
    } else {
        uint32_t p = head;
        while (p != 0 && Ld32(p + 0xc) != rec) p = Ld32(p + 0xc);
        if (p != 0) {
            const uint32_t next = Ld32(rec + 0xc);
            St32(p + 0xc, next);
            if (next == 0) St32(tail_addr, p);
        }
    }
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
}
}  // namespace

void fn_4ab38(IopContext& c) { Unlink(c, A0(c), kStartQueueHead, kStartQueueTail); }
void fn_4abe4(IopContext& c) { Unlink(c, A0(c), kRefillQueueHead, kRefillQueueTail); }

// fn_402a4(handle): resume a paused handle under the lock (0x50c): a stream (type 4) -> fn_450c4; types 0..2 and 5 ->
// the record (fn_417f8) -> fn_419f8(rec, 1); others nothing.
void fn_402a4(IopContext& c) {
    const uint32_t h = A0(c);
    Frame f(c, 0x20);
    const int32_t type = S32((h >> 24) & 0x1f);
    Call(c, 0x42f58, 0x50c);
    if (type == 4) {
        Call(c, 0x450c4, h);
    } else if (type < 3 || type == 5) {
        const uint32_t p = Call(c, 0x417f8, h);
        if (p != 0) Call(c, 0x419f8, p, 1);
    }
    Call(c, 0x430dc);
}

// fn_379cc(h): the index of the '+' marker among the definition's parts (def[4] parts of 8 bytes from def[8]; the tag
// byte at +3); -1 when none or when it is the last part.
void fn_379cc(IopContext& c) {
    const uint32_t def = Ld32(A0(c) + 4);
    if (Lds8(def + 4) <= 0) {
        Ret(c, 0xffffffffu);
        return;
    }
    uint32_t p = Ld32(def + 8) + 3;
    for (int32_t i = 0;; p += 8) {
        if (Ld8(p) == 0x2b) {
            Ret(c, i == Lds8(def + 4) - 1 ? 0xffffffffu : U32(i));
            return;
        }
        ++i;
        if (!(i < Lds8(def + 4))) break;
    }
    Ret(c, 0xffffffffu);
}

// fn_37870(h, restart, mark): a sequence handle restarted at its '+' part (fn_379cc): no marker or `restart` -> the
// handle ended (fn_35440) and, if it still owns voices, its voices keyed off (fn_4d438) and the part index -1 -> 1 / 0;
// else under the lock the voices keyed off, the part index = marker + 1 (`mark` sets flag 0x20; flag 0x40 cleared) and
// the sequence stepped (fn_3926c) until it yields or the part index is -1; -> 1 while anything is left, else the handle
// ended -> 0.
void fn_37870(IopContext& c) {
    const uint32_t h = A0(c), restart = A1(c), mark = A2(c);
    Frame f(c, 0x28);
    const int32_t idx = S32(Call(c, 0x379cc, h));
    if (idx == -1 || restart != 0) {
        Call(c, 0x35440, h);
        if (Ld32(h + 0x2c) == 0) {
            Ret(c, 0);
            return;
        }
        Call(c, 0x42f58, 0x13);
        Call(c, 0x4d438, h + 0x18, 0);
        St32(h + 0x1c, 0);
        St32(h + 0x18, 0);
        St16(h + 0x4a, 0xffff);
        Call(c, 0x430dc);
        Ret(c, 1);
        return;
    }
    Call(c, 0x42f58, 0x13);
    Call(c, 0x4d438, h + 0x18, 0);
    St32(h + 0x1c, 0);
    St32(h + 0x18, 0);
    St16(h + 0x4a, U32(idx + 1));
    if (mark != 0) St8(h + 0x16, Ld8(h + 0x16) | 0x20);
    St8(h + 0x16, Ld8(h + 0x16) & 0xbf);
    while (Lds16(h + 0x4a) != -1) {
        if (Call(c, 0x3926c, h) != 0) break;
    }
    Call(c, 0x430dc);
    if (Ld32(h + 0x18) != 0 || Ld32(h + 0x1c) != 0 || Ld32(h + 0x2c) != 0) {
        Ret(c, 1);
        return;
    }
    Call(c, 0x35440, h);
    Ret(c, 0);
}

// fn_3fa2c(h): stop a handle under the lock (0x500): the record (fn_417f8) -> fn_41da0(rec, 1, 0, 0).
void fn_3fa2c(IopContext& c) {
    const uint32_t h = A0(c);
    Frame f(c, 0x18);
    Call(c, 0x42f58, 0x500);
    const uint32_t p = Call(c, 0x417f8, h);
    if (p != 0) Call(c, 0x41da0, p, 1, 0, 0);
    Call(c, 0x430dc);
}

// fn_453fc(h): under the lock (0x54) a known handle is stopped silently: fn_4b61c(h, 0, 0) and flag bit 2 set.
void fn_453fc(IopContext& c) {
    const uint32_t h = A0(c);
    Frame f(c, 0x20);
    Call(c, 0x42f58, 0x54);
    const uint32_t p = Call(c, 0x417f8, h);
    if (p != 0) {
        Call(c, 0x4b61c, h, 0, 0);
        St8(p + 0x16, Ld8(p + 0x16) | 4);
    }
    Call(c, 0x430dc);
}

// fn_45470(st): 1 when the current refill belongs to one of the stream record's half records, else 0.
void fn_45470(IopContext& c) {
    const uint32_t cur = Ld32(kRefillCurrent);
    if (cur == 0) {
        Ret(c, 0);
        return;
    }
    for (uint32_t r = Ld32(A0(c) + 4); r != 0; r = Ld32(r + 0xc8)) {
        if (cur == r + 0x10 || cur == r + 0x3c) {
            Ret(c, 1);
            return;
        }
    }
    Ret(c, 0);
}

// fn_454cc(st): 1 when one of the two SPU DMA slots holds one of the stream record's half records, else 0.
void fn_454cc(IopContext& c) {
    if (Ld32(kTransSlot) == 0 && Ld32(kTransSlot + 4) == 0) {
        Ret(c, 0);
        return;
    }
    for (uint32_t r = Ld32(A0(c) + 4); r != 0; r = Ld32(r + 0xc8)) {
        const uint32_t s0 = Ld32(kTransSlot), s1 = Ld32(kTransSlot + 4);
        if (s0 == r + 0x10 || s0 == r + 0x3c || s1 == r + 0x10 || s1 == r + 0x3c) {
            Ret(c, 1);
            return;
        }
    }
    Ret(c, 0);
}

// fn_4ad9c(p): the chain's first words cleared (p[0] = 0 along p[0x10]).
void fn_4ad9c(IopContext& c) {
    for (uint32_t p = A0(c); p != 0; p = Ld32(p + 0x10)) St32(p, 0);
}

// fn_334c4(args): fno 1, the bank unload: fn_3c77c(args[0]); the result word = 0.
void fn_334c4(IopContext& c) {
    Frame f(c, 0x18);
    Call(c, 0x3c77c, Ld32(A0(c)));
    St32(Ld32(G::kResultPtr), 0);
    Ret(c, G::kData);
}

// fn_337e0(args): fno 7, the stop: fn_3fa2c(args[0]).
void fn_337e0(IopContext& c) {
    Frame f(c, 0x18);
    Call(c, 0x3fa2c, Ld32(A0(c)));
}

// fn_3c958(bank): 1 when `bank` is on the legacy bank list, else 0 (0 / -1 -> 0).
void fn_3c958(IopContext& c) {
    const uint32_t bank = A0(c);
    if (bank == 0 || bank == 0xffffffffu) {
        Ret(c, 0);
        return;
    }
    for (uint32_t b = Ld32(kBankList2); b != 0; b = Ld32(b + 0x30)) {
        if (b == bank) {
            Ret(c, 1);
            return;
        }
    }
    Ret(c, 0);
}

// fn_40344(bank, flag): under the lock (0x50d) every handle of every sound of the bank -> fn_41f90(sound, flag, 0)
// ('SBlk': 12-byte sounds, count at +0x16; legacy: 28-byte, count at +0x14). A null bank does nothing.
void fn_40344(IopContext& c) {
    const uint32_t bank = A0(c), flag = A1(c);
    Frame f(c, 0x28);
    if (bank == 0) return;
    Call(c, 0x42f58, 0x50d);
    if (Ld32(bank) == kSblk) {
        uint32_t off = 0;
        for (int32_t i = 0; i < Lds16(bank + 0x16); ++i, off += 0xc) Call(c, 0x41f90, Ld32(bank + 0x1c) + off, flag, 0);
    } else {
        uint32_t off = 0;
        for (int32_t i = 0; i < Lds16(bank + 0x14); ++i, off += 0x1c) Call(c, 0x41f90, Ld32(bank + 0x1c) + off, flag, 0);
    }
    Call(c, 0x430dc);
}

// fn_3d514(bank): a legacy bank removed from its list: its handles stopped (fn_40344(bank, 1)), unlinked, its SPU block
// freed (fn_42924) under suspended interrupts. Not on the list -> nothing.
void fn_3d514(IopContext& c) {
    const uint32_t bank = A0(c);
    Frame f(c, 0x30);
    if (bank == 0) return;
    uint32_t cur = Ld32(kBankList2), prev = 0;
    if (cur != 0 && cur != bank) {
        for (;;) {
            prev = cur;
            cur = Ld32(cur + 0x30);
            if (cur == 0 || cur == bank) break;
        }
    }
    if (cur != bank) return;
    Call(c, 0x40344, bank, 1);
    if (prev != 0) St32(prev + 0x30, Ld32(cur + 0x30));
    else St32(kBankList2, Ld32(cur + 0x30));
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    if (Ld32(bank + 0x2c) != 0) Call(c, 0x42924, Ld32(bank + 0x24), Ld32(bank + 0x2c));
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
}

// fn_3c8c8(bank): a legacy bank unloaded: unknown -> 0x16; else the unload callback (kUnloadCallback -> fn(bank, 0)),
// fn_3d514, the block freed.
void fn_3c8c8(IopContext& c) {
    const uint32_t bank = A0(c);
    Frame f(c, 0x20);
    if (Call(c, 0x3c958, bank) == 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x16, 0, 0, 0);
        return;
    }
    const uint32_t cb = Ld32(kUnloadCallback);
    if (cb != 0 && Ld32(cb) != 0) CallAddr(c, Ld32(cb), bank, 0, A2(c), A3(c));
    Call(c, 0x3d514, bank);
    CallAddr(c, Ld32(kHeapFree), bank, A1(c), A2(c), A3(c));
}

// fn_3c77c(bank): the bank unload: 0 / -1 -> 0x14; a legacy bank -> fn_3c8c8; an 'SBv2' bank not on its list (fn_3c878)
// -> 0x15; else the unload callback, a sequence block (flag bit 3: the word before the bank) released (fn_3cc48), the bank
// removed (fn_3d410) and its block (the header, 4 bytes before it when a sequence block exists) freed.
void fn_3c77c(IopContext& c) {
    const uint32_t bank = A0(c);
    Frame f(c, 0x28);
    uint32_t block = bank;
    if (bank == 0 || bank == 0xffffffffu) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x14, 0, 0, 0);
        return;
    }
    if (Ld32(bank) == kSblk) {
        Call(c, 0x3c8c8, bank);
        return;
    }
    if (Call(c, 0x3c878, bank) == 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x15, 0, 0, 0);
        return;
    }
    const uint32_t cb = Ld32(kUnloadCallback);
    if (cb != 0 && Ld32(cb) != 0) CallAddr(c, Ld32(cb), bank, 0, A2(c), A3(c));
    if (Ld32(bank + 8) & 8) {
        Call(c, 0x3cc48, Ld32(bank - 4));
        block = bank - 4;
    }
    Call(c, 0x3d410, bank);
    CallAddr(c, Ld32(kHeapFree), block, A1(c), A2(c), A3(c));
}

// fn_450c4(h): a paused stream resumed under the lock (0x53): flag bit 1 cleared; under suspended interrupts every playing
// chain voice gets its pitch back (fn_4be14(st[0x14], rec[8]) -> SPU pitch; the first record also for the pair voice
// st[0x38]), then its play flag 0x10 cleared and the volumes (fn_4ed08 over the voice table's pan pair) restored, the
// active-voice count bumped (again the pair voice on the first record).
void fn_450c4(IopContext& c) {
    const uint32_t h = A0(c);
    Frame f(c, 0x40);
    Call(c, 0x42f58, 0x53);
    const uint32_t p = Call(c, 0x417f8, h);
    if (p == 0 || !(Ld8(p + 0x16) & 2)) {
        Call(c, 0x430dc);
        return;
    }
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    St8(p + 0x16, Ld8(p + 0x16) & 0xfd);
    int32_t core, voice;
    auto pitch = [&](int32_t v, uint32_t rec) {
        const uint32_t e = VoiceEntryOf(v, core, voice);
        const uint32_t pr = Call(c, 0x4be14, U32(Lds16(p + 0x14)), Ld32(rec + 8));
        imp::SdSetParam(c, (e | 0x200) & 0xffff, pr);
    };
    uint32_t i = 0;
    for (uint32_t r = Ld32(p + 4); r != 0; r = Ld32(r + 0xc8), ++i) {
        if (!(Ld32(r) & 4)) continue;
        pitch(Lds32(r + 0x7c), r);
        if (i == 0 && Lds32(p + 0x38) != -1) pitch(Lds32(p + 0x38), r);
    }
    auto volumes = [&](int32_t v) {
        uint32_t e = VoiceEntryOf(v, core, voice);
        uint32_t vt = kVoiceTable + U32(v) * 56;
        uint32_t vol = Call(c, 0x4ed08, U32(Lds16(vt + 0x20)), U32(Lds8(vt + 0x1b)));
        imp::SdSetParam(c, e & 0xffff, (vol & 0xffff) >> 1);
        vol = Call(c, 0x4ed08, U32(Lds16(vt + 0x22)), U32(Lds8(vt + 0x1b)));
        e |= 0x100;
        imp::SdSetParam(c, e & 0xffff, (vol & 0xffff) >> 1);
    };
    i = 0;
    for (uint32_t r = Ld32(p + 4); r != 0; r = Ld32(r + 0xc8), ++i) {
        const uint32_t fl = Ld32(r);
        St32(r, fl & ~0x10u);
        if (!(fl & 4)) continue;
        volumes(Lds32(r + 0x7c));
        St32(kActiveStreamVoices, Ld32(kActiveStreamVoices) + 1);
        if (i == 0 && Lds32(p + 0x38) != -1) volumes(Lds32(p + 0x38));
    }
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
    Call(c, 0x430dc);
}

// fn_4555c(h): a stream handle stopped (re-entrance guarded by kStopBusy): under suspended interrupts the record
// (fn_417f8; unknown -> nothing), its chain's first words cleared (fn_4ad9c), the pair voice and every chain voice (the
// primary and the appended chains) silenced (volumes 0, pitch 0), keyed off (fn_4d318), deactivated (fn_4de04), their
// table owner byte cleared and the record's voice = -1; each half record pulled from both queues; the active-voice count
// decremented per primary record; the handle released (fn_4144c(p, 0)); the stream semaphore signalled.
void fn_4555c(IopContext& c) {
    const uint32_t h = A0(c);
    Frame f(c, 0x40);
    if (Ld32(kStopBusy) != 0) {
        Ret(c, G::kData);
        return;
    }
    St32(kStopBusy, 1);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    const uint32_t p = Call(c, 0x417f8, h);
    if (p == 0) {
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
        St32(kStopBusy, 0);
        return;
    }
    const uint32_t chain = Ld32(p + 4);
    Call(c, 0x4ad9c, Ld32(p + 0x70), (Ld32(chain) >> 12) & 1);
    int32_t core, voice;
    auto silence = [&](uint32_t slot) {
        const int32_t v = Lds32(slot);
        if (v == -1) return;
        uint32_t e = VoiceEntryOf(v, core, voice);
        imp::SdSetParam(c, e & 0xffff, 0);
        imp::SdSetParam(c, (e | 0x100) & 0xffff, 0);
        e |= 0x200;
        imp::SdSetParam(c, e & 0xffff, 0);
        Call(c, 0x4d318, Ld32(slot));
        Call(c, 0x4de04, Ld32(slot));
        St8(kVoiceTable + Ld32(slot) * 56 + 0x1a, 0);
        St32(slot, 0xffffffffu);
    };
    silence(p + 0x38);
    for (uint32_t r = chain; r != 0; r = Ld32(r + 0xc8)) {
        St32(r, 1);
        Call(c, 0x4abe4, r + 0x10);
        Call(c, 0x4abe4, r + 0x3c);
        Call(c, 0x4ab38, r + 0x10);
        Call(c, 0x4ab38, r + 0x3c);
        silence(r + 0x7c);
        St32(kActiveStreamVoices, Ld32(kActiveStreamVoices) - 1);
    }
    for (uint32_t r = Ld32(p + 0x6c); r != 0; r = Ld32(r + 0xc8)) {
        St32(r, 1);
        Call(c, 0x4abe4, r + 0x10);
        Call(c, 0x4abe4, r + 0x3c);
        Call(c, 0x4ab38, r + 0x10);
        Call(c, 0x4ab38, r + 0x3c);
        silence(r + 0x7c);
    }
    Call(c, 0x4144c, p, 0);
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
    imp::SignalSema(c, Ld32(G::kStreamSema));
    St32(kStopBusy, 0);
}

// fn_4d438(mask, notify): every voice 0..47 whose bit is in the two-word `mask` and whose table state is 1 (playing): its
// key-off bit set; with `notify` and a voice callback set (table +0x10), the callback(voice, table[+0xc], 3) outside the
// suspended-interrupt window; the callback cleared; the key-on bit cleared.
void fn_4d438(IopContext& c) {
    const uint32_t mask = A0(c), notify = A1(c);
    Frame f(c, 0x40);
    St32(f.At(0x40), mask);
    St32(f.At(0x44), notify);
    uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    for (int32_t v = 0; v < 0x30; ++v) {
        const int32_t core = Div24(v), bit = v - core * 24;
        const uint32_t b = 1u << (U32(bit) & 31);
        if (!(Ld32(Ld32(f.At(0x40)) + U32(core) * 4) & b)) continue;
        const uint32_t vt = kVoiceTable + U32(v) * 56;
        if (Ld32(vt + 8) != 1) continue;
        St32(kKeyOffMask + U32(core) * 4, Ld32(kKeyOffMask + U32(core) * 4) | b);
        if (Ld32(f.At(0x44)) != 0 && Ld32(vt + 0x10) != 0) {
            if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
            CallAddr(c, Ld32(vt + 0x10), U32(v), Ld32(vt + 0xc), 3, A3(c));
            suspended = imp::CpuSuspendIntr(c, f.At(0x10));
        }
        St32(vt + 0x10, 0);
        const uint32_t on = Ld32(kKeyOnMask + U32(core) * 4);
        if (on & b) St32(kKeyOnMask + U32(core) * 4, on & ~b);
    }
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
}

}  // namespace ps2r::title::sly2::snd989
