// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Fem
{

// Wave Function Collapse for tile maps.
//
// The implementation is the classic overlapping-free "simple tiled" variant:
// every tile id has a set of compatible neighbours in four directions, and the
// solver repeatedly collapses the lowest-entropy cell, propagates constraints
// and backtracks on a contradiction. It outputs tile ids in row-major order,
// ready to be written into ChunkedTilemap::SetTile or a WFC preview image.
//
// Limits are deliberate: at most 64 tiles (one uint64 domain per cell). A 2D
// tileset with more than 64 entries is rare, and the alternative (a bitset per
// cell) is both slower and harder to inspect in a debugger.
class Wfc
{
public:
    static constexpr int kMaxTiles = 64;

    void Clear();
    // Tile ids must be in [0, 63]. Weight controls how often a tile is chosen;
    // it does not affect compatibility. Adding the same id twice updates weight.
    bool AddTile(int id, float weight = 1.f);
    int TileCount() const
    {
        return tileCount_;
    }

    // Symmetric compatibility: a and b may touch horizontally and/or
    // vertically. Most tilesets are built this way.
    void AddPair(int a, int b, bool horizontal = true, bool vertical = true);
    // Directional rule: tile `a` may have tile `b` at offset (dx,dy), where the
    // offset must be one of (1,0), (0,1), (-1,0), (0,-1). The inverse is added
    // automatically so propagation has a consistent matrix.
    bool AddDirectional(int a, int b, int dx, int dy);
    // When true (default) no rules means "every tile may touch every tile",
    // which is useful for weight-only noise. Set false to make an empty rule set
    // unsatisfiable and catch authoring mistakes early.
    void SetOpenWhenNoRules(bool open)
    {
        openWhenNoRules_ = open;
    }

    void SetSize(int width, int height);
    int Width() const
    {
        return width_;
    }
    int Height() const
    {
        return height_;
    }

    // Runs the solver. `seed` makes a level reproducible; `attempts` restarts
    // the whole wave if backtracking runs out. Returns false on failure, in
    // which case grid() is left unchanged.
    bool Solve(std::uint32_t seed = 0, int attempts = 4);
    bool Solved() const
    {
        return solved_;
    }
    int At(int x, int y) const;
    const std::vector<int>& Grid() const
    {
        return grid_;
    }

    // Adjacency serialisation, e.g. for a tileset manifest.
    nlohmann::json ToJson() const;
    bool FromJson(const nlohmann::json& json);
    bool SaveToFile(const std::string& path) const;
    bool LoadFromFile(const std::string& path);

    // Introspection for editors: how many choices remain at a cell.
    int Entropy(int x, int y) const;

private:
    enum Direction
    {
        Right = 0,
        Down = 1,
        Left = 2,
        Up = 3
    };

    static int DirectionIndex(int dx, int dy);
    void InitializeWave();
    bool Propagate(std::vector<std::size_t>& queue, std::vector<std::uint8_t>& queued);
    bool CollapseOnce(std::mt19937& rng, std::vector<std::uint64_t>& wave, std::size_t& outCell,
                      std::uint64_t& outChosen);
    int Popcount64(std::uint64_t value) const;
    int LowestBit(std::uint64_t value) const;

    int width_ = 0;
    int height_ = 0;
    int tileCount_ = 0;
    bool openWhenNoRules_ = true;
    bool solved_ = false;

    std::array<float, kMaxTiles> weights_{};
    std::array<bool, kMaxTiles> present_{};
    std::array<std::array<std::uint64_t, kMaxTiles>, 4> allowed_{};
    std::array<bool, 4> hasRules_{};
    std::vector<int> grid_;
    std::vector<std::uint64_t> wave_;
};

} // namespace Fem
