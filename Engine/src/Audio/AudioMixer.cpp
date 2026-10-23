// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/AudioMixer.hpp"
#include <algorithm>

namespace Fem
{

AudioMixer& AudioMixer::Instance()
{
    static AudioMixer a;
    return a;
}

void AudioMixer::SetGroupVolume(const std::string& group, float v)
{
    v = std::clamp(v, 0.f, 1.f);
    const auto it = groups_.find(group);
    if (it != groups_.end() && it->second == v)
    {
        return;
    }
    groups_[group] = v;
    Notify();
}

float AudioMixer::GroupVolume(const std::string& group) const
{
    auto it = groups_.find(group);
    if (it == groups_.end())
    {
        return 1.f;
    }
    return it->second;
}

void AudioMixer::SetMasterVolume(float v)
{
    v = std::clamp(v, 0.f, 1.f);
    if (master_ == v)
    {
        return;
    }
    master_ = v;
    Notify();
}

void AudioMixer::Notify()
{
    if (changed_)
    {
        changed_();
    }
}

float AudioMixer::EffectiveVolume(const std::string& group) const
{
    return master_ * GroupVolume(group);
}

} // namespace Fem
