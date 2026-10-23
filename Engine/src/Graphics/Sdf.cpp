// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Sdf.hpp"

#include "FemboyEngine/Tilemap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Fem
{

namespace
{
struct Seed
{
    int x = -1;
    int y = -1;
    bool valid = false;
};

long long DistanceSquared(int ax, int ay, int bx, int by)
{
    const long long dx = static_cast<long long>(ax) - bx;
    const long long dy = static_cast<long long>(ay) - by;
    return dx * dx + dy * dy;
}
} // namespace

void DistanceField::JumpFlood(int passes)
{
    if (width_ <= 0 || height_ <= 0)
    {
        return;
    }

    std::vector<Seed> seeds(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_));
    for (int y = 0; y < height_; ++y)
    {
        for (int x = 0; x < width_; ++x)
        {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
            if (solid_[index])
            {
                seeds[index] = Seed{x, y, true};
            }
        }
    }

    int step = 1;
    while (step * 2 < std::max(width_, height_))
    {
        step *= 2;
    }
    for (int pass = 0; pass < std::max(1, passes) && step >= 1; ++pass)
    {
        const int s = std::max(1, step);
        for (int y = 0; y < height_; ++y)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t index =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
                Seed best = seeds[index];
                long long bestDistance =
                    best.valid ? DistanceSquared(x, y, best.x, best.y) : std::numeric_limits<long long>::max();
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int nx = x + dx * s;
                        const int ny = y + dy * s;
                        if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_)
                        {
                            continue;
                        }
                        const Seed& candidate = seeds[static_cast<std::size_t>(ny) * static_cast<std::size_t>(width_) +
                                                      static_cast<std::size_t>(nx)];
                        if (!candidate.valid)
                        {
                            continue;
                        }
                        const long long d = DistanceSquared(x, y, candidate.x, candidate.y);
                        if (d < bestDistance)
                        {
                            bestDistance = d;
                            best = candidate;
                        }
                    }
                }
                seeds[index] = best;
            }
        }
        step /= 2;
    }

    distance_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0.f);
    for (int y = 0; y < height_; ++y)
    {
        for (int x = 0; x < width_; ++x)
        {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
            const Seed& seed = seeds[index];
            distance_[index] =
                seed.valid ? std::sqrt(static_cast<float>(DistanceSquared(x, y, seed.x, seed.y))) * cellSize_ : 1.0e9f;
        }
    }
}

bool DistanceField::Build(int width, int height, float cellSize, const std::function<bool(int, int)>& solid,
                          sf::Vector2f origin, int jumpPasses)
{
    if (width <= 0 || height <= 0 || !(cellSize > 0.f) || !solid)
    {
        return false;
    }

    width_ = std::min(width, 4096);
    height_ = std::min(height, 4096);
    cellSize_ = cellSize;
    origin_ = origin;
    solid_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0);

    for (int y = 0; y < height_; ++y)
    {
        for (int x = 0; x < width_; ++x)
        {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
            solid_[index] = solid(x, y) ? 1 : 0;
        }
    }

    JumpFlood(jumpPasses);
    return Valid();
}

bool DistanceField::BuildFromTilemap(const Tilemap& map, sf::Vector2f origin)
{
    if (map.Width() <= 0 || map.Height() <= 0)
    {
        return false;
    }
    return Build(
        map.Width(), map.Height(), static_cast<float>(map.TileSize()),
        [&map](int x, int y) { return map.IsSolid(x, y); }, origin);
}

float DistanceField::RawDistanceAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
    {
        return 1.0e9f;
    }
    return distance_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
}

float DistanceField::DistanceAt(float worldX, float worldY) const
{
    if (!Valid())
    {
        return 1.0e9f;
    }

    const float gx = (worldX - origin_.x) / cellSize_;
    const float gy = (worldY - origin_.y) / cellSize_;
    const int x0 = static_cast<int>(std::floor(gx));
    const int y0 = static_cast<int>(std::floor(gy));
    const float tx = gx - static_cast<float>(x0);
    const float ty = gy - static_cast<float>(y0);

    const float d00 = RawDistanceAt(x0, y0);
    const float d10 = RawDistanceAt(x0 + 1, y0);
    const float d01 = RawDistanceAt(x0, y0 + 1);
    const float d11 = RawDistanceAt(x0 + 1, y0 + 1);
    const float a = d00 + (d10 - d00) * tx;
    const float b = d01 + (d11 - d01) * tx;
    return a + (b - a) * ty;
}

bool DistanceField::SolidAt(float worldX, float worldY) const
{
    if (!Valid())
    {
        return false;
    }
    const int x = static_cast<int>(std::floor((worldX - origin_.x) / cellSize_));
    const int y = static_cast<int>(std::floor((worldY - origin_.y) / cellSize_));
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
    {
        return false;
    }
    return solid_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)] != 0;
}

float DistanceField::SignedDistanceAt(float worldX, float worldY) const
{
    const float d = DistanceAt(worldX, worldY);
    return SolidAt(worldX, worldY) ? -std::max(d, cellSize_ * 0.5f) : d;
}

float DistanceField::SmoothFalloff(float distance, float radius)
{
    if (!(radius > 0.f))
    {
        return distance <= 0.f ? 0.f : 1.f;
    }
    const float t = std::clamp(distance / radius, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float DistanceField::AoAt(float worldX, float worldY, float radiusPixels) const
{
    // A corner is dark because two walls are close, not only one. Sampling four
    // directions around the point approximates that cheaply without a second
    // distance field.
    const float radius = std::max(1.f, radiusPixels);
    const float d = DistanceAt(worldX, worldY);
    const float direct = SmoothFalloff(d, radius);
    const float diagonal = SmoothFalloff(DistanceAt(worldX + radius * 0.5f, worldY + radius * 0.5f), radius);
    return std::clamp(direct * 0.7f + diagonal * 0.3f, 0.f, 1.f);
}

sf::Image DistanceField::ToImage() const
{
    sf::Image image;
    if (!Valid())
    {
        return image;
    }

    image.resize({static_cast<unsigned>(width_), static_cast<unsigned>(height_)}, sf::Color::White);
    float maxDistance = 1.f;
    for (float d : distance_)
    {
        if (d < 1.0e8f)
        {
            maxDistance = std::max(maxDistance, d);
        }
    }

    for (int y = 0; y < height_; ++y)
    {
        for (int x = 0; x < width_; ++x)
        {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
            const float t = solid_[index] ? 0.f : std::clamp(distance_[index] / maxDistance, 0.f, 1.f);
            const auto grey = static_cast<std::uint8_t>(std::clamp(t, 0.f, 1.f) * 255.f);
            image.setPixel({static_cast<unsigned>(x), static_cast<unsigned>(y)}, sf::Color(grey, grey, grey));
        }
    }
    return image;
}

} // namespace Fem
