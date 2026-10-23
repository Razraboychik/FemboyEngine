// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <map>

namespace Fem
{

struct AnimFrame
{
    sf::IntRect rect;
    float duration = 0.1f; // seconds
};

struct AnimClip
{
    std::string name;
    std::vector<AnimFrame> frames;
    bool loop = true;
};

class Animator
{
public:
    void AddClip(const AnimClip& clip);
    void Play(const std::string& name);
    void Update(float dt);
    void Stop();
    void SetSpeed(float speed)
    {
        speed_ = speed;
    }
    float Speed() const
    {
        return speed_;
    }
    const sf::IntRect& CurrentRect() const
    {
        return currentRect_;
    }
    size_t CurrentFrame() const
    {
        return frameIndex_;
    }
    bool IsFinished() const
    {
        return finished_;
    }
    const std::string& CurrentClip() const
    {
        return currentClip_;
    }

private:
    std::map<std::string, AnimClip> clips_;
    std::string currentClip_;
    size_t frameIndex_ = 0;
    float timer_ = 0.f;
    float speed_ = 1.f;
    bool finished_ = false;
    sf::IntRect currentRect_{{0, 0}, {0, 0}};
};

} // namespace Fem
