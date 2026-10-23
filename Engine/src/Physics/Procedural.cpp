// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Procedural.hpp"
#include "FemboyEngine/FastRandom.hpp"
#include "FemboyEngine/Boy.hpp"
#include <algorithm>
#include <limits>

namespace Fem
{

float Procedural::Fade(float t)
{
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}
float Procedural::Lerp(float a, float b, float t)
{
    return a + t * (b - a);
}

int Procedural::Hash(int x, int y, uint32_t seed)
{
    // All arithmetic in unsigned space: signed multiplication overflow is UB.
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 374761393u;
    h ^= static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<int>(h ^ (h >> 16));
}

float Procedural::Perlin2D(float x, float y, uint32_t seed)
{
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float xf = x - static_cast<float>(xi);
    const float yf = y - static_cast<float>(yi);
    const float u = Fade(xf);
    const float v = Fade(yf);

    // Dot product of the corner's pseudo-random gradient with the vector from
    // that corner to the sample point. The old code evaluated every corner with
    // the same offset, which collapsed the noise toward a single gradient.
    auto grad = [&](int cx, int cy)
    {
        const int h = Hash(cx, cy, seed) & 3;
        float gx = 0.f, gy = 0.f;
        switch (h)
        {
            case 0:
                gx = 1.f;
                gy = 1.f;
                break;
            case 1:
                gx = -1.f;
                gy = 1.f;
                break;
            case 2:
                gx = 1.f;
                gy = -1.f;
                break;
            default:
                gx = -1.f;
                gy = -1.f;
                break;
        }
        const float dx = xf - static_cast<float>(cx - xi);
        const float dy = yf - static_cast<float>(cy - yi);
        return gx * dx + gy * dy;
    };

    const float n00 = grad(xi, yi);
    const float n10 = grad(xi + 1, yi);
    const float n01 = grad(xi, yi + 1);
    const float n11 = grad(xi + 1, yi + 1);
    const float x1 = Lerp(n00, n10, u);
    const float x2 = Lerp(n01, n11, u);
    return Lerp(x1, x2, v);
}

float Procedural::Simplex2D(float x, float y, uint32_t seed)
{
    // Approximated with Perlin; kept for API stability.
    return Perlin2D(x * 1.2f, y * 1.2f, seed);
}

float Procedural::Worley2D(float x, float y, uint32_t seed)
{
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    float minDist = std::numeric_limits<float>::max();

    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int cx = xi + dx;
            const int cy = yi + dy;
            // Independent hashes for x and y: the old code reused the same
            // neighbour cell for both coordinates.
            const float px = static_cast<float>(cx) + static_cast<float>(Hash(cx, cy, seed) & 0xFFFF) / 65535.f;
            const float py =
                static_cast<float>(cy) + static_cast<float>(Hash(cx, cy, seed ^ 0x9E3779B9u) & 0xFFFF) / 65535.f;
            const float d = std::sqrt((x - px) * (x - px) + (y - py) * (y - py));
            if (d < minDist)
            {
                minDist = d;
            }
        }
    }
    return minDist;
}

float Procedural::Fbm2D(float x, float y, int octaves, float persistence, float lacunarity, uint32_t seed)
{
    if (octaves <= 0)
    {
        return 0.f;
    }
    if (octaves > 32)
    {
        octaves = 32;
    }

    float total = 0.f;
    float amp = 1.f;
    float freq = 1.f;
    float maxVal = 0.f;

    for (int i = 0; i < octaves; ++i)
    {
        total += Perlin2D(x * freq, y * freq, seed + static_cast<uint32_t>(i) * 100u) * amp;
        maxVal += std::abs(amp);
        amp *= persistence;
        freq *= lacunarity;
    }
    return (maxVal > 0.f) ? (total / maxVal) : 0.f;
}

std::vector<float> Procedural::Heightmap(int width, int height, float scale, int octaves, uint32_t seed)
{
    if (width <= 0 || height <= 0)
    {
        return {};
    }

    std::vector<float> hm(static_cast<size_t>(width) * static_cast<size_t>(height), 0.f);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            hm[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] =
                Fbm2D(static_cast<float>(x) * scale, static_cast<float>(y) * scale, octaves, 0.5f, 2.f, seed);
        }
    }
    return hm;
}

std::vector<std::pair<float, float>> Procedural::PoissonDisk(float width, float height, float minDist, int maxAttempts,
                                                             uint32_t seed)
{
    std::vector<std::pair<float, float>> points;
    if (width <= 0.f || height <= 0.f || minDist <= 0.f)
    {
        return points;
    }
    if (maxAttempts <= 0)
    {
        maxAttempts = 1;
    }

    FastRandom rng(seed);
    std::vector<std::pair<float, float>> active;

    auto inRect = [&](float x, float y) { return x >= 0.f && y >= 0.f && x < width && y < height; };
    auto tooClose = [&](float x, float y)
    {
        const float md2 = minDist * minDist;
        for (const auto& p : points)
        {
            const float dx = p.first - x;
            const float dy = p.second - y;
            if (dx * dx + dy * dy < md2)
            {
                return true;
            }
        }
        return false;
    };

    points.push_back({rng.NextFloat() * width, rng.NextFloat() * height});
    active.push_back(points.back());

    // Hard cap so a pathological minDist (tiny relative to the area) cannot
    // allocate without bound and stall the frame.
    const size_t maxPoints = 65536;
    while (!active.empty() && points.size() < maxPoints)
    {
        const int idx = rng.NextInt(0, static_cast<int>(active.size()) - 1);
        const auto pt = active[static_cast<size_t>(idx)]; // copy: the vector may reallocate below
        bool found = false;

        for (int a = 0; a < maxAttempts; ++a)
        {
            const float angle = rng.NextFloat() * 6.2831853f;
            const float dist = minDist + rng.NextFloat() * minDist;
            const float nx = pt.first + std::cos(angle) * dist;
            const float ny = pt.second + std::sin(angle) * dist;
            if (inRect(nx, ny) && !tooClose(nx, ny))
            {
                points.push_back({nx, ny});
                active.push_back({nx, ny});
                found = true;
                break;
            }
        }

        if (!found)
        {
            active.erase(active.begin() + idx);
        }
    }
    return points;
}

void Procedural::GenerateDungeon(Tilemap& map, int roomCount, int minRoomSize, int maxRoomSize, uint32_t seed)
{
    const int w = map.Width();
    const int h = map.Height();
    if (w <= 0 || h <= 0)
    {
        return;
    }

    if (roomCount < 0)
    {
        roomCount = 0;
    }
    if (minRoomSize < 2)
    {
        minRoomSize = 2;
    }
    if (maxRoomSize < minRoomSize)
    {
        maxRoomSize = minRoomSize;
    }
    maxRoomSize = std::min(maxRoomSize, std::min(w, h) - 2);
    if (maxRoomSize < 2)
    {
        BOY_WARN("Procedural", "generateDungeon: map too small");
        return;
    }
    minRoomSize = std::min(minRoomSize, maxRoomSize);

    // Start from solid rock (id 0), carve floors (id 1).
    map.Clear(0, true);

    FastRandom rng(seed);

    struct Room
    {
        int x, y, w, h;
    };
    std::vector<Room> rooms;
    rooms.reserve(static_cast<size_t>(roomCount));

    const int maxTries = std::max(roomCount * 12, 12);
    for (int attempt = 0; attempt < maxTries && static_cast<int>(rooms.size()) < roomCount; ++attempt)
    {
        const int rw = rng.NextInt(minRoomSize, maxRoomSize);
        const int rh = rng.NextInt(minRoomSize, maxRoomSize);
        if (rw + 2 >= w || rh + 2 >= h)
        {
            continue;
        }

        const int rx = rng.NextInt(1, w - rw - 1);
        const int ry = rng.NextInt(1, h - rh - 1);

        // One tile of padding so rooms never share walls.
        bool overlaps = false;
        for (const auto& other : rooms)
        {
            if (rx - 1 < other.x + other.w + 1 && rx + rw + 1 > other.x - 1 && ry - 1 < other.y + other.h + 1 &&
                ry + rh + 1 > other.y - 1)
            {
                overlaps = true;
                break;
            }
        }
        if (overlaps)
        {
            continue;
        }

        rooms.push_back({rx, ry, rw, rh});
    }

    if (rooms.empty())
    {
        // Guarantee at least one playable room in the middle.
        const int rw = std::min(maxRoomSize, w - 2);
        const int rh = std::min(maxRoomSize, h - 2);
        rooms.push_back({std::max(1, (w - rw) / 2), std::max(1, (h - rh) / 2), rw, rh});
    }

    for (const auto& r : rooms)
    {
        for (int y = r.y; y < r.y + r.h; ++y)
        {
            for (int x = r.x; x < r.x + r.w; ++x)
            {
                map.SetTile(x, y, 1, false);
            }
        }
    }

    // Connect consecutive room centres with L-shaped corridors.
    auto carveH = [&](int x0, int x1, int y)
    {
        if (x0 > x1)
        {
            std::swap(x0, x1);
        }
        for (int x = x0; x <= x1; ++x)
        {
            map.SetTile(x, y, 1, false);
        }
    };
    auto carveV = [&](int y0, int y1, int x)
    {
        if (y0 > y1)
        {
            std::swap(y0, y1);
        }
        for (int y = y0; y <= y1; ++y)
        {
            map.SetTile(x, y, 1, false);
        }
    };

    for (size_t i = 1; i < rooms.size(); ++i)
    {
        const auto& a = rooms[i - 1];
        const auto& b = rooms[i];
        const int ax = a.x + a.w / 2;
        const int ay = a.y + a.h / 2;
        const int bx = b.x + b.w / 2;
        const int by = b.y + b.h / 2;

        if (rng.NextBool())
        {
            carveH(ax, bx, ay);
            carveV(ay, by, bx);
        }
        else
        {
            carveV(ay, by, ax);
            carveH(ax, bx, by);
        }
    }

    BOY_INFO("Procedural", "generateDungeon: " + std::to_string(rooms.size()) + " rooms on " + std::to_string(w) + "x" +
                               std::to_string(h));
}

} // namespace Fem
