// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <SFML/Graphics.hpp>
#include <functional>
#include <vector>

namespace Fem
{

// Signed distance field of a 2D solid mask, built with jump flooding.
//
// The field is static: it is rebuilt when a level loads (or when an editor
// changes the mask), not per frame. That is the honest trade-off for a CPU
// engine. Dynamic geometry uses the VisibilityWorld instead; an SDF for moving
// walls would need a compute pass, which SFML cannot issue.
//
// Uses:
//   * cheap approximate ambient occlusion in corners (`aoAt`),
//   * soft shadow/light falloff (`smoothstep` on the distance),
//   * a debug/QA image of the level's free space.
class DistanceField
{
public:
    // `solid(x,y)` is evaluated in tile/cell coordinates. `cellSize` converts
    // the grid to world pixels; `origin` is the world position of cell (0,0).
    bool Build(int width, int height, float cellSize, const std::function<bool(int, int)>& solid,
               sf::Vector2f origin = {0.f, 0.f}, int jumpPasses = 8);
    bool BuildFromTilemap(const class Tilemap& map, sf::Vector2f origin = {0.f, 0.f});

    bool Valid() const
    {
        return width_ > 0 && height_ > 0 && !distance_.empty();
    }
    int Width() const
    {
        return width_;
    }
    int Height() const
    {
        return height_;
    }
    float CellSize() const
    {
        return cellSize_;
    }
    sf::Vector2f Origin() const
    {
        return origin_;
    }

    // Unsigned distance in world pixels to the nearest solid cell. Bilinear.
    float DistanceAt(float worldX, float worldY) const;
    // Negative inside a solid cell, positive in free space.
    float SignedDistanceAt(float worldX, float worldY) const;
    // True when the cell containing the world point is solid.
    bool SolidAt(float worldX, float worldY) const;

    // 1 in open space, fading to 0 at a wall over `radiusPixels`.
    float AoAt(float worldX, float worldY, float radiusPixels = 48.f) const;
    // Smooth 0..1 visibility-ish term for a point at `distance` from a wall.
    static float SmoothFalloff(float distance, float radius);

    // Debug/QA: white = free, black = solid, grey = distance.
    sf::Image ToImage() const;

private:
    float RawDistanceAt(int x, int y) const;
    void JumpFlood(int passes);

    int width_ = 0;
    int height_ = 0;
    float cellSize_ = 1.f;
    sf::Vector2f origin_{0.f, 0.f};
    std::vector<float> distance_;
    std::vector<std::uint8_t> solid_;
};

} // namespace Fem
