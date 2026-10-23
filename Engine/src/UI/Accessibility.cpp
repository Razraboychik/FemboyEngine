// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Accessibility.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/SettingsRegistry.hpp"
#include "FemboyEngine/UI.hpp"
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <sapi.h>
#endif

namespace Fem
{

Accessibility& Accessibility::Instance()
{
    static Accessibility a;
    return a;
}

namespace
{
std::uint8_t ToByte(float v)
{
    return static_cast<std::uint8_t>(std::clamp(v * 255.f, 0.f, 255.f));
}

#ifdef _WIN32
std::wstring ToWide(const std::string& utf8)
{
    if (utf8.empty())
    {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0)
    {
        return {};
    }
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

// SAPI runs on a dedicated thread: ISpVoice is apartment-bound and Speak() is
// blocking, so calling it from the render thread would stall the frame. The
// queue is capped so a flood of announcements cannot pile up into minutes of
// stale speech.
class TtsWorker
{
public:
    static TtsWorker& Instance()
    {
        static TtsWorker worker;
        return worker;
    }

    void SetVolume(float v)
    {
        volume_.store(std::clamp(static_cast<int>(v * 100.f + 0.5f), 0, 100));
    }

    void Speak(const std::string& text)
    {
        std::wstring wide = ToWide(text);
        if (wide.empty())
        {
            return;
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (queue_.size() >= 8)
            {
                queue_.pop_front();
            }
            queue_.push_back(std::move(wide));
        }
        cv_.notify_one();
    }

    bool Available() const
    {
        return available_.load();
    }

private:
    TtsWorker() : thread_([this] { Run(); })
    {
    }

    ~TtsWorker()
    {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            quit_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    TtsWorker(const TtsWorker&) = delete;
    TtsWorker& operator=(const TtsWorker&) = delete;

    void Run()
    {
        // SAPI is COM based; initialise this worker thread as an STA.
        const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ISpVoice* voice = nullptr;
        if (SUCCEEDED(comInit))
        {
            if (SUCCEEDED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice,
                                           reinterpret_cast<void**>(&voice))))
            {
                available_.store(true);
            }
        }
        if (!voice)
        {
            BOY_WARN("Accessibility", "no SAPI voice available; speech will only be logged");
        }

        for (;;)
        {
            std::wstring text;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [this] { return quit_ || !queue_.empty(); });
                if (quit_ && queue_.empty())
                {
                    break;
                }
                text = std::move(queue_.front());
                queue_.pop_front();
            }

            if (voice && !text.empty())
            {
                voice->SetVolume(static_cast<USHORT>(volume_.load()));
                // Synchronous speak keeps the queue ordered; this thread exists
                // only for speech, so blocking it costs nothing.
                voice->Speak(text.c_str(), 0, nullptr);
            }
        }

        if (voice)
        {
            voice->Release();
        }
        if (SUCCEEDED(comInit))
        {
            CoUninitialize();
        }
    }

    std::thread thread_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<std::wstring> queue_;
    std::atomic<int> volume_{100};
    std::atomic<bool> available_{false};
    bool quit_ = false;
};
#else
// Non-Windows builds keep the announcement visible in the log; the platform
// hook can be supplied by the game through setScreenReaderCallback().
struct TtsWorker
{
    static TtsWorker& Instance()
    {
        static TtsWorker worker;
        return worker;
    }
    void SetVolume(float)
    {
    }
    void Speak(const std::string&)
    {
    }
    bool Available() const
    {
        return false;
    }
};
#endif
} // namespace

// Colorblind transformation matrices (Brettel 1997)
sf::Color Accessibility::AdjustColor(const sf::Color& c) const
{
    float r = c.r / 255.f;
    float g = c.g / 255.f;
    float b = c.b / 255.f;

    switch (colorblindMode_)
    {
        case ColorblindMode::Protanopia:
        {
            const float nr = 0.567f * r + 0.433f * g + 0.0f * b;
            const float ng = 0.558f * r + 0.442f * g + 0.0f * b;
            const float nb = 0.0f * r + 0.242f * g + 0.758f * b;
            r = nr;
            g = ng;
            b = nb;
            break;
        }
        case ColorblindMode::Deuteranopia:
        {
            const float nr = 0.625f * r + 0.375f * g + 0.0f * b;
            const float ng = 0.7f * r + 0.3f * g + 0.0f * b;
            const float nb = 0.0f * r + 0.3f * g + 0.7f * b;
            r = nr;
            g = ng;
            b = nb;
            break;
        }
        case ColorblindMode::Tritanopia:
        {
            const float nr = 0.95f * r + 0.05f * g + 0.0f * b;
            const float ng = 0.0f * r + 0.433f * g + 0.567f * b;
            const float nb = 0.0f * r + 0.475f * g + 0.525f * b;
            r = nr;
            g = ng;
            b = nb;
            break;
        }
        case ColorblindMode::Achromatopsia:
        {
            const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
            r = g = b = lum;
            break;
        }
        case ColorblindMode::None:
        default:
            break;
    }

    // High contrast is an independent setting and applies even when no
    // colorblind mode is selected.
    if (highContrast_)
    {
        auto boost = [](float v)
        {
            const float out = (v < 0.5f) ? (v * 0.5f) : (v * 1.5f);
            return std::clamp(out, 0.f, 1.f);
        };
        r = boost(r);
        g = boost(g);
        b = boost(b);
    }

    sf::Color out;
    out.r = ToByte(r);
    out.g = ToByte(g);
    out.b = ToByte(b);
    out.a = c.a;
    return out;
}

void Accessibility::Speak(const std::string& text)
{
    if (text.empty())
    {
        return;
    }
    TtsWorker::Instance().Speak(text);
    if (!TtsWorker::Instance().Available())
    {
        BOY_INFO("Accessibility", "TTS: " + text);
    }
}

void Accessibility::SetTtsVolume(float v)
{
    ttsVolume_ = std::clamp(v, 0.f, 1.f);
    TtsWorker::Instance().SetVolume(ttsVolume_);
}

void Accessibility::AddSubtitle(const std::string& text, float duration)
{
    if (text.empty())
    {
        return;
    }
    Subtitle s;
    s.text = text;
    s.duration = std::max(0.1f, duration);
    s.elapsed = 0.f;
    subtitles_.push_back(std::move(s));
}

void Accessibility::UpdateSubtitles(float dt)
{
    if (dt < 0.f)
    {
        dt = 0.f;
    }
    for (auto& s : subtitles_)
    {
        s.elapsed += dt;
    }

    subtitles_.erase(
        std::remove_if(subtitles_.begin(), subtitles_.end(), [](const Subtitle& s) { return s.elapsed >= s.duration; }),
        subtitles_.end());
}

void Accessibility::Announce(const std::string& text)
{
    if (text.empty())
    {
        return;
    }
    if (screenReaderCb_)
    {
        screenReaderCb_(text);
    }
    Speak(text);
    BOY_INFO("Accessibility", "announce: " + text);
}

void Accessibility::SetUiScale(float v)
{
    // A zero or negative scale makes every UI element invisible; keep a sane range.
    uiScale_ = std::clamp(v, 0.5f, 4.f);
    // Actually apply it: the value used to be stored and never read, so the
    // accessibility UI-scale setting had no effect on PinkUI widgets.
    PinkUI::Instance().SetScale(uiScale_);
    // Apply the new scale to the tree that is already on screen; without this
    // the setting only took effect after the next load.
    PinkUI::Instance().Relayout();
}

// engine-wide accessibility settings

AccessibilitySettings& AccessibilitySettings::Instance()
{
    static AccessibilitySettings settings;
    settings.EnsureRegistered();
    return settings;
}

void AccessibilitySettings::EnsureRegistered()
{
    if (registered_)
    {
        return;
    }
    registered_ = true;

    const auto add = [](const char* key, const char* label, SettingType type, nlohmann::json defaultValue,
                        nlohmann::json minValue = nullptr, nlohmann::json maxValue = nullptr,
                        std::vector<std::string> enumLabels = {})
    {
        SettingDef def;
        def.key = key;
        def.category = "Accessibility";
        def.label = label;
        def.type = type;
        def.defaultValue = std::move(defaultValue);
        def.minValue = std::move(minValue);
        def.maxValue = std::move(maxValue);
        def.enumLabels = std::move(enumLabels);
        def.flags = static_cast<std::uint32_t>(SettingFlag_Engine | SettingFlag_UserVisible);
        def.onChange = [](const nlohmann::json&)
        {
            AccessibilitySettings& settings = AccessibilitySettings::Instance();
            settings.LoadFromRegistry();
            settings.Apply();
        };
        SettingsRegistry::Instance().Add(std::move(def));
    };

    add("engine.accessibility.uiScale", "Interface scale", SettingType::Float, 1.f, 0.75f, 2.f);
    add("engine.accessibility.colorblindMode", "Colour-blind mode", SettingType::Enum, 0, 0, 4,
        {"Off", "Protanopia", "Deuteranopia", "Tritanopia", "Achromatopsia"});
    add("engine.accessibility.highContrast", "High contrast", SettingType::Bool, false);
    add("engine.accessibility.subtitles", "Subtitles", SettingType::Bool, true);
    add("engine.accessibility.subtitleScale", "Subtitle size", SettingType::Float, 1.f, 0.75f, 2.f);
    add("engine.accessibility.screenReader", "Screen reader", SettingType::Bool, false);
    add("engine.accessibility.ttsVolume", "Speech volume", SettingType::Float, 0.8f, 0.f, 1.f);
    add("engine.accessibility.reduceMotion", "Reduce motion", SettingType::Bool, false);
    add("engine.accessibility.reduceFlash", "Reduce flashing", SettingType::Bool, false);
    add("engine.accessibility.photosensitivity", "Photosensitivity safety mode", SettingType::Bool, false);
    add("engine.accessibility.oneHanded", "One-handed mode", SettingType::Bool, false);
}

void AccessibilitySettings::LoadFromRegistry()
{
    SettingsRegistry& registry = SettingsRegistry::Instance();
    uiScale = std::clamp(registry.Get<float>("engine.accessibility.uiScale", 1.f), 0.75f, 2.f);
    colorblindMode = std::clamp(registry.Get<int>("engine.accessibility.colorblindMode", 0), 0, 4);
    highContrast = registry.Get<bool>("engine.accessibility.highContrast", false);
    subtitles = registry.Get<bool>("engine.accessibility.subtitles", true);
    subtitleScale = std::clamp(registry.Get<float>("engine.accessibility.subtitleScale", 1.f), 0.75f, 2.f);
    screenReader = registry.Get<bool>("engine.accessibility.screenReader", false);
    ttsVolume = std::clamp(registry.Get<float>("engine.accessibility.ttsVolume", 0.8f), 0.f, 1.f);
    reduceMotion = registry.Get<bool>("engine.accessibility.reduceMotion", false);
    reduceFlash = registry.Get<bool>("engine.accessibility.reduceFlash", false);
    photosensitivity = registry.Get<bool>("engine.accessibility.photosensitivity", false);
    oneHanded = registry.Get<bool>("engine.accessibility.oneHanded", false);
}

void AccessibilitySettings::WriteToRegistry() const
{
    SettingsRegistry& registry = SettingsRegistry::Instance();
    registry.Set("engine.accessibility.uiScale", std::clamp(uiScale, 0.75f, 2.f), false);
    registry.Set("engine.accessibility.colorblindMode", std::clamp(colorblindMode, 0, 4), false);
    registry.Set("engine.accessibility.highContrast", highContrast, false);
    registry.Set("engine.accessibility.subtitles", subtitles, false);
    registry.Set("engine.accessibility.subtitleScale", std::clamp(subtitleScale, 0.75f, 2.f), false);
    registry.Set("engine.accessibility.screenReader", screenReader, false);
    registry.Set("engine.accessibility.ttsVolume", std::clamp(ttsVolume, 0.f, 1.f), false);
    registry.Set("engine.accessibility.reduceMotion", reduceMotion, false);
    registry.Set("engine.accessibility.reduceFlash", reduceFlash, false);
    registry.Set("engine.accessibility.photosensitivity", photosensitivity, false);
    registry.Set("engine.accessibility.oneHanded", oneHanded, false);
}

void AccessibilitySettings::Apply()
{
    Accessibility& access = Accessibility::Instance();
    access.SetUiScale(std::clamp(uiScale, 0.75f, 2.f));
    access.SetColorblindMode(static_cast<ColorblindMode>(std::clamp(colorblindMode, 0, 4)));
    access.SetHighContrast(highContrast);
    access.SetTtsVolume(std::clamp(ttsVolume, 0.f, 1.f));
    access.SetOneHandedMode(oneHanded);
    access.SetSubtitlesEnabled(subtitles);
    access.SetSubtitleScale(std::clamp(subtitleScale, 0.75f, 2.f));
    access.SetPhotosensitivity(photosensitivity);
    access.SetReduceMotion(reduceMotion || photosensitivity);
    access.SetReduceFlash(reduceFlash || photosensitivity);
}

void AccessibilitySettings::ResetToDefaults()
{
    uiScale = 1.f;
    colorblindMode = 0;
    highContrast = false;
    subtitles = true;
    subtitleScale = 1.f;
    screenReader = false;
    ttsVolume = 0.8f;
    reduceMotion = false;
    reduceFlash = false;
    photosensitivity = false;
    oneHanded = false;
    WriteToRegistry();
    Apply();
}

nlohmann::json AccessibilitySettings::ToJson() const
{
    WriteToRegistry();
    return nlohmann::json{
        {"uiScale", std::clamp(uiScale, 0.75f, 2.f)},
        {"colorblindMode", std::clamp(colorblindMode, 0, 4)},
        {"highContrast", highContrast},
        {"subtitles", subtitles},
        {"subtitleScale", std::clamp(subtitleScale, 0.75f, 2.f)},
        {"screenReader", screenReader},
        {"ttsVolume", std::clamp(ttsVolume, 0.f, 1.f)},
        {"reduceMotion", reduceMotion},
        {"reduceFlash", reduceFlash},
        {"photosensitivity", photosensitivity},
        {"oneHanded", oneHanded},
    };
}

void AccessibilitySettings::FromJson(const nlohmann::json& json)
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

    number("uiScale", uiScale);
    number("colorblindMode", colorblindMode);
    boolean("highContrast", highContrast);
    boolean("subtitles", subtitles);
    number("subtitleScale", subtitleScale);
    boolean("screenReader", screenReader);
    number("ttsVolume", ttsVolume);
    boolean("reduceMotion", reduceMotion);
    boolean("reduceFlash", reduceFlash);
    boolean("photosensitivity", photosensitivity);
    boolean("oneHanded", oneHanded);

    uiScale = std::clamp(uiScale, 0.75f, 2.f);
    colorblindMode = std::clamp(colorblindMode, 0, 4);
    subtitleScale = std::clamp(subtitleScale, 0.75f, 2.f);
    ttsVolume = std::clamp(ttsVolume, 0.f, 1.f);
    WriteToRegistry();
    Apply();
}

} // namespace Fem
