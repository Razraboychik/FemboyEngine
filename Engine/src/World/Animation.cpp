// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Animation.hpp"
#include "FemboyEngine/Boy.hpp"

namespace Fem
{

void Animator::AddClip(const AnimClip& clip)
{
    if (clip.name.empty())
    {
        BOY_WARN("Animator", "ignoring clip with empty name");
        return;
    }
    clips_[clip.name] = clip;
}

void Animator::Play(const std::string& name)
{
    auto it = clips_.find(name);
    if (it == clips_.end())
    {
        BOY_WARN("Animator", "unknown clip: " + name);
        return;
    }
    if (currentClip_ == name && !finished_)
    {
        return;
    }

    currentClip_ = name;
    frameIndex_ = 0;
    timer_ = 0.f;
    finished_ = false;
    currentRect_ = it->second.frames.empty() ? sf::IntRect({0, 0}, {0, 0}) : it->second.frames[0].rect;
}

void Animator::Stop()
{
    currentClip_.clear();
    frameIndex_ = 0;
    timer_ = 0.f;
    finished_ = false;
}

void Animator::Update(float dt)
{
    if (currentClip_.empty() || dt <= 0.f)
    {
        return;
    }
    if (!std::isfinite(dt))
    {
        return;
    }

    auto it = clips_.find(currentClip_);
    if (it == clips_.end())
    {
        return;
    }
    const AnimClip& clip = it->second;
    if (clip.frames.empty() || finished_)
    {
        return;
    }

    // frameIndex_ can be stale if the clip was replaced with a shorter one.
    if (frameIndex_ >= clip.frames.size())
    {
        frameIndex_ = 0;
    }

    timer_ += dt * speed_;
    if (!std::isfinite(timer_))
    {
        timer_ = 0.f;
        return;
    }

    // Guard against zero/negative frame durations, which used to divide by zero
    // (infinite loop) or make the timer run backwards.
    size_t guard = 0;
    const size_t maxIterations = clip.frames.size() * 8 + 8;
    while (frameIndex_ < clip.frames.size() && timer_ >= std::max(0.0001f, clip.frames[frameIndex_].duration))
    {
        if (++guard > maxIterations)
        {
            timer_ = 0.f;
            break;
        }

        timer_ -= std::max(0.0001f, clip.frames[frameIndex_].duration);
        ++frameIndex_;
        if (frameIndex_ >= clip.frames.size())
        {
            if (clip.loop)
            {
                frameIndex_ = 0;
            }
            else
            {
                frameIndex_ = clip.frames.size() - 1;
                finished_ = true;
                currentRect_ = clip.frames[frameIndex_].rect;
                break;
            }
        }
        currentRect_ = clip.frames[frameIndex_].rect;
    }
}

} // namespace Fem
