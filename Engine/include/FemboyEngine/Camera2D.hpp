// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cmath>
#include <vector>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/ECS.hpp"
#include "FemboyEngine/ViewProjection.hpp"

namespace Fem
{

class Camera2D
{
public:
    Camera2D();
    void SetView(sf::RenderWindow& window);

    // position
    void SetPosition(float x, float y);
    void Move(float dx, float dy);
    sf::Vector2f Position() const
    {
        return {x_, y_};
    }

    // zoom
    void SetZoom(float z);
    float Zoom() const
    {
        return zoom_;
    }
    void ZoomBy(float factor)
    {
        SetZoom(zoom_ * factor);
    }

    // rotation (degrees)
    void SetRotation(float r)
    {
        rotation_ = r;
    }
    float Rotation() const
    {
        return rotation_;
    }

    // follow target
    void Follow(Entity target, float smoothing = 5.f);
    void StopFollow();
    Entity FollowTarget() const
    {
        return followTarget_;
    }

    // dead zone (area where target can move without camera moving)
    void SetDeadZone(float w, float h)
    {
        deadZoneW_ = w;
        deadZoneH_ = h;
        hasDeadZone_ = true;
    }
    void ClearDeadZone()
    {
        hasDeadZone_ = false;
    }

    // look-ahead
    void SetLookAhead(float distance, float smoothing = 3.f)
    {
        lookAheadDist_ = distance;
        lookAheadSmooth_ = smoothing;
        hasLookAhead_ = true;
    }
    void ClearLookAhead()
    {
        hasLookAhead_ = false;
    }

    // parallax
    void AddParallaxLayer(float speed, int z)
    {
        parallaxLayers_.push_back({speed, z});
    }
    std::vector<sf::Vector2f> ParallaxOffsets() const;

    // easing
    void SetEasing(float smoothness)
    {
        easing_ = std::max(0.01f, smoothness);
    }
    float Easing() const
    {
        return easing_;
    }

    // shake
    void Shake(float intensity, float duration);
    void UpdateShake(float dt);
    bool IsShaking() const
    {
        return shakeTimer_ > 0.f;
    }

    // bounds
    void SetBounds(float minX, float minY, float maxX, float maxY);
    void ClearBounds()
    {
        hasBounds_ = false;
    }

    // viewport size (defaults to 800x600; call setViewportSize to match the window)
    void SetViewportSize(float w, float h)
    {
        viewW_ = std::max(1.f, w);
        viewH_ = std::max(1.f, h);
    }
    sf::Vector2f ViewportSize() const
    {
        return {viewW_, viewH_};
    }

    // Projection. Orthographic is the historical behaviour; Isometric applies
    // an affine world transform and keeps the camera API unchanged.
    void SetProjection(ProjectionMode mode, float axisX = 1.f, float axisY = 0.5f);
    ProjectionMode Projection() const
    {
        return projection_.mode;
    }
    const ViewProjection& ProjectionState() const
    {
        return projection_;
    }
    sf::Transform WorldTransform() const
    {
        return projection_.WorldTransform();
    }
    sf::Vector2f Project(sf::Vector2f world) const
    {
        return projection_.Project(world);
    }

    // Exact world <-> screen conversion, including projection, zoom, rotation
    // and an optional sub-viewport (the editor preview, split-screen, RT).
    sf::Vector2f WorldToScreen(sf::Vector2f world) const;
    sf::Vector2f WorldToScreen(sf::Vector2f world, const sf::FloatRect& viewport) const;
    sf::Vector2f ScreenToWorld(sf::Vector2f screen) const;
    sf::Vector2f ScreenToWorld(sf::Vector2f screen, const sf::FloatRect& viewport) const;

    // Update every frame.
    void Update(float dt, Registry& reg);

    // access
    sf::View View() const
    {
        return view_;
    }

private:
    float x_ = 0, y_ = 0;
    float zoom_ = 1.f;
    float rotation_ = 0;

    float shakeIntensity_ = 0;
    float shakeDuration_ = 0;
    float shakeTimer_ = 0;
    float shakeOffsetX_ = 0;
    float shakeOffsetY_ = 0;

    Entity followTarget_ = entt::null;

    bool hasBounds_ = false;
    sf::Vector2f boundsMin_{0, 0}, boundsMax_{0, 0};

    bool hasDeadZone_ = false;
    float deadZoneW_ = 0, deadZoneH_ = 0;

    bool hasLookAhead_ = false;
    float lookAheadDist_ = 0, lookAheadSmooth_ = 3.f;
    float lookAheadX_ = 0, lookAheadY_ = 0;

    float easing_ = 5.f;
    float viewW_ = 800.f, viewH_ = 600.f;

    struct ParallaxLayer
    {
        float speed;
        int z;
    };
    std::vector<ParallaxLayer> parallaxLayers_;

    ViewProjection projection_;
    sf::View view_;
};

} // namespace Fem
