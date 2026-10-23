// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <SFML/Graphics.hpp>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "FemboyEngine/Skirt.hpp"
#include "FemboyEngine/Tilemap.hpp"

namespace Fem
{

struct ChunkCoord
{
    int x = 0;
    int y = 0;

    bool operator==(const ChunkCoord& other) const
    {
        return x == other.x && y == other.y;
    }
};

struct ChunkCoordHash
{
    std::size_t operator()(const ChunkCoord& c) const noexcept
    {
        const std::uint64_t ux = static_cast<std::uint32_t>(c.x);
        const std::uint64_t uy = static_cast<std::uint32_t>(c.y);
        return static_cast<std::size_t>((ux << 32) ^ uy);
    }
};

// A square block of tiles. Tiles are stored row-major; an empty tile has id -1.
struct TileChunk
{
    static constexpr int kSize = 32;

    ChunkCoord coord{};
    int size = kSize;
    std::vector<Tile> tiles;

    TileChunk()
    {
        tiles.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    }

    bool InBounds(int lx, int ly) const
    {
        return lx >= 0 && ly >= 0 && lx < size && ly < size;
    }
    Tile& At(int lx, int ly)
    {
        return tiles[static_cast<std::size_t>(ly) * static_cast<std::size_t>(size) + static_cast<std::size_t>(lx)];
    }
    const Tile& At(int lx, int ly) const
    {
        return tiles[static_cast<std::size_t>(ly) * static_cast<std::size_t>(size) + static_cast<std::size_t>(lx)];
    }
};

// Large-world tilemap with chunk streaming.
//
// Tilemap is deliberately a single dense grid: it is the right tool for a small
// arena or a puzzle board, and every query is one index. ChunkedTilemap is the
// large-world counterpart:
//   * chunks are generated/loaded on demand around a view rectangle,
//   * chunks outside a keep radius are unloaded (their memory is returned),
//   * a generator callback can fill a new chunk with noise, WFC or saved data,
//   * generation can run on the job pool, with completed chunks applied on the
//     main thread (the map itself is never mutated from a worker).
//
// Coordinates: tile (tx,ty) belongs to chunk (tx / size, ty / size); the tile
// index inside the chunk is the positive remainder. World pixels are tile
// coordinates times tileSize, so the map is y-down like the renderer.
class ChunkedTilemap
{
public:
    using Generator = std::function<void(TileChunk&)>;

    // `chunkSize` is clamped to [4, 128] and rounded to a multiple of 4 so a
    // render batch and a WFC pattern stay cache-friendly.
    void Configure(int chunkSize = TileChunk::kSize, int tileSize = 32);
    int ChunkSize() const
    {
        return chunkSize_;
    }
    int TileSize() const
    {
        return tileSize_;
    }

    // Optional generator. It runs with a chunk that already has the correct
    // coord and empty tiles; fill `tiles` in row-major order. The callback may
    // be called from a worker thread when streamingAsync() is used.
    void SetGenerator(Generator generator);
    bool HasGenerator() const
    {
        return static_cast<bool>(generator_);
    }

    // Streaming: loads chunks intersecting `view` (plus `margin` chunks),
    // unloads chunks outside `view` plus `keepMargin`, and spends at most
    // `budgetMs` generating synchronously. Call it once per frame with the
    // camera rectangle. Returns the number of chunks loaded this call.
    int Stream(const sf::FloatRect& view, float budgetMs = 2.f, int margin = 1, int keepMargin = 3);

    // Asynchronous variant: schedules up to `maxLoads` missing chunks on the
    // job pool and applies finished ones. `Stream()` calls this internally, so a
    // project can choose one API. Returns the number of jobs scheduled.
    int StreamAsync(const sf::FloatRect& view, int maxLoads = 4, int margin = 1, int keepMargin = 3);

    // Generation/loading primitives. `load` marks a blank chunk as loaded when
    // no generator is set, which is what a fully authored map wants.
    TileChunk* Find(const ChunkCoord& coord);
    const TileChunk* Find(const ChunkCoord& coord) const;
    TileChunk& Ensure(const ChunkCoord& coord);
    void Unload(const ChunkCoord& coord);
    void UnloadAll();
    size_t LoadedChunks() const
    {
        return chunks_.size();
    }
    const std::unordered_map<ChunkCoord, TileChunk, ChunkCoordHash>& Chunks() const
    {
        return chunks_;
    }

    // Tile access. Reads outside loaded chunks return an empty tile. setTile
    // creates the chunk if necessary, so an editor can paint anywhere.
    Tile TileAt(int tx, int ty) const;
    void SetTile(int tx, int ty, int id, bool solid = false);
    bool IsSolid(int tx, int ty) const;
    bool IsSolidAtPixel(float px, float py) const;

    // World bounds covered by the authored map. Infinite maps (only generated
    // around the camera) leave this empty.
    void SetWorldTileBounds(int width, int height);
    bool HasWorldBounds() const
    {
        return worldW_ > 0 && worldH_ > 0;
    }
    int WorldWidth() const
    {
        return worldW_;
    }
    int WorldHeight() const
    {
        return worldH_;
    }

    // Batching: one pre-built vertex array per visible chunk, cached until the
    // chunk changes. `columnsInSheet` is the tileset width in tiles.
    void Render(Skirt& FrameDrawList, const std::string& textureId, int columnsInSheet, int z = 0,
                DrawLayer layer = DrawLayer::Tiles);

    // JSON interchange for saves and the editor. Only loaded chunks are written;
    // one-shot worlds should call Stream() for the whole map first.
    nlohmann::json ToJson() const;
    bool FromJson(const nlohmann::json& json);
    bool SaveToFile(const std::string& path) const;
    bool LoadFromFile(const std::string& path);

    // Chunk cache: when set, generated chunks are written to
    // <dir>/c_<x>_<y>.json and loaded from disk before the generator runs. This
    // is what makes a procedural world persistent without keeping it in RAM.
    void SetCacheDirectory(const std::string& directory);
    const std::string& CacheDirectory() const
    {
        return cacheDir_;
    }

    // Drops cached vertex arrays (after a tileset or tile-size change).
    void InvalidateGeometry();

private:
    ChunkCoord ChunkOf(int tx, int ty) const;
    static void TileLocal(const ChunkCoord& coord, int size, int tx, int ty, int& lx, int& ly);
    void Generate(TileChunk& chunk);
    bool LoadFromCache(TileChunk& chunk);
    void SaveToCache(const TileChunk& chunk) const;
    void BuildChunkGeometry(TileChunk& chunk, const std::string& textureId, int columnsInSheet) const;

    int chunkSize_ = TileChunk::kSize;
    int tileSize_ = 32;
    int worldW_ = 0;
    int worldH_ = 0;
    Generator generator_;
    std::unordered_map<ChunkCoord, TileChunk, ChunkCoordHash> chunks_;
    std::unordered_map<ChunkCoord, std::future<TileChunk>, ChunkCoordHash> pending_;
    std::string cacheDir_;

    struct Geometry
    {
        std::string textureId;
        sf::VertexArray vertices;
    };
    mutable std::unordered_map<ChunkCoord, Geometry, ChunkCoordHash> geometry_;
};

} // namespace Fem
