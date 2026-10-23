// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Physics.hpp"
#include "FemboyEngine/Boy.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace Fem
{

PhysicsWorld::PhysicsWorld()
{
#ifdef FEMBOY_HAS_BOX2D
    b2WorldDef def = b2DefaultWorldDef();
    // Box2D is y-down here to match 2D screen space, so gravity is positive.
    def.gravity = b2Vec2{0.0f, 9.8f};
    worldId_ = b2CreateWorld(&def);
    created_ = b2World_IsValid(worldId_);
    if (!created_)
    {
        BOY_ERROR("Physics", "failed to create Box2D world");
        return;
    }
    BOY_INFO("Physics", "Box2D world created");
#else
    BOY_WARN("Physics", "Box2D not available (stub)");
#endif
}

PhysicsWorld::~PhysicsWorld()
{
#ifdef FEMBOY_HAS_BOX2D
    if (created_ && b2World_IsValid(worldId_))
    {
        b2DestroyWorld(worldId_);
    }
    created_ = false;
#endif
}

void PhysicsWorld::Step(float dt)
{
#ifdef FEMBOY_HAS_BOX2D
    if (!created_ || !b2World_IsValid(worldId_))
    {
        return;
    }
    if (!std::isfinite(dt) || dt <= 0.f)
    {
        return;
    }

    // Clamp the timestep so a stalled frame cannot tunnel bodies through walls.
    const float clamped = std::min(dt, 1.f / 30.f);
    b2World_Step(worldId_, clamped, 4);
#else
    (void)dt;
#endif
}

void PhysicsWorld::SetWorkerCount(int count)
{
#ifdef FEMBOY_HAS_BOX2D
    if (!created_ || !b2World_IsValid(worldId_))
    {
        return;
    }
    // Box2D's scheduler creates its own worker threads. It clamps 0 to its
    // serial one-worker mode. A large value is a configuration mistake, not a
    // request to spawn hundreds of threads.
    count = std::clamp(count, 1, 64);
    b2World_SetWorkerCount(worldId_, count);
    BOY_INFO("Physics", "Box2D worker count set to " + std::to_string(count));
#else
    (void)count;
#endif
}

int PhysicsWorld::WorkerCount() const
{
#ifdef FEMBOY_HAS_BOX2D
    if (!created_ || !b2World_IsValid(worldId_))
    {
        return 0;
    }
    return b2World_GetWorkerCount(worldId_);
#else
    return 0;
#endif
}

void PhysicsWorld::SetGravity(float gx, float gy)
{
#ifdef FEMBOY_HAS_BOX2D
    if (created_ && b2World_IsValid(worldId_))
    {
        b2World_SetGravity(worldId_, b2Vec2{gx, gy});
    }
#else
    (void)gx;
    (void)gy;
#endif
}

void PhysicsWorld::SetPixelsPerMeter(float ppm)
{
    // A zero or negative scale would divide by zero when converting positions.
    if (!std::isfinite(ppm) || ppm <= 0.f)
    {
        return;
    }
    pixelsPerMeter_ = std::clamp(ppm, 1.f, 1024.f);
}

#ifdef FEMBOY_HAS_BOX2D
namespace
{
b2Transform AnchorFrame(b2BodyId body, sf::Vector2f anchorPixels, float ppm, b2Rot rotation)
{
    b2Transform frame{};
    frame.p = b2Body_GetLocalPoint(body, b2ToPos({anchorPixels.x / ppm, anchorPixels.y / ppm}));
    frame.q = rotation;
    return frame;
}

b2Rot InverseBodyRotation(b2BodyId body)
{
    const b2Rot q = b2Body_GetTransform(body).q;
    return b2Rot{q.c, -q.s};
}

b2Rot LocalAxisRotation(b2BodyId body, sf::Vector2f worldAxis)
{
    const b2Vec2 local = b2Body_GetLocalVector(body, b2Vec2{worldAxis.x, worldAxis.y});
    return b2MakeRot(std::atan2(local.y, local.x));
}
} // namespace

JointHandle PhysicsWorld::CreateDistanceJoint(b2BodyId bodyA, b2BodyId bodyB, float lengthPixels, bool spring,
                                              float hertz, float dampingRatio, bool collideConnected)
{
    JointHandle handle;
    if (!created_ || !b2World_IsValid(worldId_) || !b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB))
    {
        return handle;
    }

    b2DistanceJointDef def = b2DefaultDistanceJointDef();
    def.base.bodyIdA = bodyA;
    def.base.bodyIdB = bodyB;
    def.base.collideConnected = collideConnected;
    def.base.localFrameA = AnchorFrame(bodyA, {0.f, 0.f}, pixelsPerMeter_, b2MakeRot(0.f));
    def.base.localFrameB = AnchorFrame(bodyB, {0.f, 0.f}, pixelsPerMeter_, b2MakeRot(0.f));
    def.length = std::max(0.01f, lengthPixels / pixelsPerMeter_);
    def.enableSpring = spring;
    def.hertz = std::max(0.f, hertz);
    def.dampingRatio = std::clamp(dampingRatio, 0.f, 10.f);
    handle.id = b2CreateDistanceJoint(worldId_, &def);
    return handle;
}

JointHandle PhysicsWorld::CreateRevoluteJoint(b2BodyId bodyA, b2BodyId bodyB, sf::Vector2f anchorPixels,
                                              bool enableLimit, float lowerDegrees, float upperDegrees,
                                              bool enableMotor, float motorSpeed, float maxMotorTorque,
                                              bool collideConnected)
{
    JointHandle handle;
    if (!created_ || !b2World_IsValid(worldId_) || !b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB))
    {
        return handle;
    }

    b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
    def.base.bodyIdA = bodyA;
    def.base.bodyIdB = bodyB;
    def.base.collideConnected = collideConnected;
    def.base.localFrameA = AnchorFrame(bodyA, anchorPixels, pixelsPerMeter_, b2MakeRot(0.f));
    def.base.localFrameB = AnchorFrame(bodyB, anchorPixels, pixelsPerMeter_, b2MakeRot(0.f));
    def.enableLimit = enableLimit;
    def.lowerAngle = lowerDegrees * 3.14159265358979323846f / 180.f;
    def.upperAngle = upperDegrees * 3.14159265358979323846f / 180.f;
    def.enableMotor = enableMotor;
    def.motorSpeed = motorSpeed;
    def.maxMotorTorque = std::max(0.f, maxMotorTorque);
    handle.id = b2CreateRevoluteJoint(worldId_, &def);
    return handle;
}

JointHandle PhysicsWorld::CreateWeldJoint(b2BodyId bodyA, b2BodyId bodyB, sf::Vector2f anchorPixels,
                                          bool collideConnected)
{
    JointHandle handle;
    if (!created_ || !b2World_IsValid(worldId_) || !b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB))
    {
        return handle;
    }

    b2WeldJointDef def = b2DefaultWeldJointDef();
    def.base.bodyIdA = bodyA;
    def.base.bodyIdB = bodyB;
    def.base.collideConnected = collideConnected;
    // Both local frames describe the same world anchor; their rotations are the
    // inverse body rotations, so the bodies keep the relative angle they had at
    // creation instead of snapping to zero.
    def.base.localFrameA = AnchorFrame(bodyA, anchorPixels, pixelsPerMeter_, InverseBodyRotation(bodyA));
    def.base.localFrameB = AnchorFrame(bodyB, anchorPixels, pixelsPerMeter_, InverseBodyRotation(bodyB));
    handle.id = b2CreateWeldJoint(worldId_, &def);
    return handle;
}

JointHandle PhysicsWorld::CreatePrismaticJoint(b2BodyId bodyA, b2BodyId bodyB, sf::Vector2f anchorPixels,
                                               sf::Vector2f worldAxis, bool enableLimit, float lowerPixels,
                                               float upperPixels, bool enableMotor, float motorSpeed,
                                               float maxMotorForce, bool collideConnected)
{
    JointHandle handle;
    if (!created_ || !b2World_IsValid(worldId_) || !b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB))
    {
        return handle;
    }

    b2PrismaticJointDef def = b2DefaultPrismaticJointDef();
    def.base.bodyIdA = bodyA;
    def.base.bodyIdB = bodyB;
    def.base.collideConnected = collideConnected;
    def.base.localFrameA = AnchorFrame(bodyA, anchorPixels, pixelsPerMeter_, LocalAxisRotation(bodyA, worldAxis));
    def.base.localFrameB = AnchorFrame(bodyB, anchorPixels, pixelsPerMeter_, LocalAxisRotation(bodyB, worldAxis));
    def.enableLimit = enableLimit;
    def.lowerTranslation = lowerPixels / pixelsPerMeter_;
    def.upperTranslation = upperPixels / pixelsPerMeter_;
    def.enableMotor = enableMotor;
    def.motorSpeed = motorSpeed;
    def.maxMotorForce = std::max(0.f, maxMotorForce);
    handle.id = b2CreatePrismaticJoint(worldId_, &def);
    return handle;
}

JointHandle PhysicsWorld::CreateWheelJoint(b2BodyId bodyA, b2BodyId bodyB, sf::Vector2f anchorPixels,
                                           sf::Vector2f worldAxis, bool enableSpring, float hertz, float dampingRatio,
                                           bool enableMotor, float motorSpeed, float maxMotorTorque,
                                           bool collideConnected)
{
    JointHandle handle;
    if (!created_ || !b2World_IsValid(worldId_) || !b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB))
    {
        return handle;
    }

    b2WheelJointDef def = b2DefaultWheelJointDef();
    def.base.bodyIdA = bodyA;
    def.base.bodyIdB = bodyB;
    def.base.collideConnected = collideConnected;
    def.base.localFrameA = AnchorFrame(bodyA, anchorPixels, pixelsPerMeter_, LocalAxisRotation(bodyA, worldAxis));
    def.base.localFrameB = AnchorFrame(bodyB, anchorPixels, pixelsPerMeter_, LocalAxisRotation(bodyB, worldAxis));
    def.enableSpring = enableSpring;
    def.hertz = std::max(0.f, hertz);
    def.dampingRatio = std::clamp(dampingRatio, 0.f, 10.f);
    def.enableMotor = enableMotor;
    def.motorSpeed = motorSpeed;
    def.maxMotorTorque = std::max(0.f, maxMotorTorque);
    handle.id = b2CreateWheelJoint(worldId_, &def);
    return handle;
}

void PhysicsWorld::DestroyJoint(JointHandle& joint)
{
    if (joint.Valid())
    {
        b2DestroyJoint(joint.id);
    }
    joint.id = b2JointId{};
}
#endif

} // namespace Fem
