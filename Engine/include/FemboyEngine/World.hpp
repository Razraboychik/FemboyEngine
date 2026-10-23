// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <cstddef>
#include <type_traits>
#include <utility>

#include "FemboyEngine/ECS.hpp"

namespace Fem
{

// World is the lifecycle boundary around the ECS registry.
//
// Games should ask the World to create/destroy entities instead of talking to
// the raw registry. That keeps one place for entity ids, future prefab defaults,
// pooling, replication notifications and editor integration. The registry is
// still exposed for systems that need a view, so this is an incremental step,
// not a second entity model.
class World
{
public:
    explicit World(Registry& registry) : registry_(&registry)
    {
    }

    Entity Spawn()
    {
        return registry_->create();
    }

    template <typename... Components> Entity SpawnWith(Components&&... components)
    {
        const Entity entity = registry_->create();
        (registry_->emplace<std::decay_t<Components>>(entity, std::forward<Components>(components)), ...);
        return entity;
    }

    bool Destroy(Entity entity)
    {
        if (!registry_->valid(entity))
        {
            return false;
        }
        registry_->destroy(entity);
        return true;
    }

    void Clear()
    {
        registry_->clear();
    }

    bool Valid(Entity entity) const
    {
        return registry_->valid(entity);
    }

    std::size_t Count() const
    {
        const auto& entities = registry_->storage<Entity>();
        // EnTT keeps destroyed ids in the packed array and marks the live
        // prefix with free_list(). It is the live entity count, not the
        // number of destroyed slots.
        return static_cast<std::size_t>(entities.free_list());
    }

    template <typename... Components> auto View()
    {
        return registry_->view<Components...>();
    }

    template <typename... Components> auto View() const
    {
        return registry_->view<Components...>();
    }

    template <typename Component> Component* TryGet(Entity entity)
    {
        return registry_->try_get<Component>(entity);
    }

    template <typename Component> const Component* TryGet(Entity entity) const
    {
        return registry_->try_get<Component>(entity);
    }

    template <typename Component> Component& Get(Entity entity)
    {
        return registry_->get<Component>(entity);
    }

    template <typename Component, typename... Args> Component& Emplace(Entity entity, Args&&... args)
    {
        return registry_->emplace<Component>(entity, std::forward<Args>(args)...);
    }

    template <typename... Components> bool Has(Entity entity) const
    {
        return registry_->all_of<Components...>(entity);
    }

    Registry& GetRegistry()
    {
        return *registry_;
    }

    const Registry& GetRegistry() const
    {
        return *registry_;
    }

private:
    Registry* registry_ = nullptr;
};

} // namespace Fem
