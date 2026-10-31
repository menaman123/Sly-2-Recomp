// snd989_seqev.cpp -- the sequence event handlers fn_3926c dispatches through kSeqEventHandlers (each called with
// (rec, def, entry); entry = the 8-byte event record, index words carry a 24-bit sound/modulator index): the
// repeat/loop counters (fn_3821c random, fn_38998 fixed), the random delay (fn_3832c), the random pitch (fn_38378 ->
// fn_397d4 -> fn_398cc), the modulator setup (fn_37f4c), the random volume byte (fn_38578), the end scan (fn_38084),
// and two no-ops (fn_38074 / fn_3807c). Ported from the recompiled 989snd_Library over the module's RAM.
//
// Sequence handle words used here: rec[0x4a] the current step (s16), rec[0x50] a loop is armed (u8), rec[0x52] /
// rec[0x54] the loop's stride and remaining span (s16), rec[0x58] the bank (bank[0x34] = its definitions, bank[0xc]
// its id), rec[0x4c..0x4f] the four volume bytes, rec + 0x5c + i * 0x30 the four modulators.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

#include <cstdio>

namespace ps2r::title::sly2::snd989 {

namespace {
constexpr uint32_t kDefByteTableBase = G::kData + 0x1818;   // + ~index: the global volume bytes a negative index names
}  // namespace

// fn_38074 / fn_3807c: no-op events (return 0).
void fn_38074(IopContext& c) { Ret(c, 0); }
void fn_3807c(IopContext& c) { Ret(c, 0); }

// fn_3821c(rec, def, entry): arm a random repeat: a loop already armed is an error (0xa with the bank id, the bank's
// fn_381d8 word and the step + 1; returns -1). The repeat count entry[0] picks a random target entry[2] (never the
// same twice in a row), the step moves by target * entry[1], the stride entry[1] + 1 and the remaining span
// (count - 1 - target) * entry[1] are recorded. Returns 0.
void fn_3821c(IopContext& c) {
    const uint32_t rec = A0(c), a1_in = A1(c), entry = A2(c), a3_in = A3(c);
    Frame f(c, 0x28);
    if (Lds8(rec + 0x50) != 0) {
        const uint32_t w = Call(c, 0x381d8, Ld32(rec + 0x58), a1_in, entry, a3_in);
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0xa, Ld32(Ld32(rec + 0x58) + 0xc), w, U32(Lds16(rec + 0x4a) + 1));
        Ret(c, 0xffffffffu);
        return;
    }
    const int32_t r = S32(Call(c, 0x3f440));
    const int32_t count = Lds8(entry);
    if (count == 0) {
        std::printf("[IOP-RECOMP] BREAK at 0x38294\n");
        return;
    }
    int32_t target = (count == -1) ? 0 : r % count;
    if (target == Lds8(entry + 2)) {
        target += 1;
        if (target == count) target = 0;
    }
    const int32_t stride = Lds8(entry + 1);
    St8(entry + 2, U32(target));
    St16(rec + 0x4a, Ld16(rec + 0x4a) + U32(MulLo(target, stride)));
    St16(rec + 0x52, U32(stride + 1));
    St8(rec + 0x50, 1);
    St16(rec + 0x54, U32(MulLo(count - 1 - target, stride)));
    Ret(c, 0);
}

// fn_3832c(rec, def, entry): a random delay: random % ((entry[0] & 0xffffff) + 1).
void fn_3832c(IopContext& c) {
    const uint32_t entry = A2(c);
    Frame f(c, 0x18);
    const uint32_t r = Call(c, 0x3f440);
    const uint32_t m = (Ld32(entry) & 0xffffffu) + 1;
    if (m == 0) {
        std::printf("[IOP-RECOMP] BREAK at 0x38364\n");
        return;
    }
    Ret(c, r % m);
}

// fn_397d4(h, cents): a sequence handle's pitch offset rec[0x42] (recursively to its children), then fn_398cc.
void fn_397d4(IopContext& c) {
    const uint32_t h = A0(c);
    const int32_t cents = static_cast<int16_t>(A1(c));
    Frame f(c, 0x20);
    const uint32_t rec = Call(c, 0x417f8, h);
    if (rec == 0) return;
    for (uint32_t child = Ld32(rec + 0x2c); child != 0; child = Ld32(child + 0x30)) Call(c, 0x397d4, Ld32(child), U32(cents));
    St16(rec + 0x42, U32(cents));
    Call(c, 0x398cc, rec);
}

// fn_38378(rec, def, entry): a random pitch offset: a random 0..0x7fff mapped to -0x8000..0x7fff, scaled by
// entry[0] / 100 -> fn_397d4(rec[0], cents). Returns 0.
void fn_38378(IopContext& c) {
    const uint32_t rec = A0(c), entry = A2(c);
    Frame f(c, 0x20);
    const uint32_t r = Call(c, 0x3f440);
    const uint32_t hi = MulHiU(r, 0x20005u);
    const uint32_t q = (hi + ((r - hi) >> 1)) >> 14;
    const uint32_t v0 = (r - ((q << 15) - q)) & 0xffff;
    const int32_t v1 = S32((v0 << 16) - v0);
    const int32_t centred = Div32767(v1) - 0x8000;
    const int32_t p = MulLo(centred, Lds8(entry));
    const int32_t cents = (MulHi(p, S32(0x51eb851fu)) >> 5) - (p >> 31);
    Call(c, 0x397d4, Ld32(rec), U32(static_cast<int16_t>(static_cast<uint16_t>(U32(cents)))));
    Ret(c, 0);
}

// fn_37f4c(rec, def, entry): modulator setup from the definition at bank[0x34] + (*entry & 0xffffff): byte 0 the slot,
// byte 1 the type (0 = off: the modulator is unlisted and its waveform cleared), byte 3 the waveform, byte 2 / byte 8
// the target / flags, half 6 the amount (also the depth seed), word 0xc the rate (phase step); type 2 takes half 4
// as its level; flags & 2 start at a random phase, else half 0xa; then fn_353a8 starts it. Returns 0.
void fn_37f4c(IopContext& c) {
    const uint32_t rec = A0(c), entry = A2(c);
    Frame f(c, 0x18);
    const uint32_t md = Ld32(Ld32(rec + 0x58) + 0x34) + (Ld32(entry) & 0xffffffu);
    const uint32_t node = rec + Ld8(md) * 48 + 0x5c;
    const uint32_t type = Ld8(md + 1);
    St8(node + 2, type);
    if (type == 0) {
        Call(c, 0x35480, node);
        St16(node, 0);
        Ret(c, 0);
        return;
    }
    const uint32_t wave = Ld8(md + 3);
    St16(node, wave);
    St8(node + 2, Ld8(md + 1));
    St8(node + 3, Ld8(md + 2));
    St8(node + 4, Ld8(md + 8));
    St16(node + 6, Ld16(md + 6));
    St32(node + 8, Ld16(md + 6));
    St32(node + 0x10, Ld32(md + 0xc));
    St32(node + 0x18, 0);
    St32(node + 0x24, 0);
    St32(node + 0x14, Ld32(md + 0xc));
    if (wave == 2) St32(node + 0x18, Ld16(md + 4));
    St32(node + 0x1c, 0);
    uint32_t phase;
    if (Ld8(node + 4) & 2) phase = Call(c, 0x3f440) & 0x7ff;
    else phase = Ld16(md + 0xa);
    St32(node + 0xc, phase << 16);
    Call(c, 0x353a8, node);
    Ret(c, 0);
}

// fn_38578(rec, def, entry): a random volume byte in entry[1]..entry[2] into the handle's byte rec[0x4c + entry[0]]
// (a negative entry[0] names a global byte). Returns 0.
void fn_38578(IopContext& c) {
    const uint32_t rec = A0(c), entry = A2(c);
    Frame f(c, 0x20);
    const int32_t span = Lds8(entry + 2) - Lds8(entry + 1) + 1;
    const int32_t r = S32(Call(c, 0x3f440));
    if (span == 0) {
        std::printf("[IOP-RECOMP] BREAK at 0x385b8\n");
        return;
    }
    const int32_t rem = (span == -1) ? 0 : r % span;
    const int32_t v = rem + Lds8(entry + 1);
    const int32_t slot = Lds8(entry);
    if (slot >= 0) St8(rec + 0x4c + U32(slot), U32(v));
    else St8(kDefByteTableBase + U32(~slot), U32(v));
    Ret(c, 0);
}

// fn_38998(rec, def, entry): arm a fixed repeat: a loop already armed is an error (0xd, -1). entry[2] counts the
// pass (wrapping at entry[0]); the step moves by pass * entry[1]; stride entry[1] + 1; span (count - 1 - pass) *
// entry[1]. Returns 0.
void fn_38998(IopContext& c) {
    const uint32_t rec = A0(c), entry = A2(c);
    Frame f(c, 0x20);
    if (Lds8(rec + 0x50) != 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0xd, 0, 0, 0);
        Ret(c, 0xffffffffu);
        return;
    }
    const int32_t pass = Lds8(entry + 2);
    const int32_t count = Lds8(entry);
    const uint32_t next = Ld8(entry + 2) + 1;
    St8(entry + 2, next);
    if (static_cast<int8_t>(static_cast<uint8_t>(next)) == count) St8(entry + 2, 0);
    const int32_t stride = Lds8(entry + 1);
    St16(rec + 0x4a, Ld16(rec + 0x4a) + U32(MulLo(pass, stride)));
    St16(rec + 0x52, U32(stride + 1));
    St8(rec + 0x50, 1);
    St16(rec + 0x54, U32(MulLo(count - 1 - pass, stride)));
    Ret(c, 0);
}

// fn_38084(rec, def): the end-of-track scan backwards from the step before rec[0x4a] for an event of type 0x15 (the
// 8-byte records at def[8]): found -> the step rests there, the handle's bit 0x40 set (already set -> returns 1);
// none -> error 8 and -1. Returns 0 otherwise.
void fn_38084(IopContext& c) {
    const uint32_t rec = A0(c), def = A1(c);
    Frame f(c, 0x20);
    uint32_t again = 0, found = 0;
    int32_t i = Lds16(rec + 0x4a) - 1;
    while (i >= 0) {
        const uint32_t ev = Ld32(def + 8) + U32(i) * 8;
        const int32_t prev = i - 1;
        if (Ld8(ev + 3) == 0x15) {
            const uint32_t fl = Ld8(rec + 0x16);
            found = 1;
            St16(rec + 0x4a, U32(prev));
            if (fl & 0x40) again = 1;
            else St8(rec + 0x16, fl | 0x40);
        }
        i = prev;
        if (found != 0) break;
    }
    if (found != 0) {
        Ret(c, again);
        return;
    }
    St32(f.At(0x10), 0);
    Call(c, 0x35140, 8, 0, 0, 0);
    Ret(c, 0xffffffffu);
}

}  // namespace ps2r::title::sly2::snd989
