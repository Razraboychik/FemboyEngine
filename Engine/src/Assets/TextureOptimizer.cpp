// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/TextureOptimizer.hpp"
#include "FemboyEngine/Boy.hpp"
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace Fem
{

TextureOptimizer& TextureOptimizer::Instance()
{
    static TextureOptimizer t;
    return t;
}

sf::Texture TextureOptimizer::LoadOptimized(const std::string& path, bool generateMipmaps)
{
    sf::Image img;
    if (!img.loadFromFile(path))
    {
        BOY_ERROR("TextureOptimizer", "failed to load: " + path);
        return sf::Texture();
    }

    PremultiplyAlpha(img);

    sf::Texture tex;
    if (!tex.loadFromImage(img))
    {
        BOY_ERROR("TextureOptimizer", "failed to create texture: " + path);
        return sf::Texture();
    }
    if (generateMipmaps)
    {
        (void)tex.generateMipmap();
    }
    tex.setSmooth(true);
    BOY_INFO("TextureOptimizer", "optimized: " + path);
    return tex;
}

int TextureOptimizer::NextPow2(int v)
{
    if (v <= 1)
    {
        return 1;
    }
    // Saturate instead of overflowing into a negative value.
    int r = 1;
    while (r < v)
    {
        if (r > (1 << 30))
        {
            return 1 << 30;
        }
        r <<= 1;
    }
    return r;
}

void TextureOptimizer::PremultiplyAlpha(sf::Image& img)
{
    const sf::Vector2u sz = img.getSize();
    for (unsigned y = 0; y < sz.y; ++y)
    {
        for (unsigned x = 0; x < sz.x; ++x)
        {
            sf::Color c = img.getPixel({x, y});
            if (c.a == 255)
            {
                continue;
            }
            c.r = static_cast<std::uint8_t>((c.r * c.a + 127) / 255);
            c.g = static_cast<std::uint8_t>((c.g * c.a + 127) / 255);
            c.b = static_cast<std::uint8_t>((c.b * c.a + 127) / 255);
            img.setPixel({x, y}, c);
        }
    }
}

void TextureOptimizer::ReduceColors(sf::Image& img, int maxColors)
{
    // Fewer than two colors cannot be represented by simple quantization; clamp
    // instead of dividing by zero.
    maxColors = std::clamp(maxColors, 2, 256);
    const int levels = maxColors; // distinct values per channel
    const int step = std::max(1, 256 / levels);

    const sf::Vector2u sz = img.getSize();
    for (unsigned y = 0; y < sz.y; ++y)
    {
        for (unsigned x = 0; x < sz.x; ++x)
        {
            sf::Color c = img.getPixel({x, y});
            // Quantize to the nearest level so colors do not drift darker.
            c.r = static_cast<std::uint8_t>(std::min(255, ((c.r + step / 2) / step) * step));
            c.g = static_cast<std::uint8_t>(std::min(255, ((c.g + step / 2) / step) * step));
            c.b = static_cast<std::uint8_t>(std::min(255, ((c.b + step / 2) / step) * step));
            img.setPixel({x, y}, c);
        }
    }
}

} // namespace Fem
