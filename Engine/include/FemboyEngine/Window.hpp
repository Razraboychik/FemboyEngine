// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <algorithm>
#include <string>
#include <SFML/Graphics.hpp>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/FemObject.hpp"

namespace Fem
{

// How the engine's window occupies the display.
//
//   Windowed   - a normal framed window of the requested client size.
//   Borderless - no frame, covers the whole desktop at the desktop resolution.
//                This is what most games mean by "fullscreen window": alt-tab
//                is instant, the compositor keeps working, and there is no
//                video-mode switch that can leave a monitor black.
//   Fullscreen - exclusive fullscreen, the video mode is switched.
//
// Borderless is the default for Engine::Initialize because it is the mode that
// behaves identically on every machine. A project can still ask for Windowed.
enum class WindowMode
{
    Windowed,
    Borderless,
    Fullscreen
};

const char* WindowModeName(WindowMode mode);

class Window : public FemObject
{
public:
    FEMCLASS_NO_CREATE(Window, FemObject, false)

    bool Create(const std::string& title, int w, int h, WindowMode mode = WindowMode::Windowed);
    void PollEvents();
    void Clear(const sf::Color& c = sf::Color(20, 24, 32));
    void Display();
    bool IsOpen() const;
    // Only valid while isOpen() is true.
    sf::RenderWindow& Handle()
    {
        return *window_;
    }
    const sf::RenderWindow& Handle() const
    {
        return *window_;
    }
    int Width() const
    {
        return width_;
    }
    int Height() const
    {
        return height_;
    }
    WindowMode Mode() const
    {
        return mode_;
    }
    // True when the window is borderless or exclusive fullscreen.
    bool IsFullscreen() const
    {
        return mode_ != WindowMode::Windowed;
    }
    // MSAA is a context-creation property. Setting it takes effect on the next
    // Create()/SetMode(); Engine::RequestWindowMode does that at a safe point,
    // after GPU resources are reloaded.
    void SetAntiAliasingLevel(int samples)
    {
        antiAliasingLevel_ = static_cast<unsigned>(std::clamp(samples, 0, 8));
    }
    int AntiAliasingLevel() const
    {
        return static_cast<int>(antiAliasingLevel_);
    }
    void SetSize(int w, int h);
    // Frame pacing. Kept on Window so a game never calls the RenderWindow
    // directly; both are no-ops while the window is closed.
    void SetVerticalSync(bool enabled);
    void SetFramerateLimit(unsigned fps);
    // Refreshes the cached client size from the OS without touching the window.
    // The live-resize pump runs while the event loop is parked, so the Resized
    // event that normally updates the cache has not been delivered yet.
    void SyncSize();
    // Recreates the window with another mode. This destroys and recreates the
    // OpenGL context, so every GPU resource (textures, render textures, fonts
    // uploaded to the GPU) has to be reloaded afterwards. Engine::RequestWindowMode
    // defers this to a safe point in the frame.
    bool SetMode(WindowMode mode, int w, int h);
    void Center();

private:
    std::unique_ptr<sf::RenderWindow> window_;
    std::string title_;
    int width_ = 0, height_ = 0;
    WindowMode mode_ = WindowMode::Windowed;
    unsigned antiAliasingLevel_ = 0;
};

} // namespace Fem
