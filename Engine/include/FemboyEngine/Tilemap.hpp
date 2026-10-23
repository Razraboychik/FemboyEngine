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
#include "FemboyEngine/Skirt.hpp"

namespace Fem
{

struct Tile
{
    int id = -1; // -1 = empty
    bool solid = false;
};

class Tilemap
{
public:
    Tilemap() = default;
    void Resize(int w, int h, int tileSize = 32);
    int Width() const
    {
        return w_;
    }
    int Height() const
    {
        return h_;
    }
    int TileSize() const
    {
        return tileSize_;
    }

    // Throws std::out_of_range for out-of-bounds coordinates.
    Tile& At(int x, int y);
    const Tile& At(int x, int y) const;
    bool InBounds(int x, int y) const;

    void SetTile(int x, int y, int id, bool solid = false);
    Tile TileAt(int x, int y) const;
    void Clear(int id = -1, bool solid = false);

    // Legacy direct path. Prefer the Skirt overload: it participates in the
    // global (layer, z) sort instead of drawing immediately.
    void Render(sf::RenderWindow& window, const sf::Texture& tileset, int columnsInSheet) const;
    // Single-draw-list path. `textureId` is resolved through AssetManager.
    void Render(Skirt& FrameDrawList, const std::string& textureId, int columnsInSheet, float z = 0.f,
                DrawLayer layer = DrawLayer::Tiles) const;
    // Isometric-friendly path: tiles are grouped by x+y diagonal and each band
    // gets a fractional z. That keeps one command per diagonal instead of one
    // per tile (a 200x200 map becomes <=400 commands, not 40 000), while an
    // actor can still sort between individual diagonals.
    void RenderSorted(Skirt& FrameDrawList, const std::string& textureId, int columnsInSheet, float z = 0.f,
                      float depthStep = 0.001f, DrawLayer layer = DrawLayer::Tiles) const;

    // collision
    bool IsSolid(int x, int y) const;
    bool IsSolidAtPixel(float px, float py) const;

    // serialization
    nlohmann::json ToJson() const;
    bool FromJson(const nlohmann::json& j);
    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;

private:
    // Builds the batched tile geometry, or an empty array when nothing is
    // visible/valid.
    sf::VertexArray BuildVertices(const sf::Texture& tileset, int columnsInSheet) const;

    int w_ = 0, h_ = 0, tileSize_ = 32;
    std::vector<Tile> tiles_;
};

} // namespace Fem
