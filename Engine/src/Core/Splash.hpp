// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <string>
#include <vector>

namespace Fem
{

class Window;

// Internal boot splash. Engine::Run() plays it once before the first simulated
// frame; games configure it through Engine::SetSplashEnabled/Duration and the
// optional studio/publisher cards.
namespace Splash
{

// One screen of the boot sequence. An empty path shows the caption on black; an
// empty caption shows only the image. The engine mark is always appended, so a
// project only supplies the cards it wants in front of it.
struct SplashCard
{
    std::string imagePath;
    std::string caption;
    float hold = 1.6f; // seconds fully visible, excluding the fades
    // Set by the engine for the built-in mark, which uses the embedded
    // Logo.png instead of a file on disk.
    bool embeddedLogo = false;
};

// Plays `cards` followed by the engine mark (Logo.png plus the wordmark). Any
// key press or mouse click skips the whole sequence. Consumes the window events
// it needs (close, resize, key) and returns false if the window closed.
bool Play(Window& window, const std::vector<SplashCard>& cards, float engineHold);

} // namespace Splash
} // namespace Fem
