// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <vector>
#include <cstdint>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/Tilemap.hpp"

namespace Fem
{

// Perlin-like noise and procedural generation utilities.
class Noise
{
public:
    // Deterministic and thread-safe: the permutation table is built locally per
    // call, so two threads never race on shared state.
    static float Perlin2D(float x, float y, int seed = 0);
    static float Simplex2D(float x, float y, int seed = 0);
    static float Fbm(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f, int seed = 0);

    // Generate a tilemap from noise. id 1 = land, id 0 = water; water is solid.
    static void GenerateTilemap(Tilemap& map, float scale = 0.1f, float threshold = 0.5f, int seed = 0);
};

} // namespace Fem
