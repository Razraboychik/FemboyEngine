// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
//
// Wind and weather.
//
// A wind field is a *function of position and time*, not a constant vector.
// That distinction matters for soft bodies: a flag in a uniform wind finds one
// pose and stays there, while a flag in a gusting, turning field keeps folding
// and snapping, which is what makes cloth read as cloth. The same sampling API
// also drives particles, rain, smoke and sound (a "wind" audio snapshot can be
// taken at the listener position).
//
// Forces are aerodynamic, not a push: each soft-body particle sees
//     F = k * |w - v| * (w - v)
// where v is the particle's own velocity. A stationary flag therefore catches
// the full wind, a flag already moving with it feels almost nothing, and the
// cloth never accelerates without bound.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

namespace Fem
{

struct WindSample
{
    // Air velocity in world units per second.
    sf::Vector2f velocity{};
    // 0..2: relative air density. The eye of a storm is near 0, a gust front
    // above 1. Soft bodies scale their drag by it; particles can use it for
    // spread and audio for volume.
    float pressure = 1.f;
    // Turbulence 0..1: how much the local flow wobbles. Hurricanes and tornadoes
    // are high, a steady breeze is low.
    float turbulence = 0.f;

    float Speed() const;
};

class WindField
{
public:
    virtual ~WindField() = default;
    virtual WindSample Sample(sf::Vector2f position, float time) const = 0;
};

// A steady wind. Useful as a base layer under gusts, and as a test fixture.
class ConstantWind : public WindField
{
public:
    ConstantWind() = default;
    ConstantWind(sf::Vector2f direction, float speed);

    void SetDirection(sf::Vector2f direction);
    void SetSpeed(float speed)
    {
        speed_ = speed;
    }

    WindSample Sample(sf::Vector2f position, float time) const override;

private:
    sf::Vector2f direction_{1.f, 0.f};
    float speed_ = 100.f;
};

// Base wind plus gusts, slow direction sway ("side to side") and spatial noise.
// The sway is periodic, so gameplay can rely on it (a pendulum puzzle), while
// the gust layer is noise-driven and unpredictable.
class GustWind : public WindField
{
public:
    struct Settings
    {
        sf::Vector2f direction{1.f, 0.f};
        float speed = 160.f;
        float gustStrength = 60.f;
        float gustFrequency = 0.45f;
        // Peak deviation of the wind direction, in degrees. A hurricane's
        // "side to side" sweep is large; a corridor draught is small.
        float swayDegrees = 25.f;
        float swayPeriod = 4.5f;
        float noiseScale = 0.0025f;
        float turbulence = 0.25f;
        int seed = 1;
    };

    GustWind() = default;
    explicit GustWind(const Settings& settings);

    void SetSettings(const Settings& settings);
    const Settings& GetSettings() const
    {
        return settings_;
    }
    void SetDirection(sf::Vector2f direction);

    WindSample Sample(sf::Vector2f position, float time) const override;

private:
    Settings settings_{};
    sf::Vector2f direction_{1.f, 0.f};
};

// Rankine vortex: solid-body rotation inside the core, 1/r decay outside, with
// optional inward suction and updraft. Two of these side by side make a
// believable tornado pair; a chain of them is a squall line.
class VortexWind : public WindField
{
public:
    struct Settings
    {
        sf::Vector2f center{};
        // Tangential speed at the core radius, world units/s.
        float strength = 240.f;
        float coreRadius = 120.f;
        float outerRadius = 600.f;
        // 0 = pure rotation, 0.3 = air spirals in, negative = outflow.
        float inward = 0.18f;
        float updraft = 0.6f;
        float turbulence = 0.5f;
        // The vortex can drift; center is then center + drift * time.
        sf::Vector2f drift{};
        float clockWise = 1.f; // +1 or -1
    };

    VortexWind() = default;
    explicit VortexWind(const Settings& settings);

    void SetSettings(const Settings& settings)
    {
        settings_ = settings;
    }
    const Settings& GetSettings() const
    {
        return settings_;
    }

    WindSample Sample(sf::Vector2f position, float time) const override;

private:
    Settings settings_{};
};

// Hurricane: translating eye, calm centre, eyewall maximum, spiral rain bands
// and an inflow angle. This is the field that makes a flag snap from one side
// to the other instead of settling.
class HurricaneWind : public WindField
{
public:
    struct Settings
    {
        sf::Vector2f center{};
        sf::Vector2f drift{40.f, -12.f};
        float eyeRadius = 90.f;
        float eyewallRadius = 220.f;
        float outerRadius = 1400.f;
        float maxSpeed = 420.f;
        // Angle between the tangential and the inward component at the eyewall.
        float inflowDegrees = 22.f;
        float bandStrength = 0.35f;
        int bands = 5;
        float turbulence = 0.7f;
        int seed = 7;
        float clockWise = 1.f;
    };

    HurricaneWind() = default;
    explicit HurricaneWind(const Settings& settings);

    void SetSettings(const Settings& settings)
    {
        settings_ = settings;
    }
    const Settings& GetSettings() const
    {
        return settings_;
    }
    sf::Vector2f EyeCenter(float time) const;

    WindSample Sample(sf::Vector2f position, float time) const override;

private:
    Settings settings_{};
};

// A rectangular region with falloff that hosts another field. Outside the
// rectangle the wind is zero, so a cave or a courtyard can be calm while a
// storm rages overhead.
class WindZone : public WindField
{
public:
    WindZone(sf::FloatRect bounds, std::shared_ptr<WindField> inner, float falloff = 80.f);

    void SetBounds(const sf::FloatRect& bounds)
    {
        bounds_ = bounds;
    }
    void SetFalloff(float falloff)
    {
        falloff_ = falloff;
    }
    void SetInner(std::shared_ptr<WindField> inner)
    {
        inner_ = std::move(inner);
    }

    WindSample Sample(sf::Vector2f position, float time) const override;

private:
    sf::FloatRect bounds_{};
    std::shared_ptr<WindField> inner_;
    float falloff_ = 80.f;
};

// Sum of several fields. The composite is how weather is built: base wind +
// gust layer + vortex + local zones.
class CompositeWind : public WindField
{
public:
    void Add(std::shared_ptr<WindField> field);
    void Clear()
    {
        fields_.clear();
    }
    size_t Size() const
    {
        return fields_.size();
    }

    WindSample Sample(sf::Vector2f position, float time) const override;

private:
    std::vector<std::shared_ptr<WindField>> fields_;
};

enum class WeatherKind
{
    Calm,
    Breeze,
    Storm,
    Hurricane,
    Tornado
};

// A weather preset: the wind field plus the scalar knobs a renderer, particle
// system and audio mixer need. `lightning` is the expected flashes per minute.
struct Weather
{
    WeatherKind kind = WeatherKind::Calm;
    std::shared_ptr<WindField> wind;
    float rain = 0.f;      // 0..1
    float snow = 0.f;      // 0..1
    float lightning = 0.f; // flashes per minute
    float fog = 0.f;       // 0..1
    float temperature = 18.f;
    sf::Color tint{255, 255, 255};
};

Weather MakeWeather(WeatherKind kind, unsigned seed = 1);
const char* WeatherName(WeatherKind kind);

} // namespace Fem
