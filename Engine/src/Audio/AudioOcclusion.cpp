// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/AudioOcclusion.hpp"

#include "FemboyEngine/Visibility.hpp"

#include <algorithm>
#include <cmath>

namespace Fem
{

AudioOcclusion& AudioOcclusion::Instance()
{
    static AudioOcclusion o;
    return o;
}

OcclusionResult AudioOcclusion::Compute(sf::Vector2f source) const
{
    OcclusionResult result;

    const sf::Vector2f delta = source - listener_;
    const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (distance <= 0.001f)
    {
        return result;
    }

    // Distance attenuation: near sources are full volume, far ones fade to the
    // blocked floor. A linear curve is intentionally not realistic; it is the
    // curve a player can predict while dodging.
    const float distanceT = std::clamp(1.f - distance / maxDistance_, 0.f, 1.f);
    result.distanceGain = 0.12f + 0.88f * distanceT;

    if (visibility_ && visibility_->SegmentCount() > 0)
    {
        float absorption = 0.f;
        if (!visibility_->LineOfSight(listener_, source, &absorption))
        {
            result.blocked = true;
            // One wall removes its absorption; a concrete wall (1.0) leaves the
            // blocked floor, a curtain (0.4) still lets most of the sound pass.
            result.transmission = std::max(blockedGain_, 1.f - std::clamp(absorption, 0.f, 1.f));
            result.muffle = std::clamp(absorption, 0.f, 1.f);
        }
    }

    result.gain = std::clamp(result.distanceGain * result.transmission, 0.f, 1.f);
    // A blocked sound is pitched down slightly. It is a stand-in for a low-pass
    // filter and is documented as such; no SFML sound API exposes one.
    result.pitch = 1.f - 0.12f * result.muffle;
    return result;
}

void AudioOcclusion::Apply(sf::Sound& sound, const OcclusionResult& result, float baseVolume) const
{
    sound.setVolume(std::clamp(baseVolume * result.gain, 0.f, 100.f));
    sound.setPitch(std::clamp(result.pitch, 0.5f, 2.f));
}

} // namespace Fem
