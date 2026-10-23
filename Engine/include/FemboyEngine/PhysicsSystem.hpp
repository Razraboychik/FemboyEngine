// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/ECS.hpp"
#include "FemboyEngine/Physics.hpp"
#include <entt/entt.hpp>

namespace Fem
{

// Bridges Box2D bodies and ECS transforms.
//
// createBody()   creates a Box2D body for an entity and stores its handle in
//                PhysicsBodyComponent. The entity keeps owning nothing else.
// destroyBody()  destroys the body and removes the component.
// Update()       steps the world and copies body transforms back into the ECS.
class PhysicsSystem
{
public:
    // halfWidth/halfHeight are in pixels (screen space, y down).
    bool CreateBody(Registry& reg, PhysicsWorld& world, Entity e, float halfWidth, float halfHeight,
                    bool dynamic = true, float density = 1.f, float friction = 0.3f);

    void DestroyBody(Registry& reg, Entity e);

    void Update(Registry& reg, PhysicsWorld& world, float dt);
};

} // namespace Fem
