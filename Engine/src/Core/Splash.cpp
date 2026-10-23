// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
//
// The engine boot splash.
//
// It shows an optional chain of studio/publisher cards, then the engine mark:
// Logo.png (embedded at configure time) with a clean wordmark drawn from a real
// bold system font. Everything fades in and out; any key press or click skips
// the rest of the sequence. The old splash generated its own illustration and
// stroke font at runtime, which looked wrong at small sizes and could not use
// the project's own art - that whole approach is gone.
#include "Splash.hpp"

#include "FemboyLogoData.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Window.hpp"

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace Fem
{
namespace Splash
{
namespace
{

constexpr float kFadeIn = 0.40f;
constexpr float kFadeOut = 0.40f;
constexpr const char* kEngineCaption = "FEMBOY ENGINE";

float Clamp01(float value)
{
    return value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
}

float Smoothstep01(float value)
{
    value = Clamp01(value);
    return value * value * (3.f - 2.f * value);
}

float CardDuration(float hold)
{
    return kFadeIn + hold + kFadeOut;
}

// Opacity of a card `elapsed` seconds into its own lifetime.
float CardAlpha(float elapsed, float hold)
{
    if (elapsed <= 0.f)
    {
        return 0.f;
    }
    if (elapsed < kFadeIn)
    {
        return Smoothstep01(elapsed / kFadeIn);
    }
    if (elapsed < kFadeIn + hold)
    {
        return 1.f;
    }
    const float total = CardDuration(hold);
    if (elapsed >= total)
    {
        return 0.f;
    }
    return 1.f - Smoothstep01((elapsed - kFadeIn - hold) / kFadeOut);
}

// A real bold sans face is what makes the wordmark look designed instead of
// drawn. The first candidate that opens wins; if none does the caption is
// simply omitted rather than rendered with a fallback stroke font.
bool LoadBootFont(sf::Font& font)
{
    static const std::array<const char*, 10> candidates = {
        "Data/Fonts/NotoSans-Regular.ttf",
        "assets/fonts/NotoSans-Regular.ttf",
        "C:/Windows/Fonts/seguisb.ttf",  // Segoe UI Semibold
        "C:/Windows/Fonts/segoeuib.ttf", // Segoe UI Bold
        "C:/Windows/Fonts/arialbd.ttf",  // Arial Bold
        "C:/Windows/Fonts/verdanab.ttf", // Verdana Bold
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };

    for (const char* path : candidates)
    {
        std::error_code error;
        if (!std::filesystem::exists(path, error) || error)
        {
            continue;
        }
        if (font.openFromFile(path))
        {
            return true;
        }
    }
    return false;
}

const sf::Font* BootFont()
{
    static sf::Font font;
    static const bool ready = LoadBootFont(font);
    return ready ? &font : nullptr;
}

void DrawCaption(sf::RenderTarget& target, const std::string& value, float centerX, float topY, float pixelSize,
                 sf::Color color)
{
    const sf::Font* font = BootFont();
    if (font == nullptr || value.empty())
    {
        return;
    }

    sf::Text text(*font, value, static_cast<unsigned>(std::max(10.f, pixelSize)));
    text.setFillColor(color);
    // Tracking keeps the caps even; without it a real font is still smooth, but
    // the lines read as a wordmark rather than a sentence.
    text.setLetterSpacing(1.5f);

    const sf::FloatRect bounds = text.getLocalBounds();
    text.setOrigin({bounds.position.x + bounds.size.x * 0.5f, bounds.position.y});
    text.setPosition({centerX, topY});
    target.draw(text);
}

struct LoadedCard
{
    sf::Texture texture;
    bool hasImage = false;
    std::string caption;
    float hold = 1.6f;
    float start = 0.f; // seconds into the whole sequence
};

sf::Texture LoadEmbeddedLogo()
{
    if (kFemboyLogoSize == 0)
    {
        return sf::Texture();
    }

    sf::Image image;
    if (!image.loadFromMemory(kFemboyLogoPng, kFemboyLogoSize))
    {
        return sf::Texture();
    }

    sf::Texture texture(image);
    texture.setSmooth(true);
    return texture;
}

} // namespace

bool Play(Window& window, const std::vector<SplashCard>& cards, float engineHold)
{
    if (!window.IsOpen())
    {
        return false;
    }

    std::vector<SplashCard> source;
    for (const SplashCard& card : cards)
    {
        if (!card.imagePath.empty() || !card.caption.empty())
        {
            source.push_back(card);
        }
    }

    if (engineHold > 0.f)
    {
        SplashCard engine;
        engine.caption = std::string(kEngineCaption) + "   " + FEMBOY_ENGINE_VERSION;
        engine.hold = engineHold;
        engine.embeddedLogo = true;
        source.push_back(std::move(engine));
    }

    if (source.empty())
    {
        return window.IsOpen();
    }

    // Load every texture before the first frame: a card that streams in while
    // it is already fading is visible as a hitch.
    std::vector<LoadedCard> loaded;
    loaded.reserve(source.size());
    float timeline = 0.f;
    for (const SplashCard& card : source)
    {
        LoadedCard entry;
        entry.caption = card.caption;
        entry.hold = std::max(0.2f, card.hold);
        entry.start = timeline;

        if (card.embeddedLogo)
        {
            entry.texture = LoadEmbeddedLogo();
            entry.hasImage = entry.texture.getSize().x > 0;
        }
        else if (!card.imagePath.empty())
        {
            if (entry.texture.loadFromFile(card.imagePath))
            {
                entry.texture.setSmooth(true);
                entry.hasImage = true;
            }
            else
            {
                BOY_WARN("Splash", "cannot load splash image: " + card.imagePath);
            }
        }

        timeline += CardDuration(entry.hold);
        loaded.push_back(std::move(entry));
    }
    const float total = timeline;

    sf::RenderWindow& win = window.Handle();
    const char* capturePath = std::getenv("FEMBOY_SPLASH_CAPTURE");
    bool captured = false;

    BOY_INFO("Splash", "showing boot splash");
    sf::Clock clock;

    while (window.IsOpen())
    {
        while (const std::optional<sf::Event> event = win.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                win.close();
                return false;
            }
            if (const auto* resized = event->getIf<sf::Event::Resized>())
            {
                window.SetSize(static_cast<int>(resized->size.x), static_cast<int>(resized->size.y));
            }

            // Any key or click skips the rest of the boot sequence.
            if (event->is<sf::Event::KeyPressed>() || event->is<sf::Event::MouseButtonPressed>())
            {
                BOY_INFO("Splash", "skipped by input");
                return window.IsOpen();
            }
        }

        const float elapsed = clock.getElapsedTime().asSeconds();
        if (elapsed >= total)
        {
            break;
        }

        const LoadedCard* card = &loaded.back();
        for (const LoadedCard& candidate : loaded)
        {
            if (elapsed < candidate.start + CardDuration(candidate.hold))
            {
                card = &candidate;
                break;
            }
        }

        const float local = elapsed - card->start;
        const float alpha = CardAlpha(local, card->hold);
        const float ease = Smoothstep01(std::min(local / kFadeIn, 1.f));

        const float w = static_cast<float>(win.getSize().x);
        const float h = static_cast<float>(win.getSize().y);
        if (w < 2.f || h < 2.f)
        {
            break;
        }

        // The whole card grows a hair as it fades in so the reveal reads as
        // motion rather than a plain fade.
        const float scale = 0.97f + 0.03f * ease;
        sf::View view(sf::FloatRect({0.f, 0.f}, {w, h}));
        view.zoom(1.f / scale);
        view.move({0.f, -(1.f - ease) * 10.f});
        win.setView(view);

        win.clear(sf::Color::Black);

        const auto tint = sf::Color(255, 255, 255, static_cast<std::uint8_t>(std::clamp(alpha, 0.f, 1.f) * 255.f));

        if (card->hasImage)
        {
            const sf::Vector2u size = card->texture.getSize();
            const float aspect = size.y > 0 ? static_cast<float>(size.x) / static_cast<float>(size.y) : 1.f;
            const float imageH = std::min(h * 0.46f, (w * 0.40f) / std::max(0.1f, aspect));
            const float imageW = imageH * aspect;
            const float captionSize = card->caption.empty() ? 0.f : std::max(18.f, imageH * 0.11f);
            const float blockH = imageH + (card->caption.empty() ? 0.f : captionSize * 2.1f);
            const float top = (h - blockH) * 0.5f;

            sf::Sprite sprite(card->texture);
            sprite.setScale({imageW / static_cast<float>(size.x), imageH / static_cast<float>(size.y)});
            sprite.setPosition({w * 0.5f - imageW * 0.5f, top});
            sprite.setColor(tint);
            win.draw(sprite);

            if (!card->caption.empty())
            {
                DrawCaption(win, card->caption, w * 0.5f, top + imageH + captionSize * 0.6f, captionSize, tint);
            }
        }
        else if (!card->caption.empty())
        {
            const float size = std::max(18.f, h * 0.055f);
            DrawCaption(win, card->caption, w * 0.5f, h * 0.5f - size * 0.6f, size, tint);
        }

        // Read the back buffer before the swap; this is the same trick the
        // engine's --screenshot path uses and it works under DPI scaling.
        if (capturePath != nullptr && !captured && alpha > 0.97f)
        {
            sf::Texture shot(sf::Vector2u(static_cast<unsigned>(w), static_cast<unsigned>(h)));
            shot.update(win);
            const bool saved = shot.copyToImage().saveToFile(capturePath);
            BOY_INFO("Splash", std::string(saved ? "captured: " : "capture failed: ") + capturePath);
            captured = true;
        }

        win.display();
    }

    BOY_INFO("Splash", "boot splash finished");
    return window.IsOpen();
}

} // namespace Splash
} // namespace Fem
