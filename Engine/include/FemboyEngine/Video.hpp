// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "FemboyEngine/Core.hpp"

namespace Fem
{

// Video player: plays image sequences or (with FFmpeg) video files.
// For full format support, link FFmpeg (libavcodec, libavformat, libswscale).
class VideoPlayer
{
public:
    VideoPlayer();
    ~VideoPlayer();
    bool LoadSequence(const std::string& folder, const std::string& prefix, int frameCount, float fps = 30.f);
    bool LoadFile(const std::string& path); // requires FFmpeg
    void Play();
    void Pause();
    void Stop();
    void Update(float dt);
    void Render(sf::RenderWindow& window, float x, float y, float w, float h);
    bool IsPlaying() const
    {
        return playing_;
    }
    bool IsLoaded() const
    {
        return loaded_;
    }
    float Duration() const
    {
        return duration_;
    }
    float CurrentTime() const
    {
        return currentTime_;
    }
    size_t FrameCount() const
    {
        return frames_.size();
    }
    void SetVolume(float v)
    {
        volume_ = v;
    }
    float Volume() const
    {
        return volume_;
    }
    void SetLoop(bool v)
    {
        loop_ = v;
    }
    void SetOnFinished(std::function<void()> cb)
    {
        onFinished_ = cb;
    }

    // The frame currently on screen. Used by Esthete to upload a frame into the
    // shared draw list, and by tools that want to preview the clip.
    const sf::Texture* CurrentTexture() const
    {
        if (frames_.empty())
        {
            return nullptr;
        }
        const int index = std::clamp(currentFrame_, 0, static_cast<int>(frames_.size()) - 1);
        return frames_[static_cast<size_t>(index)].get();
    }
    int CurrentFrameIndex() const
    {
        return currentFrame_;
    }
    // Replaces the clip with a single already-built texture (procedural scenes).
    void SetSingleFrame(sf::Texture texture);
    // Drops the clip and returns to the empty state.
    void Clear();
    // Frame rate for sequences. Set it before LoadFile() to control playback.
    void SetFps(float fps);
    void Seek(float seconds);

private:
    bool loaded_ = false;
    bool playing_ = false;
    bool loop_ = true;
    float fps_ = 30.f;
    float duration_ = 0;
    float currentTime_ = 0;
    float volume_ = 1.f;
    int currentFrame_ = 0;
    std::vector<std::unique_ptr<sf::Texture>> frames_;
    std::function<void()> onFinished_;
};

} // namespace Fem
