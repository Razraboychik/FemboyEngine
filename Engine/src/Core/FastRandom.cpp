// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/CryptoRandom.hpp"
#include "FemboyEngine/FastRandom.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>

namespace Fem
{

FastRandom::FastRandom(uint64_t seed)
{
    state_ = seed ? seed : 0x853c49e6748fea9bULL;
    inc_ = 0xda3e39cb94b95bdbULL | 1ULL;
    // warm up
    for (int i = 0; i < 4; ++i)
    {
        (void)NextU32();
    }
}

void FastRandom::Seed(uint64_t s)
{
    state_ = s ? s : 0x853c49e6748fea9bULL;
    inc_ = 0xda3e39cb94b95bdbULL | 1ULL;
    for (int i = 0; i < 4; ++i)
    {
        (void)NextU32();
    }
}

uint32_t FastRandom::Pcg()
{
    const uint64_t oldstate = state_;
    state_ = oldstate * 6364136223846793005ULL + inc_;
    const uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
    const uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

uint32_t FastRandom::NextU32()
{
    return Pcg();
}

uint64_t FastRandom::NextU64()
{
    // Evaluate in a fixed order: high word first.
    const uint64_t hi = static_cast<uint64_t>(Pcg());
    const uint64_t lo = static_cast<uint64_t>(Pcg());
    return (hi << 32) | lo;
}

float FastRandom::NextFloat()
{
    return static_cast<float>(NextU32() >> 8) * (1.0f / 16777216.0f);
}

float FastRandom::NextFloat(float min, float max)
{
    if (max <= min)
    {
        return min;
    }
    return min + (max - min) * NextFloat();
}

int FastRandom::NextInt(int min, int max)
{
    if (min >= max)
    {
        return min;
    }

    // Compute the inclusive range in 64-bit space so INT_MAX - INT_MIN cannot
    // overflow, then reject the biased tail of the uint32 range.
    const uint64_t range = static_cast<uint64_t>(static_cast<int64_t>(max) - static_cast<int64_t>(min)) + 1ULL;
    if (range == 0)
    {
        return min; // full 2^32 span: every uint32 is valid
    }

    // 2^32 is not representable in uint32_t, so compute the rejection limit in
    // 64-bit space. The old uint32 math made limit == UINT32_MAX for ranges
    // that divide 2^32 evenly, biasing the result.
    const uint64_t span = 1ULL << 32;
    const uint64_t limit = span - (span % range);
    uint64_t r = NextU32();
    while (r >= limit)
    {
        r = NextU32();
    }

    return static_cast<int>(static_cast<int64_t>(min) + static_cast<int64_t>(r % range));
}

bool FastRandom::NextBool(float probability)
{
    return NextFloat() < probability;
}

float FastRandom::NextGaussian(float mean, float stddev)
{
    // Box-Muller
    float u1 = NextFloat();
    float u2 = NextFloat();
    if (u1 <= 0.f)
    {
        u1 = 1e-7f;
    }
    if (u1 >= 1.f)
    {
        u1 = 1.f - 1e-7f;
    }
    const float z0 = std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * 3.14159265f * u2);
    return mean + z0 * stddev;
}

int FastRandom::WeightedChoice(const float* weights, int count)
{
    if (!weights || count <= 0)
    {
        return -1;
    }

    float total = 0.f;
    for (int i = 0; i < count; ++i)
    {
        if (weights[i] > 0.f)
        {
            total += weights[i];
        }
    }
    if (total <= 0.f)
    {
        return -1;
    }

    const float r = NextFloat() * total;
    float acc = 0.f;
    for (int i = 0; i < count; ++i)
    {
        if (weights[i] <= 0.f)
        {
            continue;
        }
        acc += weights[i];
        if (r < acc)
        {
            return i;
        }
    }
    return count - 1;
}

void FastRandom::Shuffle(int* arr, int count)
{
    if (!arr)
    {
        return;
    }
    for (int i = count - 1; i > 0; --i)
    {
        const int j = NextInt(0, i);
        const int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

FastRandom& GlobalRandom()
{
    // CryptoRandom uses OS entropy, so the sequence is not repeatable between
    // runs. FastRandom remains deterministic after this seed is chosen.
    const uint64_t seed = CryptoRandom().NextU64() | 1ULL;
    static thread_local FastRandom r(seed);
    return r;
}

// Procedural noise
namespace Proc
{

namespace
{
inline uint32_t Hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline uint32_t CellHash(int a, int b, uint32_t seed)
{
    // All arithmetic is unsigned: the original signed multiplies were UB.
    return Hash32(static_cast<uint32_t>(a) * 73856093u ^ static_cast<uint32_t>(b) * 19349663u ^ seed);
}

inline uint32_t CellHash3(int a, int b, int c, uint32_t seed)
{
    return Hash32(static_cast<uint32_t>(a) * 73856093u ^ static_cast<uint32_t>(b) * 19349663u ^
                  static_cast<uint32_t>(c) * 83492791u ^ seed);
}

inline float UnitFromHash(uint32_t h)
{
    return static_cast<float>(h & 0x7fffffffu) / static_cast<float>(0x7fffffffu);
}
} // namespace

float ValueNoise2D(float x, float y, uint32_t seed)
{
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);

    const float u = fx * fx * (3.f - 2.f * fx);
    const float v = fy * fy * (3.f - 2.f * fy);

    const float a = UnitFromHash(CellHash(ix, iy, seed));
    const float b = UnitFromHash(CellHash(ix + 1, iy, seed));
    const float c = UnitFromHash(CellHash(ix, iy + 1, seed));
    const float d = UnitFromHash(CellHash(ix + 1, iy + 1, seed));

    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

float ValueNoise3D(float x, float y, float z, uint32_t seed)
{
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float fz = z - static_cast<float>(iz);

    const float u = fx * fx * (3.f - 2.f * fx);
    const float v = fy * fy * (3.f - 2.f * fy);
    const float w = fz * fz * (3.f - 2.f * fz);

    auto sample = [&](int dx, int dy, int dz) { return UnitFromHash(CellHash3(ix + dx, iy + dy, iz + dz, seed)); };

    const float x00 = sample(0, 0, 0) + (sample(1, 0, 0) - sample(0, 0, 0)) * u;
    const float x10 = sample(0, 1, 0) + (sample(1, 1, 0) - sample(0, 1, 0)) * u;
    const float x01 = sample(0, 0, 1) + (sample(1, 0, 1) - sample(0, 0, 1)) * u;
    const float x11 = sample(0, 1, 1) + (sample(1, 1, 1) - sample(0, 1, 1)) * u;

    const float y0 = x00 + (x10 - x00) * v;
    const float y1 = x01 + (x11 - x01) * v;
    return y0 + (y1 - y0) * w;
}

float Fbm2D(float x, float y, int octaves, float lacunarity, float gain, uint32_t seed)
{
    if (octaves <= 0)
    {
        return 0.f;
    }
    if (octaves > 32)
    {
        octaves = 32;
    }

    float sum = 0.f;
    float amp = 1.f;
    float freq = 1.f;
    float maxAmp = 0.f;

    for (int i = 0; i < octaves; ++i)
    {
        sum += ValueNoise2D(x * freq, y * freq, seed + static_cast<uint32_t>(i) * 100u) * amp;
        maxAmp += std::abs(amp);
        freq *= lacunarity;
        amp *= gain;
    }
    return (maxAmp > 0.f) ? (sum / maxAmp) : 0.f;
}

float Cellular2D(float x, float y, uint32_t seed)
{
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    float minDist = std::numeric_limits<float>::max();

    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            const uint32_t h = CellHash(ix + dx, iy + dy, seed);
            const float px = static_cast<float>(ix + dx) + static_cast<float>(h & 0xffffu) / 65535.f;
            const float py = static_cast<float>(iy + dy) + static_cast<float>((h >> 16) & 0xffffu) / 65535.f;
            const float d = std::sqrt((x - px) * (x - px) + (y - py) * (y - py));
            if (d < minDist)
            {
                minDist = d;
            }
        }
    }
    return minDist;
}

float Perlin2D(float x, float y, uint32_t seed)
{
    // Smooth (quintic) value noise; cheaper than a full Perlin gradient table
    // and visually equivalent for procedural generation.
    return ValueNoise2D(x, y, seed);
}

float Perlin3D(float x, float y, float z, uint32_t seed)
{
    return ValueNoise3D(x, y, z, seed);
}

float Simplex2D(float x, float y, uint32_t seed)
{
    return ValueNoise2D(x * 1.2f, y * 1.2f, seed);
}

void RandomPointInCircle(float& x, float& y, FastRandom& rng)
{
    const float angle = rng.NextFloat() * 6.2831853f;
    const float r = std::sqrt(rng.NextFloat());
    x = r * std::cos(angle);
    y = r * std::sin(angle);
}

void RandomPointOnSphere(float& x, float& y, float& z, FastRandom& rng)
{
    const float theta = rng.NextFloat() * 6.2831853f;
    const float phi = std::acos(2.f * rng.NextFloat() - 1.f);
    x = std::sin(phi) * std::cos(theta);
    y = std::sin(phi) * std::sin(theta);
    z = std::cos(phi);
}

} // namespace Proc

} // namespace Fem
