// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <vector>
#include <utility>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/Tilemap.hpp"

namespace Fem
{

// A* pathfinding on a Tilemap (4-directional, Manhattan heuristic).
class Pathfinder
{
public:
    // Returns an empty path when no route exists. The returned path includes
    // both the start and the goal tile.
    std::vector<std::pair<int, int>> FindPath(const Tilemap& map, int startX, int startY, int endX, int endY) const;

    // Same as findPath, but allows diagonal movement (cost sqrt(2)).
    std::vector<std::pair<int, int>> FindPathDiagonal(const Tilemap& map, int startX, int startY, int endX,
                                                      int endY) const;

private:
    float Heuristic(int x1, int y1, int x2, int y2) const;
    std::vector<std::pair<int, int>> Search(const Tilemap& map, int startX, int startY, int endX, int endY,
                                            bool diagonal) const;
};

} // namespace Fem
