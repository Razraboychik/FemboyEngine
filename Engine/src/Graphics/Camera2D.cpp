// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Camera2D.hpp"
#include "FemboyEngine/Accessibility.hpp"
#include <cmath>
#include <random>

namespace Fem
{

namespace
{
float RandSigned()
{
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> d(-1.f, 1.f);
    return d(rng);
}
} // namespace

Camera2D::Camera2D()
{
    view_.setSize({viewW_, viewH_});
    view_.setCenter({viewW_ / 2.f, viewH_ / 2.f});
}

void Camera2D::SetPosition(float x, float y)
{
    x_ = x;
    y_ = y;
}
void Camera2D::Move(float dx, float dy)
{
    x_ += dx;
    y_ += dy;
}

void Camera2D::SetZoom(float z)
{
    // A non-positive or non-finite zoom collapses/inverts the view; clamp it.
    if (!std::isfinite(z) || z <= 0.f)
    {
        return;
    }
    zoom_ = std::clamp(z, 0.01f, 1000.f);
}

void Camera2D::SetProjection(ProjectionMode mode, float axisX, float axisY)
{
    projection_.mode = mode;
    projection_.isoAxisX = (std::isfinite(axisX) && std::abs(axisX) > 0.0001f) ? axisX : 1.f;
    projection_.isoAxisY = (std::isfinite(axisY) && std::abs(axisY) > 0.0001f) ? axisY : 0.5f;
    view_.setCenter(projection_.Project({x_, y_}));
}

namespace
{
sf::Vector2f RotateForView(sf::Vector2f value, float degrees)
{
    const float radians = -degrees * 3.14159265358979323846f / 180.f;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return {value.x * c - value.y * s, value.x * s + value.y * c};
}
} // namespace

sf::Vector2f Camera2D::WorldToScreen(sf::Vector2f world) const
{
    return WorldToScreen(world, sf::FloatRect({0.f, 0.f}, {viewW_, viewH_}));
}

sf::Vector2f Camera2D::WorldToScreen(sf::Vector2f world, const sf::FloatRect& viewport) const
{
    sf::Vector2f delta = projection_.Project(world) - view_.getCenter();
    delta = RotateForView(delta, rotation_);
    const sf::Vector2f size = view_.getSize();
    const float nx = size.x > 0.f ? delta.x / size.x : 0.f;
    const float ny = size.y > 0.f ? delta.y / size.y : 0.f;
    return {viewport.position.x + (nx + 0.5f) * viewport.size.x,
            viewport.position.y + (ny + 0.5f) * viewport.size.y};
}

sf::Vector2f Camera2D::ScreenToWorld(sf::Vector2f screen) const
{
    return ScreenToWorld(screen, sf::FloatRect({0.f, 0.f}, {viewW_, viewH_}));
}

sf::Vector2f Camera2D::ScreenToWorld(sf::Vector2f screen, const sf::FloatRect& viewport) const
{
    const float nx = viewport.size.x > 0.f ? (screen.x - viewport.position.x) / viewport.size.x - 0.5f : 0.f;
    const float ny = viewport.size.y > 0.f ? (screen.y - viewport.position.y) / viewport.size.y - 0.5f : 0.f;
    const sf::Vector2f size = view_.getSize();
    sf::Vector2f delta{nx * size.x, ny * size.y};
    // Undo the view rotation.
    delta = RotateForView(delta, -rotation_);
    return projection_.Unproject(view_.getCenter() + delta);
}

void Camera2D::Follow(Entity target, float smoothing)
{
    followTarget_ = target;
    // There is a single smoothing knob: setEasing() and follow() used to write
    // different fields, and only follow()'s field was ever read, so setEasing()
    // had no effect at all.
    SetEasing(smoothing);
}

void Camera2D::StopFollow()
{
    followTarget_ = entt::null;
    lookAheadX_ = 0.f;
    lookAheadY_ = 0.f;
}

void Camera2D::Shake(float intensity, float duration)
{
    // Motion comfort is enforced by the camera itself. A game can keep calling
    // Shake() for gameplay timing without every project remembering to gate it.
    if (Accessibility::Instance().ReduceMotion())
    {
        return;
    }
    if (intensity <= 0.f || duration <= 0.f)
    {
        return;
    }
    // Stronger shake replaces a weaker one; a weaker one does not cut it short.
    if (intensity >= shakeIntensity_)
    {
        shakeIntensity_ = intensity;
        shakeDuration_ = duration;
        shakeTimer_ = duration;
    }
}

void Camera2D::UpdateShake(float dt)
{
    if (Accessibility::Instance().ReduceMotion())
    {
        shakeTimer_ = 0.f;
        shakeOffsetX_ = 0.f;
        shakeOffsetY_ = 0.f;
        shakeIntensity_ = 0.f;
        return;
    }
    if (shakeTimer_ > 0.f)
    {
        shakeTimer_ = std::max(0.f, shakeTimer_ - dt);
        const float factor = shakeDuration_ > 0.f ? (shakeTimer_ / shakeDuration_) : 0.f;
        // The offset is applied to the view only, never accumulated into the
        // logical camera position (otherwise the camera slowly drifts away).
        shakeOffsetX_ = RandSigned() * shakeIntensity_ * factor;
        shakeOffsetY_ = RandSigned() * shakeIntensity_ * factor;
    }
    else
    {
        shakeOffsetX_ = 0.f;
        shakeOffsetY_ = 0.f;
        shakeIntensity_ = 0.f;
    }
}

void Camera2D::SetBounds(float minX, float minY, float maxX, float maxY)
{
    hasBounds_ = true;
    boundsMin_ = {std::min(minX, maxX), std::min(minY, maxY)};
    boundsMax_ = {std::max(minX, maxX), std::max(minY, maxY)};
}

std::vector<sf::Vector2f> Camera2D::ParallaxOffsets() const
{
    std::vector<sf::Vector2f> out;
    out.reserve(parallaxLayers_.size());
    for (const auto& layer : parallaxLayers_)
    {
        out.push_back({x_ * layer.speed, y_ * layer.speed});
    }
    return out;
}

void Camera2D::Update(float dt, Registry& reg)
{
    if (dt < 0.f)
    {
        dt = 0.f;
    }

    if (followTarget_ != entt::null && reg.valid(followTarget_) && reg.all_of<TransformComponent>(followTarget_))
    {
        const auto& t = reg.get<TransformComponent>(followTarget_);
        float targetX = t.x;
        float targetY = t.y;

        if (hasDeadZone_)
        {
            const float dx = targetX - x_;
            const float dy = targetY - y_;
            if (std::abs(dx) < deadZoneW_ * 0.5f)
            {
                targetX = x_;
            }
            if (std::abs(dy) < deadZoneH_ * 0.5f)
            {
                targetY = y_;
            }
        }

        if (hasLookAhead_)
        {
            const float dx = targetX - x_;
            const float dy = targetY - y_;
            const float mag = std::sqrt(dx * dx + dy * dy);
            if (mag > 0.01f)
            {
                const float k = std::clamp(lookAheadSmooth_ * dt, 0.f, 1.f);
                lookAheadX_ += (dx / mag * lookAheadDist_ - lookAheadX_) * k;
                lookAheadY_ += (dy / mag * lookAheadDist_ - lookAheadY_) * k;
            }
            targetX += lookAheadX_;
            targetY += lookAheadY_;
        }

        const float k = std::clamp(easing_ * dt, 0.f, 1.f);
        x_ += (targetX - x_) * k;
        y_ += (targetY - y_) * k;
    }

    UpdateShake(dt);

    if (hasBounds_)
    {
        const float halfW = viewW_ / (2.f * zoom_);
        const float halfH = viewH_ / (2.f * zoom_);
        const float minCenterX = boundsMin_.x + halfW;
        const float maxCenterX = boundsMax_.x - halfW;
        const float minCenterY = boundsMin_.y + halfH;
        const float maxCenterY = boundsMax_.y - halfH;

        // When the map is smaller than the view, center on the map instead of
        // producing inverted (min > max) clamp ranges.
        x_ = (minCenterX <= maxCenterX) ? std::clamp(x_, minCenterX, maxCenterX) : (boundsMin_.x + boundsMax_.x) * 0.5f;
        y_ = (minCenterY <= maxCenterY) ? std::clamp(y_, minCenterY, maxCenterY) : (boundsMin_.y + boundsMax_.y) * 0.5f;
    }

    // The camera lives in world space; only its visible center is projected.
    // Applying the same affine transform to commands in the renderer keeps the
    // two halves consistent.
    view_.setCenter(projection_.Project({x_ + shakeOffsetX_, y_ + shakeOffsetY_}));
    view_.setSize({viewW_ / zoom_, viewH_ / zoom_});
    view_.setRotation(sf::degrees(rotation_));
}

void Camera2D::SetView(sf::RenderWindow& window)
{
    window.setView(view_);
}

} // namespace Fem
