// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Image.hpp"
#include "FemboyEngine/Boy.hpp"
#include <algorithm>
#include <cmath>

// The engine owns the only STB implementation TU, so projects link PNG output
// without adding their own third-party source. STB_IMAGE_WRITE_STATIC keeps the
// globals local to this file: a static SFML build compiles the same stb symbols
// into sfml-graphics-s.lib, and two external definitions of
// stbi_write_png_compression_level would fail the link.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace Fem
{

Image::Image(int width, int height, sf::Color fill)
{
    Resize(width, height, fill);
}

Image::Image(int width, int height, const std::uint8_t* rgba)
{
    Resize(width, height);
    if (rgba && !pixels_.empty())
    {
        std::copy(rgba, rgba + pixels_.size(), pixels_.begin());
    }
}

void Image::Resize(int width, int height, sf::Color fill)
{
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    pixels_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4, 0);
    if (fill != sf::Color::Transparent || fill.a != 0)
    {
        this->Fill(fill);
    }
}

void Image::Fill(sf::Color color)
{
    for (size_t i = 0; i + 3 < pixels_.size(); i += 4)
    {
        pixels_[i] = color.r;
        pixels_[i + 1] = color.g;
        pixels_[i + 2] = color.b;
        pixels_[i + 3] = color.a;
    }
}

void Image::SetPixel(int x, int y, sf::Color color)
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
    {
        return;
    }
    const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)) * 4;
    pixels_[i] = color.r;
    pixels_[i + 1] = color.g;
    pixels_[i + 2] = color.b;
    pixels_[i + 3] = color.a;
}

sf::Color Image::Pixel(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
    {
        return sf::Color::Transparent;
    }
    const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)) * 4;
    return sf::Color(pixels_[i], pixels_[i + 1], pixels_[i + 2], pixels_[i + 3]);
}

sf::Color Image::Sample(float u, float v, sf::Color edge) const
{
    if (!Valid())
    {
        return edge;
    }
    if (u < 0.f || v < 0.f || u > 1.f || v > 1.f)
    {
        return edge;
    }

    const float fx = std::clamp(u * static_cast<float>(width_ - 1), 0.f, static_cast<float>(width_ - 1));
    const float fy = std::clamp(v * static_cast<float>(height_ - 1), 0.f, static_cast<float>(height_ - 1));
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, width_ - 1);
    const int y1 = std::min(y0 + 1, height_ - 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);

    const sf::Color c00 = Pixel(x0, y0);
    const sf::Color c10 = Pixel(x1, y0);
    const sf::Color c01 = Pixel(x0, y1);
    const sf::Color c11 = Pixel(x1, y1);
    const auto mix = [](std::uint8_t a, std::uint8_t b, float t)
    { return static_cast<std::uint8_t>(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t); };
    const auto blend = [&](std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d)
    { return mix(mix(a, b, tx), mix(c, d, tx), ty); };
    return sf::Color(blend(c00.r, c10.r, c01.r, c11.r), blend(c00.g, c10.g, c01.g, c11.g),
                     blend(c00.b, c10.b, c01.b, c11.b), blend(c00.a, c10.a, c01.a, c11.a));
}

bool Image::SavePng(const std::string& path) const
{
    if (!Valid())
    {
        BOY_ERROR("Image", "cannot save an empty image to " + path);
        return false;
    }
    const int ok = stbi_write_png(path.c_str(), width_, height_, 4, pixels_.data(), width_ * 4);
    if (!ok)
    {
        BOY_ERROR("Image", "PNG encoding failed for " + path);
        return false;
    }
    return true;
}

} // namespace Fem
