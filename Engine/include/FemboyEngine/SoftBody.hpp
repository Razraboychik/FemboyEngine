// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
//
// Soft bodies: ropes, chains, cloth, ribbons, in 2D.
//
// Why Verlet and not a rigid-body solver: a rope is not a stack of rigid links
// the game cares about individually. It is a curve that must hang, swing and
// collide with the owner's movement, and the cheapest way to get that is a
// particle chain with distance constraints. Verlet integrates position only, so
// a constraint pass (move two points back to their rest distance) *is* the
// solver: no mass matrices, no impulses, and adding collision is one more pass
// that moves a point.
//
// The features below are all "one more pass", which is the point of the design:
//
//   colliders      capsules/boxes/circles the cloth cannot pass through
//   self-collision hero capes, via a spatial hash, opt-in
//   plasticity     rest lengths creep above a yield strain: a crease stays
//   anisotropy     different stiffness along warp, weft, shear and bend
//   flutter        vortex shedding at the free edge, Strouhal-number frequency
//   sleeping       a still cloth skips the solver until something wakes it
//   SIMD           the 1/sqrt per link is vectorised (see Simd.hpp)
//
// Threading note: cloth constraints are solved sequentially on purpose. A
// coloured Jacobi sweep was implemented and measured, and it loses to the
// sequential Gauss-Seidel pass at every size the engine draws (2x slower at
// 16k links, break-even at 65k): every colour batch needs its own dispatch and
// waking the pool costs more than the batch. The job pool is used where each
// item carries real work or the item count is very large (see Sparkles::Update).
#pragma once

#include <cstdint>
#include <vector>

#include <SFML/Graphics.hpp>

#include "FemboyEngine/Skirt.hpp"

namespace Fem
{

class WindField;

// A static obstacle for soft bodies. Cloth in a level collides with terrain and
// props through a flat list of these, so no physics world is involved and a
// level can hand its colliders to every cloth it spawns.
enum class SoftColliderKind
{
    Circle,  // a = center, radius
    Capsule, // a..b sweep with radius
    Box      // a = center, halfExtents
};

struct SoftCollider
{
    SoftColliderKind kind = SoftColliderKind::Circle;
    sf::Vector2f a{};
    sf::Vector2f b{};
    float radius = 10.f;
    sf::Vector2f halfExtents{10.f, 10.f};
    // Tangential velocity kept on contact: 0 = ice, 1 = the point sticks.
    float friction = 0.25f;
    bool enabled = true;
};

// Closest point on a collider, with the outward normal and the penetration
// depth of `point`. Shared by every soft body so collision behaves identically
// for rope, cloth and ribbon.
float SoftColliderPenetration(const SoftCollider& collider, sf::Vector2f point, float thickness,
                              sf::Vector2f& normalOut);

// A particle chain: rope, chain, whip, tentacle, hanging cable.
class Rope
{
public:
    struct Settings
    {
        sf::Vector2f gravity{0.f, 1200.f};
        float damping = 0.995f;
        int iterations = 6;
        float maxStep = 1.f / 30.f;
        float stiffness = 1.f;

        float windDrag = 0.004f;
        float windArea = 1.f;
        float bendStiffness = 0.f; // 0 = limp rope, 0.9 = chain
        float breakStretch = 0.f;  // 0 = unbreakable

        // Collision: the chain is a swept circle of this thickness.
        float thickness = 4.f;
        float friction = 0.25f;
        bool selfCollision = false;
        float selfThickness = 6.f;

        // Creep: rest lengths follow the current length above this strain.
        float yieldStrain = 0.25f;
        float plasticity = 0.f; // 0 = elastic

        // Sleep: below this speed for this long, the solver is skipped.
        float sleepVelocity = 3.f;
        float sleepSeconds = 1.f;

        // Solve link batches through the job pool.
        bool jobs = true;
    };

    void Reset(sf::Vector2f start, sf::Vector2f end, int segments, float slack = 1.02f);
    void Clear();

    void Pin(int index, bool pinned = true);
    void MovePin(int index, sf::Vector2f position);
    bool Pinned(int index) const;

    void SetSettings(const Settings& settings);
    const Settings& GetSettings() const
    {
        return settings_;
    }
    void SetGravity(sf::Vector2f gravity)
    {
        settings_.gravity = gravity;
        Wake();
    }

    void SetWindField(const WindField* field)
    {
        wind_ = field;
        Wake();
    }
    const WindField* GetWindField() const
    {
        return wind_;
    }

    // Level colliders. The list is copied in, so a level can keep owning its own.
    void AddCollider(const SoftCollider& collider);
    void ClearColliders();
    size_t ColliderCount() const
    {
        return colliders_.size();
    }

    void Update(float dt);

    bool Asleep() const
    {
        return asleep_;
    }
    // Any external change (a hit, a level transition, a wind change) wakes the
    // chain; the next update integrates normally.
    void Wake();

    const std::vector<sf::Vector2f>& Points() const
    {
        return positions_;
    }
    sf::Vector2f Point(int index) const;
    int SegmentCount() const
    {
        return static_cast<int>(positions_.size()) - 1;
    }
    float SegmentLength() const
    {
        return segment_;
    }
    sf::Vector2f Center() const;

    void Render(Skirt& FrameDrawList, sf::Color color, float thickness, int z = 0, DrawLayer layer = DrawLayer::Entities,
                float taper = 0.f) const;
    void RenderLinks(Skirt& FrameDrawList, sf::Color color, float thickness, int z = 0,
                     DrawLayer layer = DrawLayer::Entities) const;

    int BrokenLinks() const;

    // Debug/statistics: how many link batches the colouring produced.
    int BatchCount() const
    {
        return static_cast<int>(batches_.size());
    }

private:
    struct Particle
    {
        sf::Vector2f position{};
        sf::Vector2f previous{};
        bool pinned = false;
    };

    void BuildBatches();
    void SolvePass(const std::vector<int>& segments, size_t from, size_t to);
    void Solve();
    void Collide();
    void ApplyPlasticity(float dt);
    bool IsStill() const;

    std::vector<Particle> particles_;
    std::vector<sf::Vector2f> positions_;
    std::vector<std::uint8_t> broken_; // vector<bool> is a bit proxy; this is hot
    std::vector<float> rest_;          // per segment, so plasticity can creep
    std::vector<std::vector<int>> batches_;
    std::vector<SoftCollider> colliders_;
    float segment_ = 1.f;
    float windTime_ = 0.f;
    float stillTime_ = 0.f;
    bool asleep_ = false;
    const WindField* wind_ = nullptr;
    Settings settings_{};
};

// A rectangular cloth patch: flag, cape, banner, curtain.
class Cloth
{
public:
    struct Settings
    {
        sf::Vector2f gravity{0.f, 900.f};
        sf::Vector2f wind{0.f, 0.f};
        float damping = 0.995f;
        int iterations = 5;
        float maxStep = 1.f / 30.f;
        bool shear = true;
        bool bending = true;

        float windDrag = 0.004f;
        float tearStretch = 0.f;

        // Anisotropy: warp runs along +x, weft along +y at creation time.
        float warpStiffness = 1.f;
        float weftStiffness = 1.f;
        float shearStiffness = 1.f;
        float bendStiffness = 1.f;

        float thickness = 4.f;
        float friction = 0.25f;
        bool selfCollision = false;
        float selfThickness = 8.f;

        float yieldStrain = 0.3f;
        float plasticity = 0.f;

        float sleepVelocity = 2.f;
        float sleepSeconds = 1.f;

        // Vortex shedding at the free edge. 0 disables it.
        float flutter = 0.f;
        float strouhal = 0.2f;
    };

    void Reset(sf::Vector2f origin, int columns, int rows, float spacing);
    void Clear();

    void SetWindField(const WindField* field)
    {
        wind_ = field;
        Wake();
    }
    const WindField* GetWindField() const
    {
        return wind_;
    }
    void SetWind(sf::Vector2f wind)
    {
        settings_.wind = wind;
        Wake();
    }

    void AddCollider(const SoftCollider& collider);
    void ClearColliders();
    size_t ColliderCount() const
    {
        return colliders_.size();
    }

    void Pin(int column, int row, bool pinned = true);
    void PinRow(int row, bool pinned = true);
    void MovePin(int column, int row, sf::Vector2f position);
    bool Pinned(int column, int row) const;

    void SetSettings(const Settings& settings);
    const Settings& GetSettings() const
    {
        return settings_;
    }

    void Update(float dt);

    bool Asleep() const
    {
        return asleep_;
    }
    void Wake();

    int Columns() const
    {
        return columns_;
    }
    int Rows() const
    {
        return rows_;
    }
    int TornLinks() const;

    sf::Vector2f Point(int column, int row) const;
    sf::Vector2f Center() const;

    void Render(Skirt& FrameDrawList, sf::Color color, int z = 0, DrawLayer layer = DrawLayer::Entities) const;
    void RenderWireframe(Skirt& FrameDrawList, sf::Color color, float thickness, int z = 0,
                         DrawLayer layer = DrawLayer::Entities) const;

    int BatchCount() const
    {
        return static_cast<int>(batches_.size());
    }

private:
    enum class LinkKind : std::uint8_t
    {
        Warp,
        Weft,
        Shear,
        Bend
    };

    struct Particle
    {
        sf::Vector2f position{};
        sf::Vector2f previous{};
        bool pinned = false;
        float hashCell = 0.f; // scratch for the self-collision pass
    };
    struct Link
    {
        int a = 0;
        int b = 0;
        float rest = 0.f;
        LinkKind kind = LinkKind::Warp;
        bool broken = false;
    };

    int Index(int column, int row) const
    {
        return row * columns_ + column;
    }
    sf::Vector2f Position(int index) const;
    void AddLink(int a, int b, LinkKind kind);
    void AddLinkAt(int a, int b, LinkKind kind, const std::vector<sf::Vector2f>& positions);
    float StiffnessOf(const Link& link) const;
    void BuildBatches();
    void Solve();
    void CollideWithColliders();
    void CollideWithSelf();
    void ApplyPlasticity(float dt);
    void ApplyFlutter(float dt);
    bool IsStill() const;

    std::vector<Particle> particles_;
    std::vector<Link> links_;
    std::vector<std::vector<int>> batches_;
    std::vector<SoftCollider> colliders_;
    int columns_ = 0;
    int rows_ = 0;
    float spacing_ = 1.f;
    float windTime_ = 0.f;
    float stillTime_ = 0.f;
    bool asleep_ = false;
    const WindField* wind_ = nullptr;
    Settings settings_{};
};

// A twisting ribbon: a rope with a width and a torsion angle at every point.
class Ribbon
{
public:
    struct Settings
    {
        sf::Vector2f gravity{0.f, 1100.f};
        float damping = 0.99f;
        int iterations = 6;
        float maxStep = 1.f / 30.f;
        float stiffness = 1.f;
        float windDrag = 0.004f;
        float torsionStiffness = 6.f;
        float torsionDamping = 0.96f;
        float windTorque = 0.0025f;
        float bendStiffness = 0.05f;

        float thickness = 6.f;
        float friction = 0.25f;

        float yieldStrain = 0.25f;
        float plasticity = 0.f;

        float sleepVelocity = 3.f;
        float sleepSeconds = 1.f;
        bool jobs = true;
    };

    void Reset(sf::Vector2f start, sf::Vector2f end, int segments, float width, float slack = 1.02f);
    void ResetHelix(sf::Vector2f center, float radius, float height, int segments, float width, float turns = 3.f);
    void Clear();

    void SetSettings(const Settings& settings);
    const Settings& GetSettings() const
    {
        return settings_;
    }
    void SetWindField(const WindField* field)
    {
        wind_ = field;
        Wake();
    }
    void SetWidth(float width)
    {
        width_ = std::max(0.5f, width);
    }
    float Width() const
    {
        return width_;
    }
    void SetRestTwist(float totalRadians);

    void AddCollider(const SoftCollider& collider);
    void ClearColliders();

    void Pin(int index, bool pinned = true);
    void MovePin(int index, sf::Vector2f position);
    bool Pinned(int index) const;

    void Update(float dt);

    bool Asleep() const
    {
        return asleep_;
    }
    void Wake();

    const std::vector<sf::Vector2f>& Points() const
    {
        return positions_;
    }
    sf::Vector2f Point(int index) const;
    float Twist(int index) const;
    sf::Vector2f Center() const;

    void Render(Skirt& FrameDrawList, sf::Color color, int z = 0, DrawLayer layer = DrawLayer::Entities) const;
    void RenderWireframe(Skirt& FrameDrawList, sf::Color color, float thickness, int z = 0,
                         DrawLayer layer = DrawLayer::Entities) const;

private:
    struct Particle
    {
        sf::Vector2f position{};
        sf::Vector2f previous{};
        bool pinned = false;
    };

    void BuildBatches();
    void Solve();
    void Collide();
    void ApplyPlasticity(float dt);
    void ApplyWind(float dt);
    bool IsStill() const;

    std::vector<Particle> particles_;
    std::vector<float> twist_;
    std::vector<float> restTwist_;
    std::vector<float> twistVelocity_;
    std::vector<sf::Vector2f> positions_;
    std::vector<float> restSegment_;
    std::vector<std::vector<int>> batches_;
    std::vector<SoftCollider> colliders_;
    float segment_ = 1.f;
    float width_ = 12.f;
    float windTime_ = 0.f;
    float stillTime_ = 0.f;
    bool asleep_ = false;
    const WindField* wind_ = nullptr;
    Settings settings_{};
};

} // namespace Fem
