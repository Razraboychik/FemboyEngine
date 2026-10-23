// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/FemObject.hpp"
#include "FemboyEngine/RichText.hpp"

namespace Fem
{

// Skirt: the engine's layered draw list.
//
// Every visual system (entities, tilemaps, particles, crowd, UI, debug) submits
// data-only commands here instead of touching the render target. One owner then
// sorts the whole frame by (layer, z) and batches it, which means:
//   * depth no longer depends on who happened to draw first;
//   * particles can sit above the world without a separate code path;
//   * a second backend only has to consume DrawCommand, not re-implement
//     batching, z-ordering or text interleaving.
//
// Layers are drawn in the order below; within a layer, lower z draws first and
// equal z keeps submission order (stable sort).
enum class DrawLayer
{
    Background = 0,
    Tiles,
    World,
    Entities,
    AboveWorld,
    Particles,
    // World-space lighting overlay. It sits above particles but below the UI,
    // so a multiply-blended lightmap darkens the scene without touching menus.
    Lighting,
    UI,
    Overlay,
    Debug,
    Count
};

const char* LayerName(DrawLayer layer);

enum class DrawKind
{
    Sprite,
    Text,
    Rectangle,
    Circle,
    Line,
    Vertices // pre-built geometry (tilemap batches, 3D quads)
};

// Text alignment. `text()` keeps the historical top-left anchor; rich text
// supports the other two so a label can be centred without measuring it twice.
enum class TextAlign
{
    Left,
    Center,
    Right
};

struct DrawCommand
{
    DrawKind kind = DrawKind::Sprite;
    DrawLayer layer = DrawLayer::Entities;
    // Fractional depth: an isometric scene can interleave actors and tiles
    // without inventing an integer layer per object.
    float z = 0.f;

    // geometry
    sf::Vector2f position{0.f, 0.f};
    sf::Vector2f size{0.f, 0.f};  // rectangle extents
    sf::Vector2f scale{1.f, 1.f}; // sprite scale
    float rotation = 0.f;         // degrees
    float radius = 0.f;           // circle
    static constexpr float kNoThickness = -1.f;
    float thickness = kNoThickness; // line width / outline width (< 0 == default)

    // paint
    sf::Color color = sf::Color::White;
    sf::Color outline = sf::Color::Transparent;

    // assets / text
    std::string textureId;
    // Optional non-cached texture (lightmaps, render-target snapshots). The
    // pointer must stay valid until endFrame(); LightingSystem owns its texture
    // for the lifetime of the system, which satisfies that contract.
    const sf::Texture* texture = nullptr;
    std::string fontId;
    std::string text;
    unsigned textSize = 16;
    // Rich text: when non-empty these runs replace `text`. Styles are applied by
    // the backend with sf::Text's native bold/italic/underline/strike support.
    std::vector<TextRun> textRuns;
    TextAlign textAlign = TextAlign::Left;
    sf::Color shadow = sf::Color::Transparent;
    sf::Vector2f shadowOffset{1.f, 2.f};

    // Optional custom blend for this command. `customBlend` is deliberately
    // opt-in: the default path keeps sprite batching across many commands.
    sf::BlendMode blend = sf::BlendAlpha;
    bool customBlend = false;

    // Sprite atlas support. `source` is in texture pixels; an empty size means
    // "the whole texture" so existing callers keep working. `origin` is a
    // normalized pivot inside the sprite: {0,0} is the top-left, {1,1} the
    // bottom-right. The default is the centre, which is where sprites have
    // always rotated around.
    sf::FloatRect source{};
    sf::Vector2f origin{0.5f, 0.5f};
    bool flipX = false;
    bool flipY = false;

    // pre-built geometry; when the array is non-empty it is drawn verbatim
    sf::VertexArray vertices;

    // Optional screen-space scissor. It is stored per command, not in a stack,
    // because the list is sorted by (layer, z) before it is drawn: a clip stack
    // would apply to the wrong commands after sorting.
    sf::FloatRect clip{};
    bool hasClip = false;
};

class DrawList : public FemObject
{
public:
    FEMCLASS_CREATABLE(DrawList, FemObject, false)

    void Clear();
    bool Empty() const
    {
        return commands_.empty();
    }
    size_t Size() const
    {
        return commands_.size();
    }

    void Add(const DrawCommand& cmd);
    void Add(DrawCommand&& cmd);

    const std::vector<DrawCommand>& Commands() const
    {
        return commands_;
    }

    // Sorts in place by (layer, z), stable so equal depth keeps submission order.
    void Sort();

    // Convenience submissions
    void Sprite(const std::string& textureId, sf::Vector2f pos, sf::Vector2f scale = sf::Vector2f(1.f, 1.f),
                float rotation = 0.f, sf::Color tint = sf::Color::White, float z = 0.f,
                DrawLayer layer = DrawLayer::Entities, sf::FloatRect source = sf::FloatRect{},
                sf::Vector2f origin = sf::Vector2f(0.5f, 0.5f), bool flipX = false, bool flipY = false);
    void Text(const std::string& text, const std::string& fontId, sf::Vector2f pos, unsigned size = 16,
              sf::Color color = sf::Color::White, float z = 0.f, DrawLayer layer = DrawLayer::UI);
    // Styled text with {b}/{i}/{u}/{s}/{color=}/{size=}/{font=} runs. The
    // `position` is the anchor described by `align` (top-left, top-centre or
    // top-right).
    void DrawRichText(const std::string& text, const std::string& fontId, sf::Vector2f pos, unsigned size = 16,
                      sf::Color color = sf::Color::White, float z = 0.f, DrawLayer layer = DrawLayer::UI,
                      TextAlign align = TextAlign::Left, sf::Color shadow = sf::Color::Transparent,
                      sf::Vector2f shadowOffset = {1.f, 2.f});
    void Rect(sf::FloatRect bounds, sf::Color fill, sf::Color outline = sf::Color::Transparent,
              float thickness = DrawCommand::kNoThickness, float z = 0.f, DrawLayer layer = DrawLayer::World);
    void Circle(sf::Vector2f center, float radius, sf::Color fill, float z = 0.f, DrawLayer layer = DrawLayer::Particles);
    // Filled polygon, triangulated as a fan from the first vertex. Convex
    // polygons are exact; a concave polygon must be split by the caller, which
    // is what every shape in the engine and the game already does. An optional
    // outline is stroked as a closed line loop.
    void Polygon(const std::vector<sf::Vector2f>& points, sf::Color fill, sf::Color outline = sf::Color::Transparent,
                 float thickness = DrawCommand::kNoThickness, float z = 0.f, DrawLayer layer = DrawLayer::World);
    void Line(sf::Vector2f a, sf::Vector2f b, sf::Color color, float thickness = 1.f, float z = 0.f,
              DrawLayer layer = DrawLayer::Debug);
    void Geometry(const sf::VertexArray& vertices, const std::string& textureId, float z = 0.f,
                  DrawLayer layer = DrawLayer::Tiles);
    // Multiply-blended world quad: the LightingSystem's lightmap. The texture is
    // owned by the caller and must outlive the frame.
    void Lightmap(const sf::Texture& texture, const sf::FloatRect& bounds, float z = 0.f,
                  DrawLayer layer = DrawLayer::Lighting);

    // Scissor for every command submitted until clearClip(). Coordinates are
    // screen pixels (the UI coordinate space). Used by scroll boxes: content is
    // laid out at its full height and the box decides what is visible.
    void SetClip(const sf::FloatRect& rect);
    void ClearClip();

private:
    std::vector<DrawCommand> commands_;
    sf::FloatRect clip_{};
    bool clipping_ = false;
};

} // namespace Fem
