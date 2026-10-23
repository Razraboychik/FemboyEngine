// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Display.hpp"

#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Engine.hpp"
#include "FemboyEngine/SettingsRegistry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__ANDROID__)
// Native code cannot portably read the vendor eye-comfort setting. The game can
// still provide its own value through JNI; the engine reports Unsupported.
#elif defined(__linux__)
#include <dirent.h>
#include <fstream>
#elif defined(__APPLE__)
#include <cstdio>
#endif

namespace Fem
{

namespace
{
constexpr const char* kGamma = "engine.display.gamma";
constexpr const char* kBrightness = "engine.display.brightness";
constexpr const char* kContrast = "engine.display.contrast";
constexpr const char* kSaturation = "engine.display.saturation";
constexpr const char* kEyeComfort = "engine.display.eyeComfort";
constexpr const char* kEyeComfortMode = "engine.display.eyeComfortMode";
constexpr const char* kVignette = "engine.display.vignette";
constexpr const char* kScanlines = "engine.display.scanlines";
constexpr const char* kAberration = "engine.display.chromaticAberration";
constexpr const char* kUiPixelShift = "engine.display.uiPixelShift";
constexpr const char* kUiPixelShiftStrength = "engine.display.uiPixelShiftStrength";
constexpr const char* kUiPixelShiftInterval = "engine.display.uiPixelShiftInterval";
constexpr const char* kNoise = "engine.display.noise";
constexpr const char* kUiDissolve = "engine.display.uiDissolve";
constexpr const char* kUiDissolveInterval = "engine.display.uiDissolveInterval";
constexpr const char* kUiDissolveDuration = "engine.display.uiDissolveDuration";
constexpr const char* kUiDissolveStrength = "engine.display.uiDissolveStrength";
constexpr const char* kEyeProtectionNotice = "engine.display.eyeProtectionNotice";

void AddDefinition(const char* key, const char* label, SettingType type, nlohmann::json defaultValue,
                   nlohmann::json minValue = nullptr, nlohmann::json maxValue = nullptr,
                   std::vector<std::string> enumLabels = {})
{
    SettingDef def;
    def.key = key;
    def.category = "Display";
    def.label = label;
    def.type = type;
    def.defaultValue = std::move(defaultValue);
    def.minValue = std::move(minValue);
    def.maxValue = std::move(maxValue);
    def.enumLabels = std::move(enumLabels);
    def.flags = static_cast<std::uint32_t>(SettingFlag_Engine | SettingFlag_UserVisible);
    def.onChange = [](const nlohmann::json&)
    {
        DisplaySettings& display = DisplaySettings::Instance();
        display.LoadFromRegistry();
        display.Apply();
    };
    SettingsRegistry::Instance().Add(std::move(def));
}

float Clampf(float value, float low, float high)
{
    return std::clamp(value, low, high);
}

#if defined(_WIN32)
EyeProtectionStatus ReadWindowsNightLight()
{
    EyeProtectionStatus status;
    status.source = "Windows Night Light";

    const wchar_t* keys[] = {
        L"Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store\\DefaultAccount\\Current\\default$windows."
        L"data.bluelightreduction.bluelightreductionstate\\windows.data.bluelightreduction.bluelightreductionstate",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store\\DefaultAccount\\Current\\default$windows."
        L"data.bluelightreduction.bluelightreductionstate",
    };

    for (const wchar_t* path : keys)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            continue;
        }

        DWORD type = 0;
        DWORD size = 0;
        LSTATUS query = RegQueryValueExW(key, L"Data", nullptr, &type, nullptr, &size);
        if (query != ERROR_SUCCESS || type != REG_BINARY || size < 20)
        {
            RegCloseKey(key);
            continue;
        }

        std::vector<unsigned char> data(size);
        query = RegQueryValueExW(key, L"Data", nullptr, &type, data.data(), &size);
        RegCloseKey(key);
        if (query != ERROR_SUCCESS || data.size() < 19)
        {
            continue;
        }

        // The Night Light blob is undocumented. Across shipped Windows 10/11
        // builds the byte at offset 18 carries 0x15/0x10 when enabled and
        // 0x13/0x00 when disabled. If the layout changes we report Unknown
        // rather than pretending to know.
        const unsigned char marker = data[18];
        if (marker == 0x15 || marker == 0x10)
        {
            status.state = EyeProtectionState::Active;
            status.detail = "Windows Night Light is enabled.";
            status.estimatedWarmth = 0.55f;
        }
        else if (marker == 0x13 || marker == 0x00)
        {
            status.state = EyeProtectionState::Inactive;
            status.detail = "Windows Night Light is disabled.";
        }
        else
        {
            status.state = EyeProtectionState::Unknown;
            status.detail = "Windows Night Light registry layout is not recognized.";
        }
        return status;
    }

    status.state = EyeProtectionState::Inactive;
    status.detail = "Windows Night Light has not been configured on this account.";
    return status;
}
#endif

#if defined(__linux__) && !defined(__ANDROID__)
bool processLooksLikeEyeProtection(const std::string& name)
{
    static const std::array<const char*, 9> names = {"redshift", "gammastep",  "wlsunset",          "clight", "sct",
                                                     "xflux",    "nightlight", "gnome-night-light", "kgamma"};
    for (const char* candidate : names)
    {
        if (name == candidate || name.rfind(std::string(candidate) + "-", 0) == 0)
        {
            return true;
        }
    }
    return false;
}

EyeProtectionStatus readLinuxEyeProtection()
{
    EyeProtectionStatus status;
    status.source = "Linux eye-protection service";

    DIR* proc = opendir("/proc");
    if (!proc)
    {
        status.state = EyeProtectionState::Unknown;
        status.detail = "Could not read /proc.";
        return status;
    }

    std::string found;
    while (dirent* entry = readdir(proc))
    {
        const std::string pid = entry->d_name;
        if (pid.empty() || !std::all_of(pid.begin(), pid.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        {
            continue;
        }

        std::ifstream comm("/proc/" + pid + "/comm");
        std::string name;
        if (!std::getline(comm, name))
        {
            continue;
        }
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' '))
        {
            name.pop_back();
        }
        if (processLooksLikeEyeProtection(name))
        {
            found = name;
            break;
        }
    }
    closedir(proc);

    if (!found.empty())
    {
        status.state = EyeProtectionState::Active;
        status.source = found;
        status.detail = "A colour-temperature service is running: " + found + ".";
        status.estimatedWarmth = 0.5f;
    }
    else
    {
        status.state = EyeProtectionState::Inactive;
        status.detail = "No redshift/gammastep/wlsunset/clight process was found.";
    }
    return status;
}
#endif

#if defined(__APPLE__)
EyeProtectionStatus readMacEyeProtection()
{
    EyeProtectionStatus status;
    status.source = "macOS Night Shift";

    FILE* pipe = popen("defaults read com.apple.CoreBrightness 2>/dev/null", "r");
    if (!pipe)
    {
        status.state = EyeProtectionState::Unknown;
        status.detail = "Could not query com.apple.CoreBrightness.";
        return status;
    }

    std::string text;
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
    {
        text += buffer.data();
    }
    const int result = pclose(pipe);
    if (result != 0 || text.empty())
    {
        status.state = EyeProtectionState::Unknown;
        status.detail = "Night Shift preferences are unavailable.";
        return status;
    }

    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::size_t nightShift = lower.find("nightshift");
    if (nightShift == std::string::npos)
    {
        status.state = EyeProtectionState::Unknown;
        status.detail = "Night Shift keys were not present in CoreBrightness.";
        return status;
    }

    const std::size_t end = std::min(lower.size(), nightShift + 4096);
    const std::string slice = lower.substr(nightShift, end - nightShift);
    if (slice.find("enabled = 1") != std::string::npos || slice.find("enabled\" = 1") != std::string::npos ||
        slice.find("enabled = true") != std::string::npos)
    {
        status.state = EyeProtectionState::Active;
        status.detail = "macOS Night Shift is enabled.";
        status.estimatedWarmth = 0.5f;
    }
    else
    {
        status.state = EyeProtectionState::Inactive;
        status.detail = "macOS Night Shift is disabled.";
    }
    return status;
}
#endif
} // namespace

DisplaySettings& DisplaySettings::Instance()
{
    static DisplaySettings settings;
    settings.EnsureRegistered();
    return settings;
}

void DisplaySettings::EnsureRegistered()
{
    if (registered_)
    {
        return;
    }
    registered_ = true;

    AddDefinition(kGamma, "Gamma", SettingType::Float, 1.f, 0.5f, 2.f);
    AddDefinition(kBrightness, "Brightness", SettingType::Float, 1.f, 0.5f, 1.5f);
    AddDefinition(kContrast, "Contrast", SettingType::Float, 1.f, 0.5f, 1.5f);
    AddDefinition(kSaturation, "Saturation", SettingType::Float, 1.f, 0.f, 1.5f);
    AddDefinition(kEyeComfort, "Eye comfort", SettingType::Float, 0.f, 0.f, 1.f);
    AddDefinition(kEyeComfortMode, "Eye comfort tint", SettingType::Enum, 0, 0, 1, {"Warm", "Red"});
    AddDefinition(kVignette, "Vignette", SettingType::Float, 0.f, 0.f, 1.f);
    AddDefinition(kScanlines, "Scanlines", SettingType::Float, 0.f, 0.f, 1.f);
    AddDefinition(kAberration, "Chromatic aberration", SettingType::Float, 0.f, 0.f, 1.f);
    AddDefinition(kNoise, "OLED noise", SettingType::Float, 0.f, 0.f, 0.25f);
    AddDefinition(kUiPixelShift, "OLED pixel shift", SettingType::Bool, true);
    AddDefinition(kUiPixelShiftStrength, "Pixel shift strength", SettingType::Int, 1, 1, 3);
    AddDefinition(kUiPixelShiftInterval, "Pixel shift interval", SettingType::Float, 90.f, 15.f, 600.f);
    AddDefinition(kUiDissolve, "UI dissolve", SettingType::Bool, false);
    AddDefinition(kUiDissolveInterval, "Dissolve interval", SettingType::Float, 180.f, 30.f, 1800.f);
    AddDefinition(kUiDissolveDuration, "Dissolve duration", SettingType::Float, 3.f, 0.5f, 15.f);
    AddDefinition(kUiDissolveStrength, "Dissolve strength", SettingType::Float, 0.9f, 0.f, 1.f);
    AddDefinition(kEyeProtectionNotice, "Protection notice", SettingType::Bool, true);
}

void DisplaySettings::LoadFromRegistry()
{
    SettingsRegistry& registry = SettingsRegistry::Instance();
    gamma = Clampf(registry.Get<float>(kGamma, 1.f), 0.5f, 2.f);
    brightness = Clampf(registry.Get<float>(kBrightness, 1.f), 0.5f, 1.5f);
    contrast = Clampf(registry.Get<float>(kContrast, 1.f), 0.5f, 1.5f);
    saturation = Clampf(registry.Get<float>(kSaturation, 1.f), 0.f, 1.5f);
    eyeComfort = Clampf(registry.Get<float>(kEyeComfort, 0.f), 0.f, 1.f);
    eyeComfortMode = std::clamp(registry.Get<int>(kEyeComfortMode, 0), 0, 1);
    vignette = Clampf(registry.Get<float>(kVignette, 0.f), 0.f, 1.f);
    scanlines = Clampf(registry.Get<float>(kScanlines, 0.f), 0.f, 1.f);
    chromaticAberration = Clampf(registry.Get<float>(kAberration, 0.f), 0.f, 1.f);
    noise = Clampf(registry.Get<float>(kNoise, 0.f), 0.f, 0.25f);
    uiPixelShiftEnabled = registry.Get<bool>(kUiPixelShift, true);
    uiPixelShiftStrength = std::clamp(registry.Get<int>(kUiPixelShiftStrength, 1), 1, 3);
    uiPixelShiftInterval = Clampf(registry.Get<float>(kUiPixelShiftInterval, 90.f), 15.f, 600.f);
    uiDissolveEnabled = registry.Get<bool>(kUiDissolve, false);
    uiDissolveInterval = Clampf(registry.Get<float>(kUiDissolveInterval, 180.f), 30.f, 1800.f);
    uiDissolveDuration = Clampf(registry.Get<float>(kUiDissolveDuration, 3.f), 0.5f, 15.f);
    uiDissolveStrength = Clampf(registry.Get<float>(kUiDissolveStrength, 0.9f), 0.f, 1.f);
    eyeProtectionNotice = registry.Get<bool>(kEyeProtectionNotice, true);
}

void DisplaySettings::WriteToRegistry() const
{
    SettingsRegistry& registry = SettingsRegistry::Instance();
    registry.Set(kGamma, Clampf(gamma, 0.5f, 2.f), false);
    registry.Set(kBrightness, Clampf(brightness, 0.5f, 1.5f), false);
    registry.Set(kContrast, Clampf(contrast, 0.5f, 1.5f), false);
    registry.Set(kSaturation, Clampf(saturation, 0.f, 1.5f), false);
    registry.Set(kEyeComfort, Clampf(eyeComfort, 0.f, 1.f), false);
    registry.Set(kEyeComfortMode, std::clamp(eyeComfortMode, 0, 1), false);
    registry.Set(kVignette, Clampf(vignette, 0.f, 1.f), false);
    registry.Set(kScanlines, Clampf(scanlines, 0.f, 1.f), false);
    registry.Set(kAberration, Clampf(chromaticAberration, 0.f, 1.f), false);
    registry.Set(kNoise, Clampf(noise, 0.f, 0.25f), false);
    registry.Set(kUiPixelShift, uiPixelShiftEnabled, false);
    registry.Set(kUiPixelShiftStrength, std::clamp(uiPixelShiftStrength, 1, 3), false);
    registry.Set(kUiPixelShiftInterval, Clampf(uiPixelShiftInterval, 15.f, 600.f), false);
    registry.Set(kUiDissolve, uiDissolveEnabled, false);
    registry.Set(kUiDissolveInterval, Clampf(uiDissolveInterval, 30.f, 1800.f), false);
    registry.Set(kUiDissolveDuration, Clampf(uiDissolveDuration, 0.5f, 15.f), false);
    registry.Set(kUiDissolveStrength, Clampf(uiDissolveStrength, 0.f, 1.f), false);
    registry.Set(kEyeProtectionNotice, eyeProtectionNotice, false);
}

void DisplaySettings::Apply()
{
    Engine& engine = Engine::Instance();
    if (!engine.GetWindow().IsOpen())
    {
        return;
    }
    engine.Renderer().SetPostProcess(PostProcess());
    engine.Renderer().SetUiPixelShift(UiPixelShift());
    engine.Renderer().SetUiDissolve(UiDissolve(), UiDissolveSeed());
}

void DisplaySettings::Update(float realDt, bool userActive)
{
    const float dt = std::max(0.f, realDt);
    shiftClock_ += dt;
    statusAge_ += dt;
    if (statusAge_ > 20.f)
    {
        RefreshEyeProtection();
    }

    if (!uiDissolveEnabled)
    {
        dissolveClock_ = 0.f;
        dissolveAmount_ = 0.f;
        return;
    }

    if (userActive)
    {
        NotifyActivity();
        return;
    }

    dissolveClock_ += dt;
    const float interval = Clampf(uiDissolveInterval, 30.f, 1800.f);
    if (dissolveClock_ < interval)
    {
        return;
    }

    const float duration = std::max(0.25f, Clampf(uiDissolveDuration, 0.5f, 15.f));
    const float target = Clampf(uiDissolveStrength, 0.f, 1.f);
    if (dissolveAmount_ <= 0.0001f)
    {
        // A fresh cycle gets a fresh order. The seed changes only when the
        // dissolve actually starts, so the widget order stays stable while the
        // UI fades and when the player wakes it again.
        dissolveSeed_ = dissolveSeed_ * 1664525u + 1013904223u;
    }
    dissolveAmount_ = std::min(target, dissolveAmount_ + dt / duration);
}

void DisplaySettings::NotifyActivity()
{
    dissolveClock_ = 0.f;
    dissolveAmount_ = 0.f;
}

sf::Vector2i DisplaySettings::UiPixelShift() const
{
    if (!uiPixelShiftEnabled || uiPixelShiftStrength <= 0)
    {
        return {0, 0};
    }

    // A 3x3 cycle keeps every static pixel moving without making the HUD look
    // jittery. The interval is deliberately long (default 90 s): OLED burn-in is
    // a hours-scale problem, not a per-frame effect.
    static const std::array<sf::Vector2i, 9> pattern = {
        sf::Vector2i{0, 0},  sf::Vector2i{1, 0},   sf::Vector2i{1, 1},  sf::Vector2i{0, 1},  sf::Vector2i{-1, 1},
        sf::Vector2i{-1, 0}, sf::Vector2i{-1, -1}, sf::Vector2i{0, -1}, sf::Vector2i{1, -1},
    };
    const float interval = Clampf(uiPixelShiftInterval, 15.f, 600.f);
    const int step = static_cast<int>(shiftClock_ / interval) % static_cast<int>(pattern.size());
    const int strength = std::clamp(uiPixelShiftStrength, 1, 3);
    return sf::Vector2i(pattern[static_cast<std::size_t>(step)].x * strength,
                        pattern[static_cast<std::size_t>(step)].y * strength);
}

float DisplaySettings::UiDissolve() const
{
    if (!uiDissolveEnabled)
    {
        return 0.f;
    }
    return Clampf(dissolveAmount_, 0.f, 1.f);
}

std::uint32_t DisplaySettings::UiDissolveSeed() const
{
    return dissolveSeed_;
}

PostProcessParams DisplaySettings::PostProcess() const
{
    PostProcessParams params;
    params.gamma = Clampf(gamma, 0.5f, 2.f);
    params.brightness = Clampf(brightness, 0.5f, 1.5f);
    params.contrast = Clampf(contrast, 0.5f, 1.5f);
    params.saturation = Clampf(saturation, 0.f, 1.5f);
    params.eyeComfort = Clampf(eyeComfort, 0.f, 1.f);
    params.eyeRed = eyeComfortMode == 1 ? 1.f : 0.f;
    params.vignette = Clampf(vignette, 0.f, 1.f);
    params.scanlines = Clampf(scanlines, 0.f, 1.f);
    params.chromaticAberration = Clampf(chromaticAberration, 0.f, 1.f);
    params.noise = Clampf(noise, 0.f, 0.25f);
    return params;
}

const EyeProtectionStatus& DisplaySettings::EyeProtection(bool refreshIfStale)
{
    if (refreshIfStale && statusAge_ > 5.f)
    {
        RefreshEyeProtection();
    }
    return status_;
}

void DisplaySettings::RefreshEyeProtection()
{
    status_ = QuerySystemEyeProtection();
    statusAge_ = 0.f;
}

void DisplaySettings::ResetToDefaults()
{
    gamma = 1.f;
    brightness = 1.f;
    contrast = 1.f;
    saturation = 1.f;
    eyeComfort = 0.f;
    eyeComfortMode = 0;
    vignette = 0.f;
    scanlines = 0.f;
    chromaticAberration = 0.f;
    noise = 0.f;
    uiPixelShiftEnabled = true;
    uiPixelShiftStrength = 1;
    uiPixelShiftInterval = 90.f;
    uiDissolveEnabled = false;
    uiDissolveInterval = 180.f;
    uiDissolveDuration = 3.f;
    uiDissolveStrength = 0.9f;
    dissolveClock_ = 0.f;
    dissolveAmount_ = 0.f;
    eyeProtectionNotice = true;
    WriteToRegistry();
    Apply();
}

nlohmann::json DisplaySettings::ToJson() const
{
    WriteToRegistry();
    return nlohmann::json{
        {"gamma", Clampf(gamma, 0.5f, 2.f)},
        {"brightness", Clampf(brightness, 0.5f, 1.5f)},
        {"contrast", Clampf(contrast, 0.5f, 1.5f)},
        {"saturation", Clampf(saturation, 0.f, 1.5f)},
        {"eyeComfort", Clampf(eyeComfort, 0.f, 1.f)},
        {"eyeComfortMode", std::clamp(eyeComfortMode, 0, 1)},
        {"vignette", Clampf(vignette, 0.f, 1.f)},
        {"scanlines", Clampf(scanlines, 0.f, 1.f)},
        {"chromaticAberration", Clampf(chromaticAberration, 0.f, 1.f)},
        {"noise", Clampf(noise, 0.f, 0.25f)},
        {"uiPixelShift", uiPixelShiftEnabled},
        {"uiPixelShiftStrength", std::clamp(uiPixelShiftStrength, 1, 3)},
        {"uiPixelShiftInterval", Clampf(uiPixelShiftInterval, 15.f, 600.f)},
        {"uiDissolve", uiDissolveEnabled},
        {"uiDissolveInterval", Clampf(uiDissolveInterval, 30.f, 1800.f)},
        {"uiDissolveDuration", Clampf(uiDissolveDuration, 0.5f, 15.f)},
        {"uiDissolveStrength", Clampf(uiDissolveStrength, 0.f, 1.f)},
        {"eyeProtectionNotice", eyeProtectionNotice},
    };
}

void DisplaySettings::FromJson(const nlohmann::json& json)
{
    if (!json.is_object())
    {
        return;
    }

    const auto number = [&json](const char* key, auto& out)
    {
        if (json.contains(key) && json[key].is_number())
        {
            out = json[key].get<std::decay_t<decltype(out)>>();
        }
    };
    const auto boolean = [&json](const char* key, bool& out)
    {
        if (json.contains(key) && json[key].is_boolean())
        {
            out = json[key].get<bool>();
        }
    };

    number("gamma", gamma);
    number("brightness", brightness);
    number("contrast", contrast);
    number("saturation", saturation);
    number("eyeComfort", eyeComfort);
    number("eyeComfortMode", eyeComfortMode);
    number("vignette", vignette);
    number("scanlines", scanlines);
    number("chromaticAberration", chromaticAberration);
    number("noise", noise);
    boolean("uiPixelShift", uiPixelShiftEnabled);
    number("uiPixelShiftStrength", uiPixelShiftStrength);
    number("uiPixelShiftInterval", uiPixelShiftInterval);
    boolean("uiDissolve", uiDissolveEnabled);
    number("uiDissolveInterval", uiDissolveInterval);
    number("uiDissolveDuration", uiDissolveDuration);
    number("uiDissolveStrength", uiDissolveStrength);
    boolean("eyeProtectionNotice", eyeProtectionNotice);

    gamma = Clampf(gamma, 0.5f, 2.f);
    brightness = Clampf(brightness, 0.5f, 1.5f);
    contrast = Clampf(contrast, 0.5f, 1.5f);
    saturation = Clampf(saturation, 0.f, 1.5f);
    eyeComfort = Clampf(eyeComfort, 0.f, 1.f);
    eyeComfortMode = std::clamp(eyeComfortMode, 0, 1);
    vignette = Clampf(vignette, 0.f, 1.f);
    scanlines = Clampf(scanlines, 0.f, 1.f);
    chromaticAberration = Clampf(chromaticAberration, 0.f, 1.f);
    noise = Clampf(noise, 0.f, 0.25f);
    uiPixelShiftStrength = std::clamp(uiPixelShiftStrength, 1, 3);
    uiPixelShiftInterval = Clampf(uiPixelShiftInterval, 15.f, 600.f);
    uiDissolveInterval = Clampf(uiDissolveInterval, 30.f, 1800.f);
    uiDissolveDuration = Clampf(uiDissolveDuration, 0.5f, 15.f);
    uiDissolveStrength = Clampf(uiDissolveStrength, 0.f, 1.f);
    dissolveClock_ = 0.f;
    dissolveAmount_ = 0.f;
    WriteToRegistry();
    Apply();
}

EyeProtectionStatus QuerySystemEyeProtection()
{
#if defined(_WIN32)
    return ReadWindowsNightLight();
#elif defined(__ANDROID__)
    EyeProtectionStatus status;
    status.state = EyeProtectionState::Unsupported;
    status.source = "Android";
    status.detail = "Android eye-comfort is vendor/OEM specific; query it through JNI from the game.";
    return status;
#elif defined(__linux__)
    return readLinuxEyeProtection();
#elif defined(__APPLE__)
    return readMacEyeProtection();
#else
    EyeProtectionStatus status;
    status.state = EyeProtectionState::Unknown;
    status.source = "This platform";
    status.detail = "No system eye-protection probe is available.";
    return status;
#endif
}

const char* EyeProtectionStateName(EyeProtectionState state)
{
    switch (state)
    {
        case EyeProtectionState::Active:
            return "Detected";
        case EyeProtectionState::Inactive:
            return "Not detected";
        case EyeProtectionState::Unsupported:
            return "Unsupported";
        case EyeProtectionState::Unknown:
        default:
            return "Unknown";
    }
}

} // namespace Fem
