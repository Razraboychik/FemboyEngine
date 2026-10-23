// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Visibility.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Fem
{

namespace
{
constexpr float kEpsilon = 1e-5f;
constexpr float kAngleEpsilon = 0.0005f;

float Cross(sf::Vector2f a, sf::Vector2f b)
{
    return a.x * b.y - a.y * b.x;
}

float Length(sf::Vector2f v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

sf::Vector2f Normalized(sf::Vector2f v, sf::Vector2f fallback = {1.f, 0.f})
{
    const float len = Length(v);
    if (len <= kEpsilon)
    {
        return fallback;
    }
    return v / len;
}
} // namespace

VisibilityWorld::CellKey VisibilityWorld::CellOf(sf::Vector2f p) const
{
    const float size = std::max(1.f, cellSize);
    return CellKey{static_cast<int>(std::floor(p.x / size)), static_cast<int>(std::floor(p.y / size))};
}

void VisibilityWorld::Clear()
{
    segments_.clear();
    cells_.clear();
    dirty_ = true;
}

int VisibilityWorld::AddSegment(sf::Vector2f a, sf::Vector2f b, float absorption, int material)
{
    const sf::Vector2f delta = b - a;
    if (delta.x * delta.x + delta.y * delta.y <= kEpsilon * kEpsilon)
    {
        return -1;
    }

    OccluderSegment segment;
    segment.a = a;
    segment.b = b;
    segment.absorption = std::clamp(absorption, 0.f, 1.f);
    segment.material = material;
    segments_.push_back(segment);
    const int index = static_cast<int>(segments_.size()) - 1;
    // The grid is rebuilt lazily in one pass by build(); inserting immediately
    // would be wasted work when a level adds a hundred edges in a row.
    dirty_ = true;
    return index;
}

void VisibilityWorld::AddBox(const sf::FloatRect& rect, float absorption, int material)
{
    const sf::Vector2f p0 = rect.position;
    const sf::Vector2f p1{rect.position.x + rect.size.x, rect.position.y};
    const sf::Vector2f p2{rect.position.x + rect.size.x, rect.position.y + rect.size.y};
    const sf::Vector2f p3{rect.position.x, rect.position.y + rect.size.y};
    AddSegment(p0, p1, absorption, material);
    AddSegment(p1, p2, absorption, material);
    AddSegment(p2, p3, absorption, material);
    AddSegment(p3, p0, absorption, material);
}

void VisibilityWorld::AddCircle(sf::Vector2f center, float radius, int segments, float absorption, int material)
{
    segments = std::clamp(segments, 3, 128);
    if (radius <= 0.f)
    {
        return;
    }
    const float step = 6.283185307179586f / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i)
    {
        const float a0 = step * static_cast<float>(i);
        const float a1 = step * static_cast<float>(i + 1);
        AddSegment({center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius},
                   {center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius}, absorption, material);
    }
}

void VisibilityWorld::InsertIntoCells(int index) const
{
    const OccluderSegment& segment = segments_[static_cast<std::size_t>(index)];
    const float size = std::max(1.f, cellSize);

    const int x0 = static_cast<int>(std::floor(std::min(segment.a.x, segment.b.x) / size));
    const int x1 = static_cast<int>(std::floor(std::max(segment.a.x, segment.b.x) / size));
    const int y0 = static_cast<int>(std::floor(std::min(segment.a.y, segment.b.y) / size));
    const int y1 = static_cast<int>(std::floor(std::max(segment.a.y, segment.b.y) / size));

    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            cells_[CellKey{x, y}].push_back(index);
        }
    }
}

void VisibilityWorld::Build() const
{
    if (!dirty_)
    {
        return;
    }
    cells_.clear();
    for (int i = 0; i < static_cast<int>(segments_.size()); ++i)
    {
        InsertIntoCells(i);
    }
    dirty_ = false;
}

bool VisibilityWorld::IntersectRaySegment(sf::Vector2f origin, sf::Vector2f direction, float maxDistance,
                                          const OccluderSegment& segment, float& outT, sf::Vector2f& outNormal)
{
    const sf::Vector2f edge = segment.b - segment.a;
    const float denom = Cross(direction, edge);
    if (std::abs(denom) <= kEpsilon)
    {
        return false;
    }

    const sf::Vector2f diff = segment.a - origin;
    const float t = Cross(diff, edge) / denom;
    const float u = Cross(diff, direction) / denom;
    if (t < 0.f || t > maxDistance || u < 0.f || u > 1.f)
    {
        return false;
    }

    // The normal faces the ray so callers can offset a reflection or a probe.
    sf::Vector2f normal = Normalized({edge.y, -edge.x});
    if (normal.x * direction.x + normal.y * direction.y > 0.f)
    {
        normal = -normal;
    }
    outT = t;
    outNormal = normal;
    return true;
}

RayHit VisibilityWorld::Raycast(sf::Vector2f origin, sf::Vector2f direction, float maxDistance) const
{
    Build();

    RayHit best;
    const float dirLength = Length(direction);
    if (dirLength <= kEpsilon)
    {
        return best;
    }
    direction /= dirLength;
    if (maxDistance <= 0.f)
    {
        maxDistance = 100000.f;
    }

    // Walk the broadphase with a half-cell step and test a 3x3 neighbourhood. It
    // is slightly more work than a strict DDA, but it cannot miss a wall lying
    // exactly on a cell boundary, which is common for axis-aligned rooms.
    std::unordered_set<int> tested;
    const float size = std::max(1.f, cellSize) * 0.5f;
    const int steps = std::min(4096, static_cast<int>(std::ceil(maxDistance / size)) + 1);

    for (int step = 0; step <= steps; ++step)
    {
        const sf::Vector2f point = origin + direction * (static_cast<float>(step) * size);
        const CellKey base = CellOf(point);
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const auto it = cells_.find(CellKey{base.x + dx, base.y + dy});
                if (it == cells_.end())
                {
                    continue;
                }
                for (int index : it->second)
                {
                    if (!tested.insert(index).second)
                    {
                        continue;
                    }
                    float t = 0.f;
                    sf::Vector2f normal{};
                    if (IntersectRaySegment(origin, direction, maxDistance, segments_[static_cast<std::size_t>(index)],
                                            t, normal) &&
                        (!best.hit || t < best.distance))
                    {
                        best.hit = true;
                        best.distance = t;
                        best.point = origin + direction * t;
                        best.normal = normal;
                        best.segmentIndex = index;
                        best.material = segments_[static_cast<std::size_t>(index)].material;
                        best.absorption = segments_[static_cast<std::size_t>(index)].absorption;
                    }
                }
            }
        }
    }
    return best;
}

bool VisibilityWorld::LineOfSight(sf::Vector2f a, sf::Vector2f b, float* firstAbsorption) const
{
    const sf::Vector2f delta = b - a;
    const float distance = Length(delta);
    if (distance <= kEpsilon)
    {
        return true;
    }

    const RayHit hit = Raycast(a, delta, distance);
    if (hit.hit && hit.distance < distance - 0.01f)
    {
        if (firstAbsorption)
        {
            *firstAbsorption = hit.absorption;
        }
        return false;
    }
    if (firstAbsorption)
    {
        *firstAbsorption = 0.f;
    }
    return true;
}

std::vector<sf::Vector2f> VisibilityWorld::VisibilityPolygon(sf::Vector2f origin, float radius,
                                                             float defaultRadius) const
{
    Build();

    const float maxDistance = radius > 0.f ? radius : (defaultRadius > 0.f ? defaultRadius : this->defaultRadius);
    std::vector<float> angles;
    angles.reserve(segments_.size() * 2 + 8);

    const float radiusSq = maxDistance * maxDistance;
    for (const OccluderSegment& segment : segments_)
    {
        for (const sf::Vector2f point : {segment.a, segment.b})
        {
            const sf::Vector2f delta = point - origin;
            if (delta.x * delta.x + delta.y * delta.y > radiusSq)
            {
                continue;
            }
            const float angle = std::atan2(delta.y, delta.x);
            angles.push_back(angle);
            angles.push_back(angle + kAngleEpsilon);
            angles.push_back(angle - kAngleEpsilon);
        }
    }

    std::sort(angles.begin(), angles.end());
    angles.erase(std::unique(angles.begin(), angles.end(),
                             [](float a, float b) { return std::abs(a - b) < kAngleEpsilon * 0.25f; }),
                 angles.end());

    if (angles.empty())
    {
        // No endpoints nearby: a segmented circle is still a useful light shape
        // and avoids a special case in every consumer.
        std::vector<sf::Vector2f> circle;
        constexpr int kSegments = 16;
        circle.reserve(kSegments);
        for (int i = 0; i < kSegments; ++i)
        {
            const float a = 6.283185307179586f * static_cast<float>(i) / static_cast<float>(kSegments);
            circle.push_back({origin.x + std::cos(a) * maxDistance, origin.y + std::sin(a) * maxDistance});
        }
        return circle;
    }

    std::vector<sf::Vector2f> polygon;
    polygon.reserve(angles.size());
    for (float angle : angles)
    {
        const sf::Vector2f direction{std::cos(angle), std::sin(angle)};
        const RayHit hit = Raycast(origin, direction, maxDistance);
        const sf::Vector2f point = hit.hit ? hit.point : origin + direction * maxDistance;
        if (!polygon.empty())
        {
            const sf::Vector2f delta = point - polygon.back();
            if (delta.x * delta.x + delta.y * delta.y < 0.01f)
            {
                continue;
            }
        }
        polygon.push_back(point);
    }
    return polygon;
}

} // namespace Fem
