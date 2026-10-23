// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <map>
#include <functional>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/ECS.hpp"
#include "FemboyEngine/Skirt.hpp"

namespace Fem
{

// Crowd controller: manages large numbers of entities efficiently.
// Uses LOD (level of detail), spatial partitioning, and batch processing.

struct CrowdEntity
{
    Entity entity = entt::null;
    sf::Vector2f position{0, 0};
    sf::Vector2f velocity{0, 0};
    float lod = 0; // 0 = full detail, 1 = simplified, 2 = billboard
    bool active = true;
};

class CrowdController
{
public:
    static CrowdController& Instance();

    void Initialize(size_t maxEntities = 10000);
    void Shutdown();

    // Entity management. Returns a stable slot id, or -1 when the crowd is full.
    int AddEntity(const sf::Vector2f& pos);
    void RemoveEntity(int id); // idempotent
    CrowdEntity& GetEntity(int id);
    const CrowdEntity* FindEntity(int id) const;

    size_t EntityCount() const
    {
        return activeCount_;
    } // active entities
    size_t SlotCount() const
    {
        return entities_.size();
    }
    size_t MaxEntities() const
    {
        return maxEntities_;
    }
    bool IsFull() const;

    // Update with LOD. Entities outside the view radius are kept alive but get
    // the coarsest LOD; they are never silently deactivated.
    void Update(float dt, const sf::Vector2f& cameraPos, float viewRadius);

    // Spatial queries
    std::vector<int> EntitiesInRadius(const sf::Vector2f& center, float radius) const;

    // LOD management
    void SetLodThresholds(float fullDetail, float simplified, float billboard);

    // Batch rendering
    void Render(sf::RenderWindow& window);
    // Submits the crowd to the shared draw list (DrawLayer::Entities).
    void Render(Skirt& FrameDrawList);

    // Behavior (simple movement toward target)
    void SetTarget(int id, const sf::Vector2f& target);
    void SetSpeed(int id, float speed);
    void SetVelocity(int id, const sf::Vector2f& velocity);

private:
    CrowdController() = default;

    bool IsValidSlot(int id) const;
    void RebuildSpatialGrid();
    int64_t CellKey(int x, int y) const;

    std::vector<CrowdEntity> entities_;
    std::vector<int> freeSlots_;
    size_t maxEntities_ = 10000;
    size_t activeCount_ = 0;

    // Spatial grid
    float cellSize_ = 64.f;
    std::map<int64_t, std::vector<int>> spatialGrid_;

    // LOD thresholds
    float lodFull_ = 200.f;
    float lodSimplified_ = 500.f;
    float lodBillboard_ = 1000.f;
};

} // namespace Fem
