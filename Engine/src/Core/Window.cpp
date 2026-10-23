// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Window.hpp"
#include "FemboyEngine/Boy.hpp"
#include <algorithm>

namespace Fem
{

namespace
{
constexpr int kMinWindowSize = 1;
constexpr int kMaxWindowSize = 16384;
} // namespace

const char* WindowModeName(WindowMode mode)
{
    switch (mode)
    {
        case WindowMode::Windowed:
            return "Windowed";
        case WindowMode::Borderless:
            return "Borderless";
        case WindowMode::Fullscreen:
            return "Fullscreen";
    }
    return "Windowed";
}

bool Window::Create(const std::string& title, int w, int h, WindowMode mode)
{
    w = std::clamp(w, kMinWindowSize, kMaxWindowSize);
    h = std::clamp(h, kMinWindowSize, kMaxWindowSize);
    mode_ = mode;
    title_ = title;

    sf::VideoMode video({static_cast<unsigned>(w), static_cast<unsigned>(h)});
    std::uint32_t style = sf::Style::Default;
    sf::State state = sf::State::Windowed;

    if (mode == WindowMode::Borderless)
    {
        // Desktop resolution, no decorations, no video-mode switch.
        video = sf::VideoMode::getDesktopMode();
        style = sf::Style::None;
    }
    else if (mode == WindowMode::Fullscreen)
    {
        video = sf::VideoMode::getDesktopMode();
        state = sf::State::Fullscreen;
    }

    BOY_INFO("Window", std::string("creating ") + WindowModeName(mode) + " window: " + title + " " +
                           std::to_string(video.size.x) + "x" + std::to_string(video.size.y));

    try
    {
        // Reuse the same sf::RenderWindow when switching modes. The engine hands
        // a pointer to this object to the renderer, so allocating a new one would
        // leave the renderer drawing into a destroyed window.
        if (!window_)
        {
            window_ = std::make_unique<sf::RenderWindow>();
        }
        sf::ContextSettings context;
        context.antiAliasingLevel = antiAliasingLevel_;
        window_->create(video, title, style, state, context);
    }
    catch (const std::exception& e)
    {
        BOY_ERROR("Window", std::string("window creation threw: ") + e.what());
        window_.reset();
        return false;
    }

    if (!window_ || !window_->isOpen())
    {
        BOY_ERROR("Window", "window could not be opened (no display?)");
        window_.reset();
        return false;
    }

    // The client area is the authority, not the requested size: a borderless
    // window is exactly the desktop, and the OS may clamp a framed window.
    const sf::Vector2u actual = window_->getSize();
    width_ = static_cast<int>(actual.x);
    height_ = static_cast<int>(actual.y);

    if (mode == WindowMode::Borderless)
    {
        window_->setPosition({0, 0});
    }
    else if (mode == WindowMode::Windowed)
    {
        Center();
    }

    window_->setFramerateLimit(60);
    BOY_INFO("Window", "window created: " + std::to_string(width_) + "x" + std::to_string(height_) + " (" +
                           WindowModeName(mode_) + ")");
    return true;
}

bool Window::SetMode(WindowMode mode, int w, int h)
{
    return Create(title_, w, h, mode);
}

void Window::Center()
{
    if (!window_)
    {
        return;
    }
    const sf::VideoMode desktop = sf::VideoMode::getDesktopMode();
    window_->setPosition(
        {static_cast<int>(desktop.size.x / 2) - width_ / 2, static_cast<int>(desktop.size.y / 2) - height_ / 2});
}

void Window::PollEvents()
{
    // Polling is done by the application loop, which needs every event.
}

void Window::Clear(const sf::Color& c)
{
    if (window_)
    {
        window_->clear(c);
    }
}
void Window::Display()
{
    if (window_)
    {
        window_->display();
    }
}
bool Window::IsOpen() const
{
    return window_ && window_->isOpen();
}

void Window::SetSize(int w, int h)
{
    if (w < kMinWindowSize || h < kMinWindowSize)
    {
        return;
    }
    width_ = std::min(w, kMaxWindowSize);
    height_ = std::min(h, kMaxWindowSize);
    if (window_)
    {
        window_->setSize(sf::Vector2u(static_cast<unsigned>(width_), static_cast<unsigned>(height_)));
    }
}

void Window::SetVerticalSync(bool enabled)
{
    if (window_)
    {
        window_->setVerticalSyncEnabled(enabled);
    }
}

void Window::SetFramerateLimit(unsigned fps)
{
    if (window_)
    {
        window_->setFramerateLimit(fps);
    }
}

void Window::SyncSize()
{
    if (!window_)
    {
        return;
    }
    const sf::Vector2u size = window_->getSize();
    width_ = static_cast<int>(size.x);
    height_ = static_cast<int>(size.y);
}

} // namespace Fem
