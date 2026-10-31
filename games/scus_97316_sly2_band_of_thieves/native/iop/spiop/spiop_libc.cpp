// spiop_libc.cpp -- the libc half of spiop (0x5cc08..0x5f284): the single-precision soft-float routines the
// headset monitor's microphone-level arm uses (fn_5b0cc with a device connected: an RMS over 0x400 samples),
// plus the C++ runtime's allocation-failure handler. WHAT REACHES THEM: nothing on this disc's lanes -- the
// engine's USB bus carries no device, so lgaud:18 reports state 0 forever and the arm is never entered; the
// allocation-failure handler needs AllocSysMemory to fail. They are ported for the module's completeness
// ('ported, unexercised'), each a transliteration of the recompiled body, and checked against the recompiled
// bodies by the differential test spiop/test_spiop_libc.cpp (random and edge inputs, both arms on the same RAM).
//
// The library is gcc's fp-bit.c (FLOAT), the fp_number_type {class, sign, normal_exp, fraction} at 16 bytes
// with THIS version's classes: 0 SNAN, 1 QNAN, 2 ZERO, 3 NUMBER, 4 INFINITY; FRACBITS 23 -> the fraction is
// held with the hidden bit at bit 30 (7 guard bits); QUIET_NAN 0x100000; the static NaN at 0x5f6a0.
//   fn_5cc50 pack_d        fn_5cd64 unpack_d       fn_5ce40 _fpadd_parts     fn_5d078 __addsf3  fn_5d0d0 __subsf3
//   fn_5d134 __mulsf3      fn_5d324 __divsf3       fn_5d484 __fpcmp_parts    fn_5d710 __gtsf2   fn_5d784 __gesf2
//   fn_5d8e0 __floatsisf   fn_5f0f0 sqrtf (the e_sqrtf integer algorithm, hand-written)
//   fn_5cc08 the default new-handler (operator new fn_5cb30's jalr when AllocSysMemory fails) -> fn_5db4c exit:
//            longjmp(0x5f6b0, 1) (sysclib:5) -- never returns.
// NOT PORTED (statically dead): the double-precision helpers 0x5da78 (make_fp), 0x5f034, 0x5db7c, 0x5dd40,
// 0x5de88, 0x5e960 -- reachable only from the unreferenced __extendsfdf2 at 0x5daa8 (the tail of the recompiler's
// fn_5da78 region, which no JAL / jump table / data word targets: spiop.asm grep), 0x5de88 / 0x5e960 only from
// themselves. The recompiler's fn_5d484 / fn_5d784 / fn_5d8e0 regions also carry unreferenced tails (__cmpsf2 ..
// __lesf2, __fixsfsi, __negsf2) that no path enters; only the entry function of each region is live code.
//
// Register fidelity: v0 at every exit as the recompiled body leaves it; the frames pushed with the recompiled
// sizes; the argument homes (sp+0x48.., where the callee spills a0/a1) written since unpack_d reads them.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/spiop/spiop_libc.h"

#include "engine/iop/abi/host_app/iop_stubs.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_iop_module.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/spiop/spiop_state.h"

namespace ps2r::title::sly2::spiop {

namespace {

using Ctx = ps2r::iop::IopContext;
enum Reg : int { v0 = 2, v1 = 3, a0 = 4, a1 = 5, a2 = 6, a3 = 7, s0 = 16, s1 = 17, s2 = 18, sp = 29, ra = 31 };

constexpr uint32_t kTheNan = 0x5f6a0;      // fp-bit's static NaN fp_number (`__thenan_sf`)
constexpr uint32_t kJmpBuf = 0x5f6b0;      // the module entry's setjmp buffer (exit's longjmp target)
enum Class : uint32_t { kSnan = 0, kQnan = 1, kZero = 2, kNumber = 3, kInfinity = 4 };
// fp_number_type fields
constexpr uint32_t kCls = 0, kSign = 4, kExp = 8, kFrac = 0xc;

inline uint32_t Call(Ctx& c, uint32_t addr, uint32_t ret_pc) {
    c.gpr[ra] = ret_pc;
    const auto* fn = iop_stubs::iop_resolve(addr);
    if (fn) (*fn)(c);
    return c.gpr[v0];
}
inline int32_t S(uint32_t v) { return static_cast<int32_t>(v); }
inline uint32_t U(int32_t v) { return static_cast<uint32_t>(v); }

// ---------------------------------------------------------------------------------------------------------
// fn_5cc50 pack_d(fp) -> the float's bits (v0). Leaf.
void fn_5cc50(Ctx& c) {
    const uint32_t fp = c.gpr[a0];
    const uint32_t cls = R32(fp + kCls);
    uint32_t frac = R32(fp + kFrac);
    const uint32_t sign = R32(fp + kSign);
    uint32_t exp = 0;   // a3: `move a3, zero` in the first branch's delay slot
    if (cls < 2) {      // a NaN: exponent all ones, the quiet bit forced
        exp = 0xff;
        frac |= 0x100000;
    } else if (cls == kInfinity) {
        exp = 0xff;
        frac = 0;
    } else if (cls == kZero) {
        frac = 0;
    } else if (frac != 0) {   // NUMBER (a zero fraction packs as a signed zero)
        const int32_t e = R32s(fp + kExp);
        if (e < -0x7e) {   // below the normal range: a denormal (or zero) -- shift the hidden bit down
            const int32_t sh = -0x7e - e;
            frac = sh < 0x1a ? frac >> sh : 0;
            frac >>= 7;
        } else if (e >= 0x80) {   // overflow: infinity
            exp = 0xff;
            frac = 0;
        } else {
            exp = U(e + 0x7f);
            // round to nearest, ties to even on the 7 guard bits
            if ((frac & 0x7f) != 0x40) frac += 0x3f;
            else if (frac & 0x80) frac += 0x40;
            if (S(frac) < 0) {   // the rounding carried out of the hidden bit
                frac >>= 1;
                exp += 1;
            }
            frac >>= 7;
        }
    }
    c.gpr[a1] = frac;
    c.gpr[a3] = exp;
    c.gpr[v1] = frac & 0x7fffff;
    c.gpr[a2] = (((exp & 0xff) << 23) | (frac & 0x7fffff)) & 0x7fffffff;
    c.gpr[v0] = (sign << 31) | c.gpr[a2];
}

// fn_5cd64 unpack_d(src -> float bits, dst fp). Leaf. v0 at exit: the class stored, or the fraction (NUMBER).
void fn_5cd64(Ctx& c) {
    const uint32_t src = c.gpr[a0], dst = c.gpr[a1];
    const uint32_t w = R32(src);
    uint32_t frac = w & 0x7fffff;
    const uint32_t exp = (w >> 23) & 0xff;
    const uint32_t sign = w >> 31;
    W32(dst + kSign, sign);
    if (exp == 0) {
        if (frac == 0) {
            W32(dst + kCls, kZero);
            c.gpr[a2] = 0;
            c.gpr[v0] = kZero;
            return;
        }
        frac <<= 7;
        W32(dst + kExp, U(-0x7e));
        W32(dst + kCls, kNumber);
        if (!(0x3fffffffu < frac)) {   // a denormal: normalize to the hidden bit
            int32_t e = -0x7e;
            do {
                frac <<= 1;
                e -= 1;
            } while (!(0x3fffffffu < frac));
            W32(dst + kExp, U(e));
            c.gpr[a0] = U(e);
            c.gpr[v0] = 1;
        } else {
            c.gpr[a0] = kNumber;
            c.gpr[v0] = 1;
        }
        W32(dst + kFrac, frac);
        c.gpr[a2] = frac;
        return;
    }
    if (exp != 0xff) {
        W32(dst + kExp, exp - 0x7f);
        frac = (frac << 7) | 0x40000000u;
        W32(dst + kCls, kNumber);
        W32(dst + kFrac, frac);
        c.gpr[v0] = frac;
        return;
    }
    if (frac != 0) {   // a NaN: signalling unless the quiet bit is set
        const uint32_t quiet = frac & 0x100000;
        W32(dst + kCls, quiet ? kQnan : kSnan);
        W32(dst + kFrac, frac);
        c.gpr[v0] = 1;   // `addiu v0, zero, 1` sits in the branch's delay slot: both arms leave 1
        return;
    }
    W32(dst + kCls, kInfinity);
    c.gpr[v0] = kInfinity;
}

// fn_5ce40 _fpadd_parts(a, b, tmp) -> the result fp (v0). Leaf (no calls).
void fn_5ce40(Ctx& c) {
    const uint32_t fa = c.gpr[a0], fb = c.gpr[a1], tmp = c.gpr[a2];
    const uint32_t ca = R32(fa + kCls);
    if (ca < 2) {
        c.gpr[v0] = fa;
        return;
    }
    const uint32_t cb = R32(fb + kCls);
    if (cb < 2) {
        c.gpr[v0] = fb;
        return;
    }
    if (ca == kInfinity) {
        if (cb != kInfinity || R32(fa + kSign) == R32(fb + kSign)) {
            c.gpr[v0] = fa;
            return;
        }
        c.gpr[v0] = kTheNan;   // inf - inf
        return;
    }
    if (cb == kInfinity) {
        c.gpr[v0] = fb;
        return;
    }
    if (cb == kZero) {
        if (ca != kZero) {
            c.gpr[v0] = fa;
            return;
        }
        // both zero: tmp = a, the sign AND'ed (+0 + -0 = +0)
        W32(tmp + kCls, ca);
        W32(tmp + kSign, R32(fa + kSign));
        W32(tmp + kExp, R32(fa + kExp));
        W32(tmp + kFrac, R32(fa + kFrac));
        c.gpr[v1] = R32(fa + kSign) & R32(fb + kSign);
        c.gpr[a0] = R32(fb + kSign);
        W32(tmp + kSign, c.gpr[v1]);
        c.gpr[v0] = tmp;
        return;
    }
    if (ca == kZero) {
        c.gpr[v0] = fb;
        return;
    }
    // Two numbers: align the exponents (sticky shift of the smaller), add or subtract the fractions.
    int32_t ea = R32s(fa + kExp), eb = R32s(fb + kExp);
    uint32_t ra_ = R32(fa + kFrac), rb = R32(fb + kFrac);   // t1, t2
    int32_t diff = ea - eb;
    if (diff < 0) diff = -diff;
    uint32_t sa, sb;
    int32_t e;   // t0
    if (diff < 0x20) {
        sa = R32(fa + kSign);
        sb = R32(fb + kSign);
        e = ea;
        if (eb < ea) {
            int32_t n = ea - eb;
            do {
                n -= 1;
                rb = (rb & 1) | (rb >> 1);
            } while (n != 0);
        } else if (ea < eb) {
            int32_t n = eb - ea;
            do {
                n -= 1;
                ra_ = (ra_ & 1) | (ra_ >> 1);
            } while (n != 0);
            e = eb;
        }
    } else {
        if (eb < ea) {
            e = ea;
            rb = 0;
        } else {
            e = eb;
            ra_ = 0;
        }
        sa = R32(fa + kSign);
        sb = R32(fb + kSign);
    }
    if (sa == sb) {
        W32(tmp + kSign, sa);
        W32(tmp + kExp, U(e));
        W32(tmp + kFrac, ra_ + rb);
    } else {
        const int32_t d = sa != 0 ? S(rb - ra_) : S(ra_ - rb);
        if (d < 0) {
            W32(tmp + kSign, 1);
            W32(tmp + kExp, U(e));
            W32(tmp + kFrac, U(-d));
        } else {
            W32(tmp + kSign, 0);
            W32(tmp + kExp, U(e));
            W32(tmp + kFrac, U(d));
        }
        uint32_t f = R32(tmp + kFrac);
        if (!(0x3ffffffeu < f - 1)) {   // normalize up (a zero fraction passes: 0 - 1 wraps)
            int32_t ex = R32s(tmp + kExp);
            do {
                f <<= 1;
                W32(tmp + kFrac, f);
                ex -= 1;
                W32(tmp + kExp, U(ex));
            } while (!(0x3ffffffeu < f - 1));
        }
    }
    uint32_t f = R32(tmp + kFrac);
    W32(tmp + kCls, kNumber);
    if (S(f) < 0) {   // the add carried into bit 31: one sticky shift down
        f = (f & 1) | (f >> 1);
        W32(tmp + kFrac, f);
        W32(tmp + kExp, R32(tmp + kExp) + 1);
    }
    c.gpr[v0] = tmp;
}

// The shared head of __addsf3/__subsf3/__mulsf3/__divsf3/__gtsf2/__gesf2: frame `size`, a0/a1 spilled to their
// homes at the caller's sp (+size, +size+4), unpack_d of both into fa (sp+0x10) and fb (sp+0x20), s0 = &fb.
struct TwoArgs {
    uint32_t sp, fa, fb;
};
TwoArgs UnpackTwo(Ctx& c, uint32_t size, uint32_t pc_unpack1, uint32_t pc_unpack2, uint32_t ra_slot, uint32_t s0_slot) {
    c.gpr[sp] -= size;
    const uint32_t p = c.gpr[sp];
    W32(p + size, c.gpr[a0]);
    W32(p + size + 4, c.gpr[a1]);
    W32(p + ra_slot, c.gpr[ra]);
    W32(p + s0_slot, c.gpr[s0]);
    c.gpr[a0] = p + size;
    c.gpr[a1] = p + 0x10;
    Call(c, 0x5cd64, pc_unpack1);
    c.gpr[s0] = p + 0x20;
    c.gpr[a0] = p + size + 4;
    c.gpr[a1] = c.gpr[s0];
    Call(c, 0x5cd64, pc_unpack2);
    return TwoArgs{p, p + 0x10, p + 0x20};
}
void Epilogue(Ctx& c, uint32_t size, uint32_t ra_slot, uint32_t s0_slot) {
    c.gpr[ra] = R32(c.gpr[sp] + ra_slot);
    c.gpr[s0] = R32(c.gpr[sp] + s0_slot);
    c.gpr[sp] += size;
}

// fn_5d078 __addsf3(a, b) (frame 0x48).
void fn_5d078(Ctx& c) {
    const TwoArgs t = UnpackTwo(c, 0x48, 0x5d098, 0x5d0a8, 0x44, 0x40);
    c.gpr[a0] = t.fa;
    c.gpr[a1] = t.fb;
    c.gpr[a2] = t.sp + 0x30;
    const uint32_t r = Call(c, 0x5ce40, 0x5d0b8);
    c.gpr[a0] = r;
    Call(c, 0x5cc50, 0x5d0c0);
    Epilogue(c, 0x48, 0x44, 0x40);
}

// fn_5d0d0 __subsf3(a, b) (frame 0x48): b's sign flipped, then the add.
void fn_5d0d0(Ctx& c) {
    const TwoArgs t = UnpackTwo(c, 0x48, 0x5d0f0, 0x5d100, 0x44, 0x40);
    c.gpr[a0] = t.fa;
    c.gpr[a1] = t.fb;
    c.gpr[a2] = t.sp + 0x30;
    W32(t.fb + kSign, R32(t.fb + kSign) ^ 1);
    const uint32_t r = Call(c, 0x5ce40, 0x5d11c);
    c.gpr[a0] = r;
    Call(c, 0x5cc50, 0x5d124);
    Epilogue(c, 0x48, 0x44, 0x40);
}

// fn_5d134 __mulsf3(a, b) (frame 0x48).
void fn_5d134(Ctx& c) {
    const TwoArgs t = UnpackTwo(c, 0x48, 0x5d154, 0x5d164, 0x44, 0x40);
    const uint32_t fa = t.fa, fb = t.fb, tmp = t.sp + 0x30;
    c.gpr[a1] = tmp;
    const uint32_t ca = R32(fa + kCls);
    const uint32_t cb = R32(fb + kCls);
    uint32_t result_fp;
    const uint32_t xsign = (R32(fa + kSign) ^ R32(fb + kSign)) != 0 ? 1u : 0u;
    enum { kA, kB, kNan, kMul } pick;
    if (ca < 2) pick = kA;
    else if (cb < 2) pick = kB;
    else if (ca == kInfinity) pick = cb == kZero ? kNan : kA;
    else if (cb == kInfinity) pick = ca == kZero ? kNan : kB;
    else if (ca == kZero) pick = kA;
    else if (cb == kZero) pick = kB;
    else pick = kMul;
    switch (pick) {
        case kA:
            W32(fa + kSign, xsign);
            result_fp = fa;
            c.gpr[v0] = xsign;
            break;
        case kB:
            W32(fb + kSign, xsign);
            result_fp = fb;
            c.gpr[v0] = xsign;
            break;
        case kNan:
            result_fp = kTheNan;
            break;
        default: {
            const uint64_t prod = static_cast<uint64_t>(R32(fa + kFrac)) * static_cast<uint64_t>(R32(fb + kFrac));
            uint32_t hi = static_cast<uint32_t>(prod >> 32), lo = static_cast<uint32_t>(prod);
            W32(tmp + kSign, xsign);
            int32_t e = R32s(fa + kExp) + R32s(fb + kExp) + 2;
            W32(tmp + kExp, U(e));
            if (S(hi) < 0) {   // the product filled bit 31: one shift down, the low bit carried into lo
                e += 1;
                if (hi & 1) lo = (lo >> 1) | 0x80000000u;
                hi >>= 1;
                W32(tmp + kExp, U(e));
            }
            if (!(0x3fffffffu < hi)) {   // normalize up, pulling lo's top bits
                do {
                    e -= 1;
                    const uint32_t carry = lo & 0x80000000u;
                    hi <<= 1;
                    if (carry) hi |= 1;
                    lo <<= 1;
                } while (!(0x3fffffffu < hi));
                W32(tmp + kExp, U(e));
            }
            if ((hi & 0x7f) == 0x40) {   // a tie on the guard bits: round up on an odd result or a non-zero lo
                if ((hi & 0x80) || lo != 0) hi += 0x40;
            }
            W32(tmp + kFrac, hi);
            W32(tmp + kCls, kNumber);
            result_fp = tmp;
            break;
        }
    }
    c.gpr[a0] = result_fp;
    Call(c, 0x5cc50, 0x5d314);
    Epilogue(c, 0x48, 0x44, 0x40);
}

// fn_5d324 __divsf3(a, b) (frame 0x38; the result built in fa).
void fn_5d324(Ctx& c) {
    const TwoArgs t = UnpackTwo(c, 0x38, 0x5d344, 0x5d354, 0x34, 0x30);
    const uint32_t fa = t.fa, fb = t.fb;
    const uint32_t ca = R32(fa + kCls);
    const uint32_t cb = R32(fb + kCls);
    uint32_t result_fp = fa;
    if (ca < 2) {
        result_fp = fa;
    } else if (cb < 2) {
        result_fp = fb;
    } else {
        W32(fa + kSign, R32(fa + kSign) ^ R32(fb + kSign));
        if (ca == kInfinity || ca == kZero) {
            result_fp = ca != cb ? fa : kTheNan;   // inf/inf, 0/0 = NaN
        } else if (cb == kInfinity) {
            W32(fa + kFrac, 0);
            W32(fa + kExp, 0);
            result_fp = fa;
        } else if (cb == kZero) {
            W32(fa + kCls, kInfinity);
            result_fp = fa;
        } else {
            uint32_t num = R32(fa + kFrac);
            const uint32_t den = R32(fb + kFrac);
            int32_t e = R32s(fa + kExp) - R32s(fb + kExp);
            W32(fa + kExp, U(e));
            if (num < den) {
                num <<= 1;
                W32(fa + kExp, U(e - 1));
            }
            uint32_t q = 0;
            for (uint32_t bit = 0x40000000u; bit != 0; bit >>= 1) {
                if (!(num < den)) {
                    q |= bit;
                    num -= den;
                }
                num <<= 1;
            }
            if ((q & 0x7f) == 0x40) {
                if ((q & 0x80) || num != 0) q += 0x40;
            }
            W32(fa + kFrac, q);
            result_fp = fa;
        }
    }
    c.gpr[a0] = result_fp;
    Call(c, 0x5cc50, 0x5d474);
    Epilogue(c, 0x38, 0x34, 0x30);
}

// fn_5d484 __fpcmp_parts(a, b) -> -1 / 0 / 1 (1 for any NaN). Leaf.
void fn_5d484(Ctx& c) {
    const uint32_t fa = c.gpr[a0], fb = c.gpr[a1];
    const uint32_t ca = R32(fa + kCls);
    auto ret = [&](int32_t r) { c.gpr[v0] = U(r); };
    if (ca < 2) return ret(1);
    const uint32_t cb = R32(fb + kCls);
    if (cb < 2) return ret(1);
    if (ca == kInfinity) {
        if (cb == kInfinity) return ret(S(R32(fb + kSign)) - S(R32(fa + kSign)));
        return ret(R32(fa + kSign) ? -1 : 1);
    }
    if (cb == kInfinity) return ret(R32(fb + kSign) ? 1 : -1);
    if (ca == kZero) {
        if (cb == kZero) return ret(0);
        return ret(R32(fb + kSign) ? 1 : -1);
    }
    if (cb == kZero) return ret(R32(fa + kSign) ? -1 : 1);
    const uint32_t sa = R32(fa + kSign);
    if (sa != R32(fb + kSign)) return ret(sa ? -1 : 1);
    const int32_t ea = R32s(fa + kExp), eb = R32s(fb + kExp);
    if (eb < ea) return ret(sa ? -1 : 1);
    if (ea < eb) return ret(sa ? 1 : -1);
    const uint32_t ra_ = R32(fa + kFrac), rb = R32(fb + kFrac);
    if (rb < ra_) return ret(sa ? -1 : 1);
    if (ra_ < rb) return ret(sa ? 1 : -1);
    ret(0);
}

// fn_5d710 __gtsf2 / fn_5d784 __gesf2 (frame 0x38): -1 for a NaN, else the parts compare.
void CompareNanMinus1(Ctx& c, uint32_t pc1, uint32_t pc2, uint32_t pc3) {
    const TwoArgs t = UnpackTwo(c, 0x38, pc1, pc2, 0x34, 0x30);
    if (R32(t.fa + kCls) < 2 || R32(t.fb + kCls) < 2) {
        c.gpr[v0] = U(-1);
    } else {
        c.gpr[a0] = t.fa;
        c.gpr[a1] = t.fb;
        Call(c, 0x5d484, pc3);
    }
    Epilogue(c, 0x38, 0x34, 0x30);
}
void fn_5d710(Ctx& c) { CompareNanMinus1(c, 0x5d730, 0x5d740, 0x5d770); }
void fn_5d784(Ctx& c) { CompareNanMinus1(c, 0x5d7a4, 0x5d7b4, 0x5d7e4); }

// fn_5d8e0 __floatsisf(int) (frame 0x28; the fp built at sp+0x10).
void fn_5d8e0(Ctx& c) {
    const uint32_t x = c.gpr[a0];
    c.gpr[sp] -= 0x28;
    const uint32_t p = c.gpr[sp], fp = p + 0x10;
    W32(p + 0x20, c.gpr[ra]);
    W32(fp + kCls, kNumber);
    W32(fp + kSign, x >> 31);
    if (x == 0) {
        W32(fp + kCls, kZero);
    } else {
        W32(fp + kExp, 0x1e);
        if (x >> 31) {
            if (x == 0x80000000u) {   // INT_MIN: the one value whose negation overflows -- the constant
                c.gpr[v0] = 0xcf000000u;
                c.gpr[ra] = R32(p + 0x20);
                c.gpr[sp] += 0x28;
                return;
            }
            W32(fp + kFrac, U(-S(x)));
        } else {
            W32(fp + kFrac, x);
        }
        uint32_t f = R32(fp + kFrac);
        if (!(0x3fffffffu < f)) {
            int32_t e = 0x1e;
            do {
                e -= 1;
                f <<= 1;
            } while (!(0x3fffffffu < f));
            W32(fp + kExp, U(e));
            W32(fp + kFrac, f);
        }
    }
    c.gpr[a0] = fp;
    Call(c, 0x5cc50, 0x5d980);
    c.gpr[ra] = R32(p + 0x20);
    c.gpr[sp] += 0x28;
}

// fn_5f0f0 sqrtf(x) (frame 0x20): the e_sqrtf integer bit-by-bit algorithm over the soft-float helpers.
void fn_5f0f0(Ctx& c) {
    c.gpr[sp] -= 0x20;
    const uint32_t p = c.gpr[sp];
    W32(p + 0x10, c.gpr[s0]);
    uint32_t x = c.gpr[a0];   // s0 (live in the register file: the callees spill it)
    c.gpr[s0] = x;
    W32(p + 0x1c, c.gpr[ra]);
    W32(p + 0x18, c.gpr[s2]);
    W32(p + 0x14, c.gpr[s1]);
    const uint32_t saved_s1 = c.gpr[s1];
    const uint32_t saved_s2 = c.gpr[s2];
    uint32_t result;
    const int32_t ix = S(x);
    if ((x & 0x7f800000u) == 0x7f800000u) {   // inf or NaN: x*x + x
        c.gpr[a0] = x;
        c.gpr[a1] = x;
        const uint32_t m = Call(c, 0x5d134, 0x5f124);
        c.gpr[a0] = m;
        c.gpr[a1] = x;
        result = Call(c, 0x5d078, 0x5f130);
    } else if (ix <= 0) {
        c.gpr[s2] = U(ix >> 23);
        if ((x & 0x7fffffffu) == 0) {
            result = x;   // +-0
        } else {
            // negative: (x - x) / (x - x) = NaN
            c.gpr[a0] = x;
            c.gpr[a1] = x;
            const uint32_t d1 = Call(c, 0x5d0d0, 0x5f164);
            c.gpr[a0] = x;
            c.gpr[a1] = x;
            c.gpr[s0] = d1;
            const uint32_t d2 = Call(c, 0x5d0d0, 0x5f174);
            x = d1;
            c.gpr[a0] = x;
            c.gpr[a1] = d2;
            result = Call(c, 0x5d324, 0x5f180);
        }
    } else {
        int32_t m = ix >> 23;   // s2
        c.gpr[s2] = U(m);
        uint32_t ix1 = x;       // a1
        if (m == 0) {           // subnormal: normalize
            int32_t i = 0;
            do {
                ix1 <<= 1;
                i += 1;
            } while ((ix1 & 0x800000u) == 0);
            m = 1 - i;
            c.gpr[s2] = U(m);
        }
        m -= 0x7f;
        ix1 = (ix1 & 0x7fffffu) | 0x800000u;
        if (m & 1) ix1 <<= 1;
        m >>= 1;   // sra
        c.gpr[s2] = U(m);
        ix1 <<= 1;
        uint32_t s = 0, q = 0;   // a2, s1
        for (uint32_t r = 0x1000000u; r != 0; r >>= 1) {
            const uint32_t tt = s + r;
            if (!(S(ix1) < S(tt))) {
                s = tt + r;
                ix1 -= tt;
                q += r;
            }
            ix1 <<= 1;
        }
        c.gpr[s1] = q;
        if (ix1 != 0) {
            // z = 1.0 - tiny; if (z >= 1.0) { z = 1.0 + tiny; if (z > 1.0) q += 2; else q += (q & 1); }
            // -- with `tiny` folded away by the compiler: both compares are 1.0 against itself.
            c.gpr[s0] = 0x3f800000u;
            c.gpr[a0] = 0x3f800000u;
            c.gpr[a1] = 0x3f800000u;
            const int32_t ge = S(Call(c, 0x5d784, 0x5f230));
            if (!(ge < 0)) {
                c.gpr[a0] = 0x3f800000u;
                c.gpr[a1] = 0x3f800000u;
                const int32_t gt = S(Call(c, 0x5d710, 0x5f240));
                if (gt <= 0) q += q & 1;
                else q += 2;
            }
            c.gpr[s1] = q;
        }
        const uint32_t half = U(S(q) >> 1);
        result = (half + 0x3f000000u) + U(m << 23);
    }
    c.gpr[s0] = result;
    c.gpr[v0] = result;
    c.gpr[ra] = R32(p + 0x1c);
    c.gpr[s2] = saved_s2;
    c.gpr[s1] = saved_s1;
    c.gpr[s0] = R32(p + 0x10);
    c.gpr[sp] += 0x20;
}

// fn_5cc08: operator new's default handler on an allocation failure -> exit(-1) = fn_5db4c. Never returns.
void fn_5cc08(Ctx& c) {
    c.gpr[sp] -= 0x18;
    W32(c.gpr[sp] + 0x10, c.gpr[ra]);
    c.gpr[a0] = U(-1);
    Call(c, 0x5db4c, 0x5cc18);
}

// fn_5db4c exit(status): longjmp(the module entry's jmp_buf 0x5f6b0, 1) through sysclib:5. Never returns.
void fn_5db4c(Ctx& c) {
    c.gpr[sp] -= 0x18;
    W32(c.gpr[sp] + 0x10, c.gpr[ra]);
    c.gpr[a0] = kJmpBuf;
    c.gpr[a1] = 1;
    c.gpr[ra] = 0x5db64;
    iop_stubs::import_call("sysclib", 5, c);
}

struct LibcFn {
    uint32_t addr;
    void (*body)(Ctx&);
    const char* name;
};
const LibcFn kLibc[] = {
    {0x5cc08, &fn_5cc08, "libc: default new-handler (exit -1)"},
    {0x5cc50, &fn_5cc50, "fp-bit: pack_d"},
    {0x5cd64, &fn_5cd64, "fp-bit: unpack_d"},
    {0x5ce40, &fn_5ce40, "fp-bit: _fpadd_parts"},
    {0x5d078, &fn_5d078, "fp-bit: __addsf3"},
    {0x5d0d0, &fn_5d0d0, "fp-bit: __subsf3"},
    {0x5d134, &fn_5d134, "fp-bit: __mulsf3"},
    {0x5d324, &fn_5d324, "fp-bit: __divsf3"},
    {0x5d484, &fn_5d484, "fp-bit: __fpcmp_parts"},
    {0x5d710, &fn_5d710, "fp-bit: __gtsf2"},
    {0x5d784, &fn_5d784, "fp-bit: __gesf2"},
    {0x5d8e0, &fn_5d8e0, "fp-bit: __floatsisf"},
    {0x5db4c, &fn_5db4c, "libc: exit (longjmp)"},
    {0x5f0f0, &fn_5f0f0, "libm: sqrtf"},
};

}  // namespace

const LibcFunction* LibcFunctions(size_t& count) {
    static const LibcFunction table[] = {
        {0x5cc08, &fn_5cc08}, {0x5cc50, &fn_5cc50}, {0x5cd64, &fn_5cd64}, {0x5ce40, &fn_5ce40}, {0x5d078, &fn_5d078},
        {0x5d0d0, &fn_5d0d0}, {0x5d134, &fn_5d134}, {0x5d324, &fn_5d324}, {0x5d484, &fn_5d484}, {0x5d710, &fn_5d710},
        {0x5d784, &fn_5d784}, {0x5d8e0, &fn_5d8e0}, {0x5db4c, &fn_5db4c}, {0x5f0f0, &fn_5f0f0},
    };
    count = sizeof(table) / sizeof(table[0]);
    return table;
}

void RegisterLibc(NativeIopModule& spiop) {
    for (const LibcFn& f : kLibc) spiop.Add(f.addr, f.body, f.name);
}

}  // namespace ps2r::title::sly2::spiop
