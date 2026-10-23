// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <string>

namespace Fem
{

// Texture optimizer: prepares textures for GPU (mipmaps, compression, format).
class TextureOptimizer
{
public:
    static TextureOptimizer& Instance();
    // Load and optimize a PNG for GPU
    sf::Texture LoadOptimized(const std::string& path, bool generateMipmaps = true);
    // Convert to power-of-two
    static int NextPow2(int v);
    // Premultiply alpha
    static void PremultiplyAlpha(sf::Image& img);
    // Compress (reduce color palette) - simple
    static void ReduceColors(sf::Image& img, int maxColors);

private:
    TextureOptimizer() = default;
};

} // namespace Fem
