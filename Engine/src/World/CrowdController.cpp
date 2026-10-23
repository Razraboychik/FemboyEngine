// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/CrowdController.hpp"
#include "FemboyEngine/Boy.hpp"
#include <cmath>
#include <algorithm>

namespace Fem
{

CrowdController& CrowdController::Instance()
{
    static CrowdController c;
    return c;
}

void CrowdController::Initialize(size_t maxEntities)
{
    maxEntities_ = (maxEntities > 0) ? maxEntities : 10000;
    entities_.clear();
    freeSlots_.clear();
    spatialGrid_.clear();
    activeCount_ = 0;
    entities_.reserve(maxEntities_);
    BOY_INFO("Crowd", "initialized, max entities: " + std::to_string(maxEntities_));
}

void CrowdController::Shutdown()
{
    entities_.clear();
    freeSlots_.clear();
    spatialGrid_.clear();
    activeCount_ = 0;
}

bool CrowdController::IsValidSlot(int id) const
{
    return id >= 0 && id < static_cast<int>(entities_.size());
}

bool CrowdController::IsFull() const
{
    return freeSlots_.empty() && entities_.size() >= maxEntities_;
}

int CrowdController::AddEntity(const sf::Vector2f& pos)
{
    if (!freeSlots_.empty())
    {
        const int id = freeSlots_.back();
        freeSlots_.pop_back();
        if (!IsValidSlot(id))
        {
            BOY_WARN("Crowd", "addEntity: discarding invalid free slot " + std::to_string(id));
            return -1;
        }

        CrowdEntity& e = entities_[static_cast<size_t>(id)];
        if (e.active)
        {
            // The slot was on the free list but still marked active. Returning
            // the slot to the list keeps the bookkeeping self-healing instead of
            // permanently leaking it.
            freeSlots_.push_back(id);
            BOY_WARN("Crowd", "addEntity: slot " + std::to_string(id) + " already active");
            return -1;
        }

        e = CrowdEntity{};
        e.position = pos;
        e.active = true;
        e.lod = 0;
        ++activeCount_;
        return id;
    }

    if (entities_.size() >= maxEntities_)
    {
        BOY_WARN("Crowd", "addEntity: crowd is full (" + std::to_string(maxEntities_) + ")");
        return -1;
    }

    CrowdEntity e;
    e.position = pos;
    entities_.push_back(e);
    ++activeCount_;
    return static_cast<int>(entities_.size()) - 1;
}

void CrowdController::RemoveEntity(int id)
{
    if (!IsValidSlot(id))
    {
        return;
    }
    CrowdEntity& e = entities_[static_cast<size_t>(id)];
    if (!e.active)
    {
        return; // already removed: keep freeSlots_ duplicate-free
    }

    e.active = false;
    e.velocity = {0, 0};
    e.lod = 0;
    freeSlots_.push_back(id);
    if (activeCount_ > 0)
    {
        --activeCount_;
    }
}

CrowdEntity& CrowdController::GetEntity(int id)
{
    static thread_local CrowdEntity dummy;
    if (!IsValidSlot(id))
    {
        return dummy;
    }
    return entities_[static_cast<size_t>(id)];
}

const CrowdEntity* CrowdController::FindEntity(int id) const
{
    if (!IsValidSlot(id))
    {
        return nullptr;
    }
    return &entities_[static_cast<size_t>(id)];
}

int64_t CrowdController::CellKey(int x, int y) const
{
    // Pack two signed 32-bit cell coordinates into one 64-bit key. The old
    // x * 10007 + y scheme collided for many distinct cells.
    return (static_cast<int64_t>(x) << 32) ^ static_cast<uint32_t>(y);
}

void CrowdController::RebuildSpatialGrid()
{
    spatialGrid_.clear();
    for (size_t i = 0; i < entities_.size(); ++i)
    {
        const CrowdEntity& e = entities_[i];
        if (!e.active)
        {
            continue;
        }

        const int cx = static_cast<int>(std::floor(e.position.x / cellSize_));
        const int cy = static_cast<int>(std::floor(e.position.y / cellSize_));
        spatialGrid_[CellKey(cx, cy)].push_back(static_cast<int>(i));
    }
}

std::vector<int> CrowdController::EntitiesInRadius(const sf::Vector2f& center, float radius) const
{
    std::vector<int> result;
    if (radius < 0.f || cellSize_ <= 0.f)
    {
        return result;
    }

    const int cx = static_cast<int>(std::floor(center.x / cellSize_));
    const int cy = static_cast<int>(std::floor(center.y / cellSize_));
    const int cellRadius = static_cast<int>(radius / cellSize_) + 1;
    const float rr = radius * radius;

    for (int dy = -cellRadius; dy <= cellRadius; ++dy)
    {
        for (int dx = -cellRadius; dx <= cellRadius; ++dx)
        {
            auto it = spatialGrid_.find(CellKey(cx + dx, cy + dy));
            if (it == spatialGrid_.end())
            {
                continue;
            }

            for (int id : it->second)
            {
                if (!IsValidSlot(id))
                {
                    continue;
                }
                const CrowdEntity& e = entities_[static_cast<size_t>(id)];
                if (!e.active)
                {
                    continue;
                }

                const float ddx = e.position.x - center.x;
                const float ddy = e.position.y - center.y;
                if (ddx * ddx + ddy * ddy <= rr)
                {
                    result.push_back(id);
                }
            }
        }
    }
    return result;
}

void CrowdController::SetLodThresholds(float fullDetail, float simplified, float billboard)
{
    // Enforce ascending thresholds so the LOD ladder cannot be inverted.
    lodFull_ = std::max(0.f, fullDetail);
    lodSimplified_ = std::max(lodFull_, simplified);
    lodBillboard_ = std::max(lodSimplified_, billboard);
}

void CrowdController::Update(float dt, const sf::Vector2f& cameraPos, float viewRadius)
{
    if (dt < 0.f)
    {
        dt = 0.f;
    }
    RebuildSpatialGrid();

    for (size_t i = 0; i < entities_.size(); ++i)
    {
        CrowdEntity& e = entities_[i];
        if (!e.active)
        {
            continue;
        }

        const float dx = e.position.x - cameraPos.x;
        const float dy = e.position.y - cameraPos.y;
        const float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < lodFull_)
        {
            e.lod = 0;
        }
        else if (dist < lodSimplified_)
        {
            e.lod = 1;
        }
        else
        {
            e.lod = 2;
        }

        // Entities beyond the view radius stay simulated (so they do not pop or
        // vanish when the camera returns) but are marked as the coarsest LOD.
        if (viewRadius > 0.f && dist > viewRadius)
        {
            e.lod = 2;
        }

        e.position.x += e.velocity.x * dt;
        e.position.y += e.velocity.y * dt;
    }
}

void CrowdController::Render(sf::RenderWindow& window)
{
    for (const auto& e : entities_)
    {
        if (!e.active)
        {
            continue;
        }

        const float radius = (e.lod == 2) ? 2.f : (e.lod == 1 ? 3.f : 4.f);
        sf::CircleShape shape(radius);
        shape.setFillColor(e.lod == 0 ? sf::Color::White : sf::Color(128, 128, 128));
        shape.setPosition({e.position.x - radius, e.position.y - radius});
        window.draw(shape);
    }
}

void CrowdController::Render(Skirt& FrameDrawList)
{
    for (const auto& e : entities_)
    {
        if (!e.active)
        {
            continue;
        }

        const float radius = (e.lod == 2) ? 2.f : (e.lod == 1 ? 3.f : 4.f);
        const sf::Color color = (e.lod == 0) ? sf::Color::White : sf::Color(128, 128, 128);
        FrameDrawList.Circle(e.position, radius, color, 0, DrawLayer::Entities);
    }
}

void CrowdController::SetTarget(int id, const sf::Vector2f& target)
{
    if (!IsValidSlot(id))
    {
        return;
    }
    CrowdEntity& e = entities_[static_cast<size_t>(id)];
    if (!e.active)
    {
        return;
    }

    const float dx = target.x - e.position.x;
    const float dy = target.y - e.position.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.0001f)
    {
        e.velocity = {dx / len * 50.f, dy / len * 50.f};
    }
}

void CrowdController::SetSpeed(int id, float speed)
{
    if (!IsValidSlot(id))
    {
        return;
    }
    CrowdEntity& e = entities_[static_cast<size_t>(id)];
    if (!e.active)
    {
        return;
    }

    const float len = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.y * e.velocity.y);
    if (len > 0.0001f)
    {
        e.velocity.x = e.velocity.x / len * speed;
        e.velocity.y = e.velocity.y / len * speed;
    }
}

void CrowdController::SetVelocity(int id, const sf::Vector2f& velocity)
{
    if (!IsValidSlot(id))
    {
        return;
    }
    CrowdEntity& e = entities_[static_cast<size_t>(id)];
    if (!e.active)
    {
        return;
    }
    e.velocity = velocity;
}

} // namespace Fem
