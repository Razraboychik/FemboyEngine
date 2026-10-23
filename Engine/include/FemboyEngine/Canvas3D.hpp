// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <string>
#include <memory>

namespace Fem
{

// Darkest Dungeon style: render 2D gameplay onto a 3D canvas (textured quad)
// with perspective, parallax, post-processing and lighting.
class Canvas3D
{
public:
    bool Initialize(int w, int h);
    void Shutdown();
    void Begin2D(); // render 2D scene into texture
    void End2D();

    // 3D presentation: draw the captured 2D texture as a quad in 3D space.
    // Angles are in degrees. pitch tilts the quad away from the camera, yaw
    // turns it, roll spins it in screen space.
    void Present(sf::RenderWindow& window, float pitchDeg = 15.f, float yawDeg = 0.f, float rollDeg = 0.f);

    void SetParallax(float x, float y);
    void SetScale(float scale)
    {
        scale_ = scale;
    }
    void SetPerspective(float strength)
    {
        perspective_ = strength;
    }

    // Post-processing effect name loaded through PostFX ("bloom","vignette",...).
    // An empty name disables post-processing.
    void EnablePostFx(const std::string& fx)
    {
        postFX_ = fx;
    }
    void DisablePostFx()
    {
        postFX_.clear();
    }
    const std::string& GetPostFx() const
    {
        return postFX_;
    }

    sf::RenderTexture& Texture()
    {
        return *rt_;
    }
    bool IsInitialized() const
    {
        return rt_ != nullptr;
    }

private:
    std::unique_ptr<sf::RenderTexture> rt_;
    std::unique_ptr<sf::RenderTexture> postRt_;
    std::string postFX_;
    float parallaxX_ = 0, parallaxY_ = 0;
    float scale_ = 0.9f;
    float perspective_ = 0.0015f;
};

} // namespace Fem
