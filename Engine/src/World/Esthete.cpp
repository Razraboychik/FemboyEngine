// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Esthete.hpp"

#include "FemboyEngine/Assets.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Engine.hpp"
#include "FemboyEngine/Skirt.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>

namespace Fem
{
namespace
{

std::string ResolvePath(const std::string& baseDir, const std::string& path)
{
    if (path.empty())
    {
        return {};
    }
    const std::filesystem::path p(path);
    if (p.is_absolute())
    {
        return p.string();
    }
    if (baseDir.empty())
    {
        return p.string();
    }
    return (std::filesystem::path(baseDir) / p).string();
}

// Every Esthete gets its own asset ids, so two boxes can show two clips and
// swapping one never disturbs the other.
int NextInstanceId()
{
    static int counter = 0;
    return ++counter;
}

bool ReadColor(const nlohmann::json& j, const char* key, sf::Color& out)
{
    if (!j.contains(key) || !j[key].is_array())
    {
        return false;
    }
    const auto& a = j[key];
    if (a.size() < 3)
    {
        return false;
    }
    const auto channel = [&](size_t i, std::uint8_t def) -> std::uint8_t
    {
        if (i >= a.size() || !a[i].is_number())
        {
            return def;
        }
        return static_cast<std::uint8_t>(std::clamp(a[i].get<double>(), 0.0, 255.0));
    };
    out = sf::Color(channel(0, 0), channel(1, 0), channel(2, 0), channel(3, 255));
    return true;
}

bool ReadRect(const nlohmann::json& j, const char* key, sf::FloatRect& out)
{
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 4)
    {
        return false;
    }
    const auto& a = j[key];
    for (size_t i = 0; i < 4; ++i)
    {
        if (!a[i].is_number())
        {
            return false;
        }
    }
    out = sf::FloatRect({a[0].get<float>(), a[1].get<float>()},
                        {std::max(0.f, a[2].get<float>()), std::max(0.f, a[3].get<float>())});
    return true;
}

} // namespace

bool Esthete::LoadFromFile(const std::string& configPath)
{
    configPath_ = configPath;
    error_.clear();

    std::ifstream file(configPath);
    if (!file.is_open())
    {
        error_ = "cannot open " + configPath;
        BOY_WARN("Esthete", error_);
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ApplyJson(text);

    std::error_code ec;
    stamp_ = std::filesystem::last_write_time(configPath, ec);
    hasStamp_ = !ec;
    return true;
}

void Esthete::ApplyJson(const std::string& text)
{
    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(text);
    }
    catch (const std::exception& e)
    {
        error_ = std::string("descriptor parse error: ") + e.what();
        BOY_WARN("Esthete", error_);
        return;
    }

    if (instanceId_ == 0)
    {
        instanceId_ = NextInstanceId();
    }
    if (frameId_.empty())
    {
        frameId_ = "esthete." + std::to_string(instanceId_) + ".frame";
        posterId_ = "esthete." + std::to_string(instanceId_) + ".poster";
        overlayId_ = "esthete." + std::to_string(instanceId_) + ".overlay";
    }

    const std::filesystem::path config(configPath_);
    baseDir_ = config.has_parent_path() ? config.parent_path().string() : std::string();

    watch_ = j.value("watch", true);
    video_.SetLoop(j.value("loop", true));
    video_.SetVolume(j.value("volume", 1.f));

    if (j.contains("rect"))
    {
        hasBox_ = ReadRect(j, "rect", box_);
    }
    ReadColor(j, "color", bareColor_);

    const std::string fit = j.value("fit", std::string("contain"));
    if (fit == "stretch")
    {
        fit_ = Fit::Stretch;
    }
    else if (fit == "cover")
    {
        fit_ = Fit::Cover;
    }
    else
    {
        fit_ = Fit::Contain;
    }

    // Video
    const std::string videoPath = ResolvePath(baseDir_, j.value("video", std::string()));
    if (!videoPath.empty())
    {
        videoPath_ = videoPath;
        video_.Stop();
        // The clip's frame rate is part of the descriptor: a 12 fps storyboard
        // and a 24 fps cutscene live in the same format.
        video_.SetFps(j.value("fps", 24.f));
        if (!video_.LoadFile(videoPath))
        {
            // A missing clip is not fatal: the poster (or the bare box) stands in.
            BOY_WARN("Esthete", "no playable clip at " + videoPath + "; falling back to the poster");
        }
        uploadedFrame_ = -1;
    }
    else
    {
        // A descriptor without a clip means "poster only"; do not keep the
        // previous instance's clip alive across a reload.
        video_.Clear();
        uploadedFrame_ = -1;
    }

    // Poster
    const std::string posterPath = ResolvePath(baseDir_, j.value("poster", std::string()));
    hasPoster_ = !posterPath.empty() && LoadStill(posterPath, posterId_, posterSize_);
    posterPath_ = posterPath;

    // Overlay
    const std::string overlayPath = ResolvePath(baseDir_, j.value("overlay", std::string()));
    hasOverlay_ = !overlayPath.empty() && LoadStill(overlayPath, overlayId_, overlaySize_);
    overlayPath_ = overlayPath;

    ResolveBox();
}

bool Esthete::Reload()
{
    if (configPath_.empty())
    {
        return false;
    }
    const bool wasPlaying = video_.IsPlaying();
    const float time = video_.CurrentTime();
    const bool ok = LoadFromFile(configPath_);
    if (wasPlaying)
    {
        video_.Play();
    }
    // Keep the playhead across a swap only if the clip looks unchanged.
    if (time > 0.f && time < video_.Duration())
    {
        video_.Seek(time);
    }
    return ok;
}

void Esthete::Update(float dt)
{
    video_.Update(dt);

    if (!watch_ || configPath_.empty())
    {
        return;
    }
    watchTimer_ -= dt;
    if (watchTimer_ > 0.f)
    {
        return;
    }
    watchTimer_ = 0.5f;

    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(configPath_, ec);
    if (ec || !hasStamp_)
    {
        return;
    }
    if (stamp != stamp_)
    {
        // The descriptor changed on disk: this is the "swap without editing
        // code" path, and it happens while the scene keeps running.
        BOY_INFO("Esthete", "descriptor changed, reloading: " + configPath_);
        Reload();
    }
}

void Esthete::Play()
{
    if (!video_.IsLoaded() && !posterPath_.empty())
    {
        // With no clip the poster is the scene: nothing to play, but the call is
        // still valid so a caller does not need to branch.
        return;
    }
    video_.Play();
}

void Esthete::Pause()
{
    video_.Pause();
}

void Esthete::Stop()
{
    video_.Stop();
}

void Esthete::SetLoop(bool loop)
{
    video_.SetLoop(loop);
}

bool Esthete::Playing() const
{
    return video_.IsPlaying();
}

bool Esthete::Loaded() const
{
    return video_.IsLoaded() || hasPoster_ || hasOverlay_;
}

bool Esthete::SetVideo(const std::string& path)
{
    videoPath_ = ResolvePath(baseDir_, path);
    video_.Stop();
    uploadedFrame_ = -1;
    if (videoPath_.empty())
    {
        video_.Clear();
        return true;
    }
    return video_.LoadFile(videoPath_);
}

bool Esthete::SetOverlay(const std::string& path)
{
    overlayPath_ = ResolvePath(baseDir_, path);
    hasOverlay_ = !overlayPath_.empty() && LoadStill(overlayPath_, overlayId_, overlaySize_);
    return hasOverlay_ || overlayPath_.empty();
}

bool Esthete::LoadStill(const std::string& path, const std::string& id, sf::Vector2u& size)
{
    sf::Image image;
    if (!image.loadFromFile(path))
    {
        BOY_WARN("Esthete", "could not load still: " + path);
        return false;
    }
    const sf::Vector2u imageSize = image.getSize();
    if (imageSize.x == 0 || imageSize.y == 0)
    {
        return false;
    }
    if (!AssetManager::Instance().CreateTexture(id, imageSize.x, imageSize.y, image.getPixelsPtr()))
    {
        return false;
    }
    size = imageSize;
    return true;
}

void Esthete::ResolveBox()
{
    if (hasBox_ && box_.size.x > 0.f && box_.size.y > 0.f)
    {
        return;
    }
    const sf::Vector2u window = Engine::Instance().WindowSize();
    box_ = sf::FloatRect({0.f, 0.f}, {static_cast<float>(window.x), static_cast<float>(window.y)});
}

sf::Vector2f Esthete::FitScale(const sf::Vector2u& texture, const sf::FloatRect& box) const
{
    if (texture.x == 0 || texture.y == 0 || box.size.x <= 0.f || box.size.y <= 0.f)
    {
        return {1.f, 1.f};
    }
    const float sx = box.size.x / static_cast<float>(texture.x);
    const float sy = box.size.y / static_cast<float>(texture.y);
    switch (fit_)
    {
        case Fit::Stretch:
            return {sx, sy};
        case Fit::Cover:
            return {std::max(sx, sy), std::max(sx, sy)};
        case Fit::Contain:
        default:
            return {std::min(sx, sy), std::min(sx, sy)};
    }
}

sf::FloatRect Esthete::VideoRect() const
{
    sf::FloatRect box = box_;
    if (box.size.x <= 0.f || box.size.y <= 0.f)
    {
        return box;
    }

    sf::Vector2u size{};
    if (const sf::Texture* texture = video_.CurrentTexture())
    {
        size = texture->getSize();
    }
    else if (hasPoster_)
    {
        size = posterSize_;
    }
    if (size.x == 0 || size.y == 0)
    {
        return box;
    }

    const sf::Vector2f scale = FitScale(size, box);
    const sf::Vector2f drawn{static_cast<float>(size.x) * scale.x, static_cast<float>(size.y) * scale.y};
    return sf::FloatRect(
        {box.position.x + (box.size.x - drawn.x) * 0.5f, box.position.y + (box.size.y - drawn.y) * 0.5f}, drawn);
}

bool Esthete::UploadFrame()
{
    if (!video_.IsLoaded())
    {
        return false;
    }
    const sf::Texture* texture = video_.CurrentTexture();
    if (!texture)
    {
        return false;
    }

    const int index = video_.CurrentFrameIndex();
    if (index == uploadedFrame_ && AssetManager::Instance().HasTexture(frameId_))
    {
        return true;
    }

    // A frame is copied once, on the frame it changes, into a shared texture the
    // draw list can reference like any other asset. A cutscene does not need to
    // be on the hot path, and this keeps Esthete compatible with the single
    // sorted draw list instead of adding a second rendering path.
    const sf::Image image = texture->copyToImage();
    const sf::Vector2u size = image.getSize();
    if (size.x == 0 || size.y == 0)
    {
        return false;
    }
    if (!AssetManager::Instance().CreateTexture(frameId_, size.x, size.y, image.getPixelsPtr()))
    {
        return false;
    }
    uploadedFrame_ = index;
    return true;
}

bool Esthete::Render(Skirt& FrameDrawList, int z, DrawLayer layer)
{
    ResolveBox();
    if (box_.size.x <= 0.f || box_.size.y <= 0.f)
    {
        return false;
    }

    // The bare box is always painted: it is the offline floor of the whole
    // feature, so a mandatory scene never shows the previous frame of gameplay.
    FrameDrawList.Rect(box_, bareColor_, sf::Color::Transparent, DrawCommand::kNoThickness, z++, layer);

    if (video_.IsLoaded())
    {
        if (UploadFrame())
        {
            if (const sf::Texture* texture = video_.CurrentTexture())
            {
                const sf::Vector2f scale = FitScale(texture->getSize(), box_);
                const sf::FloatRect area = VideoRect();
                if (fit_ == Fit::Cover)
                {
                    FrameDrawList.SetClip(box_);
                }
                FrameDrawList.Sprite(frameId_, area.position, scale, 0.f, sf::Color::White, z++, layer, sf::FloatRect{},
                             sf::Vector2f(0.f, 0.f));
                if (fit_ == Fit::Cover)
                {
                    FrameDrawList.ClearClip();
                }
            }
        }
    }
    else if (hasPoster_)
    {
        const sf::Vector2f scale = FitScale(posterSize_, box_);
        const sf::FloatRect area = VideoRect();
        if (fit_ == Fit::Cover)
        {
            FrameDrawList.SetClip(box_);
        }
        FrameDrawList.Sprite(posterId_, area.position, scale, 0.f, sf::Color::White, z++, layer, sf::FloatRect{},
                     sf::Vector2f(0.f, 0.f));
        if (fit_ == Fit::Cover)
        {
            FrameDrawList.ClearClip();
        }
    }

    if (hasOverlay_ && overlaySize_.x > 0 && overlaySize_.y > 0)
    {
        const sf::Vector2f scale(box_.size.x / static_cast<float>(overlaySize_.x),
                                 box_.size.y / static_cast<float>(overlaySize_.y));
        FrameDrawList.Sprite(overlayId_, box_.position, scale, 0.f, sf::Color::White, z++, layer, sf::FloatRect{},
                     sf::Vector2f(0.f, 0.f));
    }
    return true;
}

} // namespace Fem
