// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Skirt.hpp"
#include <algorithm>
#include <array>
#include <tuple>

namespace Fem
{

const char* LayerName(DrawLayer layer)
{
    switch (layer)
    {
        case DrawLayer::Background:
            return "Background";
        case DrawLayer::Tiles:
            return "Tiles";
        case DrawLayer::World:
            return "World";
        case DrawLayer::Entities:
            return "Entities";
        case DrawLayer::AboveWorld:
            return "AboveWorld";
        case DrawLayer::Particles:
            return "Particles";
        case DrawLayer::Lighting:
            return "Lighting";
        case DrawLayer::UI:
            return "UI";
        case DrawLayer::Overlay:
            return "Overlay";
        case DrawLayer::Debug:
            return "Debug";
        case DrawLayer::Count:
            break;
    }
    return "Unknown";
}

void DrawList::Clear()
{
    commands_.clear();
}

void DrawList::Add(const DrawCommand& cmd)
{
    DrawCommand copy = cmd;
    if (clipping_ && !copy.hasClip)
    {
        copy.clip = clip_;
        copy.hasClip = true;
    }
    commands_.push_back(std::move(copy));
}

void DrawList::Add(DrawCommand&& cmd)
{
    if (clipping_ && !cmd.hasClip)
    {
        cmd.clip = clip_;
        cmd.hasClip = true;
    }
    commands_.push_back(std::move(cmd));
}

void DrawList::SetClip(const sf::FloatRect& rect)
{
    clipping_ = rect.size.x > 0.f && rect.size.y > 0.f;
    clip_ = rect;
}

void DrawList::ClearClip()
{
    clipping_ = false;
}

void DrawList::Sort()
{
    // Bucket by layer first. This keeps the common case (commands submitted in
    // roughly layer order) cheap and reduces each sort to a single layer's
    // depth range instead of the whole frame. Stable sorting inside a bucket
    // preserves submission order for equal z, exactly like the previous sort.
    std::array<std::vector<DrawCommand>, static_cast<std::size_t>(DrawLayer::Count)> buckets;
    for (DrawCommand& command : commands_)
    {
        buckets[static_cast<std::size_t>(command.layer)].push_back(std::move(command));
    }

    commands_.clear();
    for (std::vector<DrawCommand>& bucket : buckets)
    {
        if (bucket.empty())
        {
            continue;
        }
        // Fractional z means integer counting sort no longer applies. The
        // stable comparison sort also guarantees equal-depth submission order.
        std::stable_sort(bucket.begin(), bucket.end(), [](const DrawCommand& a, const DrawCommand& b)
                         { return a.z < b.z; });
        for (DrawCommand& command : bucket)
        {
            commands_.push_back(std::move(command));
        }
    }
}

void DrawList::Sprite(const std::string& textureId, sf::Vector2f pos, sf::Vector2f scale, float rotation, sf::Color tint,
                   float z, DrawLayer layer, sf::FloatRect source, sf::Vector2f origin, bool flipX, bool flipY)
{
    DrawCommand c;
    c.kind = DrawKind::Sprite;
    c.textureId = textureId;
    c.position = pos;
    c.scale = scale;
    c.rotation = rotation;
    c.color = tint;
    c.z = z;
    c.layer = layer;
    c.source = source;
    c.origin = origin;
    c.flipX = flipX;
    c.flipY = flipY;
    Add(std::move(c));
}

void DrawList::Text(const std::string& value, const std::string& fontId, sf::Vector2f pos, unsigned size, sf::Color color,
                 float z, DrawLayer layer)
{
    DrawCommand c;
    c.kind = DrawKind::Text;
    c.text = value;
    c.fontId = fontId;
    c.position = pos;
    c.textSize = size;
    c.color = color;
    c.z = z;
    c.layer = layer;
    Add(std::move(c));
}

void DrawList::DrawRichText(const std::string& value, const std::string& fontId, sf::Vector2f pos, unsigned size,
                         sf::Color color, float z, DrawLayer layer, TextAlign align, sf::Color shadow,
                         sf::Vector2f shadowOffset)
{
    TextStyle base;
    base.color = color;
    base.size = size;
    base.fontId = fontId;
    RichText parsed = ParseRichText(value, base);

    DrawCommand c;
    c.kind = DrawKind::Text;
    c.text = std::move(parsed.plain);
    c.textRuns = std::move(parsed.runs);
    c.fontId = fontId;
    c.position = pos;
    c.textSize = size;
    c.color = color;
    c.textAlign = align;
    c.shadow = shadow;
    c.shadowOffset = shadowOffset;
    c.z = z;
    c.layer = layer;
    Add(std::move(c));
}

void DrawList::Rect(sf::FloatRect bounds, sf::Color fill, sf::Color outline, float thickness, float z, DrawLayer layer)
{
    DrawCommand c;
    c.kind = DrawKind::Rectangle;
    c.position = bounds.position;
    c.size = bounds.size;
    c.color = fill;
    c.outline = outline;
    c.thickness = thickness;
    c.z = z;
    c.layer = layer;
    Add(std::move(c));
}

void DrawList::Circle(sf::Vector2f center, float radius, sf::Color fill, float z, DrawLayer layer)
{
    DrawCommand c;
    c.kind = DrawKind::Circle;
    c.position = center;
    c.radius = radius;
    c.color = fill;
    c.z = z;
    c.layer = layer;
    Add(std::move(c));
}

void DrawList::Line(sf::Vector2f a, sf::Vector2f b, sf::Color color, float thickness, float z, DrawLayer layer)
{
    DrawCommand c;
    c.kind = DrawKind::Line;
    c.position = a;
    c.size = b;
    c.color = color;
    c.thickness = thickness;
    c.z = z;
    c.layer = layer;
    Add(std::move(c));
}

void DrawList::Polygon(const std::vector<sf::Vector2f>& points, sf::Color fill, sf::Color outline, float thickness, float z,
                    DrawLayer layer)
{
    if (points.size() < 3)
    {
        return;
    }

    sf::VertexArray triangles(sf::PrimitiveType::Triangles);
    triangles.resize((points.size() - 2) * 3);
    size_t v = 0;
    for (size_t i = 1; i + 1 < points.size(); ++i)
    {
        triangles[v++] = sf::Vertex(points[0], fill);
        triangles[v++] = sf::Vertex(points[i], fill);
        triangles[v++] = sf::Vertex(points[i + 1], fill);
    }
    Geometry(triangles, "", z, layer);

    if (outline.a > 0 && thickness > 0.f)
    {
        for (size_t i = 0; i < points.size(); ++i)
        {
            Line(points[i], points[(i + 1) % points.size()], outline, thickness, z, layer);
        }
    }
}

void DrawList::Geometry(const sf::VertexArray& vertices, const std::string& textureId, float z, DrawLayer layer)
{
    DrawCommand c;
    c.kind = DrawKind::Vertices;
    c.vertices = vertices;
    c.textureId = textureId;
    c.z = z;
    c.layer = layer;
    Add(std::move(c));
}

void DrawList::Lightmap(const sf::Texture& texture, const sf::FloatRect& bounds, float z, DrawLayer layer)
{
    if (bounds.size.x <= 0.f || bounds.size.y <= 0.f || texture.getSize().x == 0 || texture.getSize().y == 0)
    {
        return;
    }

    DrawCommand c;
    c.kind = DrawKind::Sprite;
    c.texture = &texture;
    c.position = bounds.position;
    c.scale = {bounds.size.x / static_cast<float>(texture.getSize().x),
               bounds.size.y / static_cast<float>(texture.getSize().y)};
    c.origin = {0.f, 0.f};
    c.layer = layer;
    c.z = z;
    c.customBlend = true;
    c.blend = sf::BlendMultiply;
    Add(std::move(c));
}

} // namespace Fem
