// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include <future>
#include <vector>
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/Image.hpp"

namespace Fem
{

// Asset cache. Missing ids never throw: a visible magenta placeholder texture
// and a default font are returned instead, and the miss is logged once.
class AssetManager
{
public:
    static AssetManager& Instance();
    // First existing system font that the default text path can use. Exposed so
    // asset-generation tools can put the same font into their manifest instead
    // of maintaining a second platform-specific candidate list.
    static std::string DefaultFontPath();
    // Directory containing the bundled Noto fonts. When set, the primary UI
    // face and every script fallback come from this directory, so packaged
    // games do not depend on any OS font being installed. Resetting the
    // directory invalidates the lazily loaded fallback faces.
    static void SetFontDirectory(const std::string& directory);
    bool LoadManifest(const std::string& path); // JSON manifest
    // Mounts a cooked .fempak produced by AssetCooker. The pack is extracted
    // into a per-pack subdirectory of `cacheDir` (or the system temp directory
    // when empty) and its cooked manifest is loaded. This keeps the normal
    // file-based SFML loaders unchanged while still shipping a single archive.
    bool LoadPackedManifest(const std::string& packPath, const std::string& cacheDir = {});
    sf::Texture& Texture(const std::string& id);
    sf::Font& Font(const std::string& id);
    // Picks a font that can render every code point in `text`: `id` first, then
    // a lazily loaded system fallback (Microsoft YaHei for CJK, Segoe UI Emoji,
    // Noto CJK on Linux). Per-run fallback, not per-glyph, keeps shaping and
    // kerning intact.
    sf::Font& FontForText(const std::string& id, const sf::String& text);
    // Nearest-fast text for pixel-art mode. Affects every loaded font because
    // SFML stores the glyph atlas inside the font object.
    void SetTextSmooth(bool smooth);
    // Global texture filtering. Pixel-art mode turns this off; newly loaded and
    // hot-reloaded textures inherit the current setting.
    void SetTexturesSmooth(bool smooth);
    bool TexturesSmooth() const
    {
        return texturesSmooth_;
    }
    sf::SoundBuffer& Sound(const std::string& id);
    // Shared ownership of a sound buffer. Voices keep the shared_ptr alive, so a
    // hot reload that swaps the cache entry can no longer free a buffer that a
    // playing sf::Sound still points at.
    std::shared_ptr<sf::SoundBuffer> SoundShared(const std::string& id);
    bool HasTexture(const std::string& id) const;
    bool HasFont(const std::string& id) const;
    bool HasSound(const std::string& id) const;

    // Atlas-aware lookup. A cooked manifest entry may map an id to a rectangle
    // inside a shared page; Texture() returns the page and TextureRect() the
    // sub-rectangle. Standalone textures return their full size.
    sf::IntRect TextureRect(const std::string& id) const;

    // Asynchronous CPU decode. The worker only calls sf::Image::LoadFromFile;
    // the GPU upload happens on the main thread in PumpAsync(). While the job is
    // pending, texture(id) keeps returning the placeholder. Engine::Run() pumps
    // this once per frame; tools that do not run the engine must call it too.
    bool LoadTextureAsync(const std::string& id, const std::string& path, bool smooth = true);
    bool IsTextureLoading(const std::string& id) const;
    void PumpAsync();

    // Registers a GPU texture from CPU pixels under `id`, replacing any cached
    // texture with that name. This is the in-memory path for procedural art:
    // the project generates an Image and never writes a file.
    bool CreateTexture(const std::string& id, const Image& image);
    bool CreateTexture(const std::string& id, unsigned width, unsigned height, const std::uint8_t* rgba);

    // Music is streamed by sf::Music, so only the resolved path is cached.
    bool HasMusic(const std::string& id) const;
    std::string MusicPath(const std::string& id) const;
    void HotReload(); // reload changed files
    void Clear();

private:
    AssetManager() = default;
    sf::Texture& PlaceholderTexture();
    sf::Font& DefaultFont();

    std::unordered_map<std::string, sf::Texture> textures_;
    std::unordered_map<std::string, sf::Font> fonts_;
    std::unordered_map<std::string, std::shared_ptr<sf::SoundBuffer>> sounds_;
    // Cooked atlas pages are shared by every id that maps into the same PNG.
    std::unordered_map<std::string, sf::Texture> atlasTextures_;
    std::unordered_map<std::string, sf::IntRect> textureRegions_;

    // Source paths remembered from the manifest so hotReload() can actually
    // re-read a changed file instead of only reporting counts.
    std::unordered_map<std::string, std::string> texturePaths_;
    std::unordered_map<std::string, bool> textureSmooth_;
    std::unordered_map<std::string, std::string> fontPaths_;
    std::unordered_map<std::string, std::string> soundPaths_;
    std::unordered_map<std::string, std::string> musicPaths_;
    std::unordered_map<std::string, std::filesystem::file_time_type> stamps_;
    // A failed reload is retried only after the file timestamp changes. Without
    // this, a half-written PNG would be retried (and logged) on every frame.
    std::unordered_map<std::string, std::filesystem::file_time_type> failedStamps_;

    struct PendingTexture
    {
        std::string path;
        bool smooth = true;
        std::future<sf::Image> image;
    };
    std::unordered_map<std::string, PendingTexture> pendingTextures_;

    std::unique_ptr<sf::Texture> placeholder_;
    std::unique_ptr<sf::Font> defaultFont_;
    std::vector<std::unique_ptr<sf::Font>> fallbackFonts_;
    bool fallbacksLoaded_ = false;
    bool texturesSmooth_ = true;
    std::vector<std::string> missingLogged_;
};

} // namespace Fem
