// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
//
// Small SIMD layer for the hot numerical loops (constraint solving, wind
// sampling, transform math).
//
// A note on "assembly inserts": MSVC does not support inline assembly in x64
// builds at all (the compiler rejects `__asm`), and hand-written MASM modules
// cannot be inlined or register-allocated with the surrounding C++. The
// supported, portable equivalent is intrinsics, which is what this header uses;
// the compiler emits the same instructions (movaps/mulps/sqrtps/rsqrtps) and can
// still schedule them across inlined code. Where an intrinsic sequence is not
// available (ARM without NEON enabled), the scalar fallback below is compiled
// instead, so no platform loses correctness.
//
// Every function is exact (no approximate reciprocal) unless it says so, so a
// SIMD path and the scalar path produce the same result to within float
// rounding, which the engine tests check.
#pragma once

#include <cmath>
#include <cstddef>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace Fem
{
namespace Simd
{

struct Features
{
    bool sse2 = false;
    bool avx2 = false;
    bool avx512 = false;
};

// Cached after the first call; safe to call from any thread (the CPUID result
// is constant for the process).
inline const Features& GetFeatures()
{
    static const Features value = []
    {
        Features f;
#if defined(_MSC_VER)
        int regs[4] = {0, 0, 0, 0};
        __cpuid(regs, 0);
        if (regs[0] >= 1)
        {
            __cpuid(regs, 1);
            f.sse2 = (regs[3] & (1 << 26)) != 0;
            f.avx2 = (regs[2] & (1 << 28)) != 0; // OSXSAVE+AVX, refined below
        }
        if (regs[0] >= 7)
        {
            __cpuidex(regs, 7, 0);
            f.avx2 = f.avx2 && (regs[1] & (1 << 5)) != 0;
            f.avx512 = (regs[1] & (1 << 16)) != 0;
        }
#elif defined(__x86_64__) || defined(__i386__)
        __builtin_cpu_init();
        f.sse2 = __builtin_cpu_supports("sse2");
        f.avx2 = __builtin_cpu_supports("avx2");
        f.avx512 = __builtin_cpu_supports("avx512f");
#else
        f = Features{};
#endif
        return f;
    }();
    return value;
}

inline bool HasSse2()
{
#if defined(_M_X64) || defined(__SSE2__)
    return GetFeatures().sse2 || true; // x64 always has SSE2
#else
    return GetFeatures().sse2;
#endif
}

// Reciprocal lengths of four (dx, dy) pairs: out[i] = 1 / sqrt(dx^2 + dy^2).
// A zero-length pair yields 0 instead of infinity, matching the scalar guard
// the solvers use. This is the single hottest operation in the constraint pass
// (one sqrt and one divide per link), so it is the one worth vectorising.
inline void ReciprocalLengths4(const float* dx, const float* dy, float* out)
{
#if defined(_M_X64) || defined(__SSE2__)
    const __m128 vx = _mm_loadu_ps(dx);
    const __m128 vy = _mm_loadu_ps(dy);
    __m128 sq = _mm_add_ps(_mm_mul_ps(vx, vx), _mm_mul_ps(vy, vy));
    // sqrt(0) is 0, and 1/0 would be +inf; step the zero case aside first.
    const __m128 zero = _mm_setzero_ps();
    const __m128 mask = _mm_cmpgt_ps(sq, _mm_set1_ps(1e-12f));
    sq = _mm_max_ps(sq, _mm_set1_ps(1e-12f));
    __m128 inv = _mm_div_ps(_mm_set1_ps(1.f), _mm_sqrt_ps(sq));
    inv = _mm_and_ps(inv, mask);
    _mm_storeu_ps(out, inv);
#else
    for (int i = 0; i < 4; ++i)
    {
        const float sq = dx[i] * dx[i] + dy[i] * dy[i];
        out[i] = sq <= 1e-12f ? 0.f : 1.f / std::sqrt(sq);
    }
#endif
}

// Scalar counterpart with the same guard, used for the tail of a batch.
inline float ReciprocalLength(float dx, float dy)
{
    const float sq = dx * dx + dy * dy;
    return sq <= 1e-12f ? 0.f : 1.f / std::sqrt(sq);
}

// out[i] = base[i] + scale[i] * add[i] for four lanes. Used for the wind force
// and for gravity integration in the cloth/rope step.
inline void ScaleAdd4(const float* base, const float* scale, const float* add, float* out)
{
#if defined(_M_X64) || defined(__SSE2__)
    const __m128 b = _mm_loadu_ps(base);
    const __m128 s = _mm_loadu_ps(scale);
    const __m128 a = _mm_loadu_ps(add);
    _mm_storeu_ps(out, _mm_add_ps(b, _mm_mul_ps(s, a)));
#else
    for (int i = 0; i < 4; ++i)
    {
        out[i] = base[i] + scale[i] * add[i];
    }
#endif
}

// Rotates four points around a pivot by (cosine, sine), in place. The points are
// four interleaved sf::Vector2f values, which is exactly the sprite corner set:
// the renderer used to call the scalar helper four times, recomputing sin/cos
// each time. This does one vectorised pass with the same arithmetic.
inline void RotatePoints4(sf::Vector2f* points, sf::Vector2f pivot, float cosine, float sine)
{
#if defined(_M_X64) || defined(__SSE2__)
    const __m128 a = _mm_loadu_ps(&points[0].x); // x0 y0 x1 y1
    const __m128 b = _mm_loadu_ps(&points[2].x); // x2 y2 x3 y3
    __m128 x = _mm_shuffle_ps(a, b, _MM_SHUFFLE(2, 0, 2, 0));
    __m128 y = _mm_shuffle_ps(a, b, _MM_SHUFFLE(3, 1, 3, 1));
    x = _mm_sub_ps(x, _mm_set1_ps(pivot.x));
    y = _mm_sub_ps(y, _mm_set1_ps(pivot.y));
    const __m128 c = _mm_set1_ps(cosine);
    const __m128 s = _mm_set1_ps(sine);
    const __m128 rx = _mm_add_ps(_mm_sub_ps(_mm_mul_ps(x, c), _mm_mul_ps(y, s)), _mm_set1_ps(pivot.x));
    const __m128 ry = _mm_add_ps(_mm_add_ps(_mm_mul_ps(x, s), _mm_mul_ps(y, c)), _mm_set1_ps(pivot.y));
    _mm_storeu_ps(&points[0].x, _mm_unpacklo_ps(rx, ry));
    _mm_storeu_ps(&points[2].x, _mm_unpackhi_ps(rx, ry));
#else
    for (int i = 0; i < 4; ++i)
    {
        const float dx = points[i].x - pivot.x;
        const float dy = points[i].y - pivot.y;
        points[i].x = pivot.x + dx * cosine - dy * sine;
        points[i].y = pivot.y + dx * sine + dy * cosine;
    }
#endif
}

} // namespace Simd
} // namespace Fem
