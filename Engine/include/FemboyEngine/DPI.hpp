// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
//
// Design-space UI scaling.
//
// Games normally lay their interface out with fixed pixel values ("a 40px
// button, a 15% margin"). That breaks the moment the window is not the size the
// layout was authored for: on a 2560x1600 display a 520px menu panel is a stamp
// in the middle of the screen, and a layout that assumed 720 pixels of height
// simply falls off the bottom.
//
// Unity and Unreal solve this with a reference resolution and a scale curve.
// FemboyEngine keeps the same idea, but small and explicit: a project declares
// the resolution it designed against (referenceHeight), the engine computes how
// many physical pixels one design pixel is worth, and the project multiplies
// its layout by that factor. Logically the screen is always `logicalSize()`
// units, which is what panels should centre themselves in.
//
// Example:
//     Fem::DpiCurve curve;
//     curve.referenceHeight = 720.f;
//     curve.userScale = settings.uiScale;
//     const sf::Vector2f window = ...;
//     const float scale = curve.scaleFor(window);       // 1.0 at 1280x720
//     const sf::Vector2f logical = curve.logicalSize(window);
//     FrameDrawList.rect({0, 0, 200 * scale, 40 * scale}, ...); // or draw scaled
#pragma once

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cmath>

namespace Fem
{

struct DpiCurve
{
    // The height the layout was designed for. Everything is authored against
    // this, so a 720p project scales cleanly from 540p to 4K.
    float referenceHeight = 720.f;
    // Accessibility / user preference multiplier (0.75 .. 2.0 in the game).
    float userScale = 1.f;
    // Keep text legible on tiny windows and stop it exploding on huge ones.
    float minScale = 0.55f;
    float maxScale = 3.5f;

    float ScaleFor(sf::Vector2f windowSize) const
    {
        const float base = windowSize.y > 0.f ? windowSize.y / std::max(1.f, referenceHeight) : 1.f;
        return std::clamp(base, minScale, maxScale) * std::clamp(userScale, 0.5f, 2.5f);
    }

    // The window expressed in design pixels. Layout code should centre panels
    // in this size, never in the raw window size.
    sf::Vector2f LogicalSize(sf::Vector2f windowSize) const
    {
        const float scale = ScaleFor(windowSize);
        return {windowSize.x / scale, windowSize.y / scale};
    }

    // Conversion helpers, useful when a value arrives in one space and has to
    // be used in the other (mouse position, saved layout sizes, ...).
    static float ToPixels(float logical, float scale)
    {
        return logical * scale;
    }
    static float ToLogical(float pixels, float scale)
    {
        return scale > 0.0001f ? pixels / scale : pixels;
    }
    static sf::Vector2f ToLogical(sf::Vector2f pixels, float scale)
    {
        return scale > 0.0001f ? pixels / scale : pixels;
    }
};

} // namespace Fem
