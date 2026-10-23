// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace Fem
{

// Pre-rendered rotation frames for pixel-art sprites.
//
// Rotating a nearest-filtered sprite at runtime gives the classic "wobbly
// pixels" look: a 16x16 sword turns into a different, mushier silhouette at
// every angle. RotSprite/rotation atlases solve that by rasterising N discrete
// angles once and treating the result as an animation. This class bakes those
// frames into one atlas texture and returns the frame for an angle.
//
// Baking needs a live OpenGL context (sf::RenderTexture); calling frame() before
// the window exists returns the original texture, unrotated, so headless tools
// and unit tests still work.
class RotationAtlas
{
public:
    struct Frame
    {
        const sf::Texture* texture = nullptr;
        sf::FloatRect source{};        // sub-rect inside the atlas
        sf::Vector2f origin{0.f, 0.f}; // pivot as a fraction of `source`
        bool baked = false;
    };

    static RotationAtlas& Instance();

    // Number of pre-rendered angles. 16 is the usual pixel-art choice; 32 is
    // noticeably smoother for large sprites, 8 is enough for a spinning coin.
    void SetAngleCount(int angles);
    int AngleCount() const
    {
        return angles_;
    }
    // Nearest angle index for a rotation in degrees. Pure function, testable
    // without a GPU.
    static int FrameIndex(float degrees, int angles);

    // Returns (and lazily bakes) the atlas frame for `source` inside `textureId`.
    // `angles` <= 0 uses the instance-wide angle count.
    Frame MakeFrame(const std::string& textureId, const sf::Texture& texture, const sf::FloatRect& source,
                    float degrees, int angles = 0);

    void Clear();
    size_t AtlasCount() const
    {
        return atlases_.size();
    }

private:
    RotationAtlas() = default;

    struct Atlas
    {
        sf::Texture texture;
        int angles = 16;
        int cell = 0;
        std::vector<sf::FloatRect> frames;
    };

    bool Bake(Atlas& atlas, const sf::Texture& sourceTexture, const sf::FloatRect& source, int angles);

    int angles_ = 16;
    std::unordered_map<std::string, Atlas> atlases_;
};

} // namespace Fem
