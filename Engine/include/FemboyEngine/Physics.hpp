// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include "FemboyEngine/Core.hpp"
#include <SFML/System/Vector2.hpp>
#ifdef FEMBOY_HAS_BOX2D
#include "box2d/box2d.h"
#endif

namespace Fem
{

// A joint is a constraint between two rigid bodies. PhysicsWorld exposes the
// five that cover almost every 2D game: a hinge, a rigid weld, a spring/rope, a
// slider and a wheel.
enum class JointType
{
    Distance,
    Revolute,
    Weld,
    Prismatic,
    Wheel
};

#ifdef FEMBOY_HAS_BOX2D
struct JointHandle
{
    b2JointId id{};

    bool Valid() const
    {
        return b2Joint_IsValid(id);
    }
};
#endif

// Thin wrapper around a Box2D world. Gravity is in meters per second squared;
// the rendering side uses 32 pixels per meter (see PhysicsSystem).
class PhysicsWorld
{
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // dt is clamped internally so a long stall cannot explode the simulation.
    void Step(float dt);
    void SetGravity(float gx, float gy);
    void SetPixelsPerMeter(float ppm);
    float PixelsPerMeter() const
    {
        return pixelsPerMeter_;
    }

#ifdef FEMBOY_HAS_BOX2D
    b2WorldId WorldId() const
    {
        return worldId_;
    }

    // Opt-in Box2D island parallelism. 0/1 keeps the deterministic serial path
    // (the default, and the only mode used by rollback); 2..N uses Box2D's own
    // worker scheduler. This is not a custom island solver: the engine only
    // forwards the count to b2World_SetWorkerCount(), which reports 1 for the
    // serial path.
    void SetWorkerCount(int count);
    int WorkerCount() const;

    // Joint factory methods. Anchors are in pixels, like PhysicsSystem's bodies;
    // they are converted with pixelsPerMeter(). Angles are degrees (converted to
    // Box2D radians internally). An invalid body or world returns an invalid
    // handle instead of throwing.
    JointHandle CreateDistanceJoint(b2BodyId bodyA, b2BodyId bodyB, float lengthPixels, bool spring = false,
                                    float hertz = 4.f, float dampingRatio = 0.7f, bool collideConnected = false);
    JointHandle CreateRevoluteJoint(b2BodyId bodyA, b2BodyId bodyB, sf::Vector2f anchorPixels, bool enableLimit = false,
                                    float lowerDegrees = -180.f, float upperDegrees = 180.f, bool enableMotor = false,
                                    float motorSpeed = 0.f, float maxMotorTorque = 0.f, bool collideConnected = false);
    JointHandle CreateWeldJoint(b2BodyId bodyA, b2BodyId bodyB, sf::Vector2f anchorPixels,
                                bool collideConnected = false);
    JointHandle CreatePrismaticJoint(b2BodyId bodyA, b2BodyId bodyB, sf::Vector2f anchorPixels, sf::Vector2f worldAxis,
                                     bool enableLimit = false, float lowerPixels = 0.f, float upperPixels = 0.f,
                                     bool enableMotor = false, float motorSpeed = 0.f, float maxMotorForce = 0.f,
                                     bool collideConnected = false);
    JointHandle CreateWheelJoint(b2BodyId bodyA, b2BodyId bodyB, sf::Vector2f anchorPixels, sf::Vector2f worldAxis,
                                 bool enableSpring = false, float hertz = 4.f, float dampingRatio = 0.7f,
                                 bool enableMotor = false, float motorSpeed = 0.f, float maxMotorTorque = 0.f,
                                 bool collideConnected = false);
    void DestroyJoint(JointHandle& joint);
#endif
    bool Available() const
    {
#ifdef FEMBOY_HAS_BOX2D
        return created_;
#else
        return false;
#endif
    }

private:
#ifdef FEMBOY_HAS_BOX2D
    b2WorldId worldId_{};
    bool created_ = false;
#endif
    float pixelsPerMeter_ = 32.f;
};

} // namespace Fem
