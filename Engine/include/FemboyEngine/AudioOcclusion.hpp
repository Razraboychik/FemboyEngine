// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Audio/Sound.hpp>
#include <algorithm>

namespace Fem
{

class VisibilityWorld;

struct OcclusionResult
{
    bool blocked = false;
    // Final multiplier: distance attenuation * wall transmission.
    float gain = 1.f;
    float distanceGain = 1.f;
    float transmission = 1.f;
    // 0 = clear, 1 = fully behind a thick wall. SFML's Sound has no filter, so
    // this is applied as a small pitch drop plus extra volume loss; the API is
    // ready for miniaudio's low-pass when that backend lands.
    float muffle = 0.f;
    float pitch = 1.f;
};

// Cheap acoustic occlusion on top of VisibilityWorld.
//
// Full acoustic simulation (diffraction, portals, reverb) is out of scope for a
// 2D CPU engine. What players actually notice is: a sound behind a wall is
// quieter and duller, and one far away is quieter. One ray from listener to
// source gives both, using the same segment set the lightmap uses. That is why
// this module exists separately from AudioSystem: gameplay code owns the wall
// geometry, the audio backend owns the voices.
class AudioOcclusion
{
public:
    static AudioOcclusion& Instance();

    void SetVisibility(const VisibilityWorld* visibility)
    {
        visibility_ = visibility;
    }
    void SetListener(sf::Vector2f position)
    {
        listener_ = position;
    }
    sf::Vector2f Listener() const
    {
        return listener_;
    }
    void SetMaxDistance(float pixels)
    {
        maxDistance_ = pixels > 0.f ? pixels : 1.f;
    }
    float MaxDistance() const
    {
        return maxDistance_;
    }
    // Volume floor for a fully blocked sound. 0.05 is audible through a wall;
    // 0 would make a door sound silent and confusing.
    void SetBlockedGain(float gain)
    {
        blockedGain_ = std::clamp(gain, 0.f, 1.f);
    }

    OcclusionResult Compute(sf::Vector2f source) const;

    // Applies gain and the muffle proxy to a live sound.
    void Apply(sf::Sound& sound, const OcclusionResult& result, float baseVolume) const;

private:
    AudioOcclusion() = default;
    const VisibilityWorld* visibility_ = nullptr;
    sf::Vector2f listener_{0.f, 0.f};
    float maxDistance_ = 1200.f;
    float blockedGain_ = 0.08f;
};

} // namespace Fem
