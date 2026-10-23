// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Video.hpp"
#include "FemboyEngine/Boy.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace Fem
{

VideoPlayer::VideoPlayer() = default;
VideoPlayer::~VideoPlayer() = default;

bool VideoPlayer::LoadSequence(const std::string& folder, const std::string& prefix, int frameCount, float fps)
{
    Stop();
    frames_.clear();
    loaded_ = false;
    duration_ = 0.f;
    currentTime_ = 0.f;
    currentFrame_ = 0;

    if (frameCount <= 0)
    {
        BOY_ERROR("Video", "frameCount must be positive");
        return false;
    }
    // Guard against a nonsensical fps that would divide by zero below.
    fps_ = (fps > 0.0001f) ? fps : 30.f;

    frames_.reserve(static_cast<size_t>(frameCount));
    for (int i = 0; i < frameCount; ++i)
    {
        char buf[1024];
        const int written = std::snprintf(buf, sizeof(buf), "%s/%s%04d.png", folder.c_str(), prefix.c_str(), i);
        if (written <= 0 || written >= static_cast<int>(sizeof(buf)))
        {
            BOY_WARN("Video", "frame path too long");
            break;
        }

        auto t = std::make_unique<sf::Texture>();
        if (!t->loadFromFile(buf))
        {
            BOY_WARN("Video", "missing frame: " + std::string(buf));
            break;
        }
        frames_.push_back(std::move(t));
    }

    if (frames_.empty())
    {
        BOY_ERROR("Video", "no frames loaded from: " + folder);
        return false;
    }

    loaded_ = true;
    duration_ = static_cast<float>(frames_.size()) / fps_;
    BOY_INFO("Video", "loaded sequence: " + std::to_string(frames_.size()) + " frames");
    return true;
}

bool VideoPlayer::LoadFile(const std::string& path)
{
    // True video containers (MP4/WebM/AVI) need FFmpeg, which is not vendored.
    // Instead of failing blindly, accept the formats the engine can decode with
    // no external dependency: a directory of numbered PNG frames and a single
    // still image (a one-frame clip).
    namespace fs = std::filesystem;

    std::error_code ec;
    if (fs::is_directory(path, ec))
    {
        // Find the first frame, derive its numeric prefix and starting index,
        // then count upwards. Numbering does not have to start at zero.
        std::string prefix;
        int firstIndex = 0;
        for (const auto& entry : fs::directory_iterator(path, ec))
        {
            if (ec)
            {
                break;
            }
            if (!entry.is_regular_file())
            {
                continue;
            }
            const std::string stem = entry.path().stem().string();
            const size_t digit = stem.find_first_of("0123456789");
            if (digit == std::string::npos)
            {
                continue;
            }
            prefix = stem.substr(0, digit);
            try
            {
                firstIndex = std::stoi(stem.substr(digit));
            }
            catch (const std::exception&)
            {
                firstIndex = 0;
            }
            break;
        }

        if (prefix.empty())
        {
            BOY_ERROR("Video", "no numbered frames in directory: " + path);
            return false;
        }

        int count = 0;
        for (;; ++count)
        {
            char probe[1024];
            const int written =
                std::snprintf(probe, sizeof(probe), "%s/%s%04d.png", path.c_str(), prefix.c_str(), firstIndex + count);
            if (written <= 0 || written >= static_cast<int>(sizeof(probe)))
            {
                break;
            }
            if (!fs::exists(probe, ec))
            {
                break;
            }
        }

        if (count == 0)
        {
            BOY_ERROR("Video", "directory has a frame prefix but no frames: " + path);
            return false;
        }

        // loadSequence() always starts numbering at zero, so feed it a
        // zero-based count and rely on the contiguous frame run it validated.
        return LoadSequence(path, prefix, count, fps_);
    }

    if (fs::is_regular_file(path, ec))
    {
        const std::string ext = fs::path(path).extension().string();
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
        {
            Stop();
            frames_.clear();
            loaded_ = false;

            auto t = std::make_unique<sf::Texture>();
            if (!t->loadFromFile(path))
            {
                BOY_ERROR("Video", "could not load image: " + path);
                return false;
            }
            frames_.push_back(std::move(t));
            loaded_ = true;
            duration_ = 1.f / ((fps_ > 0.0001f) ? fps_ : 30.f);
            currentTime_ = 0.f;
            currentFrame_ = 0;
            BOY_INFO("Video", "loaded still image as a one-frame clip: " + path);
            return true;
        }

        BOY_WARN("Video", "container format needs FFmpeg (not linked): " + path +
                              "; supply a PNG sequence or a still image instead");
        return false;
    }

    BOY_ERROR("Video", "path does not exist: " + path);
    return false;
}

void VideoPlayer::Play()
{
    if (!loaded_)
    {
        return;
    }
    // Replaying a finished non-looping clip starts from the beginning.
    if (!loop_ && currentFrame_ >= static_cast<int>(frames_.size()) - 1 && currentTime_ >= duration_)
    {
        currentTime_ = 0.f;
        currentFrame_ = 0;
    }
    playing_ = true;
}

void VideoPlayer::Pause()
{
    playing_ = false;
}

void VideoPlayer::Stop()
{
    playing_ = false;
    currentTime_ = 0.f;
    currentFrame_ = 0;
}

void VideoPlayer::Update(float dt)
{
    if (!playing_ || !loaded_ || dt < 0.f)
    {
        return;
    }

    currentTime_ += dt;
    const int lastFrame = static_cast<int>(frames_.size()) - 1;
    currentFrame_ = static_cast<int>(currentTime_ * fps_);

    if (currentFrame_ > lastFrame)
    {
        if (loop_)
        {
            currentTime_ = 0.f;
            currentFrame_ = 0;
        }
        else
        {
            playing_ = false;
            currentFrame_ = lastFrame;
            currentTime_ = duration_;
            // Fire the finished callback. `playing_` is already false, so this
            // runs exactly once per playback; clearing the callback here would
            // break replay(), which should notify again.
            auto cb = onFinished_;
            if (cb)
            {
                cb();
            }
        }
    }
}

void VideoPlayer::Clear()
{
    Stop();
    frames_.clear();
    loaded_ = false;
    duration_ = 0.f;
    currentTime_ = 0.f;
    currentFrame_ = 0;
    fps_ = 30.f;
}

void VideoPlayer::SetFps(float fps)
{
    fps_ = (fps > 0.0001f) ? fps : 30.f;
    if (loaded_ && !frames_.empty())
    {
        duration_ = static_cast<float>(frames_.size()) / fps_;
    }
}

void VideoPlayer::Seek(float seconds)
{
    if (!loaded_ || frames_.empty())
    {
        return;
    }
    duration_ = static_cast<float>(frames_.size()) / fps_;
    currentTime_ = std::clamp(seconds, 0.f, std::max(0.f, duration_ - 0.0001f));
    currentFrame_ = std::clamp(static_cast<int>(currentTime_ * fps_), 0, static_cast<int>(frames_.size()) - 1);
}

void VideoPlayer::SetSingleFrame(sf::Texture texture)
{
    Stop();
    frames_.clear();
    frames_.push_back(std::make_unique<sf::Texture>(std::move(texture)));
    loaded_ = true;
    fps_ = 30.f;
    duration_ = 1.f / fps_;
    currentTime_ = 0.f;
    currentFrame_ = 0;
}

void VideoPlayer::Render(sf::RenderWindow& window, float x, float y, float w, float h)
{
    if (!loaded_ || frames_.empty())
    {
        return;
    }

    const int idx = std::clamp(currentFrame_, 0, static_cast<int>(frames_.size()) - 1);
    const sf::Texture& tex = *frames_[static_cast<size_t>(idx)];
    sf::Sprite sp(tex);
    sp.setPosition({x, y});

    const sf::Vector2u sz = tex.getSize();
    if (sz.x > 0 && sz.y > 0)
    {
        sp.setScale({w / static_cast<float>(sz.x), h / static_cast<float>(sz.y)});
    }
    window.draw(sp);
}

} // namespace Fem
