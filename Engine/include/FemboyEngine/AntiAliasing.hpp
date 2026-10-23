// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

namespace Fem
{

// Anti-aliasing choices exposed by the engine and the game settings.
//
//   Off    - no AA. Correct for pixel art, where smoothed pixels are a bug.
//   Fxaa   - one full-screen post pass. Cheap, slightly soft, works on every
//            GPU and on top of a render scale.
//   MsaaN  - multisampled window. Best geometric edges, but the OpenGL context
//            must be created with it, so switching recreates the window and
//            every GPU resource must be reloaded.
//
// Supersampling is not a separate mode: it is RenderScale > 100 in the game's
// settings, and it composes with any mode above.
enum class AntiAliasingMode
{
    Off = 0,
    Fxaa = 1,
    Msaa2 = 2,
    Msaa4 = 3,
    Msaa8 = 4
};

constexpr int kAntiAliasingModeCount = 5;

inline const char* AntiAliasingName(AntiAliasingMode mode)
{
    switch (mode)
    {
        case AntiAliasingMode::Off:
            return "Off";
        case AntiAliasingMode::Fxaa:
            return "FXAA";
        case AntiAliasingMode::Msaa2:
            return "MSAA 2x";
        case AntiAliasingMode::Msaa4:
            return "MSAA 4x";
        case AntiAliasingMode::Msaa8:
            return "MSAA 8x";
    }
    return "Off";
}

inline int AntiAliasingSampleCount(AntiAliasingMode mode)
{
    switch (mode)
    {
        case AntiAliasingMode::Msaa2:
            return 2;
        case AntiAliasingMode::Msaa4:
            return 4;
        case AntiAliasingMode::Msaa8:
            return 8;
        default:
            return 0;
    }
}

inline AntiAliasingMode AntiAliasingFromIndex(int index)
{
    if (index < 0 || index >= kAntiAliasingModeCount)
    {
        return AntiAliasingMode::Off;
    }
    return static_cast<AntiAliasingMode>(index);
}

} // namespace Fem
