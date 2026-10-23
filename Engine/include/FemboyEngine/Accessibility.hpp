// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>
#include <SFML/Graphics.hpp>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/FemObject.hpp"

namespace Fem
{

enum class ColorblindMode
{
    None,
    Protanopia,
    Deuteranopia,
    Tritanopia,
    Achromatopsia
};

// Accessibility system: screen reader, colorblind filters, TTS, subtitles, high contrast.
class Accessibility : public FemObject
{
public:
    FEMCLASS_NO_CREATE(Accessibility, FemObject, false)
    static Accessibility& Instance();


    // Colorblind
    void SetColorblindMode(ColorblindMode m)
    {
        colorblindMode_ = m;
    }
    ColorblindMode GetColorblindMode() const
    {
        return colorblindMode_;
    }
    sf::Color AdjustColor(const sf::Color& c) const;

    // High contrast (independent of the colorblind mode)
    void SetHighContrast(bool v)
    {
        highContrast_ = v;
    }
    bool HighContrast() const
    {
        return highContrast_;
    }

    // Text-to-speech
    void Speak(const std::string& text);
    void SetTtsVolume(float v);
    float TtsVolume() const
    {
        return ttsVolume_;
    }

    // Subtitles
    struct Subtitle
    {
        std::string text;
        float duration;
        float elapsed;
    };
    void AddSubtitle(const std::string& text, float duration = 3.f);
    void UpdateSubtitles(float dt);
    // Drops every caption currently on screen. Used when a game leaves the run
    // for an ending page, so an arena announcement cannot print over the finale.
    void ClearSubtitles()
    {
        subtitles_.clear();
    }
    const std::vector<Subtitle>& Subtitles() const
    {
        return subtitles_;
    }
    void SetSubtitlesEnabled(bool v)
    {
        subtitlesEnabled_ = v;
    }
    bool SubtitlesEnabled() const
    {
        return subtitlesEnabled_;
    }
    // Multiplier for subtitle text and its backing pill. A separate value from
    // uiScale lets a player enlarge captions without reflowing every menu.
    void SetSubtitleScale(float v)
    {
        subtitleScale_ = std::clamp(v, 0.75f, 2.f);
    }
    float SubtitleScale() const
    {
        return subtitleScale_;
    }

    // Motion/flash comfort. Games query these before applying screen shake,
    // full-screen flashes and rapid decorative animation.
    void SetReduceMotion(bool v)
    {
        reduceMotion_ = v;
    }
    bool ReduceMotion() const
    {
        return reduceMotion_;
    }
    void SetReduceFlash(bool v)
    {
        reduceFlash_ = v;
    }
    bool ReduceFlash() const
    {
        return reduceFlash_;
    }

    // Photosensitivity safety is an engine-level umbrella: when it is on, both
    // reduce-motion and reduce-flash are forced, no matter what a splash screen
    // or a menu later writes into the individual flags.
    void SetPhotosensitivity(bool v)
    {
        photosensitivity_ = v;
        if (v)
        {
            reduceMotion_ = true;
            reduceFlash_ = true;
        }
    }
    bool Photosensitivity() const
    {
        return photosensitivity_;
    }

    // Screen reader announcements
    void Announce(const std::string& text);
    void SetScreenReaderCallback(std::function<void(const std::string&)> cb)
    {
        screenReaderCb_ = std::move(cb);
    }

    // Remappable input (one-handed mode)
    void SetOneHandedMode(bool v)
    {
        oneHanded_ = v;
    }
    bool OneHandedMode() const
    {
        return oneHanded_;
    }

    // UI scale
    void SetUiScale(float v);
    float UiScale() const
    {
        return uiScale_;
    }

private:
    Accessibility() = default;
    ColorblindMode colorblindMode_ = ColorblindMode::None;
    bool highContrast_ = false;
    float ttsVolume_ = 1.f;
    std::vector<Subtitle> subtitles_;
    std::function<void(const std::string&)> screenReaderCb_;
    bool oneHanded_ = false;
    float uiScale_ = 1.f;
    bool subtitlesEnabled_ = true;
    float subtitleScale_ = 1.f;
    bool reduceMotion_ = false;
    bool reduceFlash_ = false;
    bool photosensitivity_ = false;
};

// Engine-wide accessibility settings. Game-side accessibility structs (like
// NeonTriangle's Settings) can mirror these values, but the registry remains the
// common catalogue so a launcher, editor or another game can enumerate and edit
// the same controls.
class AccessibilitySettings : public FemObject
{
public:
    FEMCLASS_NO_CREATE(AccessibilitySettings, FemObject, false)
    static AccessibilitySettings& Instance();


    void EnsureRegistered();
    void LoadFromRegistry();
    void WriteToRegistry() const;
    void Apply();
    void ResetToDefaults();

    nlohmann::json ToJson() const;
    void FromJson(const nlohmann::json& json);

    // Live values
    float uiScale = 1.f;    // 0.75..2.0
    int colorblindMode = 0; // matches ColorblindMode
    bool highContrast = false;
    bool subtitles = true;
    float subtitleScale = 1.f; // 0.75..2.0
    bool screenReader = false;
    float ttsVolume = 0.8f; // 0..1
    bool reduceMotion = false;
    bool reduceFlash = false;
    bool photosensitivity = false;
    bool oneHanded = false;

private:
    AccessibilitySettings() = default;
    bool registered_ = false;
};

} // namespace Fem
