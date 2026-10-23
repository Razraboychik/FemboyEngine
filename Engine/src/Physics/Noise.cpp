// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Noise.hpp"
#include <cmath>
#include <algorithm>
#include <array>

namespace Fem
{

namespace
{
float Fade(float t)
{
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}
float Lerp(float a, float b, float t)
{
    return a + t * (b - a);
}

float Grad(int hash, float x, float y)
{
    // Four diagonal gradients. The previous table produced (-2..2) magnitudes
    // for some hashes, which made the noise field anisotropic.
    const int h = hash & 3;
    const float u = (h < 2) ? x : y;
    const float v = (h < 2) ? y : x;
    const float a = ((h & 1) == 0) ? u : -u;
    const float b = ((h & 2) == 0) ? v : -v;
    return a + b;
}

// Builds a permutation table for the given seed. Cached per thread, because
// rebuilding it on every perlin2D() call made fbm()/generateTilemap() quadratic:
// a 512x512 tilemap with 4 octaves rebuilt the table a million times. The cache
// keeps the exact same output, it just stops recomputing a constant table.
std::array<int, 512> MakePerm(int seed)
{
    std::array<int, 512> perm{};
    for (int i = 0; i < 256; ++i)
    {
        perm[static_cast<size_t>(i)] = i;
    }

    uint32_t s = static_cast<uint32_t>(seed);
    for (int i = 255; i > 0; --i)
    {
        s = s * 1103515245u + 12345u;
        const int j = static_cast<int>(s % static_cast<uint32_t>(i + 1));
        std::swap(perm[static_cast<size_t>(i)], perm[static_cast<size_t>(j)]);
    }
    for (int i = 0; i < 256; ++i)
    {
        perm[static_cast<size_t>(256 + i)] = perm[static_cast<size_t>(i)];
    }
    return perm;
}

// Small per-thread cache keyed by seed. Eight entries cover the common case
// (one seed per call site, plus up to eight fbm octaves).
constexpr int kPermCacheSlots = 8;

struct PermCache
{
    std::array<int, kPermCacheSlots> seeds{};
    std::array<std::array<int, 512>, kPermCacheSlots> tables{};
    int count = 0;
    int next = 0;

    const std::array<int, 512>& Get(int seed)
    {
        for (int i = 0; i < count; ++i)
        {
            if (seeds[static_cast<size_t>(i)] == seed)
            {
                return tables[static_cast<size_t>(i)];
            }
        }
        int slot;
        if (count < kPermCacheSlots)
        {
            slot = count++;
        }
        else
        {
            next = (next + 1) % kPermCacheSlots;
            slot = next;
        }
        seeds[static_cast<size_t>(slot)] = seed;
        tables[static_cast<size_t>(slot)] = MakePerm(seed);
        return tables[static_cast<size_t>(slot)];
    }
};

const std::array<int, 512>& PermForSeed(int seed)
{
    static thread_local PermCache cache;
    return cache.Get(seed);
}
} // namespace

float Noise::Perlin2D(float x, float y, int seed)
{
    const std::array<int, 512>& perm = PermForSeed(seed);

    const int xi = static_cast<int>(std::floor(x)) & 255;
    const int yi = static_cast<int>(std::floor(y)) & 255;
    const float xf = x - std::floor(x);
    const float yf = y - std::floor(y);
    const float u = Fade(xf);
    const float v = Fade(yf);

    const int aa = perm[static_cast<size_t>(perm[static_cast<size_t>(xi)] + yi)];
    const int ab = perm[static_cast<size_t>(perm[static_cast<size_t>(xi)] + yi + 1)];
    const int ba = perm[static_cast<size_t>(perm[static_cast<size_t>(xi + 1)] + yi)];
    const int bb = perm[static_cast<size_t>(perm[static_cast<size_t>(xi + 1)] + yi + 1)];

    const float x1 = Lerp(Grad(aa, xf, yf), Grad(ba, xf - 1.f, yf), u);
    const float x2 = Lerp(Grad(ab, xf, yf - 1.f), Grad(bb, xf - 1.f, yf - 1.f), u);
    return Lerp(x1, x2, v);
}

float Noise::Simplex2D(float x, float y, int seed)
{
    // Approximated with Perlin; kept as a separate entry point for API stability.
    return Perlin2D(x * 1.2f, y * 1.2f, seed);
}

float Noise::Fbm(float x, float y, int octaves, float lacunarity, float gain, int seed)
{
    if (octaves <= 0)
    {
        return 0.f;
    }
    if (octaves > 32)
    {
        octaves = 32;
    }

    float val = 0.f;
    float amp = 1.f;
    float freq = 1.f;
    float maxVal = 0.f;

    for (int i = 0; i < octaves; ++i)
    {
        val += Perlin2D(x * freq, y * freq, seed + i) * amp;
        maxVal += std::abs(amp);
        amp *= gain;
        freq *= lacunarity;
    }
    return (maxVal > 0.f) ? (val / maxVal) : 0.f;
}

void Noise::GenerateTilemap(Tilemap& map, float scale, float threshold, int seed)
{
    for (int y = 0; y < map.Height(); ++y)
    {
        for (int x = 0; x < map.Width(); ++x)
        {
            const float n = Fbm(static_cast<float>(x) * scale, static_cast<float>(y) * scale, 4, 2.f, 0.5f, seed);
            const bool land = n > threshold;
            map.SetTile(x, y, land ? 1 : 0, !land);
        }
    }
}

} // namespace Fem
