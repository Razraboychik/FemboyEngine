// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Tilemap.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Assets.hpp"
#include <fstream>
#include <stdexcept>
#include <cmath>

namespace Fem
{

namespace
{
constexpr int kMaxTilemapDimension = 16384;
constexpr int kMaxTileSize = 4096;
} // namespace

void Tilemap::Resize(int w, int h, int tileSize)
{
    if (w < 0)
    {
        w = 0;
    }
    if (h < 0)
    {
        h = 0;
    }
    if (w > kMaxTilemapDimension)
    {
        w = kMaxTilemapDimension;
    }
    if (h > kMaxTilemapDimension)
    {
        h = kMaxTilemapDimension;
    }
    if (tileSize <= 0)
    {
        tileSize = 32;
    }
    if (tileSize > kMaxTileSize)
    {
        tileSize = kMaxTileSize;
    }

    w_ = w;
    h_ = h;
    tileSize_ = tileSize;
    tiles_.assign(static_cast<size_t>(w_) * static_cast<size_t>(h_), Tile{});
}

Tile& Tilemap::At(int x, int y)
{
    if (!InBounds(x, y))
    {
        throw std::out_of_range("Tilemap::At out of bounds");
    }
    return tiles_[static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x)];
}

const Tile& Tilemap::At(int x, int y) const
{
    if (!InBounds(x, y))
    {
        throw std::out_of_range("Tilemap::At out of bounds");
    }
    return tiles_[static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x)];
}

bool Tilemap::InBounds(int x, int y) const
{
    return x >= 0 && y >= 0 && x < w_ && y < h_;
}

void Tilemap::SetTile(int x, int y, int id, bool solid)
{
    if (!InBounds(x, y))
    {
        return;
    }
    Tile& t = tiles_[static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x)];
    t.id = id;
    t.solid = solid;
}

Tile Tilemap::TileAt(int x, int y) const
{
    if (!InBounds(x, y))
    {
        return Tile{};
    }
    return tiles_[static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x)];
}

void Tilemap::Clear(int id, bool solid)
{
    for (auto& t : tiles_)
    {
        t.id = id;
        t.solid = solid;
    }
}

bool Tilemap::IsSolid(int x, int y) const
{
    if (!InBounds(x, y))
    {
        return true; // walls outside the map
    }
    return tiles_[static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x)].solid;
}

bool Tilemap::IsSolidAtPixel(float px, float py) const
{
    if (tileSize_ <= 0)
    {
        return true;
    }
    const int tx = static_cast<int>(std::floor(px / static_cast<float>(tileSize_)));
    const int ty = static_cast<int>(std::floor(py / static_cast<float>(tileSize_)));
    return IsSolid(tx, ty);
}

sf::VertexArray Tilemap::BuildVertices(const sf::Texture& tileset, int columnsInSheet) const
{
    sf::VertexArray va(sf::PrimitiveType::Triangles);
    if (columnsInSheet <= 0)
    {
        return va;
    }
    const int sheetW = static_cast<int>(tileset.getSize().x);
    const int sheetH = static_cast<int>(tileset.getSize().y);
    if (sheetW <= 0 || sheetH <= 0)
    {
        return va;
    }

    int ts = sheetW / columnsInSheet;
    if (ts <= 0)
    {
        ts = tileSize_;
    }

    // Build the whole map as one vertex array instead of one draw call per tile.
    // A 200x200 map went from 40 000 draw calls to a single batched draw.
    va.resize(static_cast<size_t>(w_) * static_cast<size_t>(h_) * 6);

    size_t v = 0;
    for (int y = 0; y < h_; ++y)
    {
        for (int x = 0; x < w_; ++x)
        {
            const int id = tiles_[static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x)].id;
            if (id < 0)
            {
                continue;
            }

            const int sx = (id % columnsInSheet) * ts;
            const int sy = (id / columnsInSheet) * ts;
            // Skip tiles that fall outside the sheet instead of sampling garbage.
            if (sx + ts > sheetW || sy + ts > sheetH)
            {
                continue;
            }

            const float px = static_cast<float>(x * tileSize_);
            const float py = static_cast<float>(y * tileSize_);
            const float pw = static_cast<float>(tileSize_);
            const float ph = static_cast<float>(tileSize_);
            const float u0 = static_cast<float>(sx);
            const float v0 = static_cast<float>(sy);
            const float u1 = static_cast<float>(sx + ts);
            const float v1 = static_cast<float>(sy + ts);

            const sf::Vertex quad[6] = {{sf::Vector2f(px, py), sf::Color::White, sf::Vector2f(u0, v0)},
                                        {sf::Vector2f(px + pw, py), sf::Color::White, sf::Vector2f(u1, v0)},
                                        {sf::Vector2f(px + pw, py + ph), sf::Color::White, sf::Vector2f(u1, v1)},
                                        {sf::Vector2f(px, py), sf::Color::White, sf::Vector2f(u0, v0)},
                                        {sf::Vector2f(px + pw, py + ph), sf::Color::White, sf::Vector2f(u1, v1)},
                                        {sf::Vector2f(px, py + ph), sf::Color::White, sf::Vector2f(u0, v1)}};

            for (int i = 0; i < 6; ++i)
            {
                va[v++] = quad[i];
            }
        }
    }

    if (v == 0)
    {
        return sf::VertexArray(sf::PrimitiveType::Triangles);
    }
    va.resize(v);
    return va;
}

void Tilemap::Render(sf::RenderWindow& window, const sf::Texture& tileset, int columnsInSheet) const
{
    sf::VertexArray va = BuildVertices(tileset, columnsInSheet);
    if (va.getVertexCount() == 0)
    {
        return;
    }
    sf::RenderStates states;
    states.texture = &tileset;
    window.draw(va, states);
}

void Tilemap::Render(Skirt& FrameDrawList, const std::string& textureId, int columnsInSheet, float z, DrawLayer layer) const
{
    const sf::Texture& tileset = AssetManager::Instance().Texture(textureId);
    sf::VertexArray va = BuildVertices(tileset, columnsInSheet);
    if (va.getVertexCount() == 0)
    {
        return;
    }
    FrameDrawList.Geometry(va, textureId, z, layer);
}

void Tilemap::RenderSorted(Skirt& FrameDrawList, const std::string& textureId, int columnsInSheet, float z, float depthStep,
                           DrawLayer layer) const
{
    const sf::Texture& tileset = AssetManager::Instance().Texture(textureId);
    const int sheetW = static_cast<int>(tileset.getSize().x);
    const int sheetH = static_cast<int>(tileset.getSize().y);
    if (sheetW <= 0 || sheetH <= 0 || columnsInSheet <= 0)
    {
        return;
    }
    int ts = sheetW / columnsInSheet;
    if (ts <= 0)
    {
        ts = tileSize_;
    }

    const int diagonalCount = std::max(1, w_ + h_ - 1);
    std::vector<sf::VertexArray> bands;
    bands.reserve(static_cast<std::size_t>(diagonalCount));
    for (int i = 0; i < diagonalCount; ++i)
    {
        bands.emplace_back(sf::PrimitiveType::Triangles);
    }

    for (int y = 0; y < h_; ++y)
    {
        for (int x = 0; x < w_; ++x)
        {
            const int id = tiles_[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)].id;
            if (id < 0)
            {
                continue;
            }
            const int sx = (id % columnsInSheet) * ts;
            const int sy = (id / columnsInSheet) * ts;
            if (sx + ts > sheetW || sy + ts > sheetH)
            {
                continue;
            }

            const float px = static_cast<float>(x * tileSize_);
            const float py = static_cast<float>(y * tileSize_);
            const float pw = static_cast<float>(tileSize_);
            const float ph = static_cast<float>(tileSize_);
            const float u0 = static_cast<float>(sx);
            const float v0 = static_cast<float>(sy);
            const float u1 = static_cast<float>(sx + ts);
            const float v1 = static_cast<float>(sy + ts);

            sf::VertexArray& band = bands[static_cast<std::size_t>(x + y)];
            band.append(sf::Vertex{sf::Vector2f(px, py), sf::Color::White, sf::Vector2f(u0, v0)});
            band.append(sf::Vertex{sf::Vector2f(px + pw, py), sf::Color::White, sf::Vector2f(u1, v0)});
            band.append(sf::Vertex{sf::Vector2f(px + pw, py + ph), sf::Color::White, sf::Vector2f(u1, v1)});
            band.append(sf::Vertex{sf::Vector2f(px, py), sf::Color::White, sf::Vector2f(u0, v0)});
            band.append(sf::Vertex{sf::Vector2f(px + pw, py + ph), sf::Color::White, sf::Vector2f(u1, v1)});
            band.append(sf::Vertex{sf::Vector2f(px, py + ph), sf::Color::White, sf::Vector2f(u0, v1)});
        }
    }

    for (int i = 0; i < diagonalCount; ++i)
    {
        if (bands[static_cast<std::size_t>(i)].getVertexCount() > 0)
        {
            FrameDrawList.Geometry(bands[static_cast<std::size_t>(i)], textureId, z + static_cast<float>(i) * depthStep, layer);
        }
    }
}

nlohmann::json Tilemap::ToJson() const
{
    nlohmann::json j;
    j["width"] = w_;
    j["height"] = h_;
    j["tileSize"] = tileSize_;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : tiles_)
    {
        arr.push_back({{"id", t.id}, {"solid", t.solid}});
    }
    j["tiles"] = arr;
    return j;
}

bool Tilemap::FromJson(const nlohmann::json& j)
{
    if (!j.is_object())
    {
        BOY_ERROR("Tilemap", "fromJson: root must be an object");
        return false;
    }

    // json::value() throws when the stored type does not match, so validate
    // before reading: a hand-edited map must not crash the loader.
    const auto readInt = [&](const char* key, int def)
    {
        if (!j.contains(key) || !j[key].is_number())
        {
            return def;
        }
        const double d = j[key].get<double>();
        if (!std::isfinite(d) || d < -1.0e9 || d > 1.0e9)
        {
            return def;
        }
        return static_cast<int>(d);
    };

    const int w = readInt("width", 0);
    const int h = readInt("height", 0);
    if (w < 0 || h < 0 || w > kMaxTilemapDimension || h > kMaxTilemapDimension)
    {
        BOY_ERROR("Tilemap", "fromJson: invalid dimensions");
        return false;
    }

    Resize(w, h, readInt("tileSize", 32));

    if (j.contains("tiles"))
    {
        if (!j["tiles"].is_array())
        {
            BOY_ERROR("Tilemap", "fromJson: tiles must be an array");
            return false;
        }
        size_t i = 0;
        for (const auto& t : j["tiles"])
        {
            if (i >= tiles_.size())
            {
                break;
            }
            if (!t.is_object())
            {
                ++i;
                continue;
            }
            if (t.contains("id") && t["id"].is_number())
            {
                tiles_[i].id = t["id"].get<int>();
            }
            if (t.contains("solid") && t["solid"].is_boolean())
            {
                tiles_[i].solid = t["solid"].get<bool>();
            }
            ++i;
        }
    }
    return true;
}

bool Tilemap::LoadFromFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        BOY_ERROR("Tilemap", "cannot open: " + path);
        return false;
    }

    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (const std::exception& e)
    {
        BOY_ERROR("Tilemap", std::string("parse error: ") + e.what());
        return false;
    }

    if (!FromJson(j))
    {
        return false;
    }
    BOY_INFO("Tilemap", "loaded: " + path);
    return true;
}

bool Tilemap::SaveToFile(const std::string& path) const
{
    std::ofstream f(path);
    if (!f.is_open())
    {
        BOY_ERROR("Tilemap", "cannot write: " + path);
        return false;
    }
    f << ToJson().dump(4);
    f.flush();
    if (!f)
    {
        BOY_ERROR("Tilemap", "write failed: " + path);
        return false;
    }
    BOY_INFO("Tilemap", "saved: " + path);
    return true;
}

} // namespace Fem
