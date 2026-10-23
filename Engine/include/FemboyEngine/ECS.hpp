// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <entt/entt.hpp>
#include <SFML/Graphics.hpp>
#include <cstdint>
#include <string>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/Animation.hpp"
#ifdef FEMBOY_HAS_BOX2D
#include "box2d/box2d.h"
#endif

namespace Fem
{

using Entity = entt::entity;
using Registry = entt::registry;

// Common components
struct TransformComponent
{
    float x = 0, y = 0;
    float scaleX = 1, scaleY = 1;
    float rotation = 0;

    // Previous fixed-step state, used to interpolate rendering between steps.
    // These are runtime-only and deliberately not reflected/serialized.
    float prevX = 0, prevY = 0;
    float prevScaleX = 1, prevScaleY = 1;
    float prevRotation = 0;
    bool hasPrev = false;
};

struct SpriteComponent
{
    std::string textureId;
    sf::Color tint = sf::Color::White;
    // Fractional depth so two actors can share an integer layer while still
    // sorting correctly (isometric maps especially).
    float z = 0.f;

    // Sprite-sheet source rectangle in texture pixels. A zero width or height
    // means "use the whole texture", so existing scenes need no migration.
    int srcX = 0, srcY = 0, srcW = 0, srcH = 0;
    // Normalized pivot: (0.5, 0.5) is the centre (the historical behaviour).
    float originX = 0.5f, originY = 0.5f;
    bool flipX = false, flipY = false;
};

// Drives SpriteComponent::src* from an Animator. The engine updates it on the
// fixed step and the renderer reads the resulting source rectangle.
struct AnimationComponent
{
    Animator animator;
};

struct TagComponent
{
    std::string tag;
};

// Editor-facing scene organization. The parent is stored as an entity id
// rather than an entt::entity so it survives scene serialization unchanged.
inline constexpr std::uint32_t kNoParent = 0xFFFFFFFFu;
struct HierarchyComponent
{
    std::uint32_t parent = kNoParent;
    bool expanded = true;
};

// A persistent, named collection. Unlike TagComponent it is explicitly about
// editor organization, not gameplay semantics.
struct GroupComponent
{
    std::string group;
};

#ifdef FEMBOY_HAS_BOX2D
struct PhysicsBodyComponent
{
    b2BodyId bodyId{};
    bool isDynamic = true;
    float density = 1.f;
    float friction = 0.3f;
    bool created = false;
};
#endif

} // namespace Fem
