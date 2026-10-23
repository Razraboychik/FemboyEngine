// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/ChunkedTilemap.hpp"

#include "FemboyEngine/Assets.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Jobs.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

namespace Fem
{

namespace
{
int FloorDiv(int value, int divisor)
{
    const int q = value / divisor;
    const int r = value % divisor;
    return (r != 0 && ((r < 0) != (divisor < 0))) ? q - 1 : q;
}

std::string CacheFileName(const ChunkCoord& coord)
{
    return "c_" + std::to_string(coord.x) + "_" + std::to_string(coord.y) + ".json";
}
} // namespace

void ChunkedTilemap::Configure(int chunkSize, int tileSize)
{
    chunkSize = std::clamp(chunkSize, 4, 128);
    chunkSize -= chunkSize % 4;
    tileSize = std::clamp(tileSize, 1, 512);

    if (chunkSize_ == chunkSize && tileSize_ == tileSize)
    {
        return;
    }

    chunkSize_ = chunkSize;
    tileSize_ = tileSize;
    chunks_.clear();
    pending_.clear();
    geometry_.clear();
}

void ChunkedTilemap::SetGenerator(Generator generator)
{
    generator_ = std::move(generator);
}

ChunkCoord ChunkedTilemap::ChunkOf(int tx, int ty) const
{
    return ChunkCoord{FloorDiv(tx, chunkSize_), FloorDiv(ty, chunkSize_)};
}

void ChunkedTilemap::TileLocal(const ChunkCoord& coord, int size, int tx, int ty, int& lx, int& ly)
{
    lx = tx - coord.x * size;
    ly = ty - coord.y * size;
}

TileChunk* ChunkedTilemap::Find(const ChunkCoord& coord)
{
    const auto it = chunks_.find(coord);
    return it == chunks_.end() ? nullptr : &it->second;
}

const TileChunk* ChunkedTilemap::Find(const ChunkCoord& coord) const
{
    const auto it = chunks_.find(coord);
    return it == chunks_.end() ? nullptr : &it->second;
}

TileChunk& ChunkedTilemap::Ensure(const ChunkCoord& coord)
{
    auto it = chunks_.find(coord);
    if (it != chunks_.end())
    {
        return it->second;
    }

    TileChunk chunk;
    chunk.coord = coord;
    chunk.size = chunkSize_;
    chunk.tiles.assign(static_cast<std::size_t>(chunkSize_) * static_cast<std::size_t>(chunkSize_), Tile{});
    Generate(chunk);
    SaveToCache(chunk);
    geometry_.erase(coord);
    return chunks_.emplace(coord, std::move(chunk)).first->second;
}

void ChunkedTilemap::Generate(TileChunk& chunk)
{
    chunk.size = chunkSize_;
    if (!LoadFromCache(chunk) && generator_)
    {
        generator_(chunk);
    }
}

void ChunkedTilemap::Unload(const ChunkCoord& coord)
{
    chunks_.erase(coord);
    geometry_.erase(coord);
    pending_.erase(coord);
}

void ChunkedTilemap::UnloadAll()
{
    chunks_.clear();
    geometry_.clear();
    pending_.clear();
}

void ChunkedTilemap::SetWorldTileBounds(int width, int height)
{
    worldW_ = std::max(0, width);
    worldH_ = std::max(0, height);
}

Tile ChunkedTilemap::TileAt(int tx, int ty) const
{
    const ChunkCoord coord = ChunkOf(tx, ty);
    const TileChunk* chunk = Find(coord);
    if (!chunk)
    {
        return Tile{};
    }
    int lx = 0, ly = 0;
    TileLocal(coord, chunkSize_, tx, ty, lx, ly);
    if (!chunk->InBounds(lx, ly))
    {
        return Tile{};
    }
    return chunk->At(lx, ly);
}

void ChunkedTilemap::SetTile(int tx, int ty, int id, bool solid)
{
    if (HasWorldBounds() && (tx < 0 || ty < 0 || tx >= worldW_ || ty >= worldH_))
    {
        return;
    }

    const ChunkCoord coord = ChunkOf(tx, ty);
    auto it = chunks_.find(coord);
    if (it == chunks_.end())
    {
        // A painted tile must not drag a generator into the calling thread if
        // the chunk has not streamed in yet; create a blank chunk instead and
        // let the generator fill the rest when it is loaded normally.
        TileChunk chunk;
        chunk.coord = coord;
        chunk.size = chunkSize_;
        chunk.tiles.assign(static_cast<std::size_t>(chunkSize_) * static_cast<std::size_t>(chunkSize_), Tile{});
        it = chunks_.emplace(coord, std::move(chunk)).first;
    }

    int lx = 0, ly = 0;
    TileLocal(coord, chunkSize_, tx, ty, lx, ly);
    if (!it->second.InBounds(lx, ly))
    {
        return;
    }
    Tile& tile = it->second.At(lx, ly);
    tile.id = id;
    tile.solid = solid;
    geometry_.erase(coord);
}

bool ChunkedTilemap::IsSolid(int tx, int ty) const
{
    return TileAt(tx, ty).solid;
}

bool ChunkedTilemap::IsSolidAtPixel(float px, float py) const
{
    const int tx = static_cast<int>(std::floor(px / static_cast<float>(tileSize_)));
    const int ty = static_cast<int>(std::floor(py / static_cast<float>(tileSize_)));
    return IsSolid(tx, ty);
}

int ChunkedTilemap::StreamAsync(const sf::FloatRect& view, int maxLoads, int margin, int keepMargin)
{
    // Apply whatever finished since the last call. get() is safe here: the
    // future's wait_for(0) has already reported ready.
    for (auto it = pending_.begin(); it != pending_.end();)
    {
        if (it->second.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        {
            ++it;
            continue;
        }
        try
        {
            TileChunk chunk = it->second.get();
            geometry_.erase(chunk.coord);
            const ChunkCoord coord = chunk.coord;
            SaveToCache(chunk);
            chunks_[coord] = std::move(chunk);
        }
        catch (const std::exception& e)
        {
            BOY_ERROR("ChunkedTilemap", std::string("chunk generation failed: ") + e.what());
        }
        it = pending_.erase(it);
    }

    const float chunkPixels = static_cast<float>(chunkSize_ * tileSize_);
    if (chunkPixels <= 0.f || view.size.x <= 0.f || view.size.y <= 0.f)
    {
        return 0;
    }

    const int x0 = static_cast<int>(std::floor(view.position.x / chunkPixels)) - margin;
    const int y0 = static_cast<int>(std::floor(view.position.y / chunkPixels)) - margin;
    const int x1 = static_cast<int>(std::floor((view.position.x + view.size.x - 0.001f) / chunkPixels)) + margin;
    const int y1 = static_cast<int>(std::floor((view.position.y + view.size.y - 0.001f) / chunkPixels)) + margin;

    if (HasWorldBounds())
    {
        const int maxChunkX = (worldW_ + chunkSize_ - 1) / chunkSize_ - 1;
        const int maxChunkY = (worldH_ + chunkSize_ - 1) / chunkSize_ - 1;
        if (x0 > maxChunkX || y0 > maxChunkY || x1 < 0 || y1 < 0)
        {
            return 0;
        }
    }

    int scheduled = 0;
    for (int cy = y0; cy <= y1 && scheduled < maxLoads; ++cy)
    {
        for (int cx = x0; cx <= x1 && scheduled < maxLoads; ++cx)
        {
            const ChunkCoord coord{cx, cy};
            if (HasWorldBounds())
            {
                if (cx < 0 || cy < 0 || cx >= (worldW_ + chunkSize_ - 1) / chunkSize_ ||
                    cy >= (worldH_ + chunkSize_ - 1) / chunkSize_)
                {
                    continue;
                }
            }
            if (chunks_.find(coord) != chunks_.end() || pending_.find(coord) != pending_.end())
            {
                continue;
            }

            if (!generator_)
            {
                // No generator: a blank chunk is cheap enough to create inline.
                TileChunk chunk;
                chunk.coord = coord;
                chunk.size = chunkSize_;
                chunk.tiles.assign(static_cast<std::size_t>(chunkSize_) * static_cast<std::size_t>(chunkSize_), Tile{});
                chunks_.emplace(coord, std::move(chunk));
                continue;
            }

            Generator gen = generator_;
            pending_.emplace(coord,
                             JobSystem::Instance().Submit(
                                 [gen = std::move(gen), coord, size = chunkSize_]() mutable
                                 {
                                     TileChunk chunk;
                                     chunk.coord = coord;
                                     chunk.size = size;
                                     chunk.tiles.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size),
                                                        Tile{});
                                     gen(chunk);
                                     return chunk;
                                 }));
            ++scheduled;
        }
    }

    // Unload chunks far behind the camera. This is what keeps memory bounded on
    // an effectively infinite world.
    if (keepMargin >= 0)
    {
        const int keepX0 = x0 - keepMargin;
        const int keepY0 = y0 - keepMargin;
        const int keepX1 = x1 + keepMargin;
        const int keepY1 = y1 + keepMargin;
        for (auto it = chunks_.begin(); it != chunks_.end();)
        {
            const ChunkCoord& c = it->first;
            if (c.x < keepX0 || c.x > keepX1 || c.y < keepY0 || c.y > keepY1)
            {
                geometry_.erase(c);
                it = chunks_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    return scheduled;
}

int ChunkedTilemap::Stream(const sf::FloatRect& view, float budgetMs, int margin, int keepMargin)
{
    const int scheduled = StreamAsync(view, 8, margin, keepMargin);

    // Synchronous fallback for the chunks the pool has not produced yet. A game
    // that must never hitch can call StreamAsync() itself and skip this path.
    const auto start = std::chrono::steady_clock::now();
    const float chunkPixels = static_cast<float>(chunkSize_ * tileSize_);
    if (chunkPixels <= 0.f || view.size.x <= 0.f || view.size.y <= 0.f)
    {
        return scheduled;
    }

    const int x0 = static_cast<int>(std::floor(view.position.x / chunkPixels)) - margin;
    const int y0 = static_cast<int>(std::floor(view.position.y / chunkPixels)) - margin;
    const int x1 = static_cast<int>(std::floor((view.position.x + view.size.x - 0.001f) / chunkPixels)) + margin;
    const int y1 = static_cast<int>(std::floor((view.position.y + view.size.y - 0.001f) / chunkPixels)) + margin;

    int loaded = 0;
    for (int cy = y0; cy <= y1; ++cy)
    {
        for (int cx = x0; cx <= x1; ++cx)
        {
            const ChunkCoord coord{cx, cy};
            if (HasWorldBounds())
            {
                if (cx < 0 || cy < 0 || cx >= (worldW_ + chunkSize_ - 1) / chunkSize_ ||
                    cy >= (worldH_ + chunkSize_ - 1) / chunkSize_)
                {
                    continue;
                }
            }
            if (chunks_.find(coord) != chunks_.end() || pending_.find(coord) != pending_.end())
            {
                continue;
            }

            Ensure(coord);
            ++loaded;

            const double elapsed =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (elapsed >= budgetMs)
            {
                return scheduled + loaded;
            }
        }
    }
    return scheduled + loaded;
}

void ChunkedTilemap::BuildChunkGeometry(TileChunk& chunk, const std::string& textureId, int columnsInSheet) const
{
    Geometry& geometry = geometry_[chunk.coord];
    geometry.textureId = textureId;
    geometry.vertices.clear();
    geometry.vertices.setPrimitiveType(sf::PrimitiveType::Triangles);

    if (columnsInSheet <= 0)
    {
        return;
    }

    const float ts = static_cast<float>(tileSize_);
    const float originX = static_cast<float>(chunk.coord.x * chunkSize_ * tileSize_);
    const float originY = static_cast<float>(chunk.coord.y * chunkSize_ * tileSize_);
    const float sheetWidth = static_cast<float>(columnsInSheet * tileSize_);

    // Count first so the vertex array is allocated once.
    std::size_t count = 0;
    for (const Tile& tile : chunk.tiles)
    {
        if (tile.id >= 0)
        {
            ++count;
        }
    }
    if (count == 0)
    {
        return;
    }

    geometry.vertices.resize(count * 6);
    sf::Vertex* v = &geometry.vertices[0];
    std::size_t index = 0;

    for (int ly = 0; ly < chunk.size; ++ly)
    {
        for (int lx = 0; lx < chunk.size; ++lx)
        {
            const Tile& tile = chunk.At(lx, ly);
            if (tile.id < 0)
            {
                continue;
            }

            const float px = originX + static_cast<float>(lx) * ts;
            const float py = originY + static_cast<float>(ly) * ts;
            float u0 = static_cast<float>((tile.id % columnsInSheet) * tileSize_);
            float v0 = static_cast<float>((tile.id / columnsInSheet) * tileSize_);
            if (sheetWidth > 0.f && u0 >= sheetWidth)
            {
                u0 = 0.f;
            }
            const float u1 = u0 + ts;
            const float v1 = v0 + ts;

            const sf::Vertex quad[6] = {{sf::Vector2f(px, py), sf::Color::White, sf::Vector2f(u0, v0)},
                                        {sf::Vector2f(px + ts, py), sf::Color::White, sf::Vector2f(u1, v0)},
                                        {sf::Vector2f(px + ts, py + ts), sf::Color::White, sf::Vector2f(u1, v1)},
                                        {sf::Vector2f(px, py), sf::Color::White, sf::Vector2f(u0, v0)},
                                        {sf::Vector2f(px + ts, py + ts), sf::Color::White, sf::Vector2f(u1, v1)},
                                        {sf::Vector2f(px, py + ts), sf::Color::White, sf::Vector2f(u0, v1)}};
            for (int i = 0; i < 6; ++i)
            {
                v[index++] = quad[i];
            }
        }
    }
}

void ChunkedTilemap::Render(Skirt& FrameDrawList, const std::string& textureId, int columnsInSheet, int z, DrawLayer layer)
{
    for (auto& [coord, chunk] : chunks_)
    {
        auto it = geometry_.find(coord);
        if (it == geometry_.end() || it->second.textureId != textureId)
        {
            BuildChunkGeometry(chunk, textureId, columnsInSheet);
        }

        it = geometry_.find(coord);
        if (it != geometry_.end() && it->second.vertices.getVertexCount() > 0)
        {
            FrameDrawList.Geometry(it->second.vertices, textureId, z, layer);
        }
    }
}

void ChunkedTilemap::InvalidateGeometry()
{
    geometry_.clear();
}

nlohmann::json ChunkedTilemap::ToJson() const
{
    nlohmann::json chunks = nlohmann::json::array();
    for (const auto& [coord, chunk] : chunks_)
    {
        nlohmann::json tiles = nlohmann::json::array();
        for (const Tile& tile : chunk.tiles)
        {
            tiles.push_back({{"id", tile.id}, {"solid", tile.solid}});
        }
        chunks.push_back({{"x", coord.x}, {"y", coord.y}, {"size", chunk.size}, {"tiles", std::move(tiles)}});
    }

    return {{"chunkSize", chunkSize_},
            {"tileSize", tileSize_},
            {"worldWidth", worldW_},
            {"worldHeight", worldH_},
            {"chunks", std::move(chunks)}};
}

bool ChunkedTilemap::FromJson(const nlohmann::json& json)
{
    if (!json.is_object())
    {
        BOY_ERROR("ChunkedTilemap", "fromJson: root must be an object");
        return false;
    }

    const auto readInt = [&json](const char* key, int def)
    {
        if (!json.contains(key) || !json[key].is_number())
        {
            return def;
        }
        return json[key].get<int>();
    };

    Configure(readInt("chunkSize", chunkSize_), readInt("tileSize", tileSize_));
    SetWorldTileBounds(readInt("worldWidth", 0), readInt("worldHeight", 0));
    chunks_.clear();
    geometry_.clear();

    if (!json.contains("chunks") || !json["chunks"].is_array())
    {
        return true;
    }

    for (const auto& entry : json["chunks"])
    {
        if (!entry.is_object() || !entry.contains("x") || !entry.contains("y"))
        {
            continue;
        }

        TileChunk chunk;
        chunk.coord = ChunkCoord{entry["x"].get<int>(), entry["y"].get<int>()};
        chunk.size = chunkSize_;
        chunk.tiles.assign(static_cast<std::size_t>(chunkSize_) * static_cast<std::size_t>(chunkSize_), Tile{});

        if (entry.contains("tiles") && entry["tiles"].is_array())
        {
            std::size_t i = 0;
            for (const auto& t : entry["tiles"])
            {
                if (i >= chunk.tiles.size())
                {
                    break;
                }
                if (t.is_object())
                {
                    chunk.tiles[i].id = t.value("id", -1);
                    chunk.tiles[i].solid = t.value("solid", false);
                }
                ++i;
            }
        }
        chunks_[chunk.coord] = std::move(chunk);
    }
    return true;
}

bool ChunkedTilemap::SaveToFile(const std::string& path) const
{
    std::ofstream out(path);
    if (!out)
    {
        BOY_ERROR("ChunkedTilemap", "cannot write " + path);
        return false;
    }
    out << ToJson().dump(1);
    return static_cast<bool>(out);
}

bool ChunkedTilemap::LoadFromFile(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
    {
        BOY_WARN("ChunkedTilemap", "cannot read " + path);
        return false;
    }
    nlohmann::json json;
    try
    {
        in >> json;
    }
    catch (const std::exception& e)
    {
        BOY_ERROR("ChunkedTilemap", std::string("bad JSON: ") + e.what());
        return false;
    }
    return FromJson(json);
}

void ChunkedTilemap::SetCacheDirectory(const std::string& directory)
{
    cacheDir_ = directory;
    if (!cacheDir_.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(cacheDir_, ec);
    }
}

bool ChunkedTilemap::LoadFromCache(TileChunk& chunk)
{
    if (cacheDir_.empty())
    {
        return false;
    }
    const std::filesystem::path path = std::filesystem::path(cacheDir_) / CacheFileName(chunk.coord);
    std::ifstream in(path);
    if (!in)
    {
        return false;
    }

    nlohmann::json json;
    try
    {
        in >> json;
    }
    catch (...)
    {
        return false;
    }
    if (!json.is_object() || !json.contains("tiles") || !json["tiles"].is_array())
    {
        return false;
    }

    chunk.size = chunkSize_;
    chunk.tiles.assign(static_cast<std::size_t>(chunkSize_) * static_cast<std::size_t>(chunkSize_), Tile{});
    std::size_t i = 0;
    for (const auto& t : json["tiles"])
    {
        if (i >= chunk.tiles.size())
        {
            break;
        }
        if (t.is_object())
        {
            chunk.tiles[i].id = t.value("id", -1);
            chunk.tiles[i].solid = t.value("solid", false);
        }
        ++i;
    }
    return true;
}

void ChunkedTilemap::SaveToCache(const TileChunk& chunk) const
{
    if (cacheDir_.empty())
    {
        return;
    }
    nlohmann::json tiles = nlohmann::json::array();
    for (const Tile& tile : chunk.tiles)
    {
        tiles.push_back({{"id", tile.id}, {"solid", tile.solid}});
    }

    std::ofstream out(std::filesystem::path(cacheDir_) / CacheFileName(chunk.coord));
    if (!out)
    {
        return;
    }
    out << nlohmann::json{{"x", chunk.coord.x}, {"y", chunk.coord.y}, {"size", chunk.size}, {"tiles", tiles}}.dump();
}

} // namespace Fem
