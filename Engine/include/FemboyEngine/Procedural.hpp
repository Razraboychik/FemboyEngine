// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <cstdint>
#include <vector>
#include <utility>
#include <cmath>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/Tilemap.hpp"

namespace Fem
{

// Procedural generation: noise functions, terrain, dungeon generation.
class Procedural
{
public:
    // Perlin noise (2D)
    static float Perlin2D(float x, float y, uint32_t seed = 0);
    // Simplex noise (2D)
    static float Simplex2D(float x, float y, uint32_t seed = 0);
    // Worley (cellular) noise
    static float Worley2D(float x, float y, uint32_t seed = 0);
    // Fractal Brownian Motion (layered noise)
    static float Fbm2D(float x, float y, int octaves = 6, float persistence = 0.5f, float lacunarity = 2.0f,
                       uint32_t seed = 0);

    // Generate a heightmap of width*height samples.
    static std::vector<float> Heightmap(int width, int height, float scale = 0.01f, int octaves = 6, uint32_t seed = 0);

    // Generate a dungeon (rooms + corridors) directly on a tilemap.
    // Floor tiles get id 1, walls get id 0 and are solid. Existing contents of
    // the map are replaced (its size is used, width/height arguments are gone).
    static void GenerateDungeon(Tilemap& map, int roomCount = 8, int minRoomSize = 4, int maxRoomSize = 10,
                                uint32_t seed = 0);

    // Poisson disk sampling
    static std::vector<std::pair<float, float>> PoissonDisk(float width, float height, float minDist,
                                                            int maxAttempts = 30, uint32_t seed = 0);

private:
    static float Fade(float t);
    static float Lerp(float a, float b, float t);
    static int Hash(int x, int y, uint32_t seed);
};

} // namespace Fem
