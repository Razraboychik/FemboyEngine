// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Wfc.hpp"

#include "FemboyEngine/Boy.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>

namespace Fem
{

namespace
{
constexpr int kOffsets[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
}

void Wfc::Clear()
{
    width_ = 0;
    height_ = 0;
    tileCount_ = 0;
    solved_ = false;
    weights_.fill(0.f);
    present_.fill(false);
    for (auto& dir : allowed_)
    {
        dir.fill(0);
    }
    hasRules_.fill(false);
    grid_.clear();
    wave_.clear();
}

bool Wfc::AddTile(int id, float weight)
{
    if (id < 0 || id >= kMaxTiles)
    {
        BOY_ERROR("Wfc", "tile id out of range (0..63): " + std::to_string(id));
        return false;
    }
    present_[static_cast<std::size_t>(id)] = true;
    weights_[static_cast<std::size_t>(id)] = std::max(0.001f, weight);
    tileCount_ = std::max(tileCount_, id + 1);
    return true;
}

void Wfc::AddPair(int a, int b, bool horizontal, bool vertical)
{
    if (horizontal)
    {
        AddDirectional(a, b, 1, 0);
        AddDirectional(b, a, 1, 0);
    }
    if (vertical)
    {
        AddDirectional(a, b, 0, 1);
        AddDirectional(b, a, 0, 1);
    }
}

int Wfc::DirectionIndex(int dx, int dy)
{
    if (dx == 1 && dy == 0)
    {
        return Right;
    }
    if (dx == 0 && dy == 1)
    {
        return Down;
    }
    if (dx == -1 && dy == 0)
    {
        return Left;
    }
    if (dx == 0 && dy == -1)
    {
        return Up;
    }
    return -1;
}

bool Wfc::AddDirectional(int a, int b, int dx, int dy)
{
    const int dir = DirectionIndex(dx, dy);
    if (dir < 0 || a < 0 || b < 0 || a >= kMaxTiles || b >= kMaxTiles)
    {
        return false;
    }
    if (!present_[static_cast<std::size_t>(a)] || !present_[static_cast<std::size_t>(b)])
    {
        return false;
    }

    allowed_[static_cast<std::size_t>(dir)][static_cast<std::size_t>(a)] |= (std::uint64_t{1} << b);
    const int opposite = (dir + 2) % 4;
    allowed_[static_cast<std::size_t>(opposite)][static_cast<std::size_t>(b)] |= (std::uint64_t{1} << a);
    hasRules_[static_cast<std::size_t>(dir)] = true;
    hasRules_[static_cast<std::size_t>(opposite)] = true;
    return true;
}

void Wfc::SetSize(int width, int height)
{
    width_ = std::clamp(width, 1, 512);
    height_ = std::clamp(height, 1, 512);
    grid_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), -1);
    wave_.clear();
    solved_ = false;
}

int Wfc::Popcount64(std::uint64_t value) const
{
    return std::popcount(value);
}

int Wfc::LowestBit(std::uint64_t value) const
{
    return std::countr_zero(value);
}

void Wfc::InitializeWave()
{
    std::uint64_t full = 0;
    for (int i = 0; i < kMaxTiles; ++i)
    {
        if (present_[static_cast<std::size_t>(i)])
        {
            full |= (std::uint64_t{1} << i);
        }
    }

    wave_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), full);
    grid_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), -1);
}

bool Wfc::Propagate(std::vector<std::size_t>& queue, std::vector<std::uint8_t>& queued)
{
    const bool anyRules = hasRules_[0] || hasRules_[1] || hasRules_[2] || hasRules_[3];
    const bool open = openWhenNoRules_ && !anyRules;
    std::uint64_t full = 0;
    for (int i = 0; i < kMaxTiles; ++i)
    {
        if (present_[static_cast<std::size_t>(i)])
        {
            full |= (std::uint64_t{1} << i);
        }
    }

    while (!queue.empty())
    {
        const std::size_t index = queue.back();
        queue.pop_back();
        queued[index] = 0;

        const int x = static_cast<int>(index % static_cast<std::size_t>(width_));
        const int y = static_cast<int>(index / static_cast<std::size_t>(width_));
        const std::uint64_t domain = wave_[index];
        if (domain == 0)
        {
            return false;
        }

        for (int dir = 0; dir < 4; ++dir)
        {
            std::uint64_t allowed = 0;
            if (open || !hasRules_[static_cast<std::size_t>(dir)])
            {
                allowed = full;
            }
            else
            {
                std::uint64_t bits = domain;
                while (bits)
                {
                    const int tile = LowestBit(bits);
                    bits &= bits - 1;
                    allowed |= allowed_[static_cast<std::size_t>(dir)][static_cast<std::size_t>(tile)];
                }
            }

            const int nx = x + kOffsets[dir][0];
            const int ny = y + kOffsets[dir][1];
            if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_)
            {
                continue;
            }

            const std::size_t neighbour =
                static_cast<std::size_t>(ny) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(nx);
            const std::uint64_t next = wave_[neighbour] & allowed;
            if (next == wave_[neighbour])
            {
                continue;
            }
            if (next == 0)
            {
                return false;
            }

            wave_[neighbour] = next;
            if (!queued[neighbour])
            {
                queued[neighbour] = 1;
                queue.push_back(neighbour);
            }
        }
    }
    return true;
}

bool Wfc::Solve(std::uint32_t seed, int attempts)
{
    solved_ = false;
    if (width_ <= 0 || height_ <= 0 || tileCount_ <= 0)
    {
        return false;
    }

    // A board with no rules and openWhenNoRules_ false can never satisfy its
    // first propagation; detect it here instead of inside the backtrack loop.
    const bool anyRules = hasRules_[0] || hasRules_[1] || hasRules_[2] || hasRules_[3];
    if (!anyRules && !openWhenNoRules_)
    {
        return false;
    }

    std::mt19937 rng(seed != 0 ? seed : std::random_device{}());

    struct Snapshot
    {
        std::vector<std::uint64_t> wave;
        std::size_t cell = 0;
        std::uint64_t chosen = 0;
    };

    const std::size_t total = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);

    for (int attempt = 0; attempt < std::max(1, attempts); ++attempt)
    {
        InitializeWave();
        std::vector<Snapshot> stack;
        stack.reserve(64);

        std::vector<std::size_t> queue;
        std::vector<std::uint8_t> queued(total, 0);

        bool failed = false;
        while (true)
        {
            // Pick the lowest-entropy uncollapsed cell. Ties are random, which
            // is what makes two seeds produce different but legal maps.
            int bestEntropy = std::numeric_limits<int>::max();
            std::vector<std::size_t> candidates;
            for (std::size_t i = 0; i < total; ++i)
            {
                const int entropy = Popcount64(wave_[i]);
                if (entropy <= 1)
                {
                    continue;
                }
                if (entropy < bestEntropy)
                {
                    bestEntropy = entropy;
                    candidates.clear();
                    candidates.push_back(i);
                }
                else if (entropy == bestEntropy)
                {
                    candidates.push_back(i);
                }
            }

            if (candidates.empty())
            {
                break; // fully collapsed
            }

            const std::size_t cell = candidates[rng() % candidates.size()];
            const std::uint64_t domain = wave_[cell];

            // Weighted choice among the remaining tiles.
            float weightSum = 0.f;
            for (int tile = 0; tile < kMaxTiles; ++tile)
            {
                if (domain & (std::uint64_t{1} << tile))
                {
                    weightSum += weights_[static_cast<std::size_t>(tile)];
                }
            }
            if (!(weightSum > 0.f))
            {
                break;
            }

            float pick = std::uniform_real_distribution<float>(0.f, weightSum)(rng);
            int chosenTile = -1;
            for (int tile = 0; tile < kMaxTiles; ++tile)
            {
                if (!(domain & (std::uint64_t{1} << tile)))
                {
                    continue;
                }
                pick -= weights_[static_cast<std::size_t>(tile)];
                if (pick <= 0.f)
                {
                    chosenTile = tile;
                    break;
                }
            }
            if (chosenTile < 0)
            {
                for (int tile = 0; tile < kMaxTiles; ++tile)
                {
                    if (domain & (std::uint64_t{1} << tile))
                    {
                        chosenTile = tile;
                        break;
                    }
                }
            }

            Snapshot snapshot;
            snapshot.wave = wave_;
            snapshot.cell = cell;
            snapshot.chosen = std::uint64_t{1} << chosenTile;
            if (stack.size() < 4096)
            {
                stack.push_back(std::move(snapshot));
            }

            wave_[cell] = std::uint64_t{1} << chosenTile;
            queue.clear();
            std::fill(queued.begin(), queued.end(), 0);
            queue.push_back(cell);
            queued[cell] = 1;

            bool ok = Propagate(queue, queued);
            while (!ok)
            {
                if (stack.empty())
                {
                    failed = true;
                    break;
                }

                Snapshot back = std::move(stack.back());
                stack.pop_back();
                wave_ = std::move(back.wave);
                wave_[back.cell] &= ~back.chosen;
                if (wave_[back.cell] == 0)
                {
                    ok = false;
                    continue;
                }

                queue.clear();
                std::fill(queued.begin(), queued.end(), 0);
                queue.push_back(back.cell);
                queued[back.cell] = 1;
                ok = Propagate(queue, queued);
            }

            if (failed)
            {
                break;
            }
        }

        if (failed)
        {
            continue;
        }

        grid_.assign(total, -1);
        for (std::size_t i = 0; i < total; ++i)
        {
            const std::uint64_t domain = wave_[i];
            if (Popcount64(domain) != 1)
            {
                failed = true;
                break;
            }
            grid_[i] = LowestBit(domain);
        }
        if (!failed)
        {
            solved_ = true;
            return true;
        }
    }

    solved_ = false;
    return false;
}

int Wfc::At(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
    {
        return -1;
    }
    return grid_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
}

int Wfc::Entropy(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || wave_.empty())
    {
        return 0;
    }
    return Popcount64(
        wave_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)]);
}

nlohmann::json Wfc::ToJson() const
{
    nlohmann::json tiles = nlohmann::json::array();
    for (int i = 0; i < kMaxTiles; ++i)
    {
        if (present_[static_cast<std::size_t>(i)])
        {
            tiles.push_back({{"id", i}, {"weight", weights_[static_cast<std::size_t>(i)]}});
        }
    }

    nlohmann::json rules = nlohmann::json::array();
    for (int dir = 0; dir < 4; ++dir)
    {
        for (int a = 0; a < kMaxTiles; ++a)
        {
            for (int b = 0; b < kMaxTiles; ++b)
            {
                if (allowed_[static_cast<std::size_t>(dir)][static_cast<std::size_t>(a)] & (std::uint64_t{1} << b))
                {
                    rules.push_back({{"a", a}, {"b", b}, {"dx", kOffsets[dir][0]}, {"dy", kOffsets[dir][1]}});
                }
            }
        }
    }

    return {{"width", width_}, {"height", height_}, {"tiles", std::move(tiles)}, {"rules", std::move(rules)}};
}

bool Wfc::FromJson(const nlohmann::json& json)
{
    if (!json.is_object())
    {
        return false;
    }

    Clear();
    SetSize(json.value("width", 32), json.value("height", 32));

    if (json.contains("tiles") && json["tiles"].is_array())
    {
        for (const auto& entry : json["tiles"])
        {
            if (!entry.is_object())
            {
                continue;
            }
            AddTile(entry.value("id", 0), entry.value("weight", 1.f));
        }
    }

    if (json.contains("rules") && json["rules"].is_array())
    {
        for (const auto& entry : json["rules"])
        {
            if (!entry.is_object())
            {
                continue;
            }
            const int a = entry.value("a", -1);
            const int b = entry.value("b", -1);
            if (a < 0 || b < 0)
            {
                continue;
            }
            if (entry.contains("dx") || entry.contains("dy"))
            {
                AddDirectional(a, b, entry.value("dx", 1), entry.value("dy", 0));
            }
            else
            {
                AddPair(a, b);
            }
        }
    }

    return true;
}

bool Wfc::SaveToFile(const std::string& path) const
{
    std::ofstream out(path);
    if (!out)
    {
        return false;
    }
    out << ToJson().dump(1);
    return static_cast<bool>(out);
}

bool Wfc::LoadFromFile(const std::string& path)
{
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
    catch (const std::exception& e)
    {
        BOY_ERROR("Wfc", std::string("bad JSON: ") + e.what());
        return false;
    }
    return FromJson(json);
}

} // namespace Fem
