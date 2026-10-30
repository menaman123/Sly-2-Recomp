// native_simd.inl -- the Vec4f bodies (NEON / SSE2 / scalar arms). Included from native_simd.h only.

namespace ps2r::title::sly2::simd {

inline Vec4f Load(const float* p) {   // p 16-byte aligned
    Vec4f r;
#if PS2R_NATIVE_SIMD == 1
    r.v = vld1q_f32(p);
#elif PS2R_NATIVE_SIMD == 2
    r.v = _mm_load_ps(p);
#else
    std::memcpy(r.v, p, 16);
#endif
    return r;
}

inline Vec4f LoadU(const float* p) {   // any alignment
    Vec4f r;
#if PS2R_NATIVE_SIMD == 1
    r.v = vld1q_f32(p);
#elif PS2R_NATIVE_SIMD == 2
    r.v = _mm_loadu_ps(p);
#else
    std::memcpy(r.v, p, 16);
#endif
    return r;
}

inline void Store(float* p, Vec4f a) {   // p 16-byte aligned
#if PS2R_NATIVE_SIMD == 1
    vst1q_f32(p, a.v);
#elif PS2R_NATIVE_SIMD == 2
    _mm_store_ps(p, a.v);
#else
    std::memcpy(p, a.v, 16);
#endif
}

inline void StoreU(float* p, Vec4f a) {
#if PS2R_NATIVE_SIMD == 1
    vst1q_f32(p, a.v);
#elif PS2R_NATIVE_SIMD == 2
    _mm_storeu_ps(p, a.v);
#else
    std::memcpy(p, a.v, 16);
#endif
}

inline Vec4f Set(float x, float y, float z, float w) {
    Vec4f r;
#if PS2R_NATIVE_SIMD == 1
    const float t[4] = {x, y, z, w};
    r.v = vld1q_f32(t);
#elif PS2R_NATIVE_SIMD == 2
    r.v = _mm_setr_ps(x, y, z, w);
#else
    r.v[0] = x; r.v[1] = y; r.v[2] = z; r.v[3] = w;
#endif
    return r;
}

inline Vec4f Splat(float s) {
    Vec4f r;
#if PS2R_NATIVE_SIMD == 1
    r.v = vdupq_n_f32(s);
#elif PS2R_NATIVE_SIMD == 2
    r.v = _mm_set1_ps(s);
#else
    r.v[0] = r.v[1] = r.v[2] = r.v[3] = s;
#endif
    return r;
}

inline Vec4f Zero() { return Splat(0.0f); }

inline Vec4f Add(Vec4f a, Vec4f b) {
    Vec4f r;
#if PS2R_NATIVE_SIMD == 1
    r.v = vaddq_f32(a.v, b.v);
#elif PS2R_NATIVE_SIMD == 2
    r.v = _mm_add_ps(a.v, b.v);
#else
    for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] + b.v[i];
#endif
    return r;
}

inline Vec4f Sub(Vec4f a, Vec4f b) {
    Vec4f r;
#if PS2R_NATIVE_SIMD == 1
    r.v = vsubq_f32(a.v, b.v);
#elif PS2R_NATIVE_SIMD == 2
    r.v = _mm_sub_ps(a.v, b.v);
#else
    for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] - b.v[i];
#endif
    return r;
}

inline Vec4f Mul(Vec4f a, Vec4f b) {
    Vec4f r;
#if PS2R_NATIVE_SIMD == 1
    r.v = vmulq_f32(a.v, b.v);
#elif PS2R_NATIVE_SIMD == 2
    r.v = _mm_mul_ps(a.v, b.v);
#else
    for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] * b.v[i];
#endif
    return r;
}

inline Vec4f MulAdd(Vec4f a, Vec4f b, Vec4f c) { return Add(Mul(a, b), c); }

inline Vec4f Min(Vec4f a, Vec4f b) {
    Vec4f r;
#if PS2R_NATIVE_SIMD == 1
    r.v = vminq_f32(a.v, b.v);
#elif PS2R_NATIVE_SIMD == 2
    r.v = _mm_min_ps(a.v, b.v);
#else
    for (int i = 0; i < 4; ++i) r.v[i] = b.v[i] < a.v[i] ? b.v[i] : a.v[i];
#endif
    return r;
}

inline Vec4f Max(Vec4f a, Vec4f b) {
    Vec4f r;
#if PS2R_NATIVE_SIMD == 1
    r.v = vmaxq_f32(a.v, b.v);
#elif PS2R_NATIVE_SIMD == 2
    r.v = _mm_max_ps(a.v, b.v);
#else
    for (int i = 0; i < 4; ++i) r.v[i] = b.v[i] > a.v[i] ? b.v[i] : a.v[i];
#endif
    return r;
}

inline float Lane(Vec4f a, int i) {
    alignas(16) float t[4];
    Store(t, a);
    return t[i & 3];
}

template <int I>
inline Vec4f Broadcast(Vec4f a) {
    static_assert(I >= 0 && I < 4, "lane");
    Vec4f r;
#if PS2R_NATIVE_SIMD == 1
    r.v = vdupq_laneq_f32(a.v, I);
#elif PS2R_NATIVE_SIMD == 2
    r.v = _mm_shuffle_ps(a.v, a.v, _MM_SHUFFLE(I, I, I, I));
#else
    for (int i = 0; i < 4; ++i) r.v[i] = a.v[I];
#endif
    return r;
}

inline Vec4f MatMul(const Vec4f m[4], Vec4f v) {
    Vec4f r = Mul(m[0], Broadcast<0>(v));
    r = MulAdd(m[1], Broadcast<1>(v), r);
    r = MulAdd(m[2], Broadcast<2>(v), r);
    r = MulAdd(m[3], Broadcast<3>(v), r);
    return r;
}

inline void Bits(Vec4f a, uint32_t out[4]) {
    alignas(16) float t[4];
    Store(t, a);
    std::memcpy(out, t, 16);
}

}  // namespace ps2r::title::sly2::simd
