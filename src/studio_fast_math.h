#pragma once
#include <cmath>
#include <emmintrin.h>
#include <xmmintrin.h>

namespace studio_fastmath
{
inline __m128 C(float v) { return _mm_set1_ps(v); }
inline __m128i CI(int v) { return _mm_set1_epi32(v); }

inline void SinCos4(__m128 x, __m128& sOut, __m128& cOut)
{
    const __m128 signMask = _mm_castsi128_ps(CI(static_cast<int>(0x80000000u)));
    const __m128 absMask = _mm_castsi128_ps(CI(0x7fffffff));
    __m128 signSin = _mm_and_ps(x, signMask);
    x = _mm_and_ps(x, absMask);
    __m128 y = _mm_mul_ps(x, C(1.27323954473516f));
    __m128i q = _mm_cvttps_epi32(y);
    q = _mm_add_epi32(q, CI(1));
    q = _mm_and_si128(q, CI(~1));
    y = _mm_cvtepi32_ps(q);
    __m128i qc = q;
    __m128i t = _mm_and_si128(q, CI(4));
    t = _mm_slli_epi32(t, 29);
    signSin = _mm_xor_ps(signSin, _mm_castsi128_ps(t));
    q = _mm_and_si128(q, CI(2));
    q = _mm_cmpeq_epi32(q, _mm_setzero_si128());
    const __m128 mask = _mm_castsi128_ps(q);
    x = _mm_add_ps(x, _mm_mul_ps(y, C(-0.78515625f)));
    x = _mm_add_ps(x, _mm_mul_ps(y, C(-2.4187564849853515625e-4f)));
    x = _mm_add_ps(x, _mm_mul_ps(y, C(-3.77489497744594108e-8f)));
    qc = _mm_sub_epi32(qc, CI(2));
    qc = _mm_andnot_si128(qc, CI(4));
    qc = _mm_slli_epi32(qc, 29);
    const __m128 signCos = _mm_castsi128_ps(qc);
    const __m128 z = _mm_mul_ps(x, x);
    __m128 c = C(2.443315711809948e-5f);
    c = _mm_add_ps(_mm_mul_ps(c, z), C(-1.388731625493765e-3f));
    c = _mm_add_ps(_mm_mul_ps(c, z), C(4.166664568298827e-2f));
    c = _mm_mul_ps(_mm_mul_ps(c, z), z);
    c = _mm_sub_ps(c, _mm_mul_ps(z, C(0.5f)));
    c = _mm_add_ps(c, C(1.0f));
    __m128 s = C(-1.9515295309e-4f);
    s = _mm_add_ps(_mm_mul_ps(s, z), C(8.3321608736e-3f));
    s = _mm_add_ps(_mm_mul_ps(s, z), C(-1.6666654611e-1f));
    s = _mm_mul_ps(_mm_mul_ps(s, z), x);
    s = _mm_add_ps(s, x);
    sOut = _mm_xor_ps(_mm_add_ps(_mm_and_ps(mask, s), _mm_andnot_ps(mask, c)), signSin);
    cOut = _mm_xor_ps(_mm_add_ps(_mm_and_ps(mask, c), _mm_andnot_ps(mask, s)), signCos);
}

inline float Acos(float x)
{
    constexpr float halfPi = 1.5707963267948966f;
    const float a = std::fabs(x) > 1.0f ? 1.0f : std::fabs(x);
    float z, w; bool hi;
    if (a > 0.5f) { z = 0.5f * (1.0f - a); w = std::sqrt(z); hi = true; }
    else { z = a * a; w = a; hi = false; }
    float p = ((((4.2163199048e-2f * z + 2.4181311049e-2f) * z + 4.5470025998e-2f) * z +
        7.4953002686e-2f) * z + 1.6666752422e-1f) * z * w + w;
    if (hi) p = halfPi - 2.0f * p;
    const float as = x < 0.0f ? -p : p;
    return halfPi - as;
}
}
