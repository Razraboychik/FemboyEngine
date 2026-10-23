// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <map>
#include <functional>

#include "FemboyEngine/FemObject.hpp"

namespace Fem
{

class AudioMixer : public FemObject
{
public:
    FEMCLASS_NO_CREATE(AudioMixer, FemObject, false)
    static AudioMixer& Instance();


    void SetGroupVolume(const std::string& group, float v); // 0..1
    float GroupVolume(const std::string& group) const;
    void SetMasterVolume(float v);
    float MasterVolume() const
    {
        return master_;
    }
    float EffectiveVolume(const std::string& group) const;

    // Called after any volume change. AudioSystem uses it to re-apply volumes to
    // voices that are already playing (previously only master volume did this,
    // so changing a group left existing sounds at the old level).
    void SetChangeCallback(std::function<void()> cb)
    {
        changed_ = std::move(cb);
    }

private:
    AudioMixer() = default;
    void Notify();

    float master_ = 1.f;
    std::map<std::string, float> groups_;
    std::function<void()> changed_;
};

} // namespace Fem
