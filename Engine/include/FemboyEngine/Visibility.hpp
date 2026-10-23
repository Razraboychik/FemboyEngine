// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <SFML/Graphics.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Fem
{

// A wall, a prop edge, or a glass pane. `absorption` is how much of a sound
// wave (or light ray) the segment takes away when crossed: 0 is an open edge,
// 0.6 is a curtain, 1 is concrete. `material` is a free-form id a game can map
// to footstep sounds or a colour.
struct OccluderSegment
{
    sf::Vector2f a{0.f, 0.f};
    sf::Vector2f b{0.f, 0.f};
    float absorption = 1.f;
    int material = 0;
};

struct RayHit
{
    bool hit = false;
    sf::Vector2f point{0.f, 0.f};
    sf::Vector2f normal{0.f, 0.f};
    float distance = 0.f;
    int segmentIndex = -1;
    int material = 0;
    float absorption = 0.f;
};

// 2D visibility and ray queries over a set of line segments.
//
// This is the shared geometry layer for three systems that all ask the same
// question - "what can see what":
//   * Lighting: the visibility polygon of a light becomes its lit area.
//   * AudioOcclusion: a ray from listener to source returns the walls between.
//   * Gameplay: line-of-sight for AI, lasers, grapple points.
//
// Segments are indexed by a uniform grid (not a full BVH). For the few hundred
// walls a 2D level has, this makes a ray test touch only the cells it crosses;
// a visibility sweep still examines every nearby endpoint, which is O(n) rays
// and therefore O(n^2) worst case, but n stays small in practice.
//
// All coordinates are world pixels, y-down, matching the renderer.
class VisibilityWorld
{
public:
    void Clear();
    // Adds an edge. Zero-length segments are ignored.
    int AddSegment(sf::Vector2f a, sf::Vector2f b, float absorption = 1.f, int material = 0);
    // Convenience shapes. Circles are chords, not a mathematical curve, which
    // matches how a 2D character or a round pillar is drawn.
    void AddBox(const sf::FloatRect& rect, float absorption = 1.f, int material = 0);
    void AddCircle(sf::Vector2f center, float radius, int segments = 16, float absorption = 1.f, int material = 0);
    // Rebuilds the broadphase. Called automatically by the first query after a
    // change; call it explicitly after a batch of addSegment() calls.
    void Build() const;

    size_t SegmentCount() const
    {
        return segments_.size();
    }
    const std::vector<OccluderSegment>& Segments() const
    {
        return segments_;
    }
    bool Empty() const
    {
        return segments_.empty();
    }

    // Nearest intersection along a ray. `direction` does not need to be
    // normalized. `maxDistance` <= 0 means "unbounded".
    RayHit Raycast(sf::Vector2f origin, sf::Vector2f direction, float maxDistance = 0.f) const;
    // True when the straight segment a->b crosses no occluder. A hit exactly at
    // the endpoints is ignored, so a listener standing against a wall can still
    // hear a source on the same side.
    bool LineOfSight(sf::Vector2f a, sf::Vector2f b, float* firstAbsorption = nullptr) const;

    // Boundary of the area visible from `origin`, ordered by angle. The origin
    // itself is not part of the result: build a triangle fan from origin to each
    // pair of consecutive points. A radius <= 0 uses `defaultRadius`.
    std::vector<sf::Vector2f> VisibilityPolygon(sf::Vector2f origin, float radius, float defaultRadius = 1200.f) const;

    float defaultRadius = 1200.f;
    float cellSize = 64.f;

private:
    struct CellKey
    {
        int x = 0;
        int y = 0;
        bool operator==(const CellKey& other) const
        {
            return x == other.x && y == other.y;
        }
    };
    struct CellHash
    {
        std::size_t operator()(const CellKey& c) const noexcept
        {
            const std::uint64_t ux = static_cast<std::uint32_t>(c.x);
            const std::uint64_t uy = static_cast<std::uint32_t>(c.y);
            return static_cast<std::size_t>((ux << 32) ^ uy);
        }
    };

    CellKey CellOf(sf::Vector2f p) const;
    void InsertIntoCells(int index) const;
    static bool IntersectRaySegment(sf::Vector2f origin, sf::Vector2f direction, float maxDistance,
                                    const OccluderSegment& segment, float& outT, sf::Vector2f& outNormal);

    std::vector<OccluderSegment> segments_;
    mutable std::unordered_map<CellKey, std::vector<int>, CellHash> cells_;
    mutable bool dirty_ = true;
};

} // namespace Fem
