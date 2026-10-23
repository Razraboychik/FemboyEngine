// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <cstdint>
#include <random>

namespace Fem
{

// Fast, memory-efficient PRNG. Uses PCG + xorshift. No heap allocations.
class FastRandom
{
public:
    explicit FastRandom(uint64_t seed = 0);
    void Seed(uint64_t s);

    // Core
    uint32_t NextU32();
    uint64_t NextU64();
    float NextFloat(); // [0,1)
    float NextFloat(float min, float max);
    int NextInt(int min, int max); // inclusive [min,max]
    bool NextBool(float probability = 0.5f);

    // Gaussian
    float NextGaussian(float mean = 0.f, float stddev = 1.f);

    // Weighted choice. Negative weights are treated as zero; returns -1 for an
    // empty/invalid table.
    int WeightedChoice(const float* weights, int count);

    // Utility
    void Shuffle(int* arr, int count);

private:
    uint64_t state_;
    uint64_t inc_;
    // PCG
    uint32_t Pcg();
};

// Global instance (one per thread, so concurrent generation never races).
FastRandom& GlobalRandom();

// Procedural generation helpers
namespace Proc
{
// Value noise
float ValueNoise2D(float x, float y, uint32_t seed = 0);
float ValueNoise3D(float x, float y, float z, uint32_t seed = 0);
// Perlin noise
float Perlin2D(float x, float y, uint32_t seed = 0);
float Perlin3D(float x, float y, float z, uint32_t seed = 0);
// Simplex noise
float Simplex2D(float x, float y, uint32_t seed = 0);
// FBM (fractal Brownian motion)
float Fbm2D(float x, float y, int octaves, float lacunarity, float gain, uint32_t seed = 0);
// Cellular (Worley) noise
float Cellular2D(float x, float y, uint32_t seed = 0);
// Random point in circle
void RandomPointInCircle(float& x, float& y, FastRandom& rng);
// Random point on sphere surface
void RandomPointOnSphere(float& x, float& y, float& z, FastRandom& rng);
} // namespace Proc

} // namespace Fem
