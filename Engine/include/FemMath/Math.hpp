// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

// FemMath is the engine's small math vocabulary. It intentionally starts with
// SFML value types: that keeps existing engine/game data source-compatible while
// giving new code one named home for vectors, rectangles, colours and angles.
// A future renderer backend can change these aliases without touching callers
// that use FemMath::Vec2 / FemMath::Rect / FemMath::Color.
namespace FemMath
{

using Vec2 = sf::Vector2f;
using Vec2i = sf::Vector2i;
using Color = sf::Color;

// sf::FloatRect lacks the four-scalar constructor used by UI layout code.
// FemMath::Rect keeps that ergonomic spelling and converts to sf::FloatRect
// wherever the engine expects a rectangle.
struct Rect : sf::FloatRect
{
    Rect() = default;
    Rect(float x, float y, float width, float height) : sf::FloatRect({x, y}, {width, height})
    {
    }
    Rect(Vec2 position, Vec2 size) : sf::FloatRect(position, size)
    {
    }
    Rect(const sf::FloatRect& other) : sf::FloatRect(other)
    {
    }
};

inline constexpr float Pi = 3.14159265358979323846f;
inline constexpr float Tau = 6.28318530717958647692f;

constexpr float DegreesToRadians(float degrees)
{
    return degrees * (Pi / 180.f);
}

constexpr float RadiansToDegrees(float radians)
{
    return radians * (180.f / Pi);
}

constexpr float Clamp01(float value)
{
    return std::clamp(value, 0.f, 1.f);
}

inline float LengthSquared(Vec2 value)
{
    return value.x * value.x + value.y * value.y;
}

inline float Length(Vec2 value)
{
    return std::sqrt(LengthSquared(value));
}

inline Vec2 Normalized(Vec2 value)
{
    const float length = Length(value);
    if (length <= 0.00001f)
    {
        return {0.f, 0.f};
    }
    return {value.x / length, value.y / length};
}

inline float Dot(Vec2 a, Vec2 b)
{
    return a.x * b.x + a.y * b.y;
}

inline float Cross(Vec2 a, Vec2 b)
{
    return a.x * b.y - a.y * b.x;
}

// Degrees are measured clockwise on screen: 0 points right, 90 points down.
inline Vec2 FromAngle(float degrees)
{
    const float radians = DegreesToRadians(degrees);
    return {std::cos(radians), std::sin(radians)};
}

inline float AngleOf(Vec2 value)
{
    return RadiansToDegrees(std::atan2(value.y, value.x));
}

inline Vec2 Rotated(Vec2 value, float degrees)
{
    const float radians = DegreesToRadians(degrees);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return {value.x * c - value.y * s, value.x * s + value.y * c};
}

inline float DistanceSquared(Vec2 a, Vec2 b)
{
    return LengthSquared(b - a);
}

inline float Distance(Vec2 a, Vec2 b)
{
    return std::sqrt(DistanceSquared(a, b));
}

inline float Lerp(float a, float b, float t)
{
    return a + (b - a) * Clamp01(t);
}

inline Vec2 Lerp(Vec2 a, Vec2 b, float t)
{
    return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t)};
}

inline Color Lerp(Color a, Color b, float t)
{
    t = Clamp01(t);
    const auto channel = [t](std::uint8_t from, std::uint8_t to)
    {
        return static_cast<std::uint8_t>(static_cast<float>(from) +
                                         (static_cast<float>(to) - static_cast<float>(from)) * t);
    };
    return Color(channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a));
}

inline Color WithAlpha(Color color, float alpha)
{
    color.a = static_cast<std::uint8_t>(Clamp01(alpha) * 255.f);
    return color;
}

inline bool RectContains(const Rect& rectangle, Vec2 point)
{
    return point.x >= rectangle.position.x && point.x <= rectangle.position.x + rectangle.size.x &&
           point.y >= rectangle.position.y && point.y <= rectangle.position.y + rectangle.size.y;
}

inline float DistanceToSegment(Vec2 point, Vec2 a, Vec2 b)
{
    const Vec2 ab = b - a;
    const float lengthSquared = LengthSquared(ab);
    if (lengthSquared <= 0.0001f)
    {
        return Distance(point, a);
    }
    const float t = Clamp01(Dot(point - a, ab) / lengthSquared);
    return Distance(point, a + ab * t);
}

} // namespace FemMath
