// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Shapes.hpp"
#include <cmath>
#include <algorithm>

namespace Fem
{

namespace
{
bool CircleVsCircle(float x1, float y1, float r1, float x2, float y2, float r2)
{
    const float dx = x1 - x2;
    const float dy = y1 - y2;
    const float rr = r1 + r2;
    return dx * dx + dy * dy <= rr * rr;
}

bool CircleVsBox(float cx, float cy, float r, float left, float top, float w, float h)
{
    const float closestX = std::clamp(cx, left, left + w);
    const float closestY = std::clamp(cy, top, top + h);
    const float dx = cx - closestX;
    const float dy = cy - closestY;
    return dx * dx + dy * dy <= r * r;
}

bool BoxVsBox(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}
} // namespace

sf::Vector2f ShapeRenderer::CenterOf(const ColliderComponent& c, const TransformComponent& t)
{
    switch (c.type)
    {
        case ShapeType::Circle:
        case ShapeType::Polygon:
            return {t.x, t.y};
        case ShapeType::Capsule:
        case ShapeType::Rectangle:
        case ShapeType::Triangle:
        default:
            return {t.x + c.size.x * 0.5f * t.scaleX, t.y + c.size.y * 0.5f * t.scaleY};
    }
}

float ShapeRenderer::BoundingRadius(const ColliderComponent& c)
{
    if (c.type == ShapeType::Circle)
    {
        return c.radius;
    }
    const float hw = c.size.x * 0.5f;
    const float hh = c.size.y * 0.5f;
    return std::sqrt(hw * hw + hh * hh);
}

void ShapeRenderer::RenderShape(sf::RenderWindow& window, const ShapeComponent& s, const TransformComponent& t)
{
    const sf::Vector2f scale{t.scaleX, t.scaleY};

    switch (s.type)
    {
        case ShapeType::Circle:
        {
            sf::CircleShape sh(s.radius);
            sh.setFillColor(s.fillColor);
            sh.setOutlineColor(s.outlineColor);
            sh.setOutlineThickness(s.outlineThickness);
            sh.setOrigin({s.radius, s.radius});
            sh.setPosition({t.x, t.y});
            sh.setScale(scale);
            sh.setRotation(sf::degrees(t.rotation));
            window.draw(sh);
            break;
        }
        case ShapeType::Rectangle:
        {
            sf::RectangleShape r(s.size);
            r.setFillColor(s.fillColor);
            r.setOutlineColor(s.outlineColor);
            r.setOutlineThickness(s.outlineThickness);
            r.setPosition({t.x, t.y});
            r.setRotation(sf::degrees(t.rotation));
            r.setScale(scale);
            window.draw(r);
            break;
        }
        case ShapeType::Triangle:
        {
            sf::ConvexShape tri(3);
            tri.setPoint(0, sf::Vector2f(s.size.x * 0.5f, 0.f));
            tri.setPoint(1, sf::Vector2f(0.f, s.size.y));
            tri.setPoint(2, sf::Vector2f(s.size.x, s.size.y));
            tri.setFillColor(s.fillColor);
            tri.setOutlineColor(s.outlineColor);
            tri.setOutlineThickness(s.outlineThickness);
            tri.setPosition({t.x, t.y});
            tri.setRotation(sf::degrees(t.rotation));
            tri.setScale(scale);
            window.draw(tri);
            break;
        }
        case ShapeType::Capsule:
        {
            // Approximated as a rounded rectangle: a rectangle body plus two caps.
            const float r = std::min(s.size.x, s.size.y) * 0.5f;
            sf::RectangleShape body({std::max(0.f, s.size.x - 2.f * r), std::max(0.f, s.size.y)});
            body.setFillColor(s.fillColor);
            body.setOutlineColor(s.outlineColor);
            body.setOutlineThickness(s.outlineThickness);
            body.setPosition({t.x + r, t.y});
            body.setRotation(sf::degrees(t.rotation));
            body.setScale(scale);
            window.draw(body);

            for (int i = 0; i < 2; ++i)
            {
                sf::CircleShape cap(r);
                cap.setFillColor(s.fillColor);
                cap.setOutlineColor(s.outlineColor);
                cap.setOutlineThickness(s.outlineThickness);
                cap.setOrigin({r, r});
                cap.setPosition({t.x + (i == 0 ? r : s.size.x - r), t.y + s.size.y * 0.5f});
                cap.setRotation(sf::degrees(t.rotation));
                cap.setScale(scale);
                window.draw(cap);
            }
            break;
        }
        case ShapeType::Polygon:
        {
            if (s.points.empty())
            {
                break;
            }
            sf::ConvexShape poly(s.points.size());
            for (size_t i = 0; i < s.points.size(); ++i)
            {
                poly.setPoint(i, s.points[i]);
            }
            poly.setFillColor(s.fillColor);
            poly.setOutlineColor(s.outlineColor);
            poly.setOutlineThickness(s.outlineThickness);
            poly.setPosition({t.x, t.y});
            poly.setRotation(sf::degrees(t.rotation));
            poly.setScale(scale);
            window.draw(poly);
            break;
        }
    }
}

bool ShapeRenderer::TestOverlap(const ColliderComponent& a, const TransformComponent& ta, const ColliderComponent& b,
                                const TransformComponent& tb)
{
    const sf::Vector2f ca = CenterOf(a, ta);
    const sf::Vector2f cb = CenterOf(b, tb);

    const bool aCircle = (a.type == ShapeType::Circle);
    const bool bCircle = (b.type == ShapeType::Circle);

    if (aCircle && bCircle)
    {
        const float ra = a.radius * std::max(std::abs(ta.scaleX), std::abs(ta.scaleY));
        const float rb = b.radius * std::max(std::abs(tb.scaleX), std::abs(tb.scaleY));
        return CircleVsCircle(ca.x, ca.y, ra, cb.x, cb.y, rb);
    }

    if (aCircle && !bCircle)
    {
        const float ra = a.radius * std::max(std::abs(ta.scaleX), std::abs(ta.scaleY));
        // A negative scale mirrors the box; using it as a width produced a
        // negative extent and made the overlap test always fail.
        const float bw = std::abs(b.size.x * tb.scaleX);
        const float bh = std::abs(b.size.y * tb.scaleY);
        return CircleVsBox(ca.x, ca.y, ra, cb.x - bw * 0.5f, cb.y - bh * 0.5f, bw, bh);
    }

    if (!aCircle && bCircle)
    {
        return TestOverlap(b, tb, a, ta);
    }

    // Box vs box (rectangles, triangles and polygons use their bounding box).
    const float aw = std::abs(a.size.x * ta.scaleX);
    const float ah = std::abs(a.size.y * ta.scaleY);
    const float bw = std::abs(b.size.x * tb.scaleX);
    const float bh = std::abs(b.size.y * tb.scaleY);
    return BoxVsBox(ca.x - aw * 0.5f, ca.y - ah * 0.5f, aw, ah, cb.x - bw * 0.5f, cb.y - bh * 0.5f, bw, bh);
}

} // namespace Fem
