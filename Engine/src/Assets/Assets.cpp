// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Assets.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/HotSwap.hpp"
#include "FemboyEngine/Jobs.hpp"
#include "FemboyEngine/ResourcePack.hpp"
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <algorithm>

namespace Fem
{

namespace
{
std::string g_fontDirectory;

std::string BundledFont(const char* name)
{
    if (g_fontDirectory.empty())
    {
        return {};
    }
    return (std::filesystem::path(g_fontDirectory) / name).string();
}
} // namespace

AssetManager& AssetManager::Instance()
{
    static AssetManager a;
    return a;
}

void AssetManager::SetFontDirectory(const std::string& directory)
{
    if (g_fontDirectory == directory)
    {
        return;
    }
    g_fontDirectory = directory;
    // The fallback faces are cached lazily; a different directory invalidates
    // them so a project switch cannot keep using the previous game's fonts.
    AssetManager& self = Instance();
    if (self.fallbacksLoaded_)
    {
        self.fallbackFonts_.clear();
        self.fallbacksLoaded_ = false;
    }
    if (self.defaultFont_)
    {
        self.defaultFont_.reset();
    }
}

sf::Texture& AssetManager::PlaceholderTexture()
{
    if (!placeholder_)
    {
        // 16x16 magenta/black checkerboard: instantly recognisable in-game.
        sf::Image img({16, 16}, sf::Color(255, 0, 220));
        for (unsigned y = 0; y < 16; ++y)
        {
            for (unsigned x = 0; x < 16; ++x)
            {
                if (((x / 8) + (y / 8)) % 2 == 0)
                {
                    img.setPixel({x, y}, sf::Color(30, 30, 30));
                }
            }
        }
        placeholder_ = std::make_unique<sf::Texture>();
        if (!placeholder_->loadFromImage(img))
        {
            BOY_WARN("Assets", "placeholder texture upload failed");
        }
    }
    return *placeholder_;
}

std::string AssetManager::DefaultFontPath()
{
    // Prefer the bundled, license-clean UI face. A packaged build must not
    // depend on Segoe UI, Microsoft YaHei, PingFang or any other OS font.
    const std::string bundled = BundledFont("NotoSans-Regular.ttf");
    if (!bundled.empty())
    {
        std::error_code error;
        if (std::filesystem::exists(bundled, error) && !error)
        {
            return bundled;
        }
    }

    // Last-resort development fallback for a checkout without Data/Fonts.
    static const char* candidates[] = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                                       "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                       "/System/Library/Fonts/Helvetica.ttc"};
    std::error_code error;
    for (const char* path : candidates)
    {
        if (std::filesystem::exists(path, error) && !error)
        {
            return path;
        }
        error.clear();
    }
    return {};
}

sf::Font& AssetManager::DefaultFont()
{
    if (!defaultFont_)
    {
        defaultFont_ = std::make_unique<sf::Font>();
        const std::string path = DefaultFontPath();
        if (!path.empty())
        {
            [[maybe_unused]] const bool loaded = defaultFont_->openFromFile(path);
        }
    }
    return *defaultFont_;
}

bool AssetManager::LoadManifest(const std::string& path)
{
    BOY_INFO("Assets", "loading manifest: " + path);
    std::ifstream f(path);
    if (!f.is_open())
    {
        BOY_ERROR("Assets", "cannot open manifest: " + path);
        return false;
    }

    Json j;
    try
    {
        f >> j;
    }
    catch (const std::exception& e)
    {
        BOY_ERROR("Assets", std::string("manifest parse error: ") + e.what());
        return false;
    }
    if (!j.is_object())
    {
        BOY_ERROR("Assets", "manifest root must be an object: " + path);
        return false;
    }

    // Relative paths in a manifest are relative to the manifest, not to the
    // process working directory. Cooked manifests rely on this.
    const std::filesystem::path manifestDir = std::filesystem::path(path).parent_path();
    const auto resolvePath = [&manifestDir](const std::string& value)
    {
        std::filesystem::path resolved(value);
        if (resolved.is_relative())
        {
            resolved = manifestDir / resolved;
        }
        return resolved.lexically_normal().string();
    };

    auto loadTextures = [&](const char* key)
    {
        if (!j.contains(key) || !j[key].is_object())
        {
            return;
        }
        for (auto& [id, p] : j[key].items())
        {
            std::string path;
            bool smooth = texturesSmooth_;
            sf::IntRect region;
            bool hasRegion = false;
            if (p.is_string())
            {
                path = p.get<std::string>();
            }
            else if (p.is_object())
            {
                if (!p.contains("path") || !p["path"].is_string())
                {
                    continue;
                }
                path = p["path"].get<std::string>();
                if (p.contains("smooth") && p["smooth"].is_boolean())
                {
                    smooth = p["smooth"].get<bool>();
                }
                if (p.contains("rect") && p["rect"].is_array() && p["rect"].size() == 4)
                {
                    const int x = p["rect"][0].get<int>();
                    const int y = p["rect"][1].get<int>();
                    const int w = p["rect"][2].get<int>();
                    const int h = p["rect"][3].get<int>();
                    if (w > 0 && h > 0)
                    {
                        region = sf::IntRect({x, y}, {w, h});
                        hasRegion = true;
                    }
                }
            }
            else
            {
                continue;
            }

            path = resolvePath(path);
            texturePaths_[id] = path;
            textureSmooth_[id] = smooth;
            HotSwap::Instance().Watch(path);
            if (hasRegion)
            {
                // One texture object per atlas page, shared by every region id.
                sf::Texture& page = atlasTextures_[path];
                if (page.getSize().x == 0)
                {
                    sf::Texture loaded;
                    if (loaded.loadFromFile(path))
                    {
                        loaded.setSmooth(smooth);
                        page = std::move(loaded);
                        std::error_code ec;
                        const auto stamp = std::filesystem::last_write_time(path, ec);
                        if (!ec)
                        {
                            stamps_["a:" + path] = stamp;
                        }
                    }
                    else
                    {
                        std::error_code ec;
                        const auto stamp = std::filesystem::last_write_time(path, ec);
                        if (!ec)
                        {
                            failedStamps_["a:" + path] = stamp;
                        }
                        BOY_WARN("Assets", "atlas page load failed: " + path);
                    }
                }
                textureRegions_[id] = region;
                continue;
            }

            sf::Texture t;
            if (t.loadFromFile(path))
            {
                t.setSmooth(smooth);
                textures_[id] = std::move(t);
                std::error_code ec;
                const auto stamp = std::filesystem::last_write_time(path, ec);
                if (!ec)
                {
                    stamps_["t:" + id] = stamp;
                }
            }
            else
            {
                std::error_code ec;
                const auto stamp = std::filesystem::last_write_time(path, ec);
                if (!ec)
                {
                    failedStamps_["t:" + id] = stamp;
                }
                BOY_WARN("Assets", "texture load failed: " + id + " -> " + path);
            }
        }
    };
    auto loadFonts = [&](const char* key)
    {
        if (!j.contains(key) || !j[key].is_object())
        {
            return;
        }
        for (auto& [id, p] : j[key].items())
        {
            if (!p.is_string())
            {
                continue;
            }
            const std::string path = resolvePath(p.get<std::string>());
            fontPaths_[id] = path;
            HotSwap::Instance().Watch(path);
            sf::Font fnt;
            if (fnt.openFromFile(path))
            {
                fonts_[id] = std::move(fnt);
                std::error_code ec;
                const auto stamp = std::filesystem::last_write_time(path, ec);
                if (!ec)
                {
                    stamps_["f:" + id] = stamp;
                }
            }
            else
            {
                std::error_code ec;
                const auto stamp = std::filesystem::last_write_time(path, ec);
                if (!ec)
                {
                    failedStamps_["f:" + id] = stamp;
                }
                BOY_WARN("Assets", "font load failed: " + id + " -> " + path);
            }
        }
    };
    auto loadSounds = [&](const char* key)
    {
        if (!j.contains(key) || !j[key].is_object())
        {
            return;
        }
        for (auto& [id, p] : j[key].items())
        {
            if (!p.is_string())
            {
                continue;
            }
            const std::string path = resolvePath(p.get<std::string>());
            soundPaths_[id] = path;
            HotSwap::Instance().Watch(path);
            auto b = std::make_shared<sf::SoundBuffer>();
            if (b->loadFromFile(path))
            {
                sounds_[id] = std::move(b);
                std::error_code ec;
                const auto stamp = std::filesystem::last_write_time(path, ec);
                if (!ec)
                {
                    stamps_["s:" + id] = stamp;
                }
            }
            else
            {
                std::error_code ec;
                const auto stamp = std::filesystem::last_write_time(path, ec);
                if (!ec)
                {
                    failedStamps_["s:" + id] = stamp;
                }
                BOY_WARN("Assets", "sound load failed: " + id + " -> " + path);
            }
        }
    };

    // Stop watching the old set before replacing it. Otherwise a file removed
    // from the manifest would keep firing change events for an asset that is no
    // longer loaded.
    auto unwatchAll = [](const auto& paths)
    {
        for (const auto& [id, path] : paths)
        {
            (void)id;
            HotSwap::Instance().Unwatch(path);
        }
    };
    unwatchAll(texturePaths_);
    unwatchAll(fontPaths_);
    unwatchAll(soundPaths_);

    // A manifest is a complete description of the asset set, not an additive
    // patch: without clearing, resources removed from the manifest stayed alive
    // (and kept being returned by hasTexture()/texture()) until process exit.
    // Doing it after a successful parse means a broken manifest leaves the
    // previous, working set untouched.
    textures_.clear();
    atlasTextures_.clear();
    textureRegions_.clear();
    textureSmooth_.clear();
    texturePaths_.clear();
    pendingTextures_.clear();
    fonts_.clear();
    fontPaths_.clear();
    sounds_.clear();
    soundPaths_.clear();
    musicPaths_.clear();
    stamps_.clear();
    failedStamps_.clear();
    missingLogged_.clear();

    loadTextures("textures");
    loadFonts("fonts");
    loadSounds("sounds");

    // Music is streamed rather than cached, so only remember its path.
    if (j.contains("music") && j["music"].is_object())
    {
        for (auto& [id, p] : j["music"].items())
        {
            if (p.is_string())
            {
                musicPaths_[id] = resolvePath(p.get<std::string>());
            }
        }
    }
    return true;
}

bool AssetManager::LoadPackedManifest(const std::string& packPath, const std::string& cacheDir)
{
    ResourcePack pack;
    if (!pack.Load(packPath))
    {
        return false;
    }
    if (!pack.Has("manifest.json"))
    {
        BOY_ERROR("Assets", "pack has no manifest.json: " + packPath);
        return false;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path source(packPath);
    std::string stem = source.stem().string();
    for (char& c : stem)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && c != '-' && c != '_' && c != '.')
        {
            c = '_';
        }
    }
    if (stem.empty())
    {
        stem = "assets";
    }

    // The pack's own size and timestamp make the cache directory unique per
    // build without hashing tens of megabytes again. Rebuilding the pack changes
    // at least one of them, so stale extracted paths cannot be picked up.
    const auto size = fs::file_size(packPath, ec);
    const std::uint64_t bytes = ec ? 0ull : static_cast<std::uint64_t>(size);
    ec.clear();
    const auto stamp = fs::last_write_time(packPath, ec);
    const std::uint64_t stampValue = ec ? 0ull : static_cast<std::uint64_t>(stamp.time_since_epoch().count());
    const std::uint64_t key = bytes * 1099511628211ull ^ stampValue;

    fs::path root = cacheDir.empty() ? fs::temp_directory_path(ec) / "femboyengine-assets" : fs::path(cacheDir);
    if (ec)
    {
        BOY_ERROR("Assets", "cannot resolve asset cache directory: " + ec.message());
        return false;
    }
    const fs::path extracted = root / (stem + "_" + std::to_string(key));
    fs::create_directories(root, ec);
    if (ec)
    {
        BOY_ERROR("Assets", "cannot create asset cache directory: " + ec.message());
        return false;
    }
    if (!pack.Extract(extracted.string()))
    {
        return false;
    }

    BOY_INFO("Assets", "mounted pack " + packPath + " at " + extracted.string());
    return LoadManifest((extracted / "manifest.json").string());
}

sf::Texture& AssetManager::Texture(const std::string& id)
{
    // Completed background decodes become GPU textures here, on the main thread.
    PumpAsync();
    auto it = textures_.find(id);
    if (it != textures_.end())
    {
        return it->second;
    }
    auto region = textureRegions_.find(id);
    if (region != textureRegions_.end())
    {
        auto path = texturePaths_.find(id);
        if (path != texturePaths_.end())
        {
            auto page = atlasTextures_.find(path->second);
            if (page != atlasTextures_.end() && page->second.getSize().x > 0)
            {
                return page->second;
            }
        }
    }
    if (std::find(missingLogged_.begin(), missingLogged_.end(), "t:" + id) == missingLogged_.end())
    {
        missingLogged_.push_back("t:" + id);
        BOY_WARN("Assets", "missing texture: " + id + " (using placeholder)");
    }
    return PlaceholderTexture();
}

sf::IntRect AssetManager::TextureRect(const std::string& id) const
{
    const auto region = textureRegions_.find(id);
    if (region != textureRegions_.end())
    {
        return region->second;
    }
    const auto texture = textures_.find(id);
    if (texture != textures_.end())
    {
        return sf::IntRect(
            {0, 0}, {static_cast<int>(texture->second.getSize().x), static_cast<int>(texture->second.getSize().y)});
    }
    return {};
}

sf::Font& AssetManager::Font(const std::string& id)
{
    auto it = fonts_.find(id);
    if (it != fonts_.end())
    {
        return it->second;
    }
    if (std::find(missingLogged_.begin(), missingLogged_.end(), "f:" + id) == missingLogged_.end())
    {
        missingLogged_.push_back("f:" + id);
        BOY_WARN("Assets", "missing font: " + id + " (using default)");
    }
    return DefaultFont();
}

sf::Font& AssetManager::FontForText(const std::string& id, const sf::String& text)
{
    sf::Font& primary = Font(id);

    bool missing = false;
    for (const char32_t codePoint : text)
    {
        if (codePoint == U'\n' || codePoint == U'\r')
        {
            continue;
        }
        if (!primary.hasGlyph(codePoint))
        {
            missing = true;
            break;
        }
    }
    if (!missing)
    {
        return primary;
    }

    if (!fallbacksLoaded_)
    {
        fallbacksLoaded_ = true;
        std::vector<std::string> candidates;
        const auto add = [&candidates](const std::string& path)
        {
            if (!path.empty())
            {
                candidates.push_back(path);
            }
        };

        // Bundled script faces first. One of these covers every language the
        // game ships; no system font is required on Windows, Linux or macOS.
        add(BundledFont("NotoSansCJK-Regular.ttc"));
        add(BundledFont("NotoSansArabic-Regular.ttf"));
        add(BundledFont("NotoSansHebrew-Regular.ttf"));
        add(BundledFont("NotoSansThai-Regular.ttf"));
        add(BundledFont("NotoSansDevanagari-Regular.ttf"));
        add(BundledFont("NotoSansBengali-Regular.ttf"));
        add(BundledFont("NotoSansTelugu-Regular.ttf"));
        add(BundledFont("NotoSansTamil-Regular.ttf"));
        add(BundledFont("NotoSans-Regular.ttf"));

        // Development fallback when the checkout has no Data/Fonts directory.
        // These are last resort only; packaged builds always use the bundled
        // faces above.
        if (g_fontDirectory.empty())
        {
#if defined(_WIN32)
            candidates.insert(candidates.end(),
                              {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simsun.ttc",
                               "C:/Windows/Fonts/YuGothR.ttc", "C:/Windows/Fonts/malgun.ttf",
                               "C:/Windows/Fonts/Nirmala.ttc", "C:/Windows/Fonts/LeelawUI.ttf",
                               "C:/Windows/Fonts/NotoSansHebrew-Regular.ttf",
                               "C:/Windows/Fonts/NotoSansArabic-Regular.ttf", "C:/Windows/Fonts/seguisym.ttf"});
#elif defined(__APPLE__)
            candidates.insert(candidates.end(),
                              {"/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/Apple Symbols.ttf"});
#else
            candidates.insert(candidates.end(),
                              {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                               "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                               "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"});
#endif
        }

        for (const std::string& path : candidates)
        {
            auto candidate = std::make_unique<sf::Font>();
            if (candidate->openFromFile(path))
            {
                fallbackFonts_.push_back(std::move(candidate));
            }
        }
    }

    for (auto& candidate : fallbackFonts_)
    {
        bool coversAll = true;
        for (const char32_t codePoint : text)
        {
            if (codePoint == U'\n' || codePoint == U'\r')
            {
                continue;
            }
            if (!candidate->hasGlyph(codePoint))
            {
                coversAll = false;
                break;
            }
        }
        if (coversAll)
        {
            return *candidate;
        }
    }

    // Partial coverage is better than tofu: the primary still renders Latin, and
    // the caller can split the run if a project ships its own fallback chain.
    return primary;
}

void AssetManager::SetTextSmooth(bool smooth)
{
    for (auto& [id, f] : fonts_)
    {
        (void)id;
        f.setSmooth(smooth);
    }
    if (defaultFont_)
    {
        defaultFont_->setSmooth(smooth);
    }
    for (auto& f : fallbackFonts_)
    {
        f->setSmooth(smooth);
    }
}

void AssetManager::SetTexturesSmooth(bool smooth)
{
    texturesSmooth_ = smooth;
    for (auto& [id, texture] : textures_)
    {
        (void)id;
        texture.setSmooth(smooth);
    }
    for (auto& [path, texture] : atlasTextures_)
    {
        (void)path;
        texture.setSmooth(smooth);
    }
    if (placeholder_)
    {
        placeholder_->setSmooth(smooth);
    }
}

sf::SoundBuffer& AssetManager::Sound(const std::string& id)
{
    static sf::SoundBuffer empty;
    auto it = sounds_.find(id);
    if (it != sounds_.end() && it->second)
    {
        return *it->second;
    }
    if (std::find(missingLogged_.begin(), missingLogged_.end(), "s:" + id) == missingLogged_.end())
    {
        missingLogged_.push_back("s:" + id);
        BOY_WARN("Assets", "missing sound: " + id);
    }
    return empty;
}

std::shared_ptr<sf::SoundBuffer> AssetManager::SoundShared(const std::string& id)
{
    auto it = sounds_.find(id);
    if (it != sounds_.end() && it->second)
    {
        return it->second;
    }
    if (std::find(missingLogged_.begin(), missingLogged_.end(), "s:" + id) == missingLogged_.end())
    {
        missingLogged_.push_back("s:" + id);
        BOY_WARN("Assets", "missing sound: " + id);
    }
    return nullptr;
}

bool AssetManager::HasTexture(const std::string& id) const
{
    if (textures_.count(id) > 0)
    {
        return true;
    }
    const auto region = textureRegions_.find(id);
    if (region == textureRegions_.end())
    {
        return false;
    }
    const auto path = texturePaths_.find(id);
    if (path == texturePaths_.end())
    {
        return false;
    }
    const auto page = atlasTextures_.find(path->second);
    return page != atlasTextures_.end() && page->second.getSize().x > 0;
}

bool AssetManager::CreateTexture(const std::string& id, const Image& image)
{
    return CreateTexture(id, static_cast<unsigned>(image.Width()), static_cast<unsigned>(image.Height()), image.Data());
}

bool AssetManager::CreateTexture(const std::string& id, unsigned width, unsigned height, const std::uint8_t* rgba)
{
    if (id.empty() || width == 0 || height == 0 || rgba == nullptr)
    {
        BOY_ERROR("Assets", "createTexture: invalid arguments for '" + id + "'");
        return false;
    }

    sf::Texture texture;
    if (!texture.resize({width, height}))
    {
        BOY_ERROR("Assets", "createTexture: could not allocate " + std::to_string(width) + "x" +
                                std::to_string(height) + " for '" + id + "'");
        return false;
    }
    texture.update(rgba);
    // Generated art is usually drawn scaled: smooth filtering keeps it from
    // looking like a block of solid pixels. Pixel-art projects opt out through
    // setTexturesSmooth(false), and this flag is the current setting.
    texture.setSmooth(texturesSmooth_);
    textures_[id] = std::move(texture);
    return true;
}
bool AssetManager::LoadTextureAsync(const std::string& id, const std::string& path, bool smooth)
{
    if (id.empty() || path.empty())
    {
        return false;
    }
    HotSwap::Instance().Watch(path);
    pendingTextures_[id] = PendingTexture{path, smooth,
                                          JobSystem::Instance().Submit(
                                              [path]()
                                              {
                                                  sf::Image image;
                                                  if (!image.loadFromFile(path))
                                                  {
                                                      return sf::Image();
                                                  }
                                                  return image;
                                              })};
    return true;
}

bool AssetManager::IsTextureLoading(const std::string& id) const
{
    return pendingTextures_.count(id) > 0;
}

void AssetManager::PumpAsync()
{
    for (auto it = pendingTextures_.begin(); it != pendingTextures_.end();)
    {
        if (it->second.image.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            ++it;
            continue;
        }

        sf::Image image = it->second.image.get();
        const bool valid = image.getSize().x > 0 && image.getSize().y > 0;
        if (valid)
        {
            sf::Texture texture;
            if (texture.loadFromImage(image))
            {
                texture.setSmooth(it->second.smooth);
                textures_[it->first] = std::move(texture);
                texturePaths_[it->first] = it->second.path;
                textureSmooth_[it->first] = it->second.smooth;
                std::error_code ec;
                const auto stamp = std::filesystem::last_write_time(it->second.path, ec);
                if (!ec)
                {
                    stamps_["t:" + it->first] = stamp;
                }
            }
            else
            {
                BOY_WARN("Assets", "async texture upload failed: " + it->first);
            }
        }
        else
        {
            BOY_WARN("Assets", "async texture decode failed: " + it->first + " -> " + it->second.path);
        }
        it = pendingTextures_.erase(it);
    }
}

bool AssetManager::HasFont(const std::string& id) const
{
    return fonts_.count(id) > 0;
}
bool AssetManager::HasSound(const std::string& id) const
{
    return sounds_.count(id) > 0;
}

bool AssetManager::HasMusic(const std::string& id) const
{
    return musicPaths_.count(id) > 0;
}

std::string AssetManager::MusicPath(const std::string& id) const
{
    auto it = musicPaths_.find(id);
    return it == musicPaths_.end() ? std::string{} : it->second;
}

void AssetManager::HotReload()
{
    size_t reloaded = 0;

    auto changed = [&](const std::string& key, const std::string& path, std::filesystem::file_time_type& stamp) -> bool
    {
        std::error_code ec;
        stamp = std::filesystem::last_write_time(path, ec);
        if (ec)
        {
            return false;
        }
        auto loaded = stamps_.find(key);
        if (loaded != stamps_.end() && loaded->second == stamp)
        {
            return false;
        }
        // A file that already failed to parse at this exact timestamp is not
        // retried until the editor writes another version.
        auto failed = failedStamps_.find(key);
        return failed == failedStamps_.end() || failed->second != stamp;
    };

    auto succeeded = [&](const std::string& key, const std::filesystem::file_time_type& stamp)
    {
        stamps_[key] = stamp;
        failedStamps_.erase(key);
        ++reloaded;
    };
    auto failed = [&](const std::string& key, const std::string& path, const std::filesystem::file_time_type& stamp,
                      const char* what)
    {
        failedStamps_[key] = stamp;
        BOY_WARN("Assets", std::string("hotReload: ") + what + " reload failed: " + path);
    };

    for (auto& [id, path] : texturePaths_)
    {
        if (textureRegions_.count(id) > 0)
        {
            continue; // shared atlas page, handled below
        }
        std::filesystem::file_time_type stamp;
        const std::string key = "t:" + id;
        if (!changed(key, path, stamp))
        {
            continue;
        }
        sf::Texture t;
        if (t.loadFromFile(path))
        {
            t.setSmooth(texturesSmooth_);
            textures_[id] = std::move(t);
            succeeded(key, stamp);
        }
        else
        {
            failed(key, path, stamp, "texture");
        }
    }

    for (auto& [path, texture] : atlasTextures_)
    {
        std::filesystem::file_time_type stamp;
        const std::string key = "a:" + path;
        if (!changed(key, path, stamp))
        {
            continue;
        }
        sf::Texture t;
        if (t.loadFromFile(path))
        {
            t.setSmooth(texturesSmooth_);
            texture = std::move(t);
            succeeded(key, stamp);
        }
        else
        {
            failed(key, path, stamp, "atlas");
        }
    }

    for (auto& [id, path] : fontPaths_)
    {
        std::filesystem::file_time_type stamp;
        const std::string key = "f:" + id;
        if (!changed(key, path, stamp))
        {
            continue;
        }
        sf::Font fnt;
        if (fnt.openFromFile(path))
        {
            fonts_[id] = std::move(fnt);
            succeeded(key, stamp);
        }
        else
        {
            failed(key, path, stamp, "font");
        }
    }

    for (auto& [id, path] : soundPaths_)
    {
        std::filesystem::file_time_type stamp;
        const std::string key = "s:" + id;
        if (!changed(key, path, stamp))
        {
            continue;
        }
        auto b = std::make_shared<sf::SoundBuffer>();
        if (b->loadFromFile(path))
        {
            // Voices keep the old buffer alive through their shared_ptr, so the
            // swap is safe even while the sound is still playing.
            sounds_[id] = std::move(b);
            succeeded(key, stamp);
        }
        else
        {
            failed(key, path, stamp, "sound");
        }
    }

    if (reloaded > 0)
    {
        BOY_INFO("Assets", "hotReload: refreshed " + std::to_string(reloaded) + " asset(s)");
    }
}

void AssetManager::Clear()
{
    for (const auto& [id, path] : texturePaths_)
    {
        (void)id;
        HotSwap::Instance().Unwatch(path);
    }
    for (const auto& [id, path] : fontPaths_)
    {
        (void)id;
        HotSwap::Instance().Unwatch(path);
    }
    for (const auto& [id, path] : soundPaths_)
    {
        (void)id;
        HotSwap::Instance().Unwatch(path);
    }
    textures_.clear();
    atlasTextures_.clear();
    textureRegions_.clear();
    textureSmooth_.clear();
    pendingTextures_.clear();
    fonts_.clear();
    sounds_.clear();
    texturePaths_.clear();
    fontPaths_.clear();
    soundPaths_.clear();
    musicPaths_.clear();
    stamps_.clear();
    failedStamps_.clear();
    missingLogged_.clear();
}

} // namespace Fem
