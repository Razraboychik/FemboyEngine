// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <filesystem>
#include <string>
#include <vector>

#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/Skirt.hpp"
#include "FemboyEngine/Video.hpp"

namespace Fem
{

// Esthete: a replaceable video box.
//
// A cutscene, a death scene or a menu backdrop is described by one small JSON
// file instead of code:
//
//   {
//     "video":   "intro",          // folder of 0000.png frames, or a still
//     "poster":  "intro_idle.png", // shown while loading and when offline
//     "overlay": "scanlines.png",  // optional art drawn on top
//     "fps": 24,
//     "loop": true,
//     "fit": "contain",            // stretch | contain | cover
//     "rect": [0, 0, 1920, 1080],  // optional box in screen pixels
//     "color": [8, 8, 12, 255],    // bare box when nothing else can draw
//     "watch": true                // pick up edits without restarting
//   }
//
// Everything in the box can be swapped while the game runs: `Reload()` rereads
// the JSON, `SetVideo()` / `SetOverlay()` swap a single layer, and `watch` does
// the first automatically when the file changes on disk.
//
// Offline first: a missing or undecodable clip is not an error state that stops
// the game. The poster is drawn instead, and if there is no poster the box is
// filled with its colour. A mandatory scene therefore always has something to
// show, and can never hang waiting for a decoder.
class Esthete
{
public:
    enum class Fit
    {
        Stretch,
        Contain,
        Cover
    };

    // Reads the descriptor and loads its layers. Returns false only when the
    // descriptor itself cannot be read; missing media still yields a usable box.
    bool LoadFromFile(const std::string& configPath);
    bool Reload();

    void Update(float dt);
    void Play();
    void Pause();
    void Stop();
    void SetLoop(bool loop);
    bool Playing() const;
    bool Loaded() const;
    // True when there is no playable clip and the poster (or bare box) is doing
    // the work.
    bool Offline() const
    {
        return !video_.IsLoaded();
    }
    const std::string& Error() const
    {
        return error_;
    }
    const std::string& ConfigPath() const
    {
        return configPath_;
    }

    // Runtime swaps: `video` is a folder of frames or a still image, `overlay`
    // is a still image. Passing an empty path removes that layer.
    bool SetVideo(const std::string& path);
    bool SetOverlay(const std::string& path);
    void SetBox(const sf::FloatRect& box)
    {
        box_ = box;
        hasBox_ = true;
    }
    const sf::FloatRect& Box() const
    {
        return box_;
    }
    // The letterboxed video area inside the box, after `fit` is applied.
    sf::FloatRect VideoRect() const;

    // Submits the box to an engine draw list. Safe to call with no media at all.
    bool Render(Skirt& FrameDrawList, int z = 0, DrawLayer layer = DrawLayer::Overlay);

    VideoPlayer& Player()
    {
        return video_;
    }

private:
    void ApplyJson(const std::string& text);
    void ResolveBox();
    bool UploadFrame();
    bool LoadStill(const std::string& path, const std::string& id, sf::Vector2u& size);
    sf::Vector2f FitScale(const sf::Vector2u& texture, const sf::FloatRect& box) const;

    VideoPlayer video_;
    std::string configPath_;
    std::string baseDir_;
    std::string error_;

    std::string videoPath_;
    std::string posterPath_;
    std::string overlayPath_;
    std::string frameId_;   // AssetManager id of the uploaded current frame
    std::string posterId_;  // AssetManager id of the poster
    std::string overlayId_; // AssetManager id of the overlay

    sf::Vector2u posterSize_{};
    sf::Vector2u overlaySize_{};
    bool hasPoster_ = false;
    bool hasOverlay_ = false;

    sf::FloatRect box_{{0.f, 0.f}, {0.f, 0.f}};
    bool hasBox_ = false;
    sf::Color bareColor_{8, 8, 12, 255};
    Fit fit_ = Fit::Contain;
    bool watch_ = true;

    int uploadedFrame_ = -1;
    int instanceId_ = 0;
    float watchTimer_ = 0.f;
    std::filesystem::file_time_type stamp_{};
    bool hasStamp_ = false;
};

} // namespace Fem
