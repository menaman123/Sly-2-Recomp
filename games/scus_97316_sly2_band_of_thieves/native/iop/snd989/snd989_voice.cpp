// snd989_voice.cpp -- the voice side of a sequence note: allocation (fn_4c750), the key-on programming (fn_4ca58),
// the mute switch (fn_4c458), the active-voice list (fn_4de04 / fn_4dec0), the voice-end callback (fn_39b44), the
// note-on event handler (fn_37a44); the module's LFSR (fn_3f440) and the modulator list / random waveform (fn_35310,
// fn_353a8, fn_35440, fn_35480, fn_35508). Ported from the recompiled 989snd_Library over the module's RAM.
//
// Voice table record (G::Voice(v), 56 bytes): +0 next active, +8 state (0 free, 2 allocated, 3 locked), +0xc owner
// handle, +0x10 end callback, +0x14 the sound definition, +0x18/+0x19 pitch bytes, +0x1a priority, +0x1b group,
// +0x1c the tick allocated, +0x20/+0x22 VOLL/VOLR, +0x24/+0x26 pitch words, +0x28 flags (1 = not stealable),
// +0x2c volume byte, +0x2e pan angle (s16). Sound definition `def`: +0 priority (s8), +1 volume (s8: -1..-4 = the
// handle's rec[0x4c..0x4f], -5 = random, below = a table byte), +2/+3 note/fine, +4 pan (s16, same encoding), +0xa/+0xc
// ADSR words, +0xe flags (1 = effects send, 8 = noise, 0x10 = no reverb switch, 0x20 = not stealable), +0x10 SPU address.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

namespace {
constexpr uint32_t kModListHead = G::kData + 0x17f8;      // the modulator list (node[0x2c] = next), fn_35508/fn_35480
constexpr uint32_t kModListCount = G::kData + 0x17fc;     // at most 10
constexpr uint32_t kRandIndex = G::kData + 0x19e8;        // s16: the LFSR table index
constexpr uint32_t kRandTable = G::kData + 0x19ec;        // 0xf9 u16 words
constexpr uint32_t kPrioRanges = G::kData + 0x23b8;       // 16 x {s16 first, s16 last}: the voice range per priority
constexpr uint32_t kActiveSlot0 = G::kData + 0x23fc;      // per core: {-1 word, the voice, 0 word} at 0x23fc/0x2404/0x240c
constexpr uint32_t kDefByteTable = G::kData + 0x1812;     // the volume/pan table a definition byte below -5 indexes (minus the byte)
constexpr uint32_t kVoiceEndCallback = 0x39b44;           // fn_39b44
}  // namespace

// fn_3f440(): the next LFSR word: table[i] ^= table[i + 0x67 (or i - 0x93)], i wraps at 0xf9. Returns the word.
void fn_3f440(IopContext& c) {
    const int32_t i = Lds16(kRandIndex);
    const uint32_t iu = Ld16(kRandIndex);
    const int32_t j = i < 0x93 ? i + 0x67 : i - 0x93;
    const uint32_t at = kRandTable + U32(i) * 2;
    const uint32_t w = Ld16(at) ^ Ld16(kRandTable + U32(j) * 2);
    St16(at, w);
    if (i < 0xf9) St16(kRandIndex, iu + 1);
    else St16(kRandIndex, 0);
    Ret(c, w);
}

// fn_35310(node, phase): the random waveform: a new random level (-(r & 0x7fff) or 0 by a coin) when the phase enters
// the low quarter, a new centred level ((r & 0x7fff) - 0x3fff) * 2 when it leaves; node[0x1c] remembers the half.
void fn_35310(IopContext& c) {
    const uint32_t node = A0(c);
    const int32_t phase = S32(A1(c));
    Frame f(c, 0x20);
    if (!(phase < 0x400)) {
        if (Ld32(node + 0x1c) == 1) {
            St32(node + 0x1c, 0);
            const uint32_t r = Call(c, 0x3f440);
            St32(node + 0x18, U32(S32((r & 0x7fff) - 0x3fff) << 1));
        }
    } else if (Ld32(node + 0x1c) == 0) {
        St32(node + 0x1c, 1);
        const uint32_t r1 = Call(c, 0x3f440);
        const uint32_t r2 = Call(c, 0x3f440);
        St32(node + 0x18, U32(MulLo(-S32(r1 & 0x7fff), S32(r2 & 1))));
    }
    Ret(c, Ld32(node + 0x18));
}

// fn_35508(node): put the modulator on the list (at most 10; flag node[5] bit 0 = listed). Returns 1 listed / already,
// 0 when the list is full.
void fn_35508(IopContext& c) {
    const uint32_t node = A0(c);
    if (Ld8(node + 5) & 1) {
        Ret(c, 1);
        return;
    }
    const uint32_t n = Ld32(kModListCount);
    if (n == 0xa) {
        Ret(c, 0);
        return;
    }
    St32(kModListCount, n + 1);
    St8(node + 5, Ld8(node + 5) | 1);
    const uint32_t head = Ld32(kModListHead);
    St32(node + 0x2c, 0);
    if (head == 0) {
        St32(kModListHead, node);
        Ret(c, 1);
        return;
    }
    uint32_t p = head;
    while (Ld32(p + 0x2c) != 0) p = Ld32(p + 0x2c);
    St32(p + 0x2c, node);
    Ret(c, 1);
}

// fn_35480(node): take the modulator off the list (flag cleared; the count down when it was listed).
void fn_35480(IopContext& c) {
    const uint32_t node = A0(c);
    St8(node + 5, Ld8(node + 5) & 0xfe);
    const uint32_t head = Ld32(kModListHead);
    if (head == node) {
        St32(kModListHead, Ld32(node + 0x2c));
        St32(kModListCount, Ld32(kModListCount) - 1);
        return;
    }
    uint32_t p = head;
    while (p != 0) {
        if (Ld32(p + 0x2c) == node) break;
        p = Ld32(p + 0x2c);
    }
    if (p == 0) return;
    St32(p + 0x2c, Ld32(Ld32(p + 0x2c) + 0x2c));
    St32(kModListCount, Ld32(kModListCount) - 1);
}

// fn_35440(rec): take the handle's four modulators (rec + 0x5c + i * 0x30) off the list.
void fn_35440(IopContext& c) {
    const uint32_t rec = A0(c);
    Frame f(c, 0x20);
    uint32_t node = rec + 0x5c;
    for (int i = 3; i >= 0; --i, node += 0x30) Call(c, 0x35480, node);
}

// fn_353a8(node): start a modulator: waveform 5 (random) seeds its level; the depth (fn_35aa0); under the lock it
// is listed (fn_35508) and stepped once (fn_355a4).
void fn_353a8(IopContext& c) {
    const uint32_t node = A0(c);
    Frame f(c, 0x20);
    if (Ld16(node) == 5) {
        const uint32_t r1 = Call(c, 0x3f440);
        const uint32_t r2 = Call(c, 0x3f440);
        St32(node + 0x1c, 1);
        St32(node + 0x18, U32(MulLo(-S32(r1 & 0x7fff), S32(r2 & 1))));
    }
    Call(c, 0x35aa0, node);
    Call(c, 0x42f58, 0x100);
    if (Call(c, 0x35508, node) != 0) Call(c, 0x355a4, node);
    Call(c, 0x430dc);
}

// fn_4dec0(v): unlink voice v from the active list (G::kActiveVoices; voice[0] = next), clear its state and owner;
// the core's single-voice slot (kActiveSlot0) is cleared when it held v.
void fn_4dec0(IopContext& c) {
    const int32_t v = S32(A0(c));
    const uint32_t voice = G::Voice(v);
    const uint32_t core4 = U32(Div24(v)) * 4;
    const uint32_t head = Ld32(G::kActiveVoices);
    St32(voice + 8, 0);
    St32(voice + 0xc, 0);
    if (voice == head) {
        St32(G::kActiveVoices, Ld32(voice));
    } else {
        uint32_t p = head;
        while (p != 0) {
            if (Ld32(p) == voice) {
                St32(p, Ld32(voice));
                break;
            }
            p = Ld32(p);
        }
    }
    if (Ld32(kActiveSlot0 + 8 + core4) == U32(v)) {
        St32(kActiveSlot0 + 8 + core4, 0xffffffffu);
        St32(kActiveSlot0 + core4, 0xffffffffu);
        St32(kActiveSlot0 + 0x10 + core4, 0);
    }
}

// fn_4de04(v): append voice v to the active list (state 1); a voice already listed is reported (0x52).
void fn_4de04(IopContext& c) {
    const int32_t v = S32(A0(c));
    const uint32_t voice = G::Voice(v);
    Frame f(c, 0x20);
    const uint32_t head = Ld32(G::kActiveVoices);
    St32(voice + 8, 1);
    St32(voice, 0);
    if (head == 0) {
        St32(G::kActiveVoices, voice);
        return;
    }
    auto report = [&]() {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x52, 0, 0, 0);
    };
    if (voice == head) {
        report();
        return;
    }
    uint32_t p = head;
    for (;;) {
        const uint32_t n = Ld32(p);
        if (n == 0) break;
        if (n == voice) {
            report();
            return;
        }
        p = n;
    }
    St32(p, voice);
}

// fn_4c458(v, on): zero VOLL/VOLR of voice v and (on) clear / (off) set its bit in the core's 0x1400 switch, with
// interrupts suspended.
void fn_4c458(IopContext& c) {
    const int32_t v = S32(A0(c));
    const uint32_t on = A1(c);
    Frame f(c, 0x30);
    const int32_t core = Div24(v), bit = v - core * 24;
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    const uint32_t e = U32(core) | (U32(bit) << 1);
    imp::SdSetParam(c, e & 0xffff, 0);
    imp::SdSetParam(c, (e | 0x100) & 0xffff, 0);
    const uint32_t sw = (U32(core) | 0x1400) & 0xffff;
    if (on != 0) {
        const uint32_t cur = imp::SdGetSwitch(c, sw);
        imp::SdSetSwitch(c, sw, cur & ~(1u << bit));
    } else {
        const uint32_t cur = imp::SdGetSwitch(c, sw);
        imp::SdSetSwitch(c, sw, cur | (1u << bit));
    }
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
}

// fn_4c750(grp, prio, keep): allocate a voice from group `grp`'s range (kPrioRanges[grp], grp 0..15) for a note of
// priority `prio`: the first free voice of the range, else the steal candidate (lowest priority byte, then oldest;
// a locked (3) or kept (0x28 & 1) voice is skipped) when its priority is <= `prio`, under 0x7e and not locked -- its
// end callback (voice[0x10](v, owner, 1)) runs and it leaves the active list. The voice gets priority `prio`
// (+0x1a), group `grp` (+0x1b), the keep flag, the tick, a 4-tick silence grace, and is unmuted (fn_4c458).
// Returns the voice or -1.
void fn_4c750(IopContext& c) {
    int32_t grp = S32(A0(c));
    const uint32_t prio = A1(c), keep = A2(c);
    Frame f(c, 0x38);
    if (!(grp < 0x10)) grp = 0xf;
    const uint32_t range = kPrioRanges + U32(grp) * 4;
    const int32_t first = Lds16(range);
    const int32_t end = Lds16(range + 2) + 1;
    for (int32_t v = first; v < end; ++v) {
        const uint32_t voice = G::Voice(v);
        if (Ld32(voice + 8) != 0) continue;
        St32(voice + 8, 2);
        St32(voice + 0x1c, Call(c, 0x43638));
        St8(voice + 0x1a, prio);
        St8(voice + 0x1b, U32(grp));
        St32(voice + 0x28, keep != 0 ? 1u : 0u);
        St8(G::kVoiceSilentTicks + U32(v), 4);
        Call(c, 0x4c458, U32(v), 1);
        Ret(c, U32(v));
        return;
    }
    int32_t best = first;
    for (int32_t v = first + 1; v < end; ++v) {
        const uint32_t cand = G::Voice(v);
        if (Ld32(cand + 8) == 3) continue;
        if (Ld32(cand + 0x28) & 1) continue;
        const uint32_t cur = G::Voice(best);
        const uint32_t pc = Ld8(cand + 0x1a), pb = Ld8(cur + 0x1a);
        if (pc < pb) {
            best = v;
            continue;
        }
        if (pc != pb) continue;
        if (Ld32(cand + 0x1c) < Ld32(cur + 0x1c)) best = v;
    }
    const uint32_t voice = G::Voice(best);
    if (Ld32(voice + 0x28) & 1) {
        Ret(c, 0xffffffffu);
        return;
    }
    const uint32_t vprio = Ld8(voice + 0x1a);
    if (S32(prio) < S32(vprio) || !(vprio < 0x7e) || Ld32(voice + 8) == 3) {
        Ret(c, 0xffffffffu);
        return;
    }
    const uint32_t cb = Ld32(voice + 0x10);
    if (cb != 0) CallAddr(c, cb, U32(best), Ld32(voice + 0xc), 1, U32(end));
    Call(c, 0x4dec0, U32(best));
    St32(voice + 8, 2);
    St32(voice + 0x1c, Call(c, 0x43638));
    St8(voice + 0x1a, prio);
    St32(voice + 0x28, keep != 0 ? 1u : 0u);
    St8(voice + 0x1b, U32(grp));
    St32(voice + 0x10, 0);
    St8(G::kVoiceSilentTicks + U32(best), 4);
    Call(c, 0x4c458, U32(best), 1);
    Ret(c, U32(best));
}

// fn_4ca58(v, noise): program voice v for key-on from its record: VOLL/VOLR (group-scaled), the core's 0x1800 /
// 0x1a00 switches by def[0xe] & 0x10, then either pitch (fn_4dd68 / fn_4dcc8 -> PITCH, SSA = def[0x10], 0x1400
// switch bit cleared) or the noise clock (0x1400 bit set, SdSetCoreAttr 8), ADSR1/2 from def[0xa]/def[0xc], the
// effects-send mask word (kVmix) by def[0xe] & 1, the key-on pending bit, the active list (fn_4de04). Interrupts
// suspended throughout; the tick stamped at voice[0x1c].
void fn_4ca58(IopContext& c) {
    const int32_t v = S32(A0(c));
    const uint32_t noise = A1(c);
    Frame f(c, 0x58);
    const int32_t core = Div24(v), bit = v - core * 24;
    const uint32_t voice = G::Voice(v);
    const uint32_t tick = Call(c, 0x43638);
    St32(voice + 0x1c, tick);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x20));
    St32(f.At(0x2c), suspended);
    const uint32_t e = U32(core) | (U32(bit) << 1);
    const uint32_t vl = Call(c, 0x4ed08, U32(Lds16(voice + 0x20)), U32(Lds8(voice + 0x1b)));
    imp::SdSetParam(c, e & 0xffff, (vl & 0xffff) >> 1);
    const uint32_t vr = Call(c, 0x4ed08, U32(Lds16(voice + 0x22)), U32(Lds8(voice + 0x1b)));
    imp::SdSetParam(c, (e | 0x100) & 0xffff, (vr & 0xffff) >> 1);
    uint32_t def = Ld32(voice + 0x14);
    const uint32_t bitmask = 1u << bit;
    {
        const uint32_t s0 = (U32(core) | 0x1800) & 0xffff, s1 = (U32(core) | 0x1a00) & 0xffff;
        if (!(Ld16(def + 0xe) & 0x10)) {
            imp::SdSetSwitch(c, s0, imp::SdGetSwitch(c, s0) | bitmask);
            imp::SdSetSwitch(c, s1, imp::SdGetSwitch(c, s1) | bitmask);
        } else {
            imp::SdSetSwitch(c, s0, imp::SdGetSwitch(c, s0) & ~bitmask);
            imp::SdSetSwitch(c, s1, imp::SdGetSwitch(c, s1) & ~bitmask);
        }
    }
    const uint32_t sw14 = (U32(core) | 0x1400) & 0xffff;
    if (noise == 0) {
        imp::SdSetSwitch(c, sw14, imp::SdGetSwitch(c, sw14) & ~bitmask);
        def = Ld32(voice + 0x14);
        St32(f.At(0x14), f.At(0x24));
        St32(f.At(0x18), f.At(0x28));
        St32(f.At(0x10), U32(Lds8(voice + 0x19)));
        Call(c, 0x4dd68, def, U32(Lds16(voice + 0x24)), U32(Lds16(voice + 0x26)), U32(Lds8(voice + 0x18)));
        def = Ld32(voice + 0x14);
        const uint32_t pitch = Call(c, 0x4dcc8, U32(Lds8(def + 2)), U32(Lds8(def + 3)), Ld16(f.At(0x24)), U32(Lds16(f.At(0x28))));
        imp::SdSetParam(c, (e | 0x200) & 0xffff, pitch & 0xffff);
        def = Ld32(voice + 0x14);
        imp::SdSetAddr(c, (e | 0x2040) & 0xffff, Ld32(def + 0x10));
    } else {
        const int32_t sh = Div32767(S32(U32(Lds16(voice + 0x24)) << 5));
        def = Ld32(voice + 0x14);
        int32_t clk = Lds8(def + 2) + sh;
        if (clk < 0) clk = 0;
        if (!(clk < 0x40)) clk = 0x3f;
        imp::SdSetSwitch(c, sw14, imp::SdGetSwitch(c, sw14) | bitmask);
        imp::SdSetCoreAttr(c, (U32(core) | 8) & 0xffff, U32(clk) & 0xffff);
    }
    def = Ld32(voice + 0x14);
    imp::SdSetParam(c, (e | 0x300) & 0xffff, Ld16(def + 0xa));
    def = Ld32(voice + 0x14);
    imp::SdSetParam(c, (e | 0x400) & 0xffff, Ld16(def + 0xc));
    def = Ld32(voice + 0x14);
    const uint32_t core4 = U32(core) * 4;
    if (Ld16(def + 0xe) & 1) St32(G::kVmix + core4, Ld32(G::kVmix + core4) | bitmask);
    else St32(G::kVmix + core4, Ld32(G::kVmix + core4) & ~bitmask);
    St32(G::kKonPending + core4, Ld32(G::kKonPending + core4) | bitmask);
    Call(c, 0x4de04, U32(v));
    if (Ld32(f.At(0x2c)) == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x20)));
}

// fn_39b44(v, rec): a sequence voice ended: clear its bit in the handle's two voice masks (rec[0x18]/rec[0x1c]);
// when none remain, no sequence step pends (rec[0x4a] == -1) and it is a root or has a parent: the modulators are
// unlisted and the handle stopped (fn_4144c), the parent likewise when it became idle. Under the lock.
void fn_39b44(IopContext& c) {
    const int32_t v = S32(A0(c));
    const uint32_t rec = A1(c);
    Frame f(c, 0x20);
    const int32_t core = Div24(v), bit = v - core * 24;
    const uint32_t m = rec + 0x18 + U32(core) * 4;
    St32(m, Ld32(m) & ~(1u << bit));
    if (Ld32(rec + 0x18) != 0 || Ld32(rec + 0x1c) != 0) return;
    const int32_t step = Lds16(rec + 0x4a);
    if (step != -1) return;
    if (Ld32(rec + 0x28) == 0 && Ld32(rec + 0x2c) != 0) return;
    Call(c, 0x42f58, 0x10);
    const uint32_t parent = Ld32(rec + 0x28);
    Call(c, 0x35440, rec);
    Call(c, 0x4144c, rec, 0);
    if (parent != 0 && Ld32(parent + 0x2c) == 0 && Ld32(parent + 0x18) == 0 && Ld32(parent + 0x1c) == 0 && Lds16(parent + 0x4a) == step) {
        Call(c, 0x35440, parent);
        Call(c, 0x4144c, parent, 0);
    }
    Call(c, 0x430dc);
}

// fn_37a44(rec, entry, *index): the sequence note-on event: the definition = rec[0x58]->bank[0x34] + (*index &
// 0xffffff); the handle's volume word (rec[0x38] + 0x7f clamped 0..0x7f) and pan (rec[0x3c] + rec[0x3e] wrapped);
// a voice (fn_4c564 for a noise definition, else fn_4c750 by the definition's priority byte) joins the handle's mask,
// gets the owner / end callback / definition, its volume byte (def[1]: a value, the handle's rec[0x4c + ~v] for
// -1..-4, random for -5, a table byte below), its pan (def[4] likewise, scaled by 360/127), the pitch bytes, the
// law (fn_4e628 sequence form) and the key-on (fn_4ca58). Under the voice-table lock. Returns 0.
void fn_37a44(IopContext& c) {
    const uint32_t rec = A0(c), entry = A1(c), index_ptr = A2(c);
    Frame f(c, 0x50);
    if (Ld8(rec + 0x16) & 0x20) {
        Ret(c, 0);
        return;
    }
    const uint32_t def = Ld32(Ld32(rec + 0x58) + 0x34) + (Ld32(index_ptr) & 0xffffffu);
    uint32_t vol = Ld16(rec + 0x38) + 0x7f;
    St16(rec + 0x10, vol);
    if (!(static_cast<int16_t>(static_cast<uint16_t>(vol)) < 0x80)) St16(rec + 0x10, 0x7f);
    if (Lds16(rec + 0x10) < 0) St16(rec + 0x10, 0);
    uint32_t pan = Ld16(rec + 0x3e) + Ld16(rec + 0x3c);
    St16(rec + 0x12, pan);
    if (!(static_cast<int16_t>(static_cast<uint16_t>(pan)) < 0x168)) {
        uint32_t a0;
        do {
            a0 = pan - 0x168;
            pan = a0;
        } while (!(static_cast<int16_t>(static_cast<uint16_t>(a0)) < 0x168));
        St16(rec + 0x12, a0);
    }
    if (Lds16(rec + 0x12) < 0) {
        int32_t a0 = Lds16(rec + 0x12);
        int32_t v1;
        do {
            v1 = a0 + 0x168;
            a0 = v1;
        } while (S32(U32(v1) << 16) < 0);
        St16(rec + 0x12, U32(v1));
    }
    const uint32_t prio = Call(c, 0x39d2c, U32(Lds16(rec + 0x3a)), def);
    Call(c, 0x4c1ac, 1, 0x1a);
    const uint32_t dflags = Ld16(def + 0xe);
    const int32_t v = S32((dflags & 8) ? Call(c, 0x4c564, U32(Lds8(entry + 1)), prio, dflags & 0x20)
                                      : Call(c, 0x4c750, U32(Lds8(entry + 1)), prio, dflags & 0x20));
    if (v == -1) {
        Call(c, 0x4c2f8);
        Ret(c, 0);
        return;
    }
    const int32_t core = Div24(v), bit = v - core * 24;
    const uint32_t m = rec + 0x18 + U32(core) * 4;
    St32(m, Ld32(m) | (1u << bit));
    const uint32_t voice = G::Voice(v);
    St32(voice + 0xc, rec);
    St32(voice + 0x10, kVoiceEndCallback);
    St32(voice + 0x14, def);
    {
        const int32_t vb = Lds8(def + 1);
        if (vb >= 0) {
            St8(voice + 0x2c, Ld8(def + 1));
        } else if (!(vb < -4)) {
            St8(voice + 0x2c, Ld8(rec + 0x4c + U32(~vb)));
        } else if (vb == -5) {
            St8(voice + 0x2c, Call(c, 0x3f440) & 0x7f);
        } else {
            St8(voice + 0x2c, Ld8(kDefByteTable - U32(vb)));
        }
    }
    if (Lds8(voice + 0x2c) < 0) St8(voice + 0x2c, 0);
    {
        const int32_t pb = Lds16(def + 4);
        if (pb >= 0) {
            St16(voice + 0x2e, Ld16(def + 4));
        } else if (!(pb < -4)) {
            const int32_t a1 = Lds8(rec + 0x4c + U32(~pb));
            St16(voice + 0x2e, U32(Div127(MulLo(a1, 360))));
        } else if (pb == -5) {
            const uint32_t r = Call(c, 0x3f440);
            const uint32_t q = MulHiU(r >> 3, 0x16c16c17u) >> 2;
            St16(voice + 0x2e, r - q * 360);
        } else {
            const int32_t a1 = Lds8(kDefByteTable - U32(pb));
            St16(voice + 0x2e, U32(Div127(MulLo(a1, 360))));
        }
    }
    if (Lds16(voice + 0x2e) < 0) {
        uint32_t w;
        do {
            w = Ld16(voice + 0x2e) + 0x168;
            St16(voice + 0x2e, w);
        } while (S32(w << 16) < 0);
    }
    if (!(Lds16(voice + 0x2e) < 0x168)) {
        uint32_t w;
        do {
            w = Ld16(voice + 0x2e) - 0x168;
            St16(voice + 0x2e, w);
        } while (!(static_cast<int16_t>(static_cast<uint16_t>(w)) < 0x168));
    }
    St8(voice + 0x18, Ld8(rec + 0x34));
    St8(voice + 0x19, Ld8(rec + 0x35));
    St16(voice + 0x24, Ld16(rec + 0x36));
    St8(voice + 0x1a, prio);
    St16(voice + 0x26, Ld16(rec + 0x14));
    St8(voice + 0x1b, Ld8(entry + 1));
    int32_t pan_in;
    if (Ld16(Ld32(rec + 4) + 6) & 4) pan_in = S32(Call(c, 0x39c98, U32(Lds16(voice + 0x2e)), U32(Lds16(rec + 0x3a)), entry));
    else pan_in = Lds16(voice + 0x2e);
    St16(voice + 0x22, 0);
    St16(voice + 0x20, 0);
    St32(f.At(0x18), U32(pan_in));
    St32(f.At(0x1c), voice + 0x20);
    St32(f.At(0x20), 0xa);
    St32(f.At(0x10), U32(Lds16(rec + 0x12)));
    St32(f.At(0x14), U32(Lds8(voice + 0x2c)));
    const int32_t t0 = Lds16(rec + 0x3a);
    Call(c, 0x4e628, U32((S32(U32(t0) << 7) - t0) >> 10), U32(Lds16(rec + 0xc)), 0, U32(Lds16(rec + 0x10)));
    Call(c, 0x4ca58, U32(v), (Ld16(def + 0xe) >> 3) & 1);
    Call(c, 0x4c2f8);
    Ret(c, 0);
}

}  // namespace ps2r::title::sly2::snd989
