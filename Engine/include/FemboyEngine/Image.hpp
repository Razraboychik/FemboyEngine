// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <SFML/Graphics.hpp>

namespace Fem
{

// Image: the engine's CPU-side RGBA8 bitmap.
//
// This exists so a project never has to include a graphics library to touch
// pixels. Asset generation (procedural textures, icons), screenshot downscaling
// and texture baking all go through this type: it owns the bytes, can save a
// PNG on its own, and can be handed to AssetManager::CreateTexture() to become
// a GPU texture without ever touching the filesystem.
class Image
{
public:
    Image() = default;
    Image(int width, int height, sf::Color fill = sf::Color::Transparent);
    Image(int width, int height, const std::uint8_t* rgba);

    bool Valid() const
    {
        return width_ > 0 && height_ > 0 && pixels_.size() == static_cast<size_t>(width_ * height_ * 4);
    }
    int Width() const
    {
        return width_;
    }
    int Height() const
    {
        return height_;
    }
    // Tightly packed RGBA8, `width * 4` bytes per row, top row first.
    const std::uint8_t* Data() const
    {
        return pixels_.data();
    }
    std::uint8_t* Data()
    {
        return pixels_.data();
    }
    size_t ByteSize() const
    {
        return pixels_.size();
    }

    void Resize(int width, int height, sf::Color fill = sf::Color::Transparent);
    void Fill(sf::Color color);
    void SetPixel(int x, int y, sf::Color color);
    sf::Color Pixel(int x, int y) const;
    // Bilinear sample in normalized [0,1] coordinates; outside the image returns
    // `edge`. Used when baking a larger texture from a small source.
    sf::Color Sample(float u, float v, sf::Color edge = sf::Color::Transparent) const;

    // Encodes to PNG. Returns false when the path cannot be written; the error
    // is reported through Boy, never thrown.
    bool SavePng(const std::string& path) const;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> pixels_;
};

} // namespace Fem
