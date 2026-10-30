// native_simd.h -- the ONE portable SIMD surface of the title's native module (the Windows rule:
// no arch-specific intrinsics anywhere else in native/).
//
// A 4 x float32 vector (`Vec4f`) with the operations the native kick / unpack / transform paths
// need, as NEON on AArch64, SSE2 on x86-64 (both compiler-arch macros, never OS macros), and a
// scalar fallback that is the specification: every vector body is bit-identical to the scalar
// one for finite inputs (the engine's VU float rules -- clamp, flags -- stay the caller's job:
// engine/vu/vu_float.h). PS2R_NATIVE_SIMD names the arm in force (0 scalar, 1 NEON, 2 SSE2) so
// a census can report it; PS2R_NATIVE_SIMD_SCALAR=1 forces the scalar arm on any host (the A/B).
//
// Fused multiply-add: Fma4 is a TRUE fma on NEON (vfmaq_f32) and the SSE2 arm has none -- it
// rounds twice (mul then add), as does the scalar arm (std::fma would be the single rounding).
// A path whose oracle rounds once (the VU's MADD family, VuFloat::Fma4) MUST use VuFloat; this
// header's Fma4 is for host-side arithmetic whose rounding the oracle does not observe (index
// math, bounding boxes). The scalar twin of each arm is what the gate compares.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(PS2R_NATIVE_SIMD_SCALAR) && PS2R_NATIVE_SIMD_SCALAR
#define PS2R_NATIVE_SIMD 0
#elif defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#define PS2R_NATIVE_SIMD 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define PS2R_NATIVE_SIMD 2
#else
#define PS2R_NATIVE_SIMD 0
#endif

namespace ps2r::title::sly2::simd {

struct alignas(16) Vec4f {
#if PS2R_NATIVE_SIMD == 1
    float32x4_t v;
#elif PS2R_NATIVE_SIMD == 2
    __m128 v;
#else
    float v[4];
#endif
};

// The arm compiled in: "neon", "sse2" or "scalar".
const char* ArmName();

// Load/Store: p 16-byte aligned; LoadU/StoreU: any alignment.
inline Vec4f Load(const float* p);
inline Vec4f LoadU(const float* p);
inline void Store(float* p, Vec4f a);
inline void StoreU(float* p, Vec4f a);
inline Vec4f Set(float x, float y, float z, float w);
inline Vec4f Splat(float s);
inline Vec4f Zero();
// Lane-wise IEEE single arithmetic under the caller's rounding mode.
inline Vec4f Add(Vec4f a, Vec4f b);
inline Vec4f Sub(Vec4f a, Vec4f b);
inline Vec4f Mul(Vec4f a, Vec4f b);

// a*b + c, rounded TWICE on every arm (see the header comment): mul then add.
inline Vec4f MulAdd(Vec4f a, Vec4f b, Vec4f c);
// Lane-wise min / max (b wins ties on the scalar arm only when strictly smaller / greater: the NEON and
// SSE2 arms return a for equal lanes too; -0/+0 and NaN lanes are the caller's to avoid).
inline Vec4f Min(Vec4f a, Vec4f b);
inline Vec4f Max(Vec4f a, Vec4f b);

// Lane i (0..3) as a scalar.
inline float Lane(Vec4f a, int i);

// The lane broadcast: every lane = a[i].
template <int I>
inline Vec4f Broadcast(Vec4f a);

// Column-major 4x4 (the VU's four VF rows = the matrix columns m[0..3]) times a vector:
// m0*x + m1*y + m2*z + m3*w, evaluated in that order with MulAdd (two roundings per term, the
// same order on every arm so the arms agree lane for lane).
inline Vec4f MatMul(const Vec4f m[4], Vec4f v);

// The bit patterns of a vector (an oracle compare is a byte compare).
inline void Bits(Vec4f a, uint32_t out[4]);

}  // namespace ps2r::title::sly2::simd

#include "games/scus_97316_sly2_band_of_thieves/native/native_simd.inl"
