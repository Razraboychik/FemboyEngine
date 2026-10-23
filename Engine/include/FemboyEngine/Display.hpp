// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <SFML/System/Vector2.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdint>
#include <string>

#include "FemboyEngine/FemObject.hpp"

namespace Fem
{

// Best-effort answer to "is the operating system already filtering blue light?".
// The engine never changes an OS setting; it only reads a signal so a game can
// decide whether to show an eye-comfort hint.
enum class EyeProtectionState
{
    Unknown,
    Inactive,
    Active,
    Unsupported
};

struct EyeProtectionStatus
{
    EyeProtectionState state = EyeProtectionState::Unknown;
    std::string source = "Unknown";
    std::string detail;
    // A rough 0..1 warmth estimate. It is only used for display copy; Windows
    // does not expose the exact Night Light temperature through a stable API.
    float estimatedWarmth = 0.f;

    bool Active() const
    {
        return state == EyeProtectionState::Active;
    }
    bool Known() const
    {
        return state == EyeProtectionState::Active || state == EyeProtectionState::Inactive;
    }
};

// Data handed to the renderer once per frame. Keeping this as a small POD means
// a future Vulkan/D3D backend can consume it without including the settings
// class or SFML shaders.
struct PostProcessParams
{
    float gamma = 1.f;
    float brightness = 1.f;
    float contrast = 1.f;
    float saturation = 1.f;
    float eyeComfort = 0.f;          // 0..1, multiplies a warm/red tint into the frame
    float eyeRed = 0.f;              // 0 = warm yellow, 1 = red
    float vignette = 0.f;            // 0..1
    float scanlines = 0.f;           // 0..1
    float chromaticAberration = 0.f; // 0..1
    // Animated, per-pixel grain. Subtle values (0.01..0.03) break up large flat
    // areas that would otherwise stay at one OLED brightness for hours.
    float noise = 0.f; // 0..0.25

    bool Active() const
    {
        return std::abs(gamma - 1.f) > 0.0001f || std::abs(brightness - 1.f) > 0.0001f ||
               std::abs(contrast - 1.f) > 0.0001f || std::abs(saturation - 1.f) > 0.0001f || eyeComfort > 0.0001f ||
               vignette > 0.0001f || scanlines > 0.0001f || chromaticAberration > 0.0001f || noise > 0.0001f;
    }
};

// Engine-wide display comfort settings.
//
// Unlike the game's Settings struct, these are owned by the engine because they
// describe how the engine presents a frame: gamma, colour grading, post effects
// and OLED pixel shifting. They are registered in SettingsRegistry (so a tool or
// another game can enumerate them) and serialized by the game next to its own
// settings.
//
// The UI pixel shift only affects DrawLayer::UI and DrawLayer::Overlay. World
// geometry stays put. A game that hit-tests its UI must subtract
// UiPixelShift() from the pointer position; NeonTriangle does this in Ui::Update.
class DisplaySettings : public FemObject
{
public:
    FEMCLASS_NO_CREATE(DisplaySettings, FemObject, false)
    static DisplaySettings& Instance();


    // Idempotent. Engine/game startup can call it freely.
    void EnsureRegistered();

    // Registry bridge. LoadFromRegistry() pulls externally edited values;
    // WriteToRegistry() publishes the typed fields for tools and serialization.
    void LoadFromRegistry();
    void WriteToRegistry() const;

    // Pushes the current post-process parameters and UI shift into the active
    // renderer. Safe to call headless: it becomes a no-op without a window.
    void Apply();

    // Advances the OLED pixel-shift/dissolve clocks and refreshes the cached
    // system eye protection state every few seconds. `userActive` should be true
    // while any key, button, wheel or controller action is active; it wakes a
    // dissolved UI immediately instead of waiting for the next event.
    void Update(float realDt, bool userActive = false);

    // Any real input event wakes the UI and restarts the idle timer. Games that
    // poll input without an event loop can call Update(dt, true) instead.
    void NotifyActivity();

    sf::Vector2i UiPixelShift() const;
    // 0 = fully visible, 1 = fully dissolved. The renderer applies a stable
    // per-draw random threshold so widgets disappear in a different order each
    // idle cycle instead of all fading at once.
    float UiDissolve() const;
    std::uint32_t UiDissolveSeed() const;
    PostProcessParams PostProcess() const;

    const EyeProtectionStatus& EyeProtection(bool refreshIfStale = true);
    void RefreshEyeProtection();

    void ResetToDefaults();

    nlohmann::json ToJson() const;
    void FromJson(const nlohmann::json& json);

    // Live values
    float gamma = 1.f;               // 0.5..2.0
    float brightness = 1.f;          // 0.5..1.5
    float contrast = 1.f;            // 0.5..1.5
    float saturation = 1.f;          // 0..1.5
    float eyeComfort = 0.f;          // 0..1
    int eyeComfortMode = 0;          // 0 = warm/yellow, 1 = red
    float vignette = 0.f;            // 0..1
    float scanlines = 0.f;           // 0..1
    float chromaticAberration = 0.f; // 0..1
    float noise = 0.f;               // 0..0.25 animated grain for OLED care

    // OLED protection for static UI. 0 strength disables the shift.
    bool uiPixelShiftEnabled = true;
    int uiPixelShiftStrength = 1;      // 1..3 pixels
    float uiPixelShiftInterval = 90.f; // seconds between positions

    // Periodic UI dissolve. After `uiDissolveInterval` seconds without real
    // input the UI fades out, widget by widget, over `uiDissolveDuration`.
    // Any input restores it immediately. This is intentionally opt-in: hiding
    // a HUD during play is a strong preference, not a universal default.
    bool uiDissolveEnabled = false;
    float uiDissolveInterval = 180.f; // 30..1800 seconds
    float uiDissolveDuration = 3.f;   // 0.5..15 seconds
    float uiDissolveStrength = 0.9f;  // 0..1 maximum fade

    // Show an in-game hint when neither the OS nor the game is softening the
    // image. A player who wants to manage warmth themselves can turn it off.
    bool eyeProtectionNotice = true;

private:
    DisplaySettings() = default;

    EyeProtectionStatus status_{};
    float statusAge_ = 100000.f;
    float shiftClock_ = 0.f;
    float dissolveClock_ = 0.f;
    float dissolveAmount_ = 0.f;
    std::uint32_t dissolveSeed_ = 0x9E3779B9u;
    bool registered_ = false;
};

// Reads the operating system's eye-protection state. Platform notes:
//   Windows: Night Light registry blob (best effort; the blob is undocumented).
//   Linux:   scans /proc for redshift, gammastep, wlsunset, clight, sct, xflux.
//   macOS:   reads com.apple.CoreBrightness through `defaults` when available.
//   Android: unsupported from native code; a game can supply its own JNI result.
// No platform is modified. Unknown/unsupported is a valid answer.
EyeProtectionStatus QuerySystemEyeProtection();

const char* EyeProtectionStateName(EyeProtectionState state);

} // namespace Fem
