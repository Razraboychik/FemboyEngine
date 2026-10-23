// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/SoftBody.hpp"
#include "FemboyEngine/Jobs.hpp"
#include "FemboyEngine/Simd.hpp"
#include "FemboyEngine/Wind.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Fem
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kMaxWindAccel = 6000.f;
constexpr float kEpsilon = 1e-5f;

float Distance(sf::Vector2f a, sf::Vector2f b)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

float LengthOf(sf::Vector2f v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

sf::Vector2f Lerp(sf::Vector2f a, sf::Vector2f b, float t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

sf::Color Shade(sf::Color color, float amount)
{
    const auto channel = [&](std::uint8_t value)
    {
        const float scaled = static_cast<float>(value) * amount;
        return static_cast<std::uint8_t>(std::clamp(scaled, 0.f, 255.f));
    };
    return sf::Color(channel(color.r), channel(color.g), channel(color.b), color.a);
}

// Aerodynamic acceleration on one particle:
//   a = drag * pressure * |w - v| * (w - v)
sf::Vector2f WindAcceleration(const WindField* field, sf::Vector2f position, float time, sf::Vector2f velocity,
                              float drag, float area)
{
    if (field == nullptr || drag <= 0.f)
    {
        return {};
    }
    const WindSample sample = field->Sample(position, time);
    const sf::Vector2f relative = sample.velocity - velocity;
    const float speed = LengthOf(relative);
    if (speed < 1e-4f)
    {
        return {};
    }
    sf::Vector2f accel = relative * (speed * drag * area * std::max(0.f, sample.pressure));
    const float magnitude = LengthOf(accel);
    if (magnitude > kMaxWindAccel)
    {
        accel *= kMaxWindAccel / magnitude;
    }
    return accel;
}

sf::Vector2f ClosestPointOnSegment(sf::Vector2f a, sf::Vector2f b, sf::Vector2f p)
{
    const sf::Vector2f ab = b - a;
    const float lengthSq = ab.x * ab.x + ab.y * ab.y;
    if (lengthSq < 1e-9f)
    {
        return a;
    }
    const float t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lengthSq, 0.f, 1.f);
    return a + ab * t;
}

// Greedy edge colouring: links that share no particle go into the same batch, so
// every batch can be solved in parallel without a data race. The number of
// batches is the colour count; for a cloth grid it settles at a handful.
template <typename EndpointFn>
std::vector<std::vector<int>> BuildColouredBatches(size_t linkCount, size_t particleCount, EndpointFn endpoints)
{
    std::vector<std::vector<int>> batches;
    if (linkCount == 0)
    {
        return batches;
    }

    // lastBatch[p] = index of the last batch that touched particle p.
    std::vector<int> lastBatch(particleCount, -1);
    std::vector<int> order(linkCount);
    for (size_t i = 0; i < linkCount; ++i)
    {
        order[i] = static_cast<int>(i);
    }

    for (int link : order)
    {
        const auto [a, b] = endpoints(static_cast<size_t>(link));
        int target = 0;
        // Walk batches until one has neither endpoint used.
        while (target < static_cast<int>(batches.size()) &&
               (lastBatch[static_cast<size_t>(a)] == target || lastBatch[static_cast<size_t>(b)] == target))
        {
            ++target;
        }
        if (target == static_cast<int>(batches.size()))
        {
            batches.emplace_back();
        }
        batches[static_cast<size_t>(target)].push_back(link);
        lastBatch[static_cast<size_t>(a)] = target;
        lastBatch[static_cast<size_t>(b)] = target;
    }
    return batches;
}

} // namespace

float SoftColliderPenetration(const SoftCollider& collider, sf::Vector2f point, float thickness,
                              sf::Vector2f& normalOut)
{
    normalOut = {0.f, -1.f};
    if (!collider.enabled)
    {
        return 0.f;
    }

    switch (collider.kind)
    {
        case SoftColliderKind::Circle:
        {
            const sf::Vector2f delta = point - collider.a;
            const float dist = LengthOf(delta);
            normalOut = dist > kEpsilon ? delta / dist : sf::Vector2f{0.f, -1.f};
            return (collider.radius + thickness) - dist;
        }
        case SoftColliderKind::Capsule:
        {
            const sf::Vector2f closest = ClosestPointOnSegment(collider.a, collider.b, point);
            const sf::Vector2f delta = point - closest;
            const float dist = LengthOf(delta);
            normalOut = dist > kEpsilon ? delta / dist : sf::Vector2f{0.f, -1.f};
            return (collider.radius + thickness) - dist;
        }
        case SoftColliderKind::Box:
        {
            const sf::Vector2f local = point - collider.a;
            const sf::Vector2f h = collider.halfExtents;
            const bool insideX = std::abs(local.x) < h.x;
            const bool insideY = std::abs(local.y) < h.y;
            if (insideX && insideY)
            {
                // Deepest inside: escape through the nearest face.
                const float dx = h.x - std::abs(local.x);
                const float dy = h.y - std::abs(local.y);
                if (dx < dy)
                {
                    normalOut = {local.x >= 0.f ? 1.f : -1.f, 0.f};
                    return dx + thickness;
                }
                normalOut = {0.f, local.y >= 0.f ? 1.f : -1.f};
                return dy + thickness;
            }
            const sf::Vector2f clamped{std::clamp(local.x, -h.x, h.x), std::clamp(local.y, -h.y, h.y)};
            const sf::Vector2f delta = local - clamped;
            const float dist = LengthOf(delta);
            normalOut = dist > kEpsilon ? delta / dist : sf::Vector2f{0.f, -1.f};
            return thickness - dist;
        }
    }
    return 0.f;
}

// Rope

void Rope::Reset(sf::Vector2f start, sf::Vector2f end, int segments, float slack)
{
    segments = std::clamp(segments, 1, 256);
    const float length = std::max(0.001f, Distance(start, end)) * std::max(1.f, slack);
    segment_ = length / static_cast<float>(segments);

    particles_.assign(static_cast<size_t>(segments) + 1, {});
    for (int i = 0; i <= segments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        Particle& p = particles_[static_cast<size_t>(i)];
        p.position = Lerp(start, end, t);
        p.previous = p.position;
    }
    positions_.resize(particles_.size());
    for (size_t i = 0; i < particles_.size(); ++i)
    {
        positions_[i] = particles_[i].position;
    }
    broken_.assign(static_cast<size_t>(segments), 0u);
    rest_.assign(static_cast<size_t>(segments), segment_);
    windTime_ = 0.f;
    stillTime_ = 0.f;
    asleep_ = false;
    BuildBatches();
}

void Rope::Clear()
{
    particles_.clear();
    positions_.clear();
    broken_.clear();
    rest_.clear();
    batches_.clear();
    colliders_.clear();
}

void Rope::SetSettings(const Settings& settings)
{
    settings_ = settings;
    Wake();
}

void Rope::Pin(int index, bool pinned)
{
    if (index < 0 || index >= static_cast<int>(particles_.size()))
    {
        return;
    }
    particles_[static_cast<size_t>(index)].pinned = pinned;
    Wake();
}

void Rope::MovePin(int index, sf::Vector2f position)
{
    if (index < 0 || index >= static_cast<int>(particles_.size()))
    {
        return;
    }
    Particle& p = particles_[static_cast<size_t>(index)];
    if (!p.pinned)
    {
        return;
    }
    p.position = position;
    p.previous = position;
    Wake();
}

bool Rope::Pinned(int index) const
{
    if (index < 0 || index >= static_cast<int>(particles_.size()))
    {
        return false;
    }
    return particles_[static_cast<size_t>(index)].pinned;
}

void Rope::AddCollider(const SoftCollider& collider)
{
    colliders_.push_back(collider);
    Wake();
}

void Rope::ClearColliders()
{
    colliders_.clear();
    Wake();
}

void Rope::Wake()
{
    asleep_ = false;
    stillTime_ = 0.f;
}

void Rope::BuildBatches()
{
    const auto count = static_cast<size_t>(std::max(0, static_cast<int>(particles_.size()) - 1));
    batches_ =
        BuildColouredBatches(count, particles_.size(), [this](size_t link)
                             { return std::pair<int, int>{static_cast<int>(link), static_cast<int>(link) + 1}; });
}

bool Rope::IsStill() const
{
    const float limitSq = settings_.sleepVelocity * settings_.sleepVelocity;
    for (const Particle& p : particles_)
    {
        if (p.pinned)
        {
            continue;
        }
        const sf::Vector2f velocity = p.position - p.previous;
        if (velocity.x * velocity.x + velocity.y * velocity.y > limitSq)
        {
            return false;
        }
    }
    return true;
}

void Rope::Update(float dt)
{
    if (particles_.size() < 2)
    {
        return;
    }

    dt = std::clamp(dt, 0.f, settings_.maxStep);
    if (dt <= 0.f)
    {
        return;
    }

    // A sleeping rope still wakes on a moving wind, so weather changes are not
    // silently ignored.
    if (asleep_)
    {
        if (wind_ != nullptr)
        {
            const WindSample sample = wind_->Sample(positions_.empty() ? sf::Vector2f{} : positions_[0], windTime_);
            if (sample.Speed() > settings_.sleepVelocity)
            {
                Wake();
            }
        }
        if (asleep_)
        {
            return;
        }
    }

    windTime_ += dt;
    const float damping = std::clamp(settings_.damping, 0.f, 1.f);
    const sf::Vector2f gravity = settings_.gravity * (dt * dt);

    for (Particle& p : particles_)
    {
        if (p.pinned)
        {
            continue;
        }
        const sf::Vector2f velocity = (p.position - p.previous) * damping;
        const sf::Vector2f air =
            WindAcceleration(wind_, p.position, windTime_, velocity / dt, settings_.windDrag, settings_.windArea);
        p.previous = p.position;
        p.position += velocity + gravity + air * (dt * dt);
    }

    for (int iteration = 0; iteration < std::max(1, settings_.iterations); ++iteration)
    {
        Solve();
    }

    ApplyPlasticity(dt);
    Collide();

    for (size_t i = 0; i < particles_.size(); ++i)
    {
        positions_[i] = particles_[i].position;
    }

    if (IsStill())
    {
        stillTime_ += dt;
        if (stillTime_ >= settings_.sleepSeconds)
        {
            asleep_ = true;
        }
    }
    else
    {
        stillTime_ = 0.f;
    }
}

void Rope::SolvePass(const std::vector<int>& segments, size_t from, size_t to)
{
    const float stiffness = std::clamp(settings_.stiffness, 0.05f, 1.f);
    for (size_t k = from; k < to; ++k)
    {
        const int i = segments[k];
        if (static_cast<size_t>(i) < broken_.size() && broken_[static_cast<size_t>(i)] != 0u)
        {
            continue;
        }

        Particle& a = particles_[static_cast<size_t>(i)];
        Particle& b = particles_[static_cast<size_t>(i) + 1];
        sf::Vector2f delta = b.position - a.position;
        const float current = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (current <= kEpsilon)
        {
            continue;
        }

        const float rest = static_cast<size_t>(i) < rest_.size() ? rest_[static_cast<size_t>(i)] : segment_;
        if (settings_.breakStretch > 0.f && current > rest * settings_.breakStretch)
        {
            if (static_cast<size_t>(i) < broken_.size())
            {
                broken_[static_cast<size_t>(i)] = 1u;
            }
            continue;
        }

        const float error = (current - rest) / current;
        const sf::Vector2f correction = delta * (error * stiffness);
        const bool pinnedA = a.pinned;
        const bool pinnedB = b.pinned;
        if (pinnedA && pinnedB)
        {
            continue;
        }
        if (pinnedA)
        {
            b.position -= correction;
        }
        else if (pinnedB)
        {
            a.position += correction;
        }
        else
        {
            a.position += correction * 0.5f;
            b.position -= correction * 0.5f;
        }
    }
}

void Rope::Solve()
{
    for (const auto& batch : batches_)
    {
        if (batch.empty())
        {
            continue;
        }
        if (settings_.jobs && batch.size() >= 256)
        {
            JobSystem::Instance().ParallelFor(
                0, batch.size(), [this, &batch](size_t k) { SolvePass(batch, k, k + 1); }, 32);
        }
        else
        {
            SolvePass(batch, 0, batch.size());
        }
    }

    // Bend resistance on the joints between batches: sequential because a joint
    // involves two segments, which may sit in different batches.
    const float bend = std::clamp(settings_.bendStiffness, 0.f, 1.f);
    if (bend <= 0.f)
    {
        return;
    }
    for (size_t i = 1; i + 1 < particles_.size(); ++i)
    {
        if ((i - 1 < broken_.size() && broken_[i - 1] != 0u) || (i < broken_.size() && broken_[i] != 0u))
        {
            continue;
        }
        Particle& a = particles_[i - 1];
        Particle& b = particles_[i];
        Particle& c = particles_[i + 1];
        if (b.pinned)
        {
            continue;
        }
        const sf::Vector2f mid = (a.position + c.position) * 0.5f;
        b.position += (mid - b.position) * (bend * 0.5f);
    }
}

void Rope::Collide()
{
    const bool hasColliders = !colliders_.empty() && settings_.thickness > 0.f;
    const bool self = settings_.selfCollision && particles_.size() > 3;
    if (!hasColliders && !self)
    {
        return;
    }

    const float thickness = std::max(0.f, settings_.thickness) * 0.5f;
    for (Particle& p : particles_)
    {
        if (p.pinned)
        {
            continue;
        }
        if (hasColliders)
        {
            for (const SoftCollider& collider : colliders_)
            {
                sf::Vector2f normal;
                const float depth = SoftColliderPenetration(collider, p.position, thickness, normal);
                if (depth <= 0.f)
                {
                    continue;
                }
                const sf::Vector2f correction = normal * depth;
                p.position += correction;
                // Friction: the closer to 1, the more of the normal motion is
                // also subtracted from the previous sample, i.e. the point
                // sticks to the surface instead of sliding.
                p.previous += correction * std::clamp(collider.friction, 0.f, 1.f);
            }
        }
    }

    if (self)
    {
        const float minDistance = std::max(1.f, settings_.selfThickness);
        for (size_t i = 0; i < particles_.size(); ++i)
        {
            for (size_t j = i + 2; j < particles_.size(); ++j)
            {
                Particle& a = particles_[i];
                Particle& b = particles_[j];
                sf::Vector2f delta = b.position - a.position;
                const float dist = LengthOf(delta);
                if (dist >= minDistance || dist <= kEpsilon)
                {
                    continue;
                }
                const sf::Vector2f push = delta * ((minDistance - dist) / dist * 0.5f);
                if (!a.pinned)
                {
                    a.position -= push;
                }
                if (!b.pinned)
                {
                    b.position += push;
                }
            }
        }
    }
}

void Rope::ApplyPlasticity(float dt)
{
    if (settings_.plasticity <= 0.f)
    {
        return;
    }
    const float rate = std::clamp(settings_.plasticity * dt * 60.f, 0.f, 1.f);
    for (size_t i = 0; i + 1 < particles_.size(); ++i)
    {
        if (i < broken_.size() && broken_[i] != 0u)
        {
            continue;
        }
        const float current = Distance(particles_[i].position, particles_[i + 1].position);
        const float rest = i < rest_.size() ? rest_[i] : segment_;
        const float strain = (current - rest) / std::max(kEpsilon, rest);
        if (strain > settings_.yieldStrain)
        {
            rest_[i] = rest + (current - rest) * rate;
        }
    }
}

sf::Vector2f Rope::Point(int index) const
{
    if (positions_.empty())
    {
        return {};
    }
    const int clamped = std::clamp(index, 0, static_cast<int>(positions_.size()) - 1);
    return positions_[static_cast<size_t>(clamped)];
}

sf::Vector2f Rope::Center() const
{
    if (positions_.empty())
    {
        return {};
    }
    sf::Vector2f sum{};
    for (const sf::Vector2f& p : positions_)
    {
        sum += p;
    }
    return sum / static_cast<float>(positions_.size());
}

void Rope::Render(Skirt& FrameDrawList, sf::Color color, float thickness, int z, DrawLayer layer, float taper) const
{
    if (positions_.size() < 2)
    {
        return;
    }
    const int last = static_cast<int>(positions_.size()) - 1;
    for (int i = 0; i < last; ++i)
    {
        const float t = last > 0 ? static_cast<float>(i) / static_cast<float>(last) : 0.f;
        const float width = thickness * (1.f - taper * t);
        FrameDrawList.Line(positions_[static_cast<size_t>(i)], positions_[static_cast<size_t>(i) + 1], color,
                   std::max(0.5f, width), z, layer);
    }
}

void Rope::RenderLinks(Skirt& FrameDrawList, sf::Color color, float thickness, int z, DrawLayer layer) const
{
    if (positions_.size() < 2)
    {
        return;
    }
    const float radius = std::max(1.f, thickness * 0.5f);
    for (size_t i = 0; i + 1 < positions_.size(); ++i)
    {
        if (i < broken_.size() && broken_[i] != 0u)
        {
            continue;
        }
        const sf::Vector2f a = positions_[i];
        const sf::Vector2f b = positions_[i + 1];
        const sf::Color link = (i % 2 == 0) ? color : Shade(color, 0.78f);
        FrameDrawList.Line(a, b, link, std::max(1.f, thickness * 0.62f), z, layer);
        FrameDrawList.Circle(a, radius, link, z, layer);
    }
    if (!positions_.empty())
    {
        FrameDrawList.Circle(positions_.back(), radius, color, z, layer);
    }
}

int Rope::BrokenLinks() const
{
    int count = 0;
    for (std::uint8_t b : broken_)
    {
        if (b != 0u)
        {
            ++count;
        }
    }
    return count;
}

// Cloth

void Cloth::Reset(sf::Vector2f origin, int columns, int rows, float spacing)
{
    columns = std::clamp(columns, 2, 256);
    rows = std::clamp(rows, 2, 256);
    spacing = std::max(0.5f, spacing);
    columns_ = columns;
    rows_ = rows;
    spacing_ = spacing;

    particles_.assign(static_cast<size_t>(columns) * static_cast<size_t>(rows), {});
    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            Particle& p = particles_[static_cast<size_t>(Index(column, row))];
            p.position = origin + sf::Vector2f(static_cast<float>(column) * spacing, static_cast<float>(row) * spacing);
            p.previous = p.position;
        }
    }

    links_.clear();
    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            const int self = Index(column, row);
            if (column + 1 < columns)
            {
                AddLink(self, Index(column + 1, row), LinkKind::Warp);
            }
            if (row + 1 < rows)
            {
                AddLink(self, Index(column, row + 1), LinkKind::Weft);
            }
            if (settings_.shear && column + 1 < columns && row + 1 < rows)
            {
                AddLink(self, Index(column + 1, row + 1), LinkKind::Shear);
                AddLink(Index(column + 1, row), Index(column, row + 1), LinkKind::Shear);
            }
            if (settings_.bending)
            {
                if (column + 2 < columns)
                {
                    AddLink(self, Index(column + 2, row), LinkKind::Bend);
                }
                if (row + 2 < rows)
                {
                    AddLink(self, Index(column, row + 2), LinkKind::Bend);
                }
            }
        }
    }

    windTime_ = 0.f;
    stillTime_ = 0.f;
    asleep_ = false;
    BuildBatches();
}

void Cloth::Clear()
{
    particles_.clear();
    links_.clear();
    batches_.clear();
    colliders_.clear();
    columns_ = 0;
    rows_ = 0;
}

void Cloth::SetSettings(const Settings& settings)
{
    settings_ = settings;
    // Toggling shear/bending changes the link set, so rebuild it the same way
    // reset() does, keeping the current particle positions.
    std::vector<sf::Vector2f> saved;
    saved.reserve(particles_.size());
    for (const Particle& p : particles_)
    {
        saved.push_back(p.position);
    }

    links_.clear();
    for (int row = 0; row < rows_; ++row)
    {
        for (int column = 0; column < columns_; ++column)
        {
            const int self = Index(column, row);
            if (column + 1 < columns_)
            {
                AddLinkAt(self, Index(column + 1, row), LinkKind::Warp, saved);
            }
            if (row + 1 < rows_)
            {
                AddLinkAt(self, Index(column, row + 1), LinkKind::Weft, saved);
            }
            if (settings_.shear && column + 1 < columns_ && row + 1 < rows_)
            {
                AddLinkAt(self, Index(column + 1, row + 1), LinkKind::Shear, saved);
                AddLinkAt(Index(column + 1, row), Index(column, row + 1), LinkKind::Shear, saved);
            }
            if (settings_.bending)
            {
                if (column + 2 < columns_)
                {
                    AddLinkAt(self, Index(column + 2, row), LinkKind::Bend, saved);
                }
                if (row + 2 < rows_)
                {
                    AddLinkAt(self, Index(column, row + 2), LinkKind::Bend, saved);
                }
            }
        }
    }
    BuildBatches();
    Wake();
}

void Cloth::AddLink(int a, int b, LinkKind kind)
{
    Link link;
    link.a = a;
    link.b = b;
    link.kind = kind;
    link.rest = Distance(particles_[static_cast<size_t>(a)].position, particles_[static_cast<size_t>(b)].position);
    if (link.rest > 0.f)
    {
        links_.push_back(link);
    }
}

void Cloth::AddLinkAt(int a, int b, LinkKind kind, const std::vector<sf::Vector2f>& positions)
{
    Link link;
    link.a = a;
    link.b = b;
    link.kind = kind;
    link.rest = Distance(positions[static_cast<size_t>(a)], positions[static_cast<size_t>(b)]);
    if (link.rest > 0.f)
    {
        links_.push_back(link);
    }
}

float Cloth::StiffnessOf(const Link& link) const
{
    switch (link.kind)
    {
        case LinkKind::Warp:
            return std::clamp(settings_.warpStiffness, 0.f, 1.f);
        case LinkKind::Weft:
            return std::clamp(settings_.weftStiffness, 0.f, 1.f);
        case LinkKind::Shear:
            return std::clamp(settings_.shearStiffness, 0.f, 1.f);
        case LinkKind::Bend:
            return std::clamp(settings_.bendStiffness, 0.f, 1.f);
    }
    return 1.f;
}

void Cloth::BuildBatches()
{
    batches_ = BuildColouredBatches(links_.size(), particles_.size(),
                                    [this](size_t link)
                                    {
                                        const Link& l = links_[link];
                                        return std::pair<int, int>{l.a, l.b};
                                    });
}

void Cloth::Pin(int column, int row, bool pinned)
{
    if (column < 0 || column >= columns_ || row < 0 || row >= rows_)
    {
        return;
    }
    particles_[static_cast<size_t>(Index(column, row))].pinned = pinned;
    Wake();
}

void Cloth::PinRow(int row, bool pinned)
{
    for (int column = 0; column < columns_; ++column)
    {
        Pin(column, row, pinned);
    }
}

void Cloth::MovePin(int column, int row, sf::Vector2f position)
{
    if (column < 0 || column >= columns_ || row < 0 || row >= rows_)
    {
        return;
    }
    Particle& p = particles_[static_cast<size_t>(Index(column, row))];
    if (!p.pinned)
    {
        return;
    }
    p.position = position;
    p.previous = position;
    Wake();
}

bool Cloth::Pinned(int column, int row) const
{
    if (column < 0 || column >= columns_ || row < 0 || row >= rows_)
    {
        return false;
    }
    return particles_[static_cast<size_t>(Index(column, row))].pinned;
}

void Cloth::Wake()
{
    asleep_ = false;
    stillTime_ = 0.f;
}

void Cloth::AddCollider(const SoftCollider& collider)
{
    colliders_.push_back(collider);
    Wake();
}

void Cloth::ClearColliders()
{
    colliders_.clear();
    Wake();
}

sf::Vector2f Cloth::Position(int particleIndex) const
{
    if (particleIndex < 0 || particleIndex >= static_cast<int>(particles_.size()))
    {
        return {};
    }
    return particles_[static_cast<size_t>(particleIndex)].position;
}

bool Cloth::IsStill() const
{
    const float limitSq = settings_.sleepVelocity * settings_.sleepVelocity;
    for (const Particle& p : particles_)
    {
        if (p.pinned)
        {
            continue;
        }
        const sf::Vector2f velocity = p.position - p.previous;
        if (velocity.x * velocity.x + velocity.y * velocity.y > limitSq)
        {
            return false;
        }
    }
    return true;
}

void Cloth::Update(float dt)
{
    if (particles_.empty() || columns_ < 2 || rows_ < 2)
    {
        return;
    }

    dt = std::clamp(dt, 0.f, settings_.maxStep);
    if (dt <= 0.f)
    {
        return;
    }

    if (asleep_)
    {
        if (wind_ != nullptr && wind_->Sample(Center(), windTime_).Speed() > settings_.sleepVelocity)
        {
            Wake();
        }
        if (asleep_)
        {
            return;
        }
    }

    windTime_ += dt;
    const float damping = std::clamp(settings_.damping, 0.f, 1.f);

    // Integrate. The wind force is sampled per particle, so gusts bend the
    // sheet instead of translating it.
    for (Particle& p : particles_)
    {
        if (p.pinned)
        {
            continue;
        }
        const sf::Vector2f velocity = (p.position - p.previous) * damping;
        sf::Vector2f air = settings_.wind;
        if (wind_)
        {
            air += WindAcceleration(wind_, p.position, windTime_, velocity / dt, settings_.windDrag, 1.f);
        }
        p.previous = p.position;
        p.position += velocity + (settings_.gravity + air) * (dt * dt);
    }

    ApplyFlutter(dt);

    for (int iteration = 0; iteration < std::max(1, settings_.iterations); ++iteration)
    {
        Solve();
    }

    ApplyPlasticity(dt);
    CollideWithColliders();
    CollideWithSelf();

    if (IsStill())
    {
        stillTime_ += dt;
        if (stillTime_ >= settings_.sleepSeconds)
        {
            asleep_ = true;
        }
    }
    else
    {
        stillTime_ = 0.f;
    }
}

void Cloth::Solve()
{
    // Gauss-Seidel in link order: each correction uses the position the previous
    // link just wrote, which is what makes a handful of iterations enough.
    //
    // A SIMD variant that precomputed all reciprocal lengths once per iteration
    // was measurably faster (up to 18% at 128x128) but unstable: a stale length
    // turns into an over-correction whenever a link moved a lot, and the sheet
    // gains energy until it flies apart. Vectorising this properly needs a
    // Jacobi formulation with per-particle accumulation, which converges slower
    // and loses at the sizes the engine draws. So: scalar here, SIMD where it is
    // exact (sprite corner rotation, see IRenderer).
    for (Link& link : links_)
    {
        if (link.broken)
        {
            continue;
        }
        Particle& a = particles_[static_cast<size_t>(link.a)];
        Particle& b = particles_[static_cast<size_t>(link.b)];
        const sf::Vector2f delta = b.position - a.position;
        const float inv = Simd::ReciprocalLength(delta.x, delta.y);
        if (inv <= 0.f)
        {
            continue;
        }
        if (settings_.tearStretch > 0.f && link.rest * settings_.tearStretch * inv < 1.f)
        {
            link.broken = true;
            continue;
        }

        const float stiffness = StiffnessOf(link);
        const float error = (1.f - link.rest * inv) * stiffness;
        const sf::Vector2f correction = delta * (error * 0.5f);
        if (a.pinned && b.pinned)
        {
            continue;
        }
        if (a.pinned)
        {
            b.position -= correction * 2.f;
        }
        else if (b.pinned)
        {
            a.position += correction * 2.f;
        }
        else
        {
            a.position += correction;
            b.position -= correction;
        }
    }
}

void Cloth::CollideWithColliders()
{
    if (colliders_.empty() || settings_.thickness <= 0.f)
    {
        return;
    }
    const float thickness = settings_.thickness * 0.5f;
    for (Particle& p : particles_)
    {
        if (p.pinned)
        {
            continue;
        }
        for (const SoftCollider& collider : colliders_)
        {
            sf::Vector2f normal;
            const float depth = SoftColliderPenetration(collider, p.position, thickness, normal);
            if (depth <= 0.f)
            {
                continue;
            }
            const sf::Vector2f correction = normal * depth;
            p.position += correction;
            p.previous += correction * std::clamp(collider.friction, 0.f, 1.f);
        }
    }
}

void Cloth::CollideWithSelf()
{
    if (!settings_.selfCollision || particles_.size() < 4)
    {
        return;
    }

    const float thickness = std::max(1.f, settings_.selfThickness);
    const float invCell = 1.f / thickness;

    // Bucketed spatial hash, reused between frames: clearing the buckets keeps
    // their capacity, so a steady-state cloth does not allocate.
    std::unordered_map<std::int64_t, std::vector<int>> grid;
    grid.reserve(particles_.size());

    const auto cellKey = [invCell](sf::Vector2f p)
    {
        const std::int64_t cx = static_cast<std::int64_t>(std::floor(p.x * invCell));
        const std::int64_t cy = static_cast<std::int64_t>(std::floor(p.y * invCell));
        return (cx << 32) ^ (cy & 0xffffffff);
    };

    for (size_t i = 0; i < particles_.size(); ++i)
    {
        grid[cellKey(particles_[i].position)].push_back(static_cast<int>(i));
    }

    for (size_t i = 0; i < particles_.size(); ++i)
    {
        Particle& a = particles_[i];
        if (a.pinned)
        {
            continue;
        }
        const std::int64_t cx = static_cast<std::int64_t>(std::floor(a.position.x * invCell));
        const std::int64_t cy = static_cast<std::int64_t>(std::floor(a.position.y * invCell));
        for (std::int64_t ox = -1; ox <= 1; ++ox)
        {
            for (std::int64_t oy = -1; oy <= 1; ++oy)
            {
                const std::int64_t key = ((cx + ox) << 32) ^ ((cy + oy) & 0xffffffff);
                auto it = grid.find(key);
                if (it == grid.end())
                {
                    continue;
                }
                for (int j : it->second)
                {
                    if (static_cast<size_t>(j) <= i)
                    {
                        continue;
                    }
                    // Grid neighbours are held together by a link already;
                    // pushing them apart would fight the structure.
                    const int dc = std::abs((j % columns_) - static_cast<int>(i % static_cast<size_t>(columns_)));
                    const int dr = std::abs((j / columns_) - static_cast<int>(i / static_cast<size_t>(columns_)));
                    if (dc <= 1 && dr <= 1)
                    {
                        continue;
                    }
                    Particle& b = particles_[static_cast<size_t>(j)];
                    sf::Vector2f delta = b.position - a.position;
                    const float dist = LengthOf(delta);
                    if (dist >= thickness || dist <= kEpsilon)
                    {
                        continue;
                    }
                    const sf::Vector2f push = delta * ((thickness - dist) / dist * 0.5f);
                    a.position -= push;
                    if (!b.pinned)
                    {
                        b.position += push;
                    }
                }
            }
        }
    }
}

void Cloth::ApplyPlasticity(float dt)
{
    if (settings_.plasticity <= 0.f)
    {
        return;
    }
    const float rate = std::clamp(settings_.plasticity * dt * 60.f, 0.f, 1.f);
    for (Link& link : links_)
    {
        if (link.broken)
        {
            continue;
        }
        const float current = Distance(particles_[static_cast<size_t>(link.a)].position,
                                       particles_[static_cast<size_t>(link.b)].position);
        const float strain = (current - link.rest) / std::max(kEpsilon, link.rest);
        if (strain > settings_.yieldStrain)
        {
            link.rest += (current - link.rest) * rate;
        }
    }
}

void Cloth::ApplyFlutter(float dt)
{
    if (!wind_ || settings_.flutter <= 0.f)
    {
        return;
    }

    // Free edge = the last row and the last column, which is where a real flag
    // sheds vortices. Frequency from the Strouhal number f = St * U / L, with L
    // the sheet height, so a longer flag flaps slower.
    const float sheetLength = std::max(1.f, static_cast<float>(rows_) * spacing_);
    const sf::Vector2f centerWind = wind_->Sample(Center(), windTime_).velocity;
    const float windSpeed = LengthOf(centerWind);
    if (windSpeed < 1.f)
    {
        return;
    }
    const float frequency = std::max(0.2f, settings_.strouhal * windSpeed / sheetLength);
    sf::Vector2f flow = centerWind / windSpeed;
    const sf::Vector2f lateral{-flow.y, flow.x};

    for (int row = 0; row < rows_; ++row)
    {
        for (int column = 0; column < columns_; ++column)
        {
            const bool edge = (row == rows_ - 1) || (column == columns_ - 1);
            if (!edge)
            {
                continue;
            }
            Particle& p = particles_[static_cast<size_t>(Index(column, row))];
            if (p.pinned)
            {
                continue;
            }
            const sf::Vector2f local = wind_->Sample(p.position, windTime_).velocity;
            const float localSpeed = LengthOf(local);
            if (localSpeed < 1.f)
            {
                continue;
            }
            // Travelling wave along the edge, so the flap ripples instead of
            // moving as a plate.
            const float phase = static_cast<float>(row + column) * 0.7f;
            const float wave = std::sin(2.f * kPi * frequency * windTime_ + phase);
            p.position += lateral * (settings_.flutter * localSpeed * wave * dt * dt);
        }
    }
}

int Cloth::TornLinks() const
{
    int count = 0;
    for (const Link& link : links_)
    {
        if (link.broken)
        {
            ++count;
        }
    }
    return count;
}

sf::Vector2f Cloth::Point(int column, int row) const
{
    if (column < 0 || column >= columns_ || row < 0 || row >= rows_)
    {
        return {};
    }
    return particles_[static_cast<size_t>(Index(column, row))].position;
}

sf::Vector2f Cloth::Center() const
{
    if (particles_.empty())
    {
        return {};
    }
    sf::Vector2f sum{};
    for (const Particle& p : particles_)
    {
        sum += p.position;
    }
    return sum / static_cast<float>(particles_.size());
}

void Cloth::Render(Skirt& FrameDrawList, sf::Color color, int z, DrawLayer layer) const
{
    if (columns_ < 2 || rows_ < 2)
    {
        return;
    }

    for (int row = 0; row + 1 < rows_; ++row)
    {
        for (int column = 0; column + 1 < columns_; ++column)
        {
            const sf::Vector2f a = Point(column, row);
            const sf::Vector2f b = Point(column + 1, row);
            const sf::Vector2f c = Point(column + 1, row + 1);
            const sf::Vector2f d = Point(column, row + 1);
            const float slope = (c.y - a.y) / std::max(1.f, spacing_);
            const float amount = std::clamp(1.f - slope * 0.08f, 0.62f, 1.25f);
            FrameDrawList.Polygon({a, b, c, d}, Shade(color, amount), sf::Color::Transparent, DrawCommand::kNoThickness, z,
                          layer);
        }
    }
}

void Cloth::RenderWireframe(Skirt& FrameDrawList, sf::Color color, float thickness, int z, DrawLayer layer) const
{
    for (const Link& link : links_)
    {
        if (link.broken)
        {
            continue;
        }
        FrameDrawList.Line(particles_[static_cast<size_t>(link.a)].position, particles_[static_cast<size_t>(link.b)].position,
                   color, thickness, z, layer);
    }
}

// Ribbon

void Ribbon::Reset(sf::Vector2f start, sf::Vector2f end, int segments, float width, float slack)
{
    segments = std::clamp(segments, 2, 192);
    const float length = std::max(0.001f, Distance(start, end)) * std::max(1.f, slack);
    segment_ = length / static_cast<float>(segments);
    width_ = std::max(0.5f, width);

    particles_.assign(static_cast<size_t>(segments) + 1, {});
    restSegment_.assign(static_cast<size_t>(segments), segment_);
    for (int i = 0; i <= segments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        Particle& p = particles_[static_cast<size_t>(i)];
        p.position = Lerp(start, end, t);
        p.previous = p.position;
    }
    twist_.assign(particles_.size(), 0.f);
    twistVelocity_.assign(particles_.size(), 0.f);
    restTwist_.assign(particles_.size(), 0.f);
    positions_.resize(particles_.size());
    for (size_t i = 0; i < particles_.size(); ++i)
    {
        positions_[i] = particles_[i].position;
    }
    windTime_ = 0.f;
    stillTime_ = 0.f;
    asleep_ = false;
    BuildBatches();
}

void Ribbon::ResetHelix(sf::Vector2f center, float radius, float height, int segments, float width, float turns)
{
    segments = std::clamp(segments, 4, 512);
    width_ = std::max(0.5f, width);
    radius = std::max(1.f, radius);
    turns = std::max(0.1f, turns);

    particles_.assign(static_cast<size_t>(segments) + 1, {});
    restSegment_.assign(static_cast<size_t>(segments), 1.f);
    twist_.assign(particles_.size(), 0.f);
    twistVelocity_.assign(particles_.size(), 0.f);
    restTwist_.assign(particles_.size(), 0.f);

    for (int i = 0; i <= segments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = t * turns * 2.f * kPi;
        Particle& p = particles_[static_cast<size_t>(i)];
        p.position = {center.x + std::cos(angle) * radius, center.y + (t - 0.5f) * height};
        p.previous = p.position;
        restTwist_[static_cast<size_t>(i)] = angle;
        twist_[static_cast<size_t>(i)] = angle;
    }
    for (int i = 0; i < segments; ++i)
    {
        restSegment_[static_cast<size_t>(i)] =
            Distance(particles_[static_cast<size_t>(i)].position, particles_[static_cast<size_t>(i) + 1].position);
    }
    segment_ = restSegment_.empty() ? 1.f : restSegment_.front();
    positions_.resize(particles_.size());
    for (size_t i = 0; i < particles_.size(); ++i)
    {
        positions_[i] = particles_[i].position;
    }
    windTime_ = 0.f;
    stillTime_ = 0.f;
    asleep_ = false;
    BuildBatches();
}

void Ribbon::Clear()
{
    particles_.clear();
    twist_.clear();
    twistVelocity_.clear();
    restTwist_.clear();
    restSegment_.clear();
    positions_.clear();
    batches_.clear();
    colliders_.clear();
}

void Ribbon::SetSettings(const Settings& settings)
{
    settings_ = settings;
    Wake();
}

void Ribbon::BuildBatches()
{
    const auto count = static_cast<size_t>(std::max(0, static_cast<int>(particles_.size()) - 1));
    batches_ =
        BuildColouredBatches(count, particles_.size(), [this](size_t link)
                             { return std::pair<int, int>{static_cast<int>(link), static_cast<int>(link) + 1}; });
}

void Ribbon::SetRestTwist(float totalRadians)
{
    if (restTwist_.empty())
    {
        return;
    }
    const float last = static_cast<float>(restTwist_.size() - 1);
    for (size_t i = 0; i < restTwist_.size(); ++i)
    {
        restTwist_[i] = last > 0.f ? totalRadians * (static_cast<float>(i) / last) : 0.f;
    }
    Wake();
}

void Ribbon::Pin(int index, bool pinned)
{
    if (index < 0 || index >= static_cast<int>(particles_.size()))
    {
        return;
    }
    particles_[static_cast<size_t>(index)].pinned = pinned;
    Wake();
}

void Ribbon::MovePin(int index, sf::Vector2f position)
{
    if (index < 0 || index >= static_cast<int>(particles_.size()))
    {
        return;
    }
    Particle& p = particles_[static_cast<size_t>(index)];
    if (!p.pinned)
    {
        return;
    }
    p.position = position;
    p.previous = position;
    Wake();
}

bool Ribbon::Pinned(int index) const
{
    if (index < 0 || index >= static_cast<int>(particles_.size()))
    {
        return false;
    }
    return particles_[static_cast<size_t>(index)].pinned;
}

void Ribbon::AddCollider(const SoftCollider& collider)
{
    colliders_.push_back(collider);
    Wake();
}

void Ribbon::ClearColliders()
{
    colliders_.clear();
    Wake();
}

void Ribbon::Wake()
{
    asleep_ = false;
    stillTime_ = 0.f;
}

void Ribbon::ApplyWind(float dt)
{
    if (!wind_ || settings_.windDrag <= 0.f)
    {
        return;
    }

    for (size_t i = 0; i < particles_.size(); ++i)
    {
        Particle& p = particles_[i];
        if (p.pinned)
        {
            continue;
        }
        const sf::Vector2f velocity = (p.position - p.previous) / std::max(1e-5f, dt);
        const sf::Vector2f air = WindAcceleration(wind_, p.position, windTime_, velocity, settings_.windDrag, 1.f);
        p.position += air * (dt * dt);

        const size_t next = std::min(i + 1, particles_.size() - 1);
        const size_t prev = (i == 0) ? 0 : i - 1;
        const sf::Vector2f tangent = particles_[next].position - particles_[prev].position;
        const sf::Vector2f w = wind_->Sample(p.position, windTime_).velocity;
        const float cross = tangent.x * w.y - tangent.y * w.x;
        twistVelocity_[i] += cross * settings_.windTorque * dt;
    }
}

bool Ribbon::IsStill() const
{
    const float limitSq = settings_.sleepVelocity * settings_.sleepVelocity;
    for (const Particle& p : particles_)
    {
        if (p.pinned)
        {
            continue;
        }
        const sf::Vector2f velocity = p.position - p.previous;
        if (velocity.x * velocity.x + velocity.y * velocity.y > limitSq)
        {
            return false;
        }
    }
    return true;
}

void Ribbon::Update(float dt)
{
    if (particles_.size() < 2)
    {
        return;
    }
    dt = std::clamp(dt, 0.f, settings_.maxStep);
    if (dt <= 0.f)
    {
        return;
    }

    if (asleep_)
    {
        if (wind_ != nullptr && wind_->Sample(Center(), windTime_).Speed() > settings_.sleepVelocity)
        {
            Wake();
        }
        if (asleep_)
        {
            return;
        }
    }

    windTime_ += dt;
    ApplyWind(dt);

    const float damping = std::clamp(settings_.damping, 0.f, 1.f);
    const sf::Vector2f gravity = settings_.gravity * (dt * dt);
    for (Particle& p : particles_)
    {
        if (p.pinned)
        {
            continue;
        }
        const sf::Vector2f velocity = (p.position - p.previous) * damping;
        p.previous = p.position;
        p.position += velocity + gravity;
    }

    for (int iteration = 0; iteration < std::max(1, settings_.iterations); ++iteration)
    {
        Solve();
    }

    ApplyPlasticity(dt);
    Collide();

    const float stiffness = std::max(0.f, settings_.torsionStiffness);
    const float torsionDamping = std::clamp(settings_.torsionDamping, 0.f, 1.f);
    for (size_t i = 0; i < twist_.size(); ++i)
    {
        twistVelocity_[i] += (restTwist_[i] - twist_[i]) * stiffness * dt;
        twistVelocity_[i] *= torsionDamping;
        twist_[i] += twistVelocity_[i] * dt;
    }

    for (size_t i = 0; i < particles_.size(); ++i)
    {
        positions_[i] = particles_[i].position;
    }

    if (IsStill())
    {
        stillTime_ += dt;
        if (stillTime_ >= settings_.sleepSeconds)
        {
            asleep_ = true;
        }
    }
    else
    {
        stillTime_ = 0.f;
    }
}

void Ribbon::Solve()
{
    const float stiffness = std::clamp(settings_.stiffness, 0.05f, 1.f);
    for (size_t i = 0; i + 1 < particles_.size(); ++i)
    {
        Particle& a = particles_[i];
        Particle& b = particles_[i + 1];
        const sf::Vector2f delta = b.position - a.position;
        const float current = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (current <= kEpsilon)
        {
            continue;
        }
        const float rest = (i < restSegment_.size()) ? restSegment_[i] : segment_;
        const float error = (current - rest) / current * stiffness;
        const sf::Vector2f correction = delta * error;
        if (a.pinned && b.pinned)
        {
            continue;
        }
        if (a.pinned)
        {
            b.position -= correction;
        }
        else if (b.pinned)
        {
            a.position += correction;
        }
        else
        {
            a.position += correction * 0.5f;
            b.position -= correction * 0.5f;
        }
    }

    const float bend = std::clamp(settings_.bendStiffness, 0.f, 1.f);
    if (bend > 0.f)
    {
        for (size_t i = 1; i + 1 < particles_.size(); ++i)
        {
            Particle& a = particles_[i - 1];
            Particle& b = particles_[i];
            Particle& c = particles_[i + 1];
            if (b.pinned)
            {
                continue;
            }
            const sf::Vector2f mid = (a.position + c.position) * 0.5f;
            b.position += (mid - b.position) * (bend * 0.5f);
        }
    }
}

void Ribbon::Collide()
{
    if (colliders_.empty() || settings_.thickness <= 0.f)
    {
        return;
    }
    const float thickness = settings_.thickness * 0.5f;
    for (Particle& p : particles_)
    {
        if (p.pinned)
        {
            continue;
        }
        for (const SoftCollider& collider : colliders_)
        {
            sf::Vector2f normal;
            const float depth = SoftColliderPenetration(collider, p.position, thickness, normal);
            if (depth <= 0.f)
            {
                continue;
            }
            const sf::Vector2f correction = normal * depth;
            p.position += correction;
            p.previous += correction * std::clamp(collider.friction, 0.f, 1.f);
        }
    }
}

void Ribbon::ApplyPlasticity(float dt)
{
    if (settings_.plasticity <= 0.f)
    {
        return;
    }
    const float rate = std::clamp(settings_.plasticity * dt * 60.f, 0.f, 1.f);
    for (size_t i = 0; i + 1 < particles_.size(); ++i)
    {
        const float current = Distance(particles_[i].position, particles_[i + 1].position);
        const float rest = i < restSegment_.size() ? restSegment_[i] : segment_;
        const float strain = (current - rest) / std::max(kEpsilon, rest);
        if (strain > settings_.yieldStrain)
        {
            restSegment_[i] = rest + (current - rest) * rate;
        }
    }
}

sf::Vector2f Ribbon::Point(int index) const
{
    if (positions_.empty())
    {
        return {};
    }
    return positions_[static_cast<size_t>(std::clamp(index, 0, static_cast<int>(positions_.size()) - 1))];
}

float Ribbon::Twist(int index) const
{
    if (twist_.empty())
    {
        return 0.f;
    }
    return twist_[static_cast<size_t>(std::clamp(index, 0, static_cast<int>(twist_.size()) - 1))];
}

sf::Vector2f Ribbon::Center() const
{
    if (positions_.empty())
    {
        return {};
    }
    sf::Vector2f sum{};
    for (const sf::Vector2f& p : positions_)
    {
        sum += p;
    }
    return sum / static_cast<float>(positions_.size());
}

void Ribbon::Render(Skirt& FrameDrawList, sf::Color color, int z, DrawLayer layer) const
{
    if (positions_.size() < 2)
    {
        return;
    }

    for (size_t i = 0; i + 1 < positions_.size(); ++i)
    {
        const sf::Vector2f a = positions_[i];
        const sf::Vector2f b = positions_[i + 1];
        sf::Vector2f delta = b - a;
        const float len = LengthOf(delta);
        if (len < 0.0001f)
        {
            continue;
        }
        const sf::Vector2f dir = delta / len;
        const sf::Vector2f perp{-dir.y, dir.x};

        const float twistA = Twist(static_cast<int>(i));
        const float twistB = Twist(static_cast<int>(std::min(i + 1, twist_.size() - 1)));
        const float widthA = width_ * 0.5f * std::cos(twistA);
        const float widthB = width_ * 0.5f * std::cos(twistB);

        const float facing = (std::cos(twistA) + std::cos(twistB)) * 0.5f;
        const sf::Color cell = Shade(color, std::clamp(0.55f + 0.45f * std::abs(facing), 0.45f, 1.f));
        FrameDrawList.Polygon({a + perp * widthA, b + perp * widthB, b - perp * widthB, a - perp * widthA}, cell,
                      sf::Color::Transparent, DrawCommand::kNoThickness, z, layer);
    }
}

void Ribbon::RenderWireframe(Skirt& FrameDrawList, sf::Color color, float thickness, int z, DrawLayer layer) const
{
    for (size_t i = 0; i + 1 < positions_.size(); ++i)
    {
        FrameDrawList.Line(positions_[i], positions_[i + 1], color, thickness, z, layer);
    }

    for (size_t i = 0; i < positions_.size(); ++i)
    {
        const size_t next = std::min(i + 1, positions_.size() - 1);
        const size_t prev = (i == 0) ? 0 : i - 1;
        sf::Vector2f dir = positions_[next] - positions_[prev];
        const float len = LengthOf(dir);
        if (len < 0.0001f)
        {
            continue;
        }
        dir /= len;
        const sf::Vector2f perp{-dir.y, dir.x};
        const sf::Vector2f half = perp * (width_ * 0.5f * std::cos(Twist(static_cast<int>(i))));
        FrameDrawList.Line(positions_[i] - half, positions_[i] + half, color, std::max(0.5f, thickness * 0.6f), z, layer);
    }
}

} // namespace Fem
