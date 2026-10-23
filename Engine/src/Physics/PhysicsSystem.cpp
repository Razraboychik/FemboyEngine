// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/PhysicsSystem.hpp"
#include "FemboyEngine/Boy.hpp"
#include <cmath>

namespace Fem
{

bool PhysicsSystem::CreateBody(Registry& reg, PhysicsWorld& world, Entity e, float halfWidth, float halfHeight,
                               bool dynamic, float density, float friction)
{
    if (!reg.valid(e))
    {
        BOY_WARN("Physics", "createBody: invalid entity");
        return false;
    }

#ifdef FEMBOY_HAS_BOX2D
    if (!world.Available())
    {
        BOY_WARN("Physics", "createBody: world unavailable");
        return false;
    }

    // Bodies already carrying a live body must not leak a second one.
    if (auto* existing = reg.try_get<PhysicsBodyComponent>(e))
    {
        if (existing->created && b2Body_IsValid(existing->bodyId))
        {
            return true;
        }
    }

    const float ppm = world.PixelsPerMeter();
    if (!std::isfinite(ppm) || ppm <= 0.f)
    {
        return false;
    }

    TransformComponent* t = reg.try_get<TransformComponent>(e);
    const float px = t ? t->x : 0.f;
    const float py = t ? t->y : 0.f;

    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = dynamic ? b2_dynamicBody : b2_staticBody;
    bodyDef.position = b2Vec2{px / ppm, py / ppm};
    if (t)
    {
        bodyDef.rotation = b2MakeRot(t->rotation * 3.14159265358979f / 180.f);
    }

    b2BodyId body = b2CreateBody(world.WorldId(), &bodyDef);
    if (!b2Body_IsValid(body))
    {
        BOY_ERROR("Physics", "createBody: b2CreateBody failed");
        return false;
    }

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = density;
    shapeDef.material.friction = friction;

    const float hw = std::max(0.001f, halfWidth) / ppm;
    const float hh = std::max(0.001f, halfHeight) / ppm;
    const b2Polygon box = b2MakeBox(hw, hh);
    b2CreatePolygonShape(body, &shapeDef, &box);

    PhysicsBodyComponent pb;
    pb.bodyId = body;
    pb.isDynamic = dynamic;
    pb.density = density;
    pb.friction = friction;
    pb.created = true;
    reg.emplace_or_replace<PhysicsBodyComponent>(e, pb);
    return true;
#else
    (void)world;
    (void)e;
    (void)halfWidth;
    (void)halfHeight;
    (void)dynamic;
    (void)density;
    (void)friction;
    BOY_WARN("Physics", "createBody: Box2D not available");
    return false;
#endif
}

void PhysicsSystem::DestroyBody(Registry& reg, Entity e)
{
    if (!reg.valid(e))
    {
        return;
    }

#ifdef FEMBOY_HAS_BOX2D
    if (auto* pb = reg.try_get<PhysicsBodyComponent>(e))
    {
        if (pb->created && b2Body_IsValid(pb->bodyId))
        {
            b2DestroyBody(pb->bodyId);
        }
        pb->created = false;
        pb->bodyId = b2BodyId{};
        reg.remove<PhysicsBodyComponent>(e);
    }
#else
    (void)e;
#endif
}

void PhysicsSystem::Update(Registry& reg, PhysicsWorld& world, float dt)
{
#ifdef FEMBOY_HAS_BOX2D
    world.Step(dt);

    auto view = reg.view<TransformComponent, PhysicsBodyComponent>();
    for (auto e : view)
    {
        auto& pb = view.get<PhysicsBodyComponent>(e);
        if (!pb.created || !b2Body_IsValid(pb.bodyId))
        {
            continue;
        }

        auto& t = view.get<TransformComponent>(e);
        const b2Vec2 pos = b2Body_GetPosition(pb.bodyId);
        const float ppm = world.PixelsPerMeter();
        t.x = pos.x * ppm;
        t.y = pos.y * ppm;
        t.rotation = b2Rot_GetAngle(b2Body_GetRotation(pb.bodyId)) * 180.f / 3.14159265358979f;
    }
#else
    (void)reg;
    (void)world;
    (void)dt;
#endif
}

} // namespace Fem
