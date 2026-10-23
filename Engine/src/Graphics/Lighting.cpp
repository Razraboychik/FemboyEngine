// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Lighting.hpp"
#include "FemboyEngine/Boy.hpp"

#include "FemboyEngine/Sdf.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Fem
{

namespace
{
std::uint8_t ToByte(float value)
{
    return static_cast<std::uint8_t>(std::clamp(value, 0.f, 255.f));
}

float Smoothstep01(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
} // namespace

void LightingSystem::SetResolution(float pixelsPerUnit)
{
    pixelsPerUnit_ = std::clamp(pixelsPerUnit, 0.02f, 4.f);
    dirty_ = true;
}

void LightingSystem::SetRefreshInterval(float seconds)
{
    refreshInterval_ = std::max(0.f, seconds);
}

void LightingSystem::SetAmbient(const sf::Color& color)
{
    ambient_ = color;
    dirty_ = true;
}

int LightingSystem::AddLight(const Light2D& light)
{
    lights_.push_back(light);
    dirty_ = true;
    return static_cast<int>(lights_.size()) - 1;
}

void LightingSystem::ClearLights()
{
    lights_.clear();
    dirty_ = true;
}

Light2D* LightingSystem::Light(int index)
{
    if (index < 0 || index >= static_cast<int>(lights_.size()))
    {
        return nullptr;
    }
    return &lights_[static_cast<std::size_t>(index)];
}

void LightingSystem::SetProbeGrid(int columns, int rows)
{
    probeCols_ = std::clamp(columns, 0, 256);
    probeRows_ = std::clamp(rows, 0, 256);
    dirty_ = true;
}

void LightingSystem::RasterizePolygon(const std::vector<sf::Vector2f>& polygon, const sf::FloatRect& bounds, int width,
                                      int height, std::vector<std::uint8_t>& mask) const
{
    mask.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    if (polygon.size() < 3 || width <= 0 || height <= 0)
    {
        return;
    }

    std::vector<float> intersections;
    intersections.reserve(polygon.size());

    for (int y = 0; y < height; ++y)
    {
        const float sy = bounds.position.y + (static_cast<float>(y) + 0.5f) / pixelsPerUnit_;
        intersections.clear();

        for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
        {
            const sf::Vector2f& a = polygon[j];
            const sf::Vector2f& b = polygon[i];
            if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy))
            {
                const float t = (sy - a.y) / (b.y - a.y);
                intersections.push_back(a.x + t * (b.x - a.x));
            }
        }

        if (intersections.size() < 2)
        {
            continue;
        }
        std::sort(intersections.begin(), intersections.end());

        for (std::size_t i = 0; i + 1 < intersections.size(); i += 2)
        {
            const int x0 =
                std::max(0, static_cast<int>(std::floor((intersections[i] - bounds.position.x) * pixelsPerUnit_)));
            const int x1 = std::min(
                width - 1, static_cast<int>(std::ceil((intersections[i + 1] - bounds.position.x) * pixelsPerUnit_)));
            for (int x = x0; x <= x1; ++x)
            {
                mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = 1;
            }
        }
    }
}

bool LightingSystem::Update(float dt, const sf::FloatRect& bounds)
{
    sinceRefresh_ += std::max(0.f, dt);
    if (!(pixelsPerUnit_ > 0.f) || bounds.size.x <= 0.f || bounds.size.y <= 0.f)
    {
        return false;
    }

    const bool boundsChanged = bounds_.position != bounds.position || bounds_.size != bounds.size;
    if (!dirty_ && !boundsChanged && (refreshInterval_ > 0.f && sinceRefresh_ < refreshInterval_))
    {
        return false;
    }

    const int width = std::clamp(static_cast<int>(std::ceil(bounds.size.x * pixelsPerUnit_)), 1, 4096);
    const int height = std::clamp(static_cast<int>(std::ceil(bounds.size.y * pixelsPerUnit_)), 1, 4096);
    const bool resized = width != width_ || height != height_ || pixels_.empty();

    width_ = width;
    height_ = height;
    bounds_ = bounds;
    if (resized)
    {
        pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4, 255);
        probes_.clear();
    }

    const auto start = std::chrono::steady_clock::now();

    // Ambient pass. AO is sampled from the static SDF when one is attached; the
    // alternative (a second, screen-space AO) would need depth, which a 2D
    // painter's renderer does not have.
    for (int y = 0; y < height_; ++y)
    {
        for (int x = 0; x < width_; ++x)
        {
            const float wx = bounds.position.x + (static_cast<float>(x) + 0.5f) / pixelsPerUnit_;
            const float wy = bounds.position.y + (static_cast<float>(y) + 0.5f) / pixelsPerUnit_;
            float ao = 1.f;
            if (field_)
            {
                ao = field_->AoAt(wx, wy, 56.f);
            }
            const float k = 1.f - aoStrength_ * (1.f - ao);
            const std::size_t index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) * 4;
            pixels_[index + 0] = ToByte(static_cast<float>(ambient_.r) * k);
            pixels_[index + 1] = ToByte(static_cast<float>(ambient_.g) * k);
            pixels_[index + 2] = ToByte(static_cast<float>(ambient_.b) * k);
            pixels_[index + 3] = 255;
        }
    }

    std::vector<std::uint8_t> mask;
    std::vector<sf::Vector2f> circle;

    for (const Light2D& light : lights_)
    {
        if (!light.enabled || light.radius <= 0.f || light.intensity <= 0.f)
        {
            continue;
        }

        float intensity = light.intensity;
        if (light.flicker > 0.f)
        {
            intensity *= 1.f + light.flicker * std::sin(light.phase);
        }

        std::vector<sf::Vector2f> polygon;
        if (light.castsShadows && visibility_ && visibility_->SegmentCount() > 0)
        {
            polygon = visibility_->VisibilityPolygon(light.position, light.radius);
        }
        else
        {
            circle.clear();
            constexpr int kSegments = 24;
            circle.reserve(kSegments);
            for (int i = 0; i < kSegments; ++i)
            {
                const float a = 6.283185307179586f * static_cast<float>(i) / static_cast<float>(kSegments);
                circle.push_back(
                    {light.position.x + std::cos(a) * light.radius, light.position.y + std::sin(a) * light.radius});
            }
            polygon = circle;
        }

        RasterizePolygon(polygon, bounds_, width_, height_, mask);
        if (mask.empty())
        {
            continue;
        }

        for (int y = 0; y < height_; ++y)
        {
            for (int x = 0; x < width_; ++x)
            {
                const std::size_t cell =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
                if (!mask[cell])
                {
                    continue;
                }

                const float wx = bounds.position.x + (static_cast<float>(x) + 0.5f) / pixelsPerUnit_;
                const float wy = bounds.position.y + (static_cast<float>(y) + 0.5f) / pixelsPerUnit_;
                const float dx = wx - light.position.x;
                const float dy = wy - light.position.y;
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance > light.radius)
                {
                    continue;
                }

                // Inverse-square-ish core with a soft rim. A pure 1/d^2 looks
                // harsh at this resolution; the smoothstep keeps the falloff
                // art-directable and free of a singularity at the centre.
                const float t = 1.f - distance / light.radius;
                const float falloff = Smoothstep01(t);

                const std::size_t index = cell * 4;
                pixels_[index + 0] = ToByte(static_cast<float>(pixels_[index + 0]) +
                                            static_cast<float>(light.color.r) * intensity * falloff);
                pixels_[index + 1] = ToByte(static_cast<float>(pixels_[index + 1]) +
                                            static_cast<float>(light.color.g) * intensity * falloff);
                pixels_[index + 2] = ToByte(static_cast<float>(pixels_[index + 2]) +
                                            static_cast<float>(light.color.b) * intensity * falloff);
            }
        }
    }

    image_.resize({static_cast<unsigned>(width_), static_cast<unsigned>(height_)}, pixels_.data());
    textureReady_ = true;
    if (texture_.getSize() != sf::Vector2u{static_cast<unsigned>(width_), static_cast<unsigned>(height_)})
    {
        // A headless process (unit tests, a dedicated server) has no GL context,
        // so the texture half of the lightmap can fail while the CPU image is
        // perfectly usable through SampleAt()/ProbeColorAt(). Warn once and keep
        // going instead of discarding the rebuild.
        if (!texture_.resize({static_cast<unsigned>(width_), static_cast<unsigned>(height_)}))
        {
            if (!textureWarned_)
            {
                BOY_WARN("Lighting", "lightmap texture unavailable; CPU sampling still works");
                textureWarned_ = true;
            }
            textureReady_ = false;
        }
    }
    if (textureReady_)
    {
        texture_.update(image_);
    }

    if (probeCols_ > 0 && probeRows_ > 0)
    {
        BuildProbes();
    }

    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    lastRebuildMs_ = static_cast<float>(ms);

    dirty_ = false;
    sinceRefresh_ = 0.f;
    return true;
}

void LightingSystem::Submit(Skirt& FrameDrawList, int z) const
{
    if (!Valid() || !textureReady_ || texture_.getSize().x == 0 || texture_.getSize().y == 0)
    {
        return;
    }
    FrameDrawList.Lightmap(texture_, bounds_, z);
}

void LightingSystem::BuildProbes()
{
    const int columns = std::min(probeCols_, width_);
    const int rows = std::min(probeRows_, height_);
    probes_.assign(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows), sf::Color::Black);
    if (columns <= 0 || rows <= 0)
    {
        return;
    }

    for (int py = 0; py < rows; ++py)
    {
        for (int px = 0; px < columns; ++px)
        {
            const int x0 = px * width_ / columns;
            const int x1 = std::max(x0 + 1, (px + 1) * width_ / columns);
            const int y0 = py * height_ / rows;
            const int y1 = std::max(y0 + 1, (py + 1) * height_ / rows);

            int r = 0, g = 0, b = 0, count = 0;
            for (int y = y0; y < y1; ++y)
            {
                for (int x = x0; x < x1; ++x)
                {
                    const std::size_t index =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) *
                        4;
                    r += pixels_[index + 0];
                    g += pixels_[index + 1];
                    b += pixels_[index + 2];
                    ++count;
                }
            }
            if (count > 0)
            {
                probes_[static_cast<std::size_t>(py) * static_cast<std::size_t>(columns) +
                        static_cast<std::size_t>(px)] =
                    sf::Color(static_cast<std::uint8_t>(r / count), static_cast<std::uint8_t>(g / count),
                              static_cast<std::uint8_t>(b / count), 255);
            }
        }
    }
    probeCols_ = columns;
    probeRows_ = rows;
}

sf::Color LightingSystem::ProbeColorAt(sf::Vector2f world) const
{
    if (probeCols_ <= 0 || probeRows_ <= 0 || probes_.empty() || bounds_.size.x <= 0.f || bounds_.size.y <= 0.f)
    {
        return SampleAt(world);
    }

    const float gx = (world.x - bounds_.position.x) / bounds_.size.x * static_cast<float>(probeCols_ - 1);
    const float gy = (world.y - bounds_.position.y) / bounds_.size.y * static_cast<float>(probeRows_ - 1);
    const int x0 = std::clamp(static_cast<int>(std::floor(gx)), 0, probeCols_ - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(gy)), 0, probeRows_ - 1);
    const int x1 = std::min(x0 + 1, probeCols_ - 1);
    const int y1 = std::min(y0 + 1, probeRows_ - 1);
    const float tx = std::clamp(gx - static_cast<float>(x0), 0.f, 1.f);
    const float ty = std::clamp(gy - static_cast<float>(y0), 0.f, 1.f);

    const auto at = [this](int x, int y)
    {
        return probes_[static_cast<std::size_t>(y) * static_cast<std::size_t>(probeCols_) +
                       static_cast<std::size_t>(x)];
    };
    const sf::Color c00 = at(x0, y0);
    const sf::Color c10 = at(x1, y0);
    const sf::Color c01 = at(x0, y1);
    const sf::Color c11 = at(x1, y1);

    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float r0 = lerp(static_cast<float>(c00.r), static_cast<float>(c10.r), tx);
    const float g0 = lerp(static_cast<float>(c00.g), static_cast<float>(c10.g), tx);
    const float b0 = lerp(static_cast<float>(c00.b), static_cast<float>(c10.b), tx);
    const float r1 = lerp(static_cast<float>(c01.r), static_cast<float>(c11.r), tx);
    const float g1 = lerp(static_cast<float>(c01.g), static_cast<float>(c11.g), tx);
    const float b1 = lerp(static_cast<float>(c01.b), static_cast<float>(c11.b), tx);

    return sf::Color(ToByte(lerp(r0, r1, ty)), ToByte(lerp(g0, g1, ty)), ToByte(lerp(b0, b1, ty)), 255);
}

sf::Color LightingSystem::SampleAt(sf::Vector2f world) const
{
    if (!Valid() || bounds_.size.x <= 0.f || bounds_.size.y <= 0.f)
    {
        return sf::Color::White;
    }

    const int x = std::clamp(static_cast<int>((world.x - bounds_.position.x) * pixelsPerUnit_), 0, width_ - 1);
    const int y = std::clamp(static_cast<int>((world.y - bounds_.position.y) * pixelsPerUnit_), 0, height_ - 1);
    const std::size_t index =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) * 4;
    return sf::Color(pixels_[index + 0], pixels_[index + 1], pixels_[index + 2], 255);
}

} // namespace Fem
