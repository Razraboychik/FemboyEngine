// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Pathfinding.hpp"
#include <queue>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>

namespace Fem
{

namespace
{
struct OpenNode
{
    float f = 0.f;
    int index = -1;
};

struct OpenCompare
{
    bool operator()(const OpenNode& a, const OpenNode& b) const
    {
        // std::priority_queue is a max-heap; invert to get the lowest f first.
        return a.f > b.f;
    }
};
} // namespace

float Pathfinder::Heuristic(int x1, int y1, int x2, int y2) const
{
    return static_cast<float>(std::abs(x1 - x2) + std::abs(y1 - y2));
}

std::vector<std::pair<int, int>> Pathfinder::FindPath(const Tilemap& map, int startX, int startY, int endX,
                                                      int endY) const
{
    return Search(map, startX, startY, endX, endY, false);
}

std::vector<std::pair<int, int>> Pathfinder::FindPathDiagonal(const Tilemap& map, int startX, int startY, int endX,
                                                              int endY) const
{
    return Search(map, startX, startY, endX, endY, true);
}

std::vector<std::pair<int, int>> Pathfinder::Search(const Tilemap& map, int startX, int startY, int endX, int endY,
                                                    bool diagonal) const
{
    std::vector<std::pair<int, int>> result;

    const int w = map.Width();
    const int h = map.Height();
    if (w <= 0 || h <= 0)
    {
        return result;
    }
    if (!map.InBounds(startX, startY) || !map.InBounds(endX, endY))
    {
        return result;
    }

    // The goal must be walkable; the start may be solid only if it is also the
    // goal (an entity stuck inside a wall cannot path anywhere).
    if (map.IsSolid(endX, endY))
    {
        return result;
    }
    if (map.IsSolid(startX, startY) && !(startX == endX && startY == endY))
    {
        return result;
    }

    const size_t cellCount = static_cast<size_t>(w) * static_cast<size_t>(h);
    const int startIdx = startY * w + startX;
    const int goalIdx = endY * w + endX;
    if (startIdx == goalIdx)
    {
        result.push_back({startX, startY});
        return result;
    }

    struct Node
    {
        float g = std::numeric_limits<float>::infinity();
        int parent = -1;
        bool closed = false;
        bool opened = false;
    };

    std::vector<Node> nodes(cellCount);
    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenCompare> open;

    // Manhattan distance is only admissible for 4-way movement. With diagonals
    // it overestimates (a straight diagonal is sqrt(2), not 2 cells), which
    // makes A* return visibly suboptimal paths. Octile distance is exact on an
    // empty grid and admissible around obstacles.
    const auto estimate = [&](int x, int y)
    {
        const int adx = std::abs(x - endX);
        const int ady = std::abs(y - endY);
        if (!diagonal)
        {
            return static_cast<float>(adx + ady);
        }
        const int mn = std::min(adx, ady);
        const int mx = std::max(adx, ady);
        return 1.41421356f * static_cast<float>(mn) + static_cast<float>(mx - mn);
    };

    nodes[static_cast<size_t>(startIdx)].g = 0.f;
    nodes[static_cast<size_t>(startIdx)].opened = true;
    open.push({estimate(startX, startY), startIdx});

    const int dx4[4] = {1, -1, 0, 0};
    const int dy4[4] = {0, 0, 1, -1};
    const int dx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    const int dy8[8] = {0, 0, 1, -1, 1, -1, 1, -1};

    while (!open.empty())
    {
        const OpenNode top = open.top();
        open.pop();

        const int curIdx = top.index;
        Node& cur = nodes[static_cast<size_t>(curIdx)];
        if (cur.closed)
        {
            continue; // stale heap entry
        }
        cur.closed = true;

        if (curIdx == goalIdx)
        {
            // Reconstruct through the parent chain.
            int i = curIdx;
            while (i != -1)
            {
                result.push_back({i % w, i / w});
                i = nodes[static_cast<size_t>(i)].parent;
            }
            std::reverse(result.begin(), result.end());

            // Remove redundant waypoints: A* on a grid produces staircase paths
            // even across open floor. Only smooth diagonal paths; a 4-way mover
            // cannot follow a straight diagonal line.
            if (diagonal && result.size() > 2)
            {
                const auto hasLineOfSight = [&](int x0, int y0, int x1, int y1)
                {
                    int cx = x0;
                    int cy = y0;
                    int dx = std::abs(x1 - x0);
                    int dy = std::abs(y1 - y0);
                    const int sx = (x0 < x1) ? 1 : -1;
                    const int sy = (y0 < y1) ? 1 : -1;
                    int err = dx - dy;
                    while (true)
                    {
                        if (map.IsSolid(cx, cy))
                        {
                            return false;
                        }
                        if (cx == x1 && cy == y1)
                        {
                            return true;
                        }

                        const int px = cx;
                        const int py = cy;
                        const int e2 = 2 * err;
                        if (e2 > -dy)
                        {
                            err -= dy;
                            cx += sx;
                        }
                        if (e2 < dx)
                        {
                            err += dx;
                            cy += sy;
                        }
                        // Diagonal shortcuts may not squeeze between two walls.
                        if (cx != px && cy != py && (map.IsSolid(cx, py) || map.IsSolid(px, cy)))
                        {
                            return false;
                        }
                    }
                };

                // Bound the look-ahead so a long open corridor stays O(n) instead
                // of O(n^2) on large maps.
                constexpr size_t kLookahead = 64;
                std::vector<std::pair<int, int>> smoothed;
                smoothed.reserve(result.size());
                smoothed.push_back(result.front());
                size_t i = 0;
                while (i + 1 < result.size())
                {
                    size_t j = std::min(result.size() - 1, i + kLookahead);
                    while (j > i + 1 &&
                           !hasLineOfSight(result[i].first, result[i].second, result[j].first, result[j].second))
                    {
                        --j;
                    }
                    smoothed.push_back(result[j]);
                    i = j;
                }
                result.swap(smoothed);
            }
            return result;
        }

        const int cx = curIdx % w;
        const int cy = curIdx / w;
        const int count = diagonal ? 8 : 4;

        for (int d = 0; d < count; ++d)
        {
            const int nx = cx + (diagonal ? dx8[d] : dx4[d]);
            const int ny = cy + (diagonal ? dy8[d] : dy4[d]);
            if (!map.InBounds(nx, ny))
            {
                continue;
            }
            if (map.IsSolid(nx, ny))
            {
                continue;
            }

            // Do not cut diagonal corners through two solid tiles.
            if (diagonal && d >= 4)
            {
                const int sx = dx8[d];
                const int sy = dy8[d];
                // Horizontal/vertical legs of this diagonal step.
                if (map.IsSolid(cx + sx, cy) || map.IsSolid(cx, cy + sy))
                {
                    continue;
                }
            }

            const int nIdx = ny * w + nx;
            Node& nb = nodes[static_cast<size_t>(nIdx)];
            if (nb.closed)
            {
                continue;
            }

            const float stepCost = (diagonal && d >= 4) ? 1.41421356f : 1.f;
            const float tentative = cur.g + stepCost;
            if (tentative < nb.g)
            {
                nb.g = tentative;
                nb.parent = curIdx;
                nb.opened = true;
                open.push({tentative + estimate(nx, ny), nIdx});
            }
        }
    }

    return result;
}

} // namespace Fem
