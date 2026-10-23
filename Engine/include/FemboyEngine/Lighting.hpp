// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "FemboyEngine/Skirt.hpp"
#include "FemboyEngine/Visibility.hpp"

namespace Fem
{

class DistanceField;

// A point/area light. A light with castsShadows asks VisibilityWorld for its
// visible polygon; one without is a cheap radial light (faster, useful for
// muzzle flashes and UI glows).
struct Light2D
{
    sf::Vector2f position{0.f, 0.f};
    float radius = 280.f;
    sf::Color color = sf::Color::White;
    float intensity = 1.f;
    bool castsShadows = true;
    bool enabled = true;
    // A deterministic flicker in [1-flicker, 1+flicker], driven by `phase`.
    float flicker = 0.f;
    float phase = 0.f;
    // Light probes: dynamic objects sample the nearest baked probe instead of
    // reading the lightmap texture.
    bool contributesToProbes = true;
};

// Low-resolution CPU lightmap + probe grid.
//
// The map is deliberately not per-pixel: a 320x180 lightmap for a 1500x950
// world is 0.21 screen pixels per world unit, and the texture is smoothly
// scaled by the GPU. That is enough for the soft, neon look this engine targets
// and keeps the CPU cost to a few tenths of a millisecond for a handful of
// lights. Each light's visibility polygon is scanline-filled into the image,
// then attenuated by distance and, optionally, by a static SDF for corner AO.
class LightingSystem
{
public:
    // pixelsPerUnit controls lightmap resolution. 0.25 means a 1500 px wide
    // world gets a 375 px image. Clamped to [0.02, 4].
    void SetResolution(float pixelsPerUnit);
    float Resolution() const
    {
        return pixelsPerUnit_;
    }
    // Rebuild at most every `seconds`. 0 rebuilds whenever Update() is called
    // and something changed.
    void SetRefreshInterval(float seconds);
    void SetAmbient(const sf::Color& color);
    sf::Color Ambient() const
    {
        return ambient_;
    }
    void SetAmbientOcclusion(float strength)
    {
        aoStrength_ = std::clamp(strength, 0.f, 1.f);
    }
    void SetDistanceField(const DistanceField* field)
    {
        field_ = field;
    }
    void SetVisibility(const VisibilityWorld* visibility)
    {
        visibility_ = visibility;
    }
    void SetTextureId(const std::string& id)
    {
        textureId_ = id;
    }

    int AddLight(const Light2D& light);
    void ClearLights();
    std::vector<Light2D>& Lights()
    {
        return lights_;
    }
    const std::vector<Light2D>& Lights() const
    {
        return lights_;
    }
    Light2D* Light(int index);
    void Invalidate()
    {
        dirty_ = true;
    }

    // Rebuilds the lightmap for the given world rectangle. Returns true when the
    // image was rebuilt.
    bool Update(float dt, const sf::FloatRect& bounds);
    // Force a rebuild on the next update.
    void MarkDirty()
    {
        dirty_ = true;
    }

    bool Valid() const
    {
        return width_ > 0 && height_ > 0 && !pixels_.empty();
    }
    const sf::Image& Image() const
    {
        return image_;
    }
    sf::FloatRect Bounds() const
    {
        return bounds_;
    }

    // Submits the lightmap as a multiply-blended sprite on DrawLayer::Lighting.
    // The UI is above that layer, so menus are never darkened.
    void Submit(Skirt& FrameDrawList, int z = 0) const;
    // Light probes: a coarse grid over the same bounds, sampled bilinearly.
    // `ProbeColorAt` is what a dynamic sprite uses for its tint.
    void SetProbeGrid(int columns, int rows);
    int ProbeColumns() const
    {
        return probeCols_;
    }
    int ProbeRows() const
    {
        return probeRows_;
    }
    sf::Color ProbeColorAt(sf::Vector2f world) const;

    // CPU sample of the lightmap; the game can use it for fog, HUD or tests.
    sf::Color SampleAt(sf::Vector2f world) const;

    float LastRebuildMs() const
    {
        return lastRebuildMs_;
    }

private:
    void RasterizePolygon(const std::vector<sf::Vector2f>& polygon, const sf::FloatRect& bounds, int width, int height,
                          std::vector<std::uint8_t>& mask) const;
    void BuildProbes();

    float pixelsPerUnit_ = 0.25f;
    float refreshInterval_ = 0.f;
    float sinceRefresh_ = 1.0e9f;
    sf::Color ambient_ = sf::Color(120, 128, 150);
    float aoStrength_ = 0.65f;
    const DistanceField* field_ = nullptr;
    const VisibilityWorld* visibility_ = nullptr;
    std::string textureId_ = "__lightmap";

    std::vector<Light2D> lights_;
    std::vector<sf::Color> probes_;
    int probeCols_ = 0;
    int probeRows_ = 0;

    sf::Image image_;
    sf::Texture texture_;
    std::vector<std::uint8_t> pixels_; // RGBA8
    sf::FloatRect bounds_{};
    int width_ = 0;
    int height_ = 0;
    bool dirty_ = true;
    bool textureReady_ = false;
    bool textureWarned_ = false;
    float lastRebuildMs_ = 0.f;
};

} // namespace Fem
