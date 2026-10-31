// snd989_param.cpp -- the per-handle parameter path: volume / pan setters for the two handle kinds (fn_3fba0 ->
// fn_4b61c streams, fn_393b0 sequences), the per-voice volume+pan law (fn_4e628), pitch (fn_4dd68 / fn_4dcc8 /
// fn_398cc), the auto-pan (fn_35c14 + its sound-list node fn_35f48), the modulators (fn_355a4 / fn_35aa0), the
// stream chunk callbacks (fn_47950), fno 33 (fn_33a04 -> fn_3fc34), and the sound-list node helpers (fn_42380 /
// fn_4234c / fn_42448 / fn_423b8 / fn_42400). Ported from the recompiled 989snd_Library over the module's RAM.
//
// Handle record `rec` (0x78 bytes, fn_417f8): rec[4] the first voice record (streams) / the sound definition
// (sequences), rec[8] the sound-list head (nodes {flags, a, b, next, handler(0x10), .., target(0x20)}), rec[0xc]
// the definition's volume (s16), rec[0x10] / rec[0x12] current volume / pan (s16), rec[0x16] flags (2 = paused),
// rec[0x17] the volume group, rec[0x2c] the child list (-> child[0] handle, child[0x30] next), rec[0x38] the
// helper voice, rec[0x3a] the requested volume, rec[0x3c] / rec[0x3e] pan base / offset, rec[0x40..0x46] the
// pitch modulation words. The voice table (G::Voice): +0x14 the voice's definition, +0x1a priority, +0x1b group,
// +0x20 / +0x22 VOLL / VOLR before group scaling, +0x24 / +0x26 the pitch words, +0x2c / +0x2e pan words.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

#include <cstdio>

namespace ps2r::title::sly2::snd989 {

namespace {
constexpr uint32_t kPanAngleTable = G::kData + 0x1be4;        // pointer to the 0xb4-entry (L, R) s16 table
constexpr uint32_t kVolumeLaw = G::kData + 0x1918;            // non-zero: every input is squared (flags = -1)
constexpr uint32_t kPanMode = G::kData + 0x2518;              // s16: 1 = mono (no pan), 2 = ...
constexpr uint32_t kWaveTable = G::kData + 0x1800;            // modulator waveform index -> function
constexpr uint32_t kModJumpTable = G::kData - 0x35c;          // 0x4fca4: fn_355a4's six case targets (rodata)
constexpr uint32_t kNodePool = G::kData + 0x78b8;             // 64 sound-list nodes of 0x24 (fn_423b8)
constexpr uint32_t kFadeHandler = 0x35f48;                    // the auto-pan node's handler

inline int32_t Abs(int32_t v) { return v < 0 ? -v : v; }
// 0..0x167 wrap of an angle, the compiled loops verbatim (fn_4b3d8 / fn_393b0 / fn_4e628 share the shape).
inline int32_t WrapAngle(int32_t a) {
    if (!(a < 0x168)) {
        a -= 0x168;
        bool lt;
        do {
            lt = a < 0x168;
            a -= 0x168;
        } while (!lt);
        a += 0x168;
    }
    if (a < 0) {
        a += 0x168;
        bool neg;
        do {
            neg = a < 0;
            a += 0x168;
        } while (neg);
        a -= 0x168;
    }
    return a;
}
}  // namespace

// fn_42380(list, mask): the first sound-list node of `list` (head at list[8], next at node[0xc]) whose flags carry
// a bit of `mask`, or 0.
void fn_42380(IopContext& c) {
    const uint32_t list = A0(c), mask = A1(c);
    uint32_t n = Ld32(list + 8);
    while (n != 0) {
        if (Ld32(n) & mask) break;
        n = Ld32(n + 0xc);
    }
    Ret(c, n);
}

// fn_4234c(list [, mask in $a1 as the caller left it]): unlink the first node matching the mask (fn_42448).
void fn_4234c(IopContext& c) {
    const uint32_t list = A0(c), mask = A1(c);
    const uint32_t n = Call(c, 0x42380, list, mask);
    if (n != 0) Call(c, 0x42448, list, n);
}

// fn_42448(list, node): clear the node's in-use bit and unlink it from the list (interrupts suspended).
void fn_42448(IopContext& c) {
    const uint32_t list = A0(c), node = A1(c);
    if (node == 0) return;
    Frame f(c, 0x28);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    St32(node, Ld32(node) & ~1u);
    const uint32_t head = Ld32(list + 8);
    if (node == head) {
        St32(list + 8, Ld32(node + 0xc));
    } else {
        uint32_t p = head;
        while (p != 0) {
            const uint32_t q = Ld32(p + 0xc);
            if (q == node) {
                St32(p + 0xc, Ld32(q + 0xc));
                p = 0;
            } else {
                p = q;
            }
        }
    }
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
}

// fn_423b8(): a free node from the 64-entry pool (flags bit 0 clear): flags = 1, next = 0; or 0 when none.
void fn_423b8(IopContext& c) {
    uint32_t n = kNodePool;
    for (int i = 1; ; ++i) {
        if (!(Ld32(n) & 1)) {
            St32(n, 1);
            St32(n + 0xc, 0);
            Ret(c, n);
            return;
        }
        if (!(i < 0x40)) break;
        n += 0x24;
    }
    Ret(c, 0);
}

// fn_42400(list, node): append the node at the list's tail.
void fn_42400(IopContext& c) {
    const uint32_t list = A0(c), node = A1(c);
    uint32_t p = Ld32(list + 8);
    if (p == 0) {
        St32(list + 8, node);
        return;
    }
    while (Ld32(p + 0xc) != 0) p = Ld32(p + 0xc);
    St32(p + 0xc, node);
}

// fn_4b394(cur, req): the stream volume request -> 0..0x7f: negative = its magnitude, 0x7fffffff = keep `cur`,
// else req * 127 / 1024; clamped at 0x7f.
void fn_4b394(IopContext& c) {
    const int32_t cur = S32(A0(c));
    int32_t v = S32(A1(c));
    if (v < 0) v = -v;
    else if (v == 0x7fffffff) v = cur;
    else v = (S32(U32(v) << 7) - v) >> 10;
    if (!(v < 0x80)) v = 0x7f;
    Ret(c, U32(v));
}

// fn_4b3d8(cur, req): the stream pan request -> an angle 0..0x167: -1 = 0, -2 = keep `cur`, else req wrapped.
void fn_4b3d8(IopContext& c) {
    const int32_t cur = S32(A0(c));
    int32_t v = S32(A1(c));
    if (v == -1) v = 0;
    else if (v == -2) v = cur;
    bool lt;
    do {
        lt = v < 0x168;
        v -= 0x168;
    } while (!lt);
    v += 0x168;
    if (v < 0) {
        v += 0x168;
        bool neg;
        do {
            neg = v < 0;
            v += 0x168;
        } while (neg);
        v -= 0x168;
    }
    Ret(c, U32(v));
}

// fn_4dcc8(note, fine, a2, a3): SdNote2Pitch of |note| (a negative note is passed negated), the result scaled by
// 44100/48000 for a non-negative note. Returns the 16-bit pitch.
void fn_4dcc8(IopContext& c) {
    const int32_t note = static_cast<int8_t>(A0(c));
    const int32_t fine = static_cast<int8_t>(A1(c));
    const uint32_t a2 = A2(c) & 0xffff;
    const int32_t a3 = static_cast<int16_t>(A3(c));
    Frame f(c, 0x18);
    const bool scale = note >= 0;
    const int32_t mag = scale ? note : static_cast<int8_t>(static_cast<uint8_t>(0u - U32(note)));
    uint32_t r = Import(c, "libsd", 13, U32(mag) & 0xffff, U32(fine) & 0xffff, a2, U32(a3));
    if (scale) r = MulHiU(44100u * r, 0x057619f1u) >> 10;
    Ret(c, r & 0xffff);
}

// fn_4dd68(def, mod, a2, a3, base, *note_out, *fine_out): the pitch words of a voice: (a3 << 7) + base + a2 plus
// the definition's bend range (def[8] down / def[9] up) scaled by `mod`; split into a note (/128) and the rest.
void fn_4dd68(IopContext& c) {
    const uint32_t def = A0(c);
    const int32_t mod = S32(A1(c));
    const int32_t a2 = S32(A2(c)), a3 = S32(A3(c));
    const int32_t base = S32(StackArg(c, 0));
    const uint32_t note_out = StackArg(c, 1), fine_out = StackArg(c, 2);
    int32_t acc = S32(U32(a3) << 7) + base + a2;
    if (mod < 0) {
        int32_t v = MulLo(static_cast<int8_t>(Ld8(def + 8)), S32(U32(mod) << 7));
        if (v < 0) v += 0x7fff;
        acc += v >> 15;
    } else {
        const int32_t v = MulLo(static_cast<int8_t>(Ld8(def + 9)), S32(U32(mod) << 7));
        acc += Div32767(v);
    }
    const int32_t note = (acc < 0 ? acc + 0x7f : acc) >> 7;
    St32(note_out, U32(note));
    St32(fine_out, U32(acc - S32(U32(note) << 7)));
}

// fn_3fba0(h, volume, pan): the handle's volume/pan request, under the lock, by handle type (4 = stream, 5 =
// sequence; the type is bits 24..28 of the handle word).
void fn_3fba0(IopContext& c) {
    const uint32_t h = A0(c), vol = A1(c), pan = A2(c);
    const uint32_t type = (h >> 24) & 0x1f;
    Call(c, 0x42f58, 0x501);
    if (type == 4) Call(c, 0x4b61c, h, vol, pan);
    else if (type == 5) Call(c, 0x393b0, h, vol, pan, 0);
    Call(c, 0x430dc);
}

// fn_4b61c(h, volume, pan): a stream handle's volume/pan: the request words (fn_4b394 / fn_4b3d8), then every
// voice record's VOLL/VOLR through the law (fn_4e628) and the group scale (fn_4ed08); the helper voice mirrors
// the first record with L/R swapped. Under the lock and the voice-table lock.
void fn_4b61c(IopContext& c) {
    const uint32_t h = A0(c);
    const int32_t vol = S32(A1(c));
    const uint32_t pan = A2(c);
    Frame f(c, 0x50);
    Call(c, 0x42f58, 0x5a);
    const uint32_t rec = Call(c, 0x417f8, h);
    if (rec == 0) {
        Call(c, 0x430dc);
        return;
    }
    if (vol >= 0) Call(c, 0x4234c, rec, 2);
    St16(rec + 0x10, Call(c, 0x4b394, U32(Lds16(rec + 0x10)), U32(vol)));
    const uint32_t p = Call(c, 0x4b3d8, U32(Lds16(rec + 0x12)), pan);
    uint32_t st = Ld32(rec + 4);
    St16(rec + 0x12, p);
    if (st == 0) {
        Call(c, 0x430dc);
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x4d, 0, 0, 0);
        return;
    }
    const uint32_t vlock = Call(c, 0x4c1ac, 1, 0x5d);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x2c));
    uint32_t n = 0;
    for (; st != 0; st = Ld32(st + 0xc8), ++n) {
        if (!(Ld32(st) & 4)) continue;
        const int32_t v = Lds32(st + 0x7c);
        if (v < 0 || !(v < 0x30)) continue;
        const uint32_t voice = G::Voice(v);
        St32(f.At(0x14), 0x7f);
        St32(f.At(0x10), U32(Lds16(st + 6)));
        St32(f.At(0x18), U32(Lds16(rec + 0x4c + U32(Lds8(st + 5)) * 2)));
        St32(f.At(0x20), 4);
        St32(f.At(0x1c), voice + 0x20);
        Call(c, 0x4e628, U32(Lds8(rec + 0x3c + U32(Lds8(st + 5)))), U32(Lds16(rec + 0x10)), U32(Lds16(rec + 0x12)),
             U32(Lds8(st + 4)));
        if (n == 0) {
            const int32_t helper = Lds32(rec + 0x38);
            if (helper != -1) {
                St16(G::Voice(helper) + 0x22, Ld16(voice + 0x20));
                St16(G::Voice(helper) + 0x20, Ld16(voice + 0x22));
            }
        }
        if (Ld8(rec + 0x16) & 2) continue;
        const uint32_t vl = (Call(c, 0x4ed08, U32(Lds16(voice + 0x20)), U32(Lds8(voice + 0x1b))) & 0xffff) >> 1;
        St16(f.At(0x28), vl);
        const uint32_t vr = (Call(c, 0x4ed08, U32(Lds16(voice + 0x22)), U32(Lds8(voice + 0x1b))) & 0xffff) >> 1;
        St16(f.At(0x2a), vr);
        imp::SdSetParam(c, VoiceEntry(v) & 0xffff, Ld16(f.At(0x28)));
        imp::SdSetParam(c, (VoiceEntry(v) | 0x100) & 0xffff, Ld16(f.At(0x2a)));
        if (n == 0) {
            const int32_t helper = Lds32(rec + 0x38);
            if (helper != -1) {
                imp::SdSetParam(c, (VoiceEntry(helper) | 0x100) & 0xffff, Ld16(f.At(0x28)));
                imp::SdSetParam(c, VoiceEntry(helper) & 0xffff, Ld16(f.At(0x2a)));
            }
        }
    }
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x2c)));
    if (vlock != 0) Call(c, 0x4c2f8);
    Call(c, 0x430dc);
}

// fn_4e628(v0, v1, pan, v2, pan_b, v3, pan_c, out, flags): the voice volume law. The four volume inputs (v0 s8,
// v1 s16, v2 s8, v3 = 0x7f or the voice's own) are squared per `flags` (all of them under kVolumeLaw), combined
// as v1 * 258 * v3 * v2 * v0 / 127^3; a zero result clears out[0..1]; kPanMode 1 writes it to both; else the pan
// angle (pan + pan_b + pan_c, wrapped, rotated by 90) picks (L, R) from the 180-entry table, scaled /16383, and
// the signs of the previous out words steer the new ones (a sign change on one side scales by the ratio).
void fn_4e628(IopContext& c) {
    int32_t t3 = S32(A0(c)), a1 = S32(A1(c)), a2 = S32(A2(c)), t1 = S32(A3(c));
    int32_t a0 = S32(StackArg(c, 1));
    int32_t t0 = S32(StackArg(c, 4));
    const uint32_t t2 = StackArg(c, 3);
    const int32_t pan_b = S32(StackArg(c, 0)), pan_c = S32(StackArg(c, 2));
    if (Ld32(kVolumeLaw) != 0) t0 = -1;
    if (t0 & 1) t3 = Div127(MulLo(t3, t3));
    if (t0 & 2) a1 = Div127(MulLo(a1, a1));
    if (t0 & 4) t1 = Div127(MulLo(t1, t1));
    if (t0 & 8) a0 = Div127(MulLo(a0, a0));
    int32_t a3 = MulLo(a1, 258);
    a3 = Div127(MulLo(a3, a0));
    a3 = Div127(MulLo(a3, t1));
    a3 = Div127(MulLo(a3, t3));
    if (a3 == 0) {
        St16(t2, 0);
        St16(t2 + 2, 0);
        return;
    }
    if (Lds16(kPanMode) == 1) {
        St16(t2, U32(a3));
        St16(t2 + 2, U32(a3));
        return;
    }
    a2 += pan_c + pan_b;
    a2 = WrapAngle(a2);
    a2 = (a2 < 0x10e) ? a2 + 0x5a : a2 - 0x10e;
    const uint32_t tbl = Ld32(kPanAngleTable);
    const int32_t out0 = Lds16(t2), out1 = Lds16(t2 + 2);
    if (a2 < 0xb4) {
        const uint32_t e = tbl + U32(a2) * 4;
        int32_t l = Div16383(MulLo(a3, Lds16(e)));
        int32_t r = Div16383(MulLo(a3, Lds16(e + 2)));
        if (out0 < 0 && out1 < 0) {
            l = -l;
            r = -r;
        }
        if ((out0 < 0) != (out1 < 0)) {
            // One side changed sign: scale by the sign of the larger previous magnitude.
            const int32_t m0 = Abs(out0), m1 = Abs(out1);
            if (m1 < m0) {
                if (m0 == 0) {
                    std::printf("[IOP-RECOMP] BREAK at 0x4e920\n");
                    return;
                }
                const int32_t q = MipsDiv(out0, m0);
                l = MulLo(l, q);
                r = MulLo(r, q);
            } else {
                if (m1 == 0) {
                    std::printf("[IOP-RECOMP] BREAK at 0x4e950\n");
                    return;
                }
                const int32_t q = MipsDiv(out1, m1);
                l = MulLo(l, q);
                r = MulLo(r, q);
            }
        }
        St16(t2, U32(l));
        St16(t2 + 2, U32(r));
        return;
    }
    a2 -= 0xb4;
    const uint32_t e = tbl + U32(a2) * 4;
    int32_t r = Div16383(MulLo(a3, Lds16(e)));
    int32_t l = Div16383(MulLo(a3, Lds16(e + 2)));
    if (Lds16(kPanMode) != 2) {
        // The rear half-plane: one side is negated -- the side the previous words single out, else the
        // smaller of the two (0x4ea34: the fall-through of both compares negates L).
        if (out0 >= 0) {
            if (out1 >= 0) {
                if (r < l) r = -r;
                else l = -l;
            } else {
                r = -r;
            }
        } else if (out1 >= 0) {
            l = -l;
        } else if (l < r) {
            r = -r;
        } else {
            l = -l;
        }
    }
    St16(t2, U32(l));
    St16(t2 + 2, U32(r));
}

// fn_39c98(angle, volume): the sequence voice pan law: volume >= 0x356 keeps the angle; <= 0xaa centres it;
// between, the angle is pulled towards 0 / 0x168 by (volume - 0xaa) / 0x2ac.
void fn_39c98(IopContext& c) {
    int32_t a0 = S32(A0(c));
    const int32_t vol = S32(A1(c));
    if (vol < 0x356) {
        if (!(0xaa < vol)) {
            a0 = 0;
        } else if (a0 < 0xb5) {
            a0 = MipsDiv(MulLo(a0, vol - 0xaa), 0x2ac);
        } else {
            a0 = 0x168 - MipsDiv(MulLo(0x168 - a0, vol - 0xaa), 0x2ac);
        }
    }
    Ret(c, U32(a0));
}

// fn_39d2c(volume, def): a voice's priority byte from its definition (def[0] s8, def[0xe] bits 1..2 the volume
// sensitivity: 1 = /5, 2 = /2, 3 = full): the share is scaled down by (0x3b6 - volume) / 0x3b6 under 0x3b7.
void fn_39d2c(IopContext& c) {
    const int32_t vol = S32(A0(c));
    const uint32_t def = A1(c);
    const int32_t base = static_cast<int8_t>(Ld8(def));
    const uint32_t fl = Ld16(def + 0xe);
    if ((fl & 6) == 0) {
        Ret(c, U32(base));
        return;
    }
    const uint32_t sel = (fl >> 1) & 3;
    int32_t share;
    if (sel == 1) share = DivMagic(base, 0x66666667u, 1);
    else if (sel == 2) share = (base + S32(U32(base) >> 31)) >> 1;
    else share = base;
    int32_t out = base;
    if (share != 0 && vol < 0x3b7) {
        const int32_t p = MulLo(share, vol);
        const int32_t q = (MulHi(p, S32(0x44fc3a35u)) >> 8) - (p >> 31);
        out = base - (share - q);
    }
    Ret(c, U32(static_cast<int8_t>(static_cast<uint8_t>(U32(out)))));
}

// fn_393b0(h, volume, pan, no_children): a sequence handle's volume/pan. volume: negative = its magnitude,
// 0x7fffffff = keep, else set (and the volume node unlinked); pan: -1 = the definition's, -2 = keep, else set.
// Nothing changes -> return. Children (rec[0x2c] list) get the scaled volume recursively unless `no_children`.
// Then every owned voice (fn_42064) gets VOLL/VOLR through the law (fn_4e628, the sequence form) and its
// priority (fn_39d2c), under the voice-table lock with interrupts suspended.
void fn_393b0(IopContext& c) {
    const uint32_t h = A0(c);
    const int32_t vol = S32(A1(c));
    const int32_t pan = S32(A2(c));
    const uint32_t no_children = A3(c);
    Frame f(c, 0x60);
    uint32_t changed = 0;
    const uint32_t rec = Call(c, 0x417f8, h);
    if (rec == 0) return;
    const uint32_t def = Ld32(rec + 4);
    St32(f.At(0x34), def);
    if (vol < 0) {
        St16(rec + 0x3a, U32(-vol));
        changed = 1;
    } else if (vol != 0x7fffffff) {
        Call(c, 0x4234c, rec, 2);
        St16(rec + 0x3a, U32(vol));
        changed = 1;
    }
    if (pan == -1) St16(rec + 0x3e, Ld16(def + 2));
    else if (pan != -2) St16(rec + 0x3e, U32(pan));
    int32_t v = Lds16(rec + 0x38) + 0x7f;
    if (!(v < 0x80)) v = 0x7f;
    if (v < 0) v = 0;
    int32_t p = Lds16(rec + 0x3e) + Lds16(rec + 0x3c);
    const uint32_t cur_v = Ld16(rec + 0x10);
    p = WrapAngle(p);
    if (v == static_cast<int16_t>(cur_v) && p == Lds16(rec + 0x12) && changed == 0) return;
    if (no_children == 0) {
        for (uint32_t child = Ld32(rec + 0x2c); child != 0; child = Ld32(child + 0x30)) {
            const int32_t cv = Div127(MulLo(Lds16(rec + 0x3a), Lds16(rec + 0xc)));
            Call(c, 0x393b0, Ld32(child), U32(cv), U32(pan), no_children);
        }
    }
    St16(rec + 0x10, U32(v));
    St16(rec + 0x12, U32(p));
    const uint32_t vlock = Call(c, 0x4c1ac, 1, 0x1b);
    St32(f.At(0x30), vlock);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x2c));
    uint32_t next = 0;
    for (int32_t vi = S32(Call(c, 0x42064, rec, next)); vi != -1; vi = S32(Call(c, 0x42064, rec, next))) {
        const uint32_t voice = G::Voice(vi);
        int32_t pan_in;
        if (Ld16(Ld32(rec + 4) + 6) & 4) pan_in = S32(Call(c, 0x39c98, U32(Lds16(voice + 0x2e)), U32(Lds16(rec + 0x3a)), Ld32(f.At(0x34))));
        else pan_in = Lds16(voice + 0x2e);
        const int32_t t0 = Lds16(rec + 0x3a);
        St32(f.At(0x18), U32(pan_in));
        St32(f.At(0x1c), voice + 0x20);
        St32(f.At(0x20), 0xa);
        St32(f.At(0x10), U32(Lds16(rec + 0x12)));
        St32(f.At(0x14), U32(Lds8(voice + 0x2c)));
        Call(c, 0x4e628, U32((S32(U32(t0) << 7) - t0) >> 10), U32(Lds16(rec + 0xc)), 0, U32(Lds16(rec + 0x10)));
        if (!(Ld8(rec + 0x16) & 2)) {
            const uint32_t vl = (Call(c, 0x4ed08, U32(Lds16(voice + 0x20)), U32(Lds8(rec + 0x17))) & 0xffff) >> 1;
            St16(f.At(0x28), vl);
            const uint32_t vr = (Call(c, 0x4ed08, U32(Lds16(voice + 0x22)), U32(Lds8(rec + 0x17))) & 0xffff) >> 1;
            St16(f.At(0x2a), vr);
            imp::SdSetParam(c, VoiceEntry(vi) & 0xffff, Ld16(f.At(0x28)));
            imp::SdSetParam(c, (VoiceEntry(vi) | 0x100) & 0xffff, Ld16(f.At(0x2a)));
        }
        const uint32_t prio = Call(c, 0x39d2c, U32(Lds16(rec + 0x3a)), Ld32(voice + 0x14));
        next = U32(vi + 1);
        St8(voice + 0x1a, prio);
    }
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x2c)));
    if (Ld32(f.At(0x30)) != 0) Call(c, 0x4c2f8);
}

// fn_398cc(rec): a sequence handle's pitch words -> every owned voice: rec[0x14] = rec[0x44] + rec[0x46],
// rec[0x36] = clamp16(rec[0x40] + rec[0x42]); per voice (fn_42064) the pitch (fn_4dd68 -> fn_4dcc8 -> PITCH) or,
// for a noise definition (def[0xe] & 8), the core's noise clock (SdSetCoreAttr 8). Paused handles only store.
void fn_398cc(IopContext& c) {
    const uint32_t rec = A0(c);
    Frame f(c, 0x50);
    St16(rec + 0x14, Ld16(rec + 0x46) + Ld16(rec + 0x44));
    const int32_t sum = Lds16(rec + 0x42) + Lds16(rec + 0x40);
    if (0x7fff < sum) St16(rec + 0x36, 0x7fff);
    else if (sum < -0x8000) St16(rec + 0x36, 0x8000);
    else St16(rec + 0x36, U32(sum));
    const uint32_t vlock = Call(c, 0x4c1ac, 1, 0x1c);
    for (int32_t v = S32(Call(c, 0x42064, rec, 0)); v != -1; v = S32(Call(c, 0x42064, rec, U32(v + 1)))) {
        const uint32_t voice = G::Voice(v);
        St16(voice + 0x24, Ld16(rec + 0x36));
        St16(voice + 0x26, Ld16(rec + 0x14));
        if (Ld8(rec + 0x16) & 2) continue;
        const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x20));
        uint32_t def = Ld32(voice + 0x14);
        if (!(Ld16(def + 0xe) & 8)) {
            St32(f.At(0x14), f.At(0x24));
            St32(f.At(0x18), f.At(0x28));
            St32(f.At(0x10), U32(Lds8(rec + 0x35)));
            Call(c, 0x4dd68, def, U32(Lds16(rec + 0x36)), U32(Lds16(rec + 0x14)), U32(Lds8(rec + 0x34)));
            def = Ld32(voice + 0x14);
            const uint32_t pitch = Call(c, 0x4dcc8, U32(Lds8(def + 2)), U32(Lds8(def + 3)), Ld16(f.At(0x24)), U32(Lds16(f.At(0x28))));
            imp::SdSetParam(c, (VoiceEntry(v) | 0x200) & 0xffff, pitch);
        } else {
            const int32_t sh = Div32767(S32(U32(Lds16(voice + 0x24)) << 5));
            int32_t clk = Lds8(def + 2) + sh;
            if (clk < 0) clk = 0;
            if (!(clk < 0x40)) clk = 0x3f;
            imp::SdSetCoreAttr(c, (U32(Div24(v)) | 8) & 0xffff, U32(clk) & 0xffff);
        }
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x20)));
    }
    if (vlock != 0) Call(c, 0x4c2f8);
}

// fn_35aa0(node): a modulator's depth word node[0x20] from its type (node[2]) and amount (node[6] u16):
// 1 = amount * the handle's first definition byte / 1024, 2 = amount * 45 / 256, 3 = amount * 381 / 64,
// 4 = amount * 32767 / 1024; other types only clear node[0x24].
void fn_35aa0(IopContext& c) {
    const uint32_t node = A0(c);
    const uint32_t type = Ld8(node + 2);
    const int32_t amt = S32(Ld16(node + 6));
    int32_t depth;
    if (type == 1) {
        const uint32_t def = Ld32(Ld32(node + 0x28) + 4);
        depth = MulLo(static_cast<int8_t>(Ld8(def)), amt) >> 10;
    } else if (type == 2) {
        depth = (S32(U32(amt * 3) << 4) - amt * 3) >> 8;
    } else if (type == 3) {
        depth = (S32(U32(amt * 3) << 7) - amt * 3) >> 6;
    } else if (type == 4) {
        depth = (S32(U32(amt) << 15) - amt) >> 10;
    } else {
        St32(node + 0x24, 0);
        return;
    }
    St32(node + 0x20, U32(depth));
    St32(node + 0x24, 0);
}

// fn_355a4(node): one modulator step. The phase node[0xc] advances by node[0x10] * 2 (kept under 0x8000000); the
// waveform function (kWaveTable[node[0]], called with (node, old phase >> 16, $a2, $a3 as this body received
// them)) gives the sample, negated under node[4] & 1, scaled by the depth node[0x20]; the target by type
// (node[2]): 1 -> h[0x38] (volume, (s - 0x7fff) >> 16; returns 1 on change), 2 -> h[0x3c] (>> 15; 1), 3 ->
// h[0x44] (2), 4 -> h[0x40] (2), 5 -> the voice-group record h + node[3] * 48 + 0x5c's word 0x10 (the pitch, a
// fraction of its word 0x14 scaled by node[6]), 6 -> its half-word 6 (+ fn_35aa0 of the record). Returns 0 else.
void fn_355a4(IopContext& c) {
    const uint32_t node = A0(c), a2_in = A2(c), a3_in = A3(c);
    const uint32_t h = Ld32(node + 0x28);
    const uint32_t idx = Ld8(node + 2) - 1;
    if (!(idx < 6)) {
        Ret(c, 0);
        return;
    }
    Frame f(c, 0x20);
    auto phase = [&]() -> int32_t {
        const uint32_t old = Ld32(node + 0xc);
        const uint32_t nw = old + (Ld32(node + 0x10) << 1);
        St32(node + 0xc, nw);
        if (0x7ffffff < S32(nw)) St32(node + 0xc, nw + 0xf8000000u);
        return S32(old) >> 16;
    };
    auto wave = [&]() -> int32_t {
        const int32_t a1 = phase();
        int32_t s = S32(CallAddr(c, Ld32(kWaveTable + Ld16(node) * 4), node, U32(a1), a2_in, a3_in));
        if (Ld8(node + 4) & 1) s = -s;
        return s;
    };
    uint32_t result = 0;
    const uint32_t target = Ld32(kModJumpTable + idx * 4);
    switch (target) {
    case 0x355f4: {
        const int32_t s = wave();
        const int32_t r = MulLo(Lds32(node + 0x20), s - 0x7fff) >> 16;
        if (Lds16(h + 0x38) != r) {
            St16(h + 0x38, U32(r));
            result = 1;
        }
        break;
    }
    case 0x35690: {
        const int32_t s = wave();
        const int32_t r = MulLo(Lds32(node + 0x20), s) >> 15;
        if (Lds16(h + 0x3c) != r) {
            St16(h + 0x3c, U32(r));
            result = 1;
        }
        break;
    }
    case 0x3572c: {
        const int32_t s = wave();
        const int32_t r = MulLo(s, Lds32(node + 0x20)) >> 15;
        if (r != Lds16(h + 0x44)) {
            St16(h + 0x44, U32(r));
            result = 2;
        }
        break;
    }
    case 0x357c8: {
        const int32_t s = wave();
        const int32_t r = MulLo(s, Lds32(node + 0x20)) >> 15;
        if (r != Lds16(h + 0x40)) {
            St16(h + 0x40, U32(r));
            result = 2;
        }
        break;
    }
    case 0x35864: {
        const uint32_t g = h + Ld8(node + 3) * 48 + 0x5c;
        const int32_t s = wave();
        const uint32_t base = Ld32(g + 0x14);
        const uint32_t frac = ((base >> 8) * Ld16(node + 6)) >> 10;
        const int32_t d = S32(U32(MulLo(S32(frac), s) >> 15) << 8);
        St32(g + 0x10, base + U32(d));
        break;
    }
    case 0x35924: {
        const uint32_t g = h + Ld8(node + 3) * 48 + 0x5c;
        const int32_t s = wave();
        const uint32_t frac = (Ld32(g + 8) * Ld16(node + 6)) >> 10;
        const int32_t d = MulLo(S32(frac), s) >> 15;
        St16(g + 6, Ld16(g + 8) + U32(d));
        Call(c, 0x35aa0, g);
        break;
    }
    default:
        std::printf("[IOP-RECOMP] fn_355a4: jump table 0x%x -> 0x%08x not a case\n", idx, target);
        break;
    }
    Ret(c, result);
}

// fn_47950(st, arg, which): a stream voice record's chunk callbacks (the record's hook block at st[0x94]: words
// 8/0xc the position copied from st[0xa0]/st[0xa4], 0x10 / 0x14 two handlers (st hook, handle id, arg), 0x18 a
// semaphore signalled when >= 0). Needs st flags bit 16; `which` != 0 runs handler 0x10, else the position is
// copied, the semaphore signalled and handler 0x14 runs; under the lock. A non-zero handler result is reported
// (0x7d) and stops the owning handle (h[0x16] |= 4). Returns the handler result (0 when nothing ran).
void fn_47950(IopContext& c) {
    const uint32_t st = A0(c), arg = A1(c), which = A2(c), a3_in = A3(c);
    Frame f(c, 0x28);
    uint32_t hook = Ld32(st + 0x94);
    if (!(Ld32(st) & 0x10000) || hook == 0) {
        Ret(c, 0);
        return;
    }
    uint32_t r;
    if (which != 0) {
        if (Ld32(hook + 0x10) == 0) {
            Ret(c, 0);
            return;
        }
        Call(c, 0x42f58, 0x5c);
        r = CallAddr(c, Ld32(hook + 0x10), hook, Ld32(Ld32(st + 0xd0)), arg, a3_in);
        Call(c, 0x430dc);
    } else {
        St32(hook + 8, Ld32(st + 0xa0));
        St32(hook + 0xc, Ld32(st + 0xa4));
        const int32_t sema = Lds32(hook + 0x18);
        if (sema >= 0) imp::SignalSema(c, U32(sema));
        if (Ld32(hook + 0x14) == 0) {
            Ret(c, 0);
            return;
        }
        Call(c, 0x42f58, 0x5d);
        r = CallAddr(c, Ld32(hook + 0x14), hook, Ld32(Ld32(st + 0xd0)), arg, a3_in);
        Call(c, 0x430dc);
    }
    if (r != 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x7d, r, 0, 0);
        const uint32_t h = Ld32(st + 0xd0);
        St8(h + 0x16, Ld8(h + 0x16) | 4);
    }
    Ret(c, r);
}

// fn_35c14(h, target, dir, speed, mode): the auto-pan: rotate the handle's pan towards `target` (an angle) in
// direction `dir` (0 = the shorter way, > 0 up, < 0 down) at `speed` (mode 4: speed is per 180 degrees). A zero
// speed or distance sets the pan at once (fn_3fba0). Otherwise the handle's pan node (sound-list mask 4; a fresh
// pool node with handler fn_35f48 when none) is programmed: node[0x18] step per tick (or node[0x1c] ticks per
// degree with node[8] = 1 and node[0x18] = +-1 when the step would be under 1), node[0x20] the target. mode 3 on
// an existing node rescales the speed by the remaining distance. Under the lock.
void fn_35c14(IopContext& c) {
    const uint32_t h = A0(c);
    const int32_t target = S32(A1(c));
    int32_t dir = S32(A2(c)), speed = S32(A3(c));
    const uint32_t mode = StackArg(c, 0);
    Frame f(c, 0x38);
    uint32_t shortest = 0, fresh = 0;
    Call(c, 0x42f58, 0x9a);
    const uint32_t rec = Call(c, 0x417f8, h);
    if (rec == 0) {
        Call(c, 0x430dc);
        return;
    }
    int32_t dist = target - Lds16(rec + 0x12);
    if (dir == 0) {
        shortest = 1;
        dir = 1;
    }
    if (dir > 0 && dist < 0) dist += 0x168;
    if (dir < 0 && dist > 0) dist -= 0x168;
    if (shortest != 0 && !(Abs(dist) < 0xb5)) {
        dir = -dir;
        dist -= 0x168;
    }
    if (mode == 4) speed = Div180(MulLo(speed, Abs(dist)));
    auto set_now = [&]() {
        Call(c, 0x3fba0, h, 0x7fffffff, U32(target));
        Call(c, 0x430dc);
    };
    if (speed == 0) {
        Call(c, 0x4234c, rec, 4);
        set_now();
        return;
    }
    if (dist == 0) {
        Call(c, 0x4234c, rec, 4);
        Call(c, 0x430dc);
        return;
    }
    uint32_t node = Call(c, 0x42380, rec, 4);
    if (node == 0) {
        node = Call(c, 0x423b8);
        fresh = 1;
        if (node == 0) {
            set_now();
            return;
        }
        St32(node + 0x10, kFadeHandler);
        St32(node, Ld32(node) | 4);
    } else {
        St32(node, Ld32(node) | 0x80000000u);
        if (mode == 3) {
            int32_t rem = target - Lds32(node + 0x20);
            if (dir > 0 && rem < 0) rem += 0x168;
            if (dir < 0 && rem > 0) rem -= 0x168;
            if (rem == 0) {
                Call(c, 0x4234c, rec, 4);
                set_now();
                return;
            }
            speed = Abs(MipsDiv(MulLo(dist, speed), rem));
            if (!(speed > 0)) speed = 1;
        }
    }
    if (Abs(dist) < speed) {
        if (dist == 0) {
            std::printf("[IOP-RECOMP] BREAK at 0x35df4\n");
            return;
        }
        const int32_t q = Abs(MipsDiv(speed, dist));
        St32(node + 0x1c, U32(q));
        St32(node + 8, 1);
        St32(node + 4, U32(q));
        St32(node + 0x18, dist < 0 ? 0xffffffffu : 1u);
    } else {
        if (speed == 0) {
            std::printf("[IOP-RECOMP] BREAK at 0x35ed4\n");
            return;
        }
        St32(node + 0x18, U32(MipsDiv(S32(U32(dist) << 5), speed)));
        St32(node + 8, 0);
        St32(node + 4, 0);
    }
    St32(node + 0x20, U32(target));
    if (fresh != 0) Call(c, 0x42400, rec, node);
    else St32(node, Ld32(node) & 0x7fffffffu);
    Call(c, 0x430dc);
}

// fn_35f48(node, rec): the auto-pan node's tick (the sound-list handler fn_4218c calls): the pan advances by
// node[0x18] (>= 2 in magnitude: 1/32 degree units added to pan * 32; else whole degrees), wrapping through
// 0x168 towards node[0x20]; reaching the target (or passing it) sets the pan to the target and unlinks the node;
// a per-degree node (node[8] == 1) reloads its countdown node[4] from node[0x1c]. Returns 0.
void fn_35f48(IopContext& c) {
    const uint32_t node = A0(c), rec = A1(c);
    Frame f(c, 0x20);
    const int32_t step = Lds32(node + 0x18);
    int32_t pan;
    uint32_t cur;
    if (!(Abs(step) < 2)) {
        cur = Ld16(rec + 0x12);
        pan = S32(U32((Lds16(rec + 0x12) << 5) + step) << 11) >> 16;
    } else {
        cur = Ld16(rec + 0x12);
        pan = static_cast<int16_t>(static_cast<uint16_t>(cur + Ld16(node + 0x18)));
    }
    const bool in_range = pan < 0x168;
    const int32_t cur_s = static_cast<int16_t>(static_cast<uint16_t>(cur));
    int32_t tgt;
    if (!in_range && Lds32(node + 0x20) < cur_s) tgt = static_cast<int16_t>(static_cast<uint16_t>(Ld16(node + 0x20) + 0x168));
    else if (pan < 0 && cur_s < Lds32(node + 0x20)) tgt = static_cast<int16_t>(static_cast<uint16_t>(Ld16(node + 0x20) - 0x168));
    else tgt = Lds16(node + 0x20);
    bool hit = false;
    if (step > 0) hit = tgt < pan && cur_s < tgt;
    if (!hit && step < 0) hit = pan < tgt && tgt < cur_s;
    if (hit) pan = Lds16(node + 0x20);
    else if (pan < 0) pan = static_cast<int16_t>(static_cast<uint16_t>(U32(pan + 0x168)));
    else if (!in_range) pan = static_cast<int16_t>(static_cast<uint16_t>(U32(pan - 0x168)));
    Call(c, 0x3fba0, Ld32(rec), 0x7fffffff, U32(pan));
    if (pan == Lds32(node + 0x20)) {
        Call(c, 0x42448, rec, node);
    } else if (Lds32(node + 8) == 1) {
        St32(node + 4, Ld32(node + 0x1c));
    }
    Ret(c, 0);
}

// fn_3fc34(h, mask, volume, pan, pitch_a, pitch_b): the fno 33 body: mask bit 0 volume, bit 1 pan (both through
// fn_3fba0; bits 1+2 together drop the pan), bit 2 the auto-pan to `pan` (fn_35c14 speed 0x1e mode 4), bit 3
// fn_3ffb0(pitch_a), bit 4 fn_3ff4c(pitch_b). Returns the handle, 0 for an unknown one.
void fn_3fc34(IopContext& c) {
    const uint32_t h = A0(c);
    uint32_t mask = A1(c);
    uint32_t vol = A2(c);
    const uint32_t pan = A3(c);
    const int32_t pa = static_cast<int16_t>(StackArg(c, 0)), pb = static_cast<int16_t>(StackArg(c, 1));
    Frame f(c, 0x38);
    if (Call(c, 0x417f8, h) == 0) {
        Ret(c, 0);
        return;
    }
    if ((mask & 6) == 6) mask &= ~2u;
    uint32_t p = pan;
    if (!(mask & 1)) vol = 0x7fffffff;
    if (!(mask & 2)) p = 0xfffffffeu;
    if (mask & 3) Call(c, 0x3fba0, h, vol, p);
    if (mask & 4) {
        St32(f.At(0x10), 4);
        Call(c, 0x35c14, h, pan, 0, 0x1e);
    }
    if (mask & 8) Call(c, 0x3ffb0, h, U32(pa));
    if (mask & 0x10) Call(c, 0x3ff4c, h, U32(pb));
    Ret(c, h);
}

// fn_33a04(buff): the fno 33 shim: fn_3fc34(buff[0..3], buff[0x10] s16, buff[0x14] s16) -> the reply word.
void fn_33a04(IopContext& c) {
    const uint32_t b = A0(c);
    Frame f(c, 0x20);
    St32(f.At(0x10), U32(Lds16(b + 0x10)));
    St32(f.At(0x14), U32(Lds16(b + 0x14)));
    const uint32_t r = Call(c, 0x3fc34, Ld32(b), Ld32(b + 4), Ld32(b + 8), Ld32(b + 0xc));
    St32(Ld32(G::kResultPtr), r);
}

}  // namespace ps2r::title::sly2::snd989
