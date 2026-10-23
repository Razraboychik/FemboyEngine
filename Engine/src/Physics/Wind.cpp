// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Wind.hpp"
#include "FemboyEngine/Noise.hpp"

#include <algorithm>
#include <cmath>

namespace Fem
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.f;

sf::Vector2f Normalized(sf::Vector2f v, sf::Vector2f fallback = {1.f, 0.f})
{
    const float length = std::sqrt(v.x * v.x + v.y * v.y);
    if (length < 1e-6f)
    {
        return fallback;
    }
    return v / length;
}

sf::Vector2f Perpendicular(sf::Vector2f v)
{
    return {-v.y, v.x};
}

float SmoothStep(float edge0, float edge1, float x)
{
    if (edge1 <= edge0)
    {
        return x >= edge1 ? 1.f : 0.f;
    }
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
} // namespace

float WindSample::Speed() const
{
    return std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
}

ConstantWind::ConstantWind(sf::Vector2f direction, float speed)
{
    SetDirection(direction);
    speed_ = speed;
}

void ConstantWind::SetDirection(sf::Vector2f direction)
{
    direction_ = Normalized(direction);
}

WindSample ConstantWind::Sample(sf::Vector2f, float) const
{
    WindSample s;
    s.velocity = direction_ * speed_;
    s.pressure = 1.f;
    return s;
}

GustWind::GustWind(const Settings& settings)
{
    SetSettings(settings);
}

void GustWind::SetSettings(const Settings& settings)
{
    settings_ = settings;
    direction_ = Normalized(settings_.direction);
}

void GustWind::SetDirection(sf::Vector2f direction)
{
    settings_.direction = direction;
    direction_ = Normalized(direction);
}

WindSample GustWind::Sample(sf::Vector2f position, float time) const
{
    const float baseAngle = std::atan2(direction_.y, direction_.x);
    const float period = std::max(0.1f, settings_.swayPeriod);
    const float sway = std::sin(2.f * kPi * time / period) * settings_.swayDegrees * kDegToRad;

    // Two noise layers: slow spatial variation (buildings, hills) and a faster
    // temporal gust so a flag never repeats exactly.
    const float spatial =
        Noise::Fbm(position.x * settings_.noiseScale, position.y * settings_.noiseScale, 3, 2.f, 0.5f, settings_.seed);
    const float gust = Noise::Fbm(time * settings_.gustFrequency, 11.3f, 2, 2.f, 0.5f, settings_.seed + 17);

    const float speed = std::max(0.f, settings_.speed + settings_.gustStrength * gust);
    const float angle = baseAngle + sway + spatial * settings_.swayDegrees * kDegToRad * 0.5f;

    WindSample s;
    s.velocity = {std::cos(angle) * speed, std::sin(angle) * speed};
    s.pressure = std::clamp(1.f + 0.35f * gust, 0.1f, 2.f);
    s.turbulence = std::clamp(settings_.turbulence + 0.25f * std::abs(gust), 0.f, 1.f);
    return s;
}

VortexWind::VortexWind(const Settings& settings)
{
    SetSettings(settings);
}

WindSample VortexWind::Sample(sf::Vector2f position, float time) const
{
    const sf::Vector2f center = settings_.center + settings_.drift * time;
    sf::Vector2f delta = position - center;
    const float radius = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (radius < 0.001f)
    {
        WindSample eye;
        eye.pressure = 0.05f;
        return eye;
    }

    const float core = std::max(1.f, settings_.coreRadius);
    float tangential = 0.f;
    if (radius < core)
    {
        tangential = settings_.strength * (radius / core); // solid-body core
    }
    else
    {
        tangential = settings_.strength * (core / radius); // free vortex
    }

    // Beyond the outer radius the vortex fades instead of influencing the whole
    // map.
    const float outer = std::max(core * 1.5f, settings_.outerRadius);
    if (radius > outer)
    {
        tangential *= std::max(0.f, 1.f - (radius - outer) / (outer * 0.5f));
    }

    const sf::Vector2f radial = delta / radius;
    const sf::Vector2f tangent = Perpendicular(radial) * (settings_.clockWise >= 0.f ? 1.f : -1.f);
    const sf::Vector2f direction = Normalized(tangent + radial * (-settings_.inward), tangent);

    WindSample s;
    s.velocity = direction * tangential;
    s.pressure = 1.f + settings_.updraft * (1.f - std::clamp(radius / outer, 0.f, 1.f));
    s.turbulence = std::clamp(settings_.turbulence * (tangential / std::max(1.f, settings_.strength)), 0.f, 1.f);
    return s;
}

HurricaneWind::HurricaneWind(const Settings& settings)
{
    SetSettings(settings);
}

sf::Vector2f HurricaneWind::EyeCenter(float time) const
{
    return settings_.center + settings_.drift * time;
}

WindSample HurricaneWind::Sample(sf::Vector2f position, float time) const
{
    const sf::Vector2f center = EyeCenter(time);
    const sf::Vector2f delta = position - center;
    const float radius = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (radius < 0.001f)
    {
        WindSample eye;
        eye.pressure = 0.05f;
        return eye;
    }

    const float eye = std::max(1.f, settings_.eyeRadius);
    const float wall = std::max(eye * 1.2f, settings_.eyewallRadius);
    const float outer = std::max(wall * 1.5f, settings_.outerRadius);

    float speed = 0.f;
    if (radius < eye)
    {
        speed = settings_.maxSpeed * 0.12f * (radius / eye); // calm but not dead
    }
    else if (radius < wall)
    {
        speed = settings_.maxSpeed * (0.12f + 0.88f * SmoothStep(eye, wall, radius));
    }
    else
    {
        speed = settings_.maxSpeed * std::pow(wall / radius, 0.75f);
    }

    if (radius > outer)
    {
        speed *= std::max(0.f, 1.f - (radius - outer) / (outer * 0.6f));
    }

    const float theta = std::atan2(delta.y, delta.x);
    const float bandWave = std::sin(settings_.bands * theta + 3.f * std::log(radius / wall + 1.f) - time * 0.8f +
                                    static_cast<float>(settings_.seed) * 0.37f);
    const float band = 1.f + settings_.bandStrength * bandWave;

    const sf::Vector2f radial = delta / radius;
    sf::Vector2f tangent = Perpendicular(radial);
    if (settings_.clockWise < 0.f)
    {
        tangent = -tangent;
    }

    const float inflow = std::tan(settings_.inflowDegrees * kDegToRad);
    const sf::Vector2f direction = Normalized(tangent + radial * (-inflow), tangent);

    WindSample s;
    s.velocity = direction * (speed * std::max(0.2f, band));
    s.pressure = std::clamp(0.15f + 0.9f * (speed / std::max(1.f, settings_.maxSpeed)), 0.05f, 1.5f);
    s.turbulence = std::clamp(settings_.turbulence * (0.4f + 0.6f * std::abs(bandWave)), 0.f, 1.f);
    return s;
}

WindZone::WindZone(sf::FloatRect bounds, std::shared_ptr<WindField> inner, float falloff)
    : bounds_(bounds), inner_(std::move(inner)), falloff_(std::max(1.f, falloff))
{
}

WindSample WindZone::Sample(sf::Vector2f position, float time) const
{
    const float left = bounds_.position.x;
    const float top = bounds_.position.y;
    const float right = left + bounds_.size.x;
    const float bottom = top + bounds_.size.y;

    if (position.x < left - falloff_ || position.x > right + falloff_ || position.y < top - falloff_ ||
        position.y > bottom + falloff_)
    {
        return {};
    }

    // Distance to the rectangle: 0 inside, growing outside.
    const float dx = std::max(left - position.x, position.x - right);
    const float dy = std::max(top - position.y, position.y - bottom);
    const float outside = std::sqrt(std::max(0.f, dx) * std::max(0.f, dx) + std::max(0.f, dy) * std::max(0.f, dy));
    const float weight = 1.f - SmoothStep(0.f, falloff_, outside);
    if (weight <= 0.f || !inner_)
    {
        return {};
    }

    WindSample s = inner_->Sample(position, time);
    s.velocity *= weight;
    s.pressure *= weight;
    s.turbulence *= weight;
    return s;
}

void CompositeWind::Add(std::shared_ptr<WindField> field)
{
    if (field)
    {
        fields_.push_back(std::move(field));
    }
}

WindSample CompositeWind::Sample(sf::Vector2f position, float time) const
{
    WindSample result;
    if (fields_.empty())
    {
        return result;
    }

    float pressureSum = 0.f;
    float turbulenceMax = 0.f;
    for (const auto& field : fields_)
    {
        if (!field)
        {
            continue;
        }
        const WindSample s = field->Sample(position, time);
        result.velocity += s.velocity;
        pressureSum += s.pressure;
        turbulenceMax = std::max(turbulenceMax, s.turbulence);
    }
    result.pressure = pressureSum / static_cast<float>(fields_.size());
    result.turbulence = turbulenceMax;
    return result;
}

Weather MakeWeather(WeatherKind kind, unsigned seed)
{
    Weather weather;
    weather.kind = kind;
    const int noiseSeed = static_cast<int>(seed);

    switch (kind)
    {
        case WeatherKind::Calm:
            weather.wind = std::make_shared<ConstantWind>(sf::Vector2f{1.f, 0.f}, 0.f);
            weather.tint = sf::Color(255, 255, 255);
            break;

        case WeatherKind::Breeze:
        {
            auto gusts = std::make_shared<GustWind>();
            GustWind::Settings s;
            s.direction = Normalized({1.f, -0.25f});
            s.speed = 70.f;
            s.gustStrength = 25.f;
            s.swayDegrees = 12.f;
            s.swayPeriod = 7.f;
            s.turbulence = 0.12f;
            s.seed = noiseSeed;
            gusts->SetSettings(s);
            weather.wind = gusts;
            weather.rain = 0.05f;
            weather.tint = sf::Color(250, 252, 255);
            break;
        }

        case WeatherKind::Storm:
        {
            auto composite = std::make_shared<CompositeWind>();
            auto gusts = std::make_shared<GustWind>();
            GustWind::Settings s;
            s.direction = Normalized({1.f, 0.35f});
            s.speed = 190.f;
            s.gustStrength = 90.f;
            s.swayDegrees = 35.f;
            s.swayPeriod = 3.2f;
            s.turbulence = 0.45f;
            s.seed = noiseSeed;
            gusts->SetSettings(s);
            composite->Add(gusts);

            auto vortex = std::make_shared<VortexWind>();
            VortexWind::Settings v;
            v.center = {-260.f, 120.f};
            v.strength = 90.f;
            v.coreRadius = 90.f;
            v.outerRadius = 520.f;
            v.inward = 0.1f;
            v.updraft = 0.25f;
            v.turbulence = 0.3f;
            v.clockWise = 1.f;
            vortex->SetSettings(v);
            composite->Add(vortex);

            weather.wind = composite;
            weather.rain = 0.75f;
            weather.lightning = 6.f;
            weather.fog = 0.2f;
            weather.temperature = 12.f;
            weather.tint = sf::Color(190, 200, 215);
            break;
        }

        case WeatherKind::Hurricane:
        {
            auto hurricane = std::make_shared<HurricaneWind>();
            HurricaneWind::Settings s;
            s.center = {-300.f, 0.f};
            s.drift = {55.f, -18.f};
            s.eyeRadius = 110.f;
            s.eyewallRadius = 240.f;
            s.outerRadius = 1500.f;
            s.maxSpeed = 430.f;
            s.inflowDegrees = 22.f;
            s.bandStrength = 0.35f;
            s.bands = 5;
            s.turbulence = 0.7f;
            s.seed = noiseSeed;
            hurricane->SetSettings(s);
            weather.wind = hurricane;
            weather.rain = 1.f;
            weather.lightning = 14.f;
            weather.fog = 0.45f;
            weather.temperature = 22.f;
            weather.tint = sf::Color(140, 150, 165);
            break;
        }

        case WeatherKind::Tornado:
        {
            auto composite = std::make_shared<CompositeWind>();
            auto vortex = std::make_shared<VortexWind>();
            VortexWind::Settings v;
            v.center = {200.f, 0.f};
            v.strength = 520.f;
            v.coreRadius = 40.f;
            v.outerRadius = 380.f;
            v.inward = 0.3f;
            v.updraft = 1.f;
            v.turbulence = 0.9f;
            v.drift = {18.f, 0.f};
            vortex->SetSettings(v);
            composite->Add(vortex);

            auto gusts = std::make_shared<GustWind>();
            GustWind::Settings s;
            s.direction = Normalized({0.6f, -0.8f});
            s.speed = 120.f;
            s.gustStrength = 60.f;
            s.swayDegrees = 45.f;
            s.swayPeriod = 2.2f;
            s.turbulence = 0.6f;
            s.seed = noiseSeed + 3;
            gusts->SetSettings(s);
            composite->Add(gusts);

            weather.wind = composite;
            weather.rain = 0.9f;
            weather.lightning = 10.f;
            weather.fog = 0.5f;
            weather.temperature = 19.f;
            weather.tint = sf::Color(120, 125, 135);
            break;
        }
    }
    return weather;
}

const char* WeatherName(WeatherKind kind)
{
    switch (kind)
    {
        case WeatherKind::Calm:
            return "Calm";
        case WeatherKind::Breeze:
            return "Breeze";
        case WeatherKind::Storm:
            return "Storm";
        case WeatherKind::Hurricane:
            return "Hurricane";
        case WeatherKind::Tornado:
            return "Tornado";
    }
    return "Calm";
}

} // namespace Fem
