// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/ECS.hpp"

namespace Fem
{

// 2D primitive shapes as actors with collision (2D meshes).
enum class ShapeType
{
    Circle,
    Rectangle,
    Triangle,
    Polygon,
    Capsule
};

struct ShapeComponent
{
    ShapeType type = ShapeType::Rectangle;
    sf::Vector2f size{32, 32};        // for rect / triangle / capsule
    float radius = 16.f;              // for circle
    std::vector<sf::Vector2f> points; // for polygon
    sf::Color fillColor = sf::Color(120, 180, 255);
    sf::Color outlineColor = sf::Color::White;
    float outlineThickness = 1.f;
    bool isTrigger = false;
};

struct ColliderComponent
{
    ShapeType type = ShapeType::Rectangle;
    sf::Vector2f size{32, 32};
    float radius = 16.f;
    std::vector<sf::Vector2f> points;
    bool isTrigger = false;
    bool isStatic = false;
};

class ShapeRenderer
{
public:
    // Shapes are positioned by their top-left corner (like TransformComponent);
    // circle/polygon geometry is offset around the transform origin.
    static void RenderShape(sf::RenderWindow& window, const ShapeComponent& s, const TransformComponent& t);

    // Center-based overlap test. Transform x/y is treated as the shape center
    // for circles and as the top-left corner for boxes; both are normalized.
    static bool TestOverlap(const ColliderComponent& a, const TransformComponent& ta, const ColliderComponent& b,
                            const TransformComponent& tb);

    static sf::Vector2f CenterOf(const ColliderComponent& c, const TransformComponent& t);
    static float BoundingRadius(const ColliderComponent& c);
};

} // namespace Fem
