// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <cmath>

namespace Fem
{

enum class ProjectionMode
{
    Orthographic,
    Isometric
};

// World-to-view projection.
//
// An isometric view is an affine map, not a different camera: (x, y) becomes
// ((x - y) * axisX, (x + y) * axisY). Keeping it as an sf::Transform has three
// useful consequences:
//   * sprites, tiles and procedural geometry all share one transform;
//   * screen<->world helpers are exact inverses;
//   * switching projection never changes the scene or the renderer backend.
//
// sf::View alone cannot represent it (a view is translation/zoom/rotation only),
// which is why the renderer applies this transform to world-space commands.
struct ViewProjection
{
    ProjectionMode mode = ProjectionMode::Orthographic;
    // Half-width/half-height of an isometric tile in screen pixels for a
    // one-unit step. The classic 2:1 tile uses (1, 0.5).
    float isoAxisX = 1.f;
    float isoAxisY = 0.5f;

    sf::Vector2f Project(sf::Vector2f world) const
    {
        if (mode == ProjectionMode::Orthographic)
        {
            return world;
        }
        return {(world.x - world.y) * isoAxisX, (world.x + world.y) * isoAxisY};
    }

    sf::Vector2f Unproject(sf::Vector2f view) const
    {
        if (mode == ProjectionMode::Orthographic)
        {
            return view;
        }
        const float a = isoAxisX != 0.f ? isoAxisX : 1.f;
        const float b = isoAxisY != 0.f ? isoAxisY : 1.f;
        const float sum = view.y / b;
        const float diff = view.x / a;
        return {(sum + diff) * 0.5f, (sum - diff) * 0.5f};
    }

    // Linear transform, no translation: Camera2D already positions the view at
    // the projected camera center.
    sf::Transform WorldTransform() const
    {
        if (mode == ProjectionMode::Orthographic)
        {
            return sf::Transform::Identity;
        }
        return sf::Transform(isoAxisX, -isoAxisX, 0.f, isoAxisY, isoAxisY, 0.f, 0.f, 0.f, 1.f);
    }

    bool IsIsometric() const
    {
        return mode == ProjectionMode::Isometric;
    }
};

} // namespace Fem
