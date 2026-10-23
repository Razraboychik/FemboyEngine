// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Canvas3D.hpp"
#include "FemboyEngine/PostFX.hpp"
#include "FemboyEngine/Boy.hpp"
#include <cmath>
#include <algorithm>

namespace Fem
{

namespace
{
constexpr float kPi = 3.14159265358979323846f;

struct Vec3
{
    float x, y, z;
};

Vec3 RotateX(const Vec3& p, float r)
{
    const float c = std::cos(r), s = std::sin(r);
    return {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
}

Vec3 RotateY(const Vec3& p, float r)
{
    const float c = std::cos(r), s = std::sin(r);
    return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
}

Vec3 RotateZ(const Vec3& p, float r)
{
    const float c = std::cos(r), s = std::sin(r);
    return {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
}
} // namespace

bool Canvas3D::Initialize(int w, int h)
{
    if (w <= 0 || h <= 0)
    {
        BOY_ERROR("Canvas3D", "invalid size");
        return false;
    }
    BOY_INFO("Canvas3D", "initializing: " + std::to_string(w) + "x" + std::to_string(h));

    try
    {
        rt_ = std::make_unique<sf::RenderTexture>(sf::Vector2u(static_cast<unsigned>(w), static_cast<unsigned>(h)));
    }
    catch (const std::exception& e)
    {
        BOY_ERROR("Canvas3D", std::string("failed to create render texture: ") + e.what());
        rt_.reset();
        return false;
    }

    if (!rt_ || !rt_->getTexture().getSize().x)
    {
        BOY_ERROR("Canvas3D", "render texture is invalid");
        rt_.reset();
        return false;
    }

    rt_->setSmooth(true);
    BOY_INFO("Canvas3D", "initialized");
    return true;
}

void Canvas3D::Shutdown()
{
    rt_.reset();
}

void Canvas3D::Begin2D()
{
    if (rt_)
    {
        rt_->clear(sf::Color::Transparent);
    }
}
void Canvas3D::End2D()
{
    if (rt_)
    {
        rt_->display();
    }
}

void Canvas3D::SetParallax(float x, float y)
{
    parallaxX_ = x;
    parallaxY_ = y;
}

void Canvas3D::Present(sf::RenderWindow& window, float pitchDeg, float yawDeg, float rollDeg)
{
    if (!rt_)
    {
        return;
    }

    const sf::Texture* source = &rt_->getTexture();

    // Optional post-processing pass: run the captured canvas through the shader
    // and present the result instead.
    if (!postFX_.empty() && PostFX::Instance().Has(postFX_))
    {
        if (!postRt_)
        {
            const sf::Vector2u size = rt_->getSize();
            if (size.x && size.y)
            {
                try
                {
                    postRt_ = std::make_unique<sf::RenderTexture>(size);
                    postRt_->setSmooth(true);
                }
                catch (const std::exception&)
                {
                    postRt_.reset();
                }
            }
        }

        if (postRt_)
        {
            postRt_->clear(sf::Color::Transparent);
            PostFX::Instance().Apply(postFX_, *postRt_, rt_->getTexture());
            postRt_->display();
            source = &postRt_->getTexture();
        }
    }

    const float w = static_cast<float>(rt_->getSize().x);
    const float h = static_cast<float>(rt_->getSize().y);
    if (w <= 0.f || h <= 0.f)
    {
        return;
    }

    const float cx = static_cast<float>(window.getSize().x) * 0.5f + parallaxX_;
    const float cy = static_cast<float>(window.getSize().y) * 0.5f + parallaxY_;

    const float hw = w * 0.5f * scale_;
    const float hh = h * 0.5f * scale_;

    const float pitch = pitchDeg * kPi / 180.f;
    const float yaw = yawDeg * kPi / 180.f;
    const float roll = rollDeg * kPi / 180.f;

    const Vec3 local[4] = {{-hw, -hh, 0.f}, {hw, -hh, 0.f}, {hw, hh, 0.f}, {-hw, hh, 0.f}};

    sf::Vertex v[4];
    const sf::Vector2f uv[4] = {{0.f, 0.f}, {w, 0.f}, {w, h}, {0.f, h}};

    for (int i = 0; i < 4; ++i)
    {
        Vec3 p = local[i];
        p = RotateX(p, pitch);
        p = RotateY(p, yaw);
        p = RotateZ(p, roll);

        // Simple perspective divide: points further away (positive z) shrink.
        float f = 1.f + p.z * perspective_;
        if (f < 0.05f)
        {
            f = 0.05f;
        }

        v[i].position = {cx + p.x * f, cy + p.y * f};
        v[i].texCoords = uv[i];
        v[i].color = sf::Color::White;
    }

    sf::VertexArray quad(sf::PrimitiveType::Triangles, 6);
    quad[0] = v[0];
    quad[1] = v[1];
    quad[2] = v[2];
    quad[3] = v[0];
    quad[4] = v[2];
    quad[5] = v[3];

    sf::RenderStates states;
    states.texture = source;
    window.draw(quad, states);
}

} // namespace Fem
