// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/AssetPipeline.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/ResourcePack.hpp"
#include "FemboyEngine/Vanity.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <fstream>
#include <limits>
#include <set>
#include <unordered_set>

namespace Fem
{
namespace
{
namespace fs = std::filesystem;

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string ExtensionOf(const std::string& path)
{
    return Lower(fs::path(path).extension().string());
}

AssetKind InferKind(const std::string& path, AssetKind fallback = AssetKind::Unknown)
{
    const std::string ext = ExtensionOf(path);
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".gif" ||
        ext == ".psd")
    {
        return AssetKind::Texture;
    }
    if (ext == ".ttf" || ext == ".otf" || ext == ".ttc")
    {
        return AssetKind::Font;
    }
    if (ext == ".wav" || ext == ".flac" || ext == ".ogg" || ext == ".mp3" || ext == ".opus")
    {
        return AssetKind::Sound;
    }
    if (ext == ".json" || ext == ".txt" || ext == ".scene" || ext == ".prefab" || ext == ".fbs")
    {
        return AssetKind::Data;
    }
    return fallback;
}

bool NonNegative(int value)
{
    return value >= 0;
}

void AppendError(std::vector<std::string>* out, const std::string& message)
{
    if (out)
    {
        out->push_back(message);
    }
}

std::string SafeOutputPath(const std::string& requested, const std::string& id, const std::string& fallbackExt)
{
    if (requested.empty())
    {
        return Vanity::SanitizeFileName(id) + fallbackExt;
    }

    fs::path p(requested);
    if (p.is_absolute() || p.has_root_name() || p.has_root_directory())
    {
        p = p.filename();
    }
    fs::path safe;
    for (const fs::path& part : p)
    {
        const std::string text = part.string();
        if (text.empty() || text == ".")
        {
            continue;
        }
        if (text == "..")
        {
            safe.clear();
            break;
        }
        safe /= Vanity::SanitizeFileName(text, "asset");
    }
    if (safe.empty())
    {
        return Vanity::SanitizeFileName(id) + fallbackExt;
    }
    return safe.generic_string();
}

std::string RelativeTo(const fs::path& file, const fs::path& root)
{
    std::error_code ec;
    fs::path rel = fs::relative(file, root, ec);
    if (!ec && !rel.empty())
    {
        return rel.generic_string();
    }
    rel = file.lexically_relative(root);
    return rel.empty() ? file.filename().generic_string() : rel.generic_string();
}

sf::Image ResizeImage(const sf::Image& source, unsigned width, unsigned height)
{
    const sf::Vector2u sourceSize = source.getSize();
    if (sourceSize.x == 0 || sourceSize.y == 0 || width == 0 || height == 0)
    {
        return sf::Image(sf::Vector2u{std::max(1u, width), std::max(1u, height)});
    }
    if (sourceSize.x == width && sourceSize.y == height)
    {
        return source;
    }

    sf::Image out(sf::Vector2u{width, height});
    const float scaleX = static_cast<float>(sourceSize.x) / static_cast<float>(width);
    const float scaleY = static_cast<float>(sourceSize.y) / static_cast<float>(height);
    for (unsigned y = 0; y < height; ++y)
    {
        for (unsigned x = 0; x < width; ++x)
        {
            const float sx = (static_cast<float>(x) + 0.5f) * scaleX - 0.5f;
            const float sy = (static_cast<float>(y) + 0.5f) * scaleY - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(sx)), 0, static_cast<int>(sourceSize.x) - 1);
            const int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, static_cast<int>(sourceSize.y) - 1);
            const int x1 = std::clamp(x0 + 1, 0, static_cast<int>(sourceSize.x) - 1);
            const int y1 = std::clamp(y0 + 1, 0, static_cast<int>(sourceSize.y) - 1);
            const float fx = std::clamp(sx - static_cast<float>(x0), 0.f, 1.f);
            const float fy = std::clamp(sy - static_cast<float>(y0), 0.f, 1.f);

            const sf::Color c00 = source.getPixel({static_cast<unsigned>(x0), static_cast<unsigned>(y0)});
            const sf::Color c10 = source.getPixel({static_cast<unsigned>(x1), static_cast<unsigned>(y0)});
            const sf::Color c01 = source.getPixel({static_cast<unsigned>(x0), static_cast<unsigned>(y1)});
            const sf::Color c11 = source.getPixel({static_cast<unsigned>(x1), static_cast<unsigned>(y1)});
            const auto mix = [fx, fy](std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d)
            {
                const float top = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * fx;
                const float bottom = static_cast<float>(c) + (static_cast<float>(d) - static_cast<float>(c)) * fx;
                return static_cast<std::uint8_t>(std::clamp(top + (bottom - top) * fy, 0.f, 255.f));
            };
            out.setPixel({x, y}, sf::Color(mix(c00.r, c10.r, c01.r, c11.r), mix(c00.g, c10.g, c01.g, c11.g),
                                           mix(c00.b, c10.b, c01.b, c11.b), mix(c00.a, c10.a, c01.a, c11.a)));
        }
    }
    return out;
}

void PremultiplyAlpha(sf::Image& image)
{
    const sf::Vector2u size = image.getSize();
    for (unsigned y = 0; y < size.y; ++y)
    {
        for (unsigned x = 0; x < size.x; ++x)
        {
            sf::Color color = image.getPixel({x, y});
            const unsigned alpha = color.a;
            color.r = static_cast<std::uint8_t>((static_cast<unsigned>(color.r) * alpha + 127u) / 255u);
            color.g = static_cast<std::uint8_t>((static_cast<unsigned>(color.g) * alpha + 127u) / 255u);
            color.b = static_cast<std::uint8_t>((static_cast<unsigned>(color.b) * alpha + 127u) / 255u);
            image.setPixel({x, y}, color);
        }
    }
}

sf::Image PrepareTexture(const sf::Image& source, const AssetImportSettings& settings)
{
    sf::Image image = source;
    if (settings.premultiplyAlpha)
    {
        PremultiplyAlpha(image);
    }

    unsigned width = image.getSize().x;
    unsigned height = image.getSize().y;
    if (width == 0 || height == 0)
    {
        return image;
    }

    if (settings.maxSize > 0)
    {
        const unsigned limit = static_cast<unsigned>(settings.maxSize);
        const unsigned largest = std::max(width, height);
        if (largest > limit)
        {
            const float scale = static_cast<float>(limit) / static_cast<float>(largest);
            width = std::max(1u, static_cast<unsigned>(std::lround(static_cast<float>(width) * scale)));
            height = std::max(1u, static_cast<unsigned>(std::lround(static_cast<float>(height) * scale)));
        }
    }

    if (settings.powerOfTwo)
    {
        width = static_cast<unsigned>(AtlasPacker::NextPowerOfTwo(static_cast<int>(width)));
        height = static_cast<unsigned>(AtlasPacker::NextPowerOfTwo(static_cast<int>(height)));
    }

    if (width != image.getSize().x || height != image.getSize().y)
    {
        image = ResizeImage(image, width, height);
    }
    return image;
}

struct AtlasEntry
{
    std::string id;
    sf::Image image;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int offsetX = 0;
    int offsetY = 0;
};

AtlasEntry TrimEntry(const AtlasInput& input, bool trim)
{
    AtlasEntry entry;
    entry.id = input.id;
    entry.image = input.image;
    entry.sourceWidth = static_cast<int>(input.image.getSize().x);
    entry.sourceHeight = static_cast<int>(input.image.getSize().y);
    if (!trim)
    {
        return entry;
    }

    const sf::Vector2u size = input.image.getSize();
    int minX = static_cast<int>(size.x);
    int minY = static_cast<int>(size.y);
    int maxX = -1;
    int maxY = -1;
    for (unsigned y = 0; y < size.y; ++y)
    {
        for (unsigned x = 0; x < size.x; ++x)
        {
            if (input.image.getPixel({x, y}).a == 0)
            {
                continue;
            }
            minX = std::min(minX, static_cast<int>(x));
            minY = std::min(minY, static_cast<int>(y));
            maxX = std::max(maxX, static_cast<int>(x));
            maxY = std::max(maxY, static_cast<int>(y));
        }
    }

    if (maxX < minX || maxY < minY)
    {
        // A fully transparent source still needs a valid one-pixel region.
        entry.image = sf::Image(sf::Vector2u{1, 1}, sf::Color::Transparent);
        entry.offsetX = 0;
        entry.offsetY = 0;
        return entry;
    }

    const int width = maxX - minX + 1;
    const int height = maxY - minY + 1;
    entry.offsetX = minX;
    entry.offsetY = minY;
    entry.image =
        sf::Image(sf::Vector2u{static_cast<unsigned>(width), static_cast<unsigned>(height)}, sf::Color::Transparent);
    (void)entry.image.copy(input.image, {0, 0}, sf::IntRect({minX, minY}, {width, height}));
    return entry;
}

} // namespace

const char* AssetKindName(AssetKind kind)
{
    switch (kind)
    {
        case AssetKind::Texture:
            return "texture";
        case AssetKind::Font:
            return "font";
        case AssetKind::Sound:
            return "sound";
        case AssetKind::Music:
            return "music";
        case AssetKind::Data:
            return "data";
        default:
            return "unknown";
    }
}

AssetKind AssetKindFromString(const std::string& value)
{
    const std::string kind = Lower(value);
    if (kind == "texture" || kind == "image")
    {
        return AssetKind::Texture;
    }
    if (kind == "font")
    {
        return AssetKind::Font;
    }
    if (kind == "sound" || kind == "audio" || kind == "sfx")
    {
        return AssetKind::Sound;
    }
    if (kind == "music")
    {
        return AssetKind::Music;
    }
    if (kind == "data" || kind == "json" || kind == "scene" || kind == "prefab")
    {
        return AssetKind::Data;
    }
    return AssetKind::Unknown;
}

nlohmann::json AssetImportSettings::ToJson() const
{
    nlohmann::json value = extra.is_object() ? extra : nlohmann::json::object();
    value["type"] = AssetKindName(kind);
    value["path"] = path;
    if (!output.empty())
    {
        value["output"] = output;
    }
    if (!atlas.empty())
    {
        value["atlas"] = atlas;
    }
    value["smooth"] = smooth;
    value["premultiplyAlpha"] = premultiplyAlpha;
    value["powerOfTwo"] = powerOfTwo;
    value["maxSize"] = maxSize;
    value["padding"] = padding;
    value["trim"] = trim;
    return value;
}

AssetImportSettings AssetImportSettings::FromJson(const std::string& id, const nlohmann::json& value)
{
    AssetImportSettings settings;
    settings.id = id;
    if (value.is_string())
    {
        settings.path = value.get<std::string>();
        settings.kind = InferKind(settings.path);
        return settings;
    }
    if (!value.is_object())
    {
        return settings;
    }

    if (value.contains("path") && value["path"].is_string())
    {
        settings.path = value["path"].get<std::string>();
    }
    if (value.contains("type") && value["type"].is_string())
    {
        settings.kind = AssetKindFromString(value["type"].get<std::string>());
    }
    else if (value.contains("kind") && value["kind"].is_string())
    {
        settings.kind = AssetKindFromString(value["kind"].get<std::string>());
    }
    if (settings.kind == AssetKind::Unknown)
    {
        settings.kind = InferKind(settings.path);
    }

    if (value.contains("output") && value["output"].is_string())
    {
        settings.output = value["output"].get<std::string>();
    }
    if (value.contains("atlas") && value["atlas"].is_string())
    {
        settings.atlas = value["atlas"].get<std::string>();
    }
    if (value.contains("smooth") && value["smooth"].is_boolean())
    {
        settings.smooth = value["smooth"].get<bool>();
    }
    if (value.contains("premultiplyAlpha") && value["premultiplyAlpha"].is_boolean())
    {
        settings.premultiplyAlpha = value["premultiplyAlpha"].get<bool>();
    }
    if (value.contains("powerOfTwo") && value["powerOfTwo"].is_boolean())
    {
        settings.powerOfTwo = value["powerOfTwo"].get<bool>();
    }
    if (value.contains("trim") && value["trim"].is_boolean())
    {
        settings.trim = value["trim"].get<bool>();
    }
    if (value.contains("maxSize") && value["maxSize"].is_number_integer())
    {
        settings.maxSize = std::max(0, value["maxSize"].get<int>());
    }
    if (value.contains("padding") && value["padding"].is_number_integer())
    {
        settings.padding = std::clamp(value["padding"].get<int>(), 0, 64);
    }

    static const std::set<std::string> known{
        "path",       "type",    "kind",    "output", "atlas", "smooth", "premultiplyAlpha",
        "powerOfTwo", "maxSize", "padding", "trim"};
    for (auto it = value.begin(); it != value.end(); ++it)
    {
        if (known.count(it.key()) == 0)
        {
            settings.extra[it.key()] = it.value();
        }
    }
    return settings;
}

void AssetDatabase::Clear()
{
    manifestPath_.clear();
    manifestDirectory_.clear();
    assets_.clear();
    dependencies_.clear();
    loadErrors_.clear();
    raw_ = nlohmann::json::object();
}

bool AssetDatabase::Load(const std::string& manifestPath)
{
    Clear();
    manifestPath_ = manifestPath;
    manifestDirectory_ = fs::path(manifestPath).parent_path().string();

    const Result<std::string> text = Vanity::ReadText(manifestPath);
    if (!text)
    {
        loadErrors_.push_back(text.Error());
        return false;
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(text.Value());
    }
    catch (const std::exception& error)
    {
        loadErrors_.push_back(std::string("manifest parse error: ") + error.what());
        return false;
    }
    if (!root.is_object())
    {
        loadErrors_.push_back("manifest root must be an object");
        return false;
    }

    const auto addAsset = [this](const std::string& id, const nlohmann::json& value, AssetKind hint)
    {
        if (id.empty())
        {
            loadErrors_.push_back("asset id must not be empty");
            return;
        }
        if (assets_.count(id) != 0)
        {
            loadErrors_.push_back("duplicate asset id: " + id);
            return;
        }
        AssetImportSettings settings = AssetImportSettings::FromJson(id, value);
        if (settings.kind == AssetKind::Unknown && hint != AssetKind::Unknown)
        {
            settings.kind = hint;
        }
        if (settings.kind == AssetKind::Unknown)
        {
            loadErrors_.push_back("asset '" + id + "' has no type and its extension is not recognised");
        }
        if (settings.path.empty())
        {
            loadErrors_.push_back("asset '" + id + "' has no path");
        }
        assets_.emplace(id, std::move(settings));
    };

    if (root.contains("assets"))
    {
        if (root["assets"].is_object())
        {
            for (auto it = root["assets"].begin(); it != root["assets"].end(); ++it)
            {
                addAsset(it.key(), it.value(), AssetKind::Unknown);
            }
        }
        else
        {
            loadErrors_.push_back("'assets' must be an object");
        }
    }

    const struct
    {
        const char* key;
        AssetKind kind;
    } groups[] = {{"textures", AssetKind::Texture},
                  {"fonts", AssetKind::Font},
                  {"sounds", AssetKind::Sound},
                  {"music", AssetKind::Music},
                  {"data", AssetKind::Data}};
    for (const auto& group : groups)
    {
        if (!root.contains(group.key))
        {
            continue;
        }
        if (!root[group.key].is_object())
        {
            loadErrors_.push_back(std::string("'") + group.key + "' must be an object");
            continue;
        }
        for (auto it = root[group.key].begin(); it != root[group.key].end(); ++it)
        {
            addAsset(it.key(), it.value(), group.kind);
        }
    }

    // Import overrides make it possible to add pipeline settings to an existing
    // AssetManager manifest without rewriting its texture/font/sound maps.
    if (root.contains("import") && root["import"].is_object())
    {
        for (auto it = root["import"].begin(); it != root["import"].end(); ++it)
        {
            auto asset = assets_.find(it.key());
            if (asset == assets_.end())
            {
                loadErrors_.push_back("import override for unknown asset: " + it.key());
                continue;
            }
            const AssetImportSettings override = AssetImportSettings::FromJson(it.key(), it.value());
            const nlohmann::json& value = it.value();
            if (value.contains("type") || value.contains("kind"))
            {
                asset->second.kind = override.kind;
            }
            if (value.contains("path"))
            {
                asset->second.path = override.path;
            }
            if (value.contains("output"))
            {
                asset->second.output = override.output;
            }
            if (value.contains("atlas"))
            {
                asset->second.atlas = override.atlas;
            }
            if (value.contains("smooth"))
            {
                asset->second.smooth = override.smooth;
            }
            if (value.contains("premultiplyAlpha"))
            {
                asset->second.premultiplyAlpha = override.premultiplyAlpha;
            }
            if (value.contains("powerOfTwo"))
            {
                asset->second.powerOfTwo = override.powerOfTwo;
            }
            if (value.contains("trim"))
            {
                asset->second.trim = override.trim;
            }
            if (value.contains("maxSize"))
            {
                asset->second.maxSize = override.maxSize;
            }
            if (value.contains("padding"))
            {
                asset->second.padding = override.padding;
            }
            for (auto extra = override.extra.begin(); extra != override.extra.end(); ++extra)
            {
                asset->second.extra[extra.key()] = extra.value();
            }
        }
    }

    if (root.contains("dependencies"))
    {
        if (!root["dependencies"].is_object())
        {
            loadErrors_.push_back("'dependencies' must be an object");
        }
        else
        {
            for (auto it = root["dependencies"].begin(); it != root["dependencies"].end(); ++it)
            {
                if (!it.value().is_array())
                {
                    loadErrors_.push_back("dependencies for '" + it.key() + "' must be an array");
                    continue;
                }
                std::vector<std::string> deps;
                for (const nlohmann::json& dep : it.value())
                {
                    if (dep.is_string())
                    {
                        deps.push_back(dep.get<std::string>());
                    }
                }
                dependencies_[it.key()] = std::move(deps);
            }
        }
    }

    raw_ = std::move(root);
    return true;
}

bool AssetDatabase::Save(const std::string& path) const
{
    nlohmann::json root = nlohmann::json::object();
    root["assets"] = nlohmann::json::object();
    for (const auto& [id, settings] : assets_)
    {
        root["assets"][id] = settings.ToJson();
    }
    if (!dependencies_.empty())
    {
        root["dependencies"] = nlohmann::json::object();
        for (const auto& [id, deps] : dependencies_)
        {
            root["dependencies"][id] = deps;
        }
    }
    const Result<void> written = Vanity::WriteJson(path, root);
    if (!written)
    {
        BOY_ERROR("AssetPipeline", written.Error());
        return false;
    }
    return true;
}

bool AssetDatabase::Validate(std::vector<std::string>* errors) const
{
    std::vector<std::string> found = loadErrors_;
    for (const auto& [id, settings] : assets_)
    {
        (void)id;
        if (settings.path.empty())
        {
            continue; // already reported by load()
        }
        const std::string source = ResolvedPath(settings.id);
        std::error_code ec;
        if (source.empty() || !fs::is_regular_file(source, ec))
        {
            found.push_back("missing source for '" + settings.id + "': " + source);
        }
        if (settings.kind == AssetKind::Unknown)
        {
            found.push_back("unknown kind for '" + settings.id + "'");
        }
        if (settings.maxSize < 0)
        {
            found.push_back("negative maxSize for '" + settings.id + "'");
        }
        if (settings.padding < 0)
        {
            found.push_back("negative padding for '" + settings.id + "'");
        }
    }
    for (const auto& [id, deps] : dependencies_)
    {
        for (const std::string& dep : deps)
        {
            if (assets_.count(dep) == 0)
            {
                found.push_back("asset '" + id + "' depends on unknown asset '" + dep + "'");
            }
        }
    }

    // The topological sort also reports this, but validation is the documented
    // one-call check, so it must not let a cycle through.
    std::map<std::string, int> state; // 0 = unvisited, 1 = visiting, 2 = done
    const std::function<void(const std::string&)> visit = [&](const std::string& id)
    {
        int& current = state[id];
        if (current == 1)
        {
            found.push_back("dependency cycle at '" + id + "'");
            return;
        }
        if (current == 2)
        {
            return;
        }
        current = 1;
        for (const std::string& dep : Dependencies(id))
        {
            if (assets_.count(dep) != 0)
            {
                visit(dep);
            }
        }
        current = 2;
    };
    for (const auto& [id, settings] : assets_)
    {
        (void)settings;
        visit(id);
    }
    if (errors)
    {
        *errors = found;
    }
    return found.empty();
}

const AssetImportSettings* AssetDatabase::Find(const std::string& id) const
{
    const auto it = assets_.find(id);
    return it == assets_.end() ? nullptr : &it->second;
}

const std::vector<std::string>& AssetDatabase::Dependencies(const std::string& id) const
{
    static const std::vector<std::string> empty;
    const auto it = dependencies_.find(id);
    return it == dependencies_.end() ? empty : it->second;
}

bool AssetDatabase::DependsOn(const std::string& id, const std::string& dependency) const
{
    std::unordered_set<std::string> visited;
    const std::function<bool(const std::string&)> walk = [&](const std::string& current)
    {
        if (!visited.insert(current).second)
        {
            return false;
        }
        for (const std::string& dep : Dependencies(current))
        {
            if (dep == dependency || walk(dep))
            {
                return true;
            }
        }
        return false;
    };
    return walk(id);
}

std::vector<std::string> AssetDatabase::TopologicalOrder(std::vector<std::string>* errors) const
{
    std::vector<std::string> order;
    std::vector<std::string> found;
    std::map<std::string, int> state; // 0 = unvisited, 1 = visiting, 2 = done

    const std::function<bool(const std::string&)> visit = [&](const std::string& id)
    {
        int& current = state[id];
        if (current == 2)
        {
            return true;
        }
        if (current == 1)
        {
            found.push_back("dependency cycle at '" + id + "'");
            return false;
        }
        current = 1;
        for (const std::string& dep : Dependencies(id))
        {
            if (assets_.count(dep) == 0)
            {
                found.push_back("asset '" + id + "' depends on unknown asset '" + dep + "'");
                continue;
            }
            if (!visit(dep))
            {
                return false;
            }
        }
        current = 2;
        order.push_back(id);
        return true;
    };

    for (const auto& [id, settings] : assets_)
    {
        (void)settings;
        if (!visit(id))
        {
            break;
        }
    }
    if (errors)
    {
        *errors = found;
    }
    if (!found.empty())
    {
        return {};
    }
    return order;
}

std::string AssetDatabase::ResolvedPath(const std::string& id) const
{
    const AssetImportSettings* settings = Find(id);
    if (!settings || settings->path.empty())
    {
        return {};
    }
    fs::path path(settings->path);
    if (path.is_relative())
    {
        path = fs::path(manifestDirectory_) / path;
    }
    return path.lexically_normal().string();
}

std::uint64_t AssetDatabase::SourceHash(const std::string& id) const
{
    const std::string path = ResolvedPath(id);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        return 0;
    }
    std::uint64_t hash = 14695981039346656037ull;
    char buffer[64 * 1024];
    while (file)
    {
        file.read(buffer, sizeof(buffer));
        const std::streamsize read = file.gcount();
        for (std::streamsize i = 0; i < read; ++i)
        {
            hash ^= static_cast<std::uint8_t>(buffer[i]);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

std::string AssetDatabase::HashHex(std::uint64_t hash)
{
    char buffer[17]{};
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

std::vector<std::string> AssetDatabase::Stale(const std::string& outputDir) const
{
    std::vector<std::string> result;
    const fs::path cachePath = fs::path(outputDir) / "cooked.json";
    const Result<nlohmann::json> parsed = Vanity::ReadJson(cachePath.string());
    if (!parsed || !parsed.Value().is_object() || !parsed.Value().contains("assets") ||
        !parsed.Value()["assets"].is_object())
    {
        for (const auto& [id, settings] : assets_)
        {
            (void)settings;
            result.push_back(id);
        }
        return result;
    }

    const nlohmann::json& cached = parsed.Value()["assets"];
    for (const auto& [id, settings] : assets_)
    {
        (void)settings;
        if (!cached.contains(id) || !cached[id].is_object() || !cached[id].contains("hash"))
        {
            result.push_back(id);
            continue;
        }
        const std::string current = HashHex(SourceHash(id));
        if (!cached[id]["hash"].is_string() || cached[id]["hash"].get<std::string>() != current)
        {
            result.push_back(id);
        }
    }
    return result;
}

int AtlasPacker::NextPowerOfTwo(int value)
{
    if (value <= 1)
    {
        return 1;
    }
    unsigned v = static_cast<unsigned>(value - 1);
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return static_cast<int>(v + 1u);
}

AtlasResult AtlasPacker::Pack(const std::vector<AtlasInput>& inputs, const AtlasOptions& options)
{
    AtlasResult result;
    if (inputs.empty())
    {
        result.ok = true;
        return result;
    }

    const int maxSize = std::clamp(options.maxSize, 64, 16384);
    const int padding = std::clamp(options.padding, 0, 64);
    const int minSize = std::clamp(options.minSize, 1, maxSize);

    struct Entry
    {
        AtlasEntry source;
        int width = 0;
        int height = 0;
    };
    std::vector<Entry> entries;
    entries.reserve(inputs.size());
    std::uint64_t totalArea = 0;
    for (const AtlasInput& input : inputs)
    {
        if (input.id.empty())
        {
            result.error = "atlas input has an empty id";
            return result;
        }
        const sf::Vector2u size = input.image.getSize();
        if (size.x == 0 || size.y == 0)
        {
            result.error = "atlas input '" + input.id + "' is empty";
            return result;
        }
        AtlasEntry trimmed = TrimEntry(input, options.trim);
        Entry entry;
        entry.source = std::move(trimmed);
        entry.width = static_cast<int>(entry.source.image.getSize().x);
        entry.height = static_cast<int>(entry.source.image.getSize().y);
        if (entry.width + padding * 2 > maxSize || entry.height + padding * 2 > maxSize)
        {
            result.error = "atlas input '" + input.id + "' is larger than maxSize";
            return result;
        }
        totalArea += static_cast<std::uint64_t>(entry.width + padding * 2) *
                     static_cast<std::uint64_t>(entry.height + padding * 2);
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b)
              {
                  if (a.height != b.height)
                  {
                      return a.height > b.height;
                  }
                  if (a.width != b.width)
                  {
                      return a.width > b.width;
                  }
                  return a.source.id < b.source.id;
              });

    int estimate = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(totalArea) * 1.15)));
    int pageWidth = NextPowerOfTwo(std::max(minSize, estimate));
    pageWidth = std::clamp(pageWidth, minSize, maxSize);

    struct Placement
    {
        std::size_t entry = 0;
        int x = 0;
        int y = 0;
    };
    struct PageBuild
    {
        int width = 0;
        int height = 0;
        int cursorX = 0;
        int shelfY = 0;
        int shelfHeight = 0;
        int usedWidth = 0;
        int usedHeight = 0;
        std::vector<Placement> placements;
    };

    std::vector<PageBuild> pages;
    PageBuild page;
    page.width = pageWidth;
    page.height = maxSize;
    page.cursorX = padding;
    page.shelfY = padding;

    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const Entry& entry = entries[i];
        const int needed = entry.width + padding * 2;
        if (page.cursorX + entry.width + padding > page.width)
        {
            // Grow the page before starting a new shelf: a wider page keeps the
            // current row usable and usually beats a fresh page.
            if (page.width < maxSize && page.cursorX + entry.width + padding <= maxSize)
            {
                page.width = std::min(maxSize, NextPowerOfTwo(page.cursorX + entry.width + padding));
            }
            else
            {
                page.cursorX = padding;
                page.shelfY += page.shelfHeight;
                page.shelfHeight = 0;
                if (page.width < maxSize && needed > page.width && needed <= maxSize)
                {
                    page.width = NextPowerOfTwo(needed);
                }
            }
        }

        if (page.shelfY + entry.height + padding > page.height)
        {
            pages.push_back(std::move(page));
            page = PageBuild{};
            page.width = std::clamp(NextPowerOfTwo(needed), minSize, maxSize);
            page.height = maxSize;
            page.cursorX = padding;
            page.shelfY = padding;
        }

        Placement placement;
        placement.entry = i;
        placement.x = page.cursorX;
        placement.y = page.shelfY;
        page.placements.push_back(placement);
        page.cursorX += entry.width + padding;
        page.shelfHeight = std::max(page.shelfHeight, entry.height + padding);
        page.usedWidth = std::max(page.usedWidth, page.cursorX);
        page.usedHeight = std::max(page.usedHeight, page.shelfY + entry.height + padding);
    }
    pages.push_back(std::move(page));

    std::uint64_t pageArea = 0;
    std::uint64_t regionArea = 0;
    for (std::size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
    {
        PageBuild& build = pages[pageIndex];
        int width = std::max(build.width, build.usedWidth);
        int height = std::max(minSize, build.usedHeight);
        if (options.powerOfTwo)
        {
            width = NextPowerOfTwo(width);
            height = NextPowerOfTwo(height);
        }
        width = std::clamp(width, minSize, maxSize);
        height = std::clamp(height, minSize, maxSize);
        if (build.usedWidth > width || build.usedHeight > height)
        {
            result.error = "atlas packing overflow";
            return result;
        }

        AtlasPage atlasPage;
        atlasPage.width = width;
        atlasPage.height = height;
        atlasPage.image = sf::Image(sf::Vector2u{static_cast<unsigned>(width), static_cast<unsigned>(height)},
                                    sf::Color::Transparent);
        pageArea += static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);

        for (const Placement& placement : build.placements)
        {
            const Entry& entry = entries[placement.entry];
            (void)atlasPage.image.copy(entry.source.image,
                                       {static_cast<unsigned>(placement.x), static_cast<unsigned>(placement.y)},
                                       sf::IntRect({0, 0}, {entry.width, entry.height}));
            AtlasRegion region;
            region.id = entry.source.id;
            region.page = static_cast<int>(pageIndex);
            region.x = placement.x;
            region.y = placement.y;
            region.width = entry.width;
            region.height = entry.height;
            region.sourceWidth = entry.source.sourceWidth;
            region.sourceHeight = entry.source.sourceHeight;
            region.offsetX = entry.source.offsetX;
            region.offsetY = entry.source.offsetY;
            result.regions.push_back(region);
            regionArea += static_cast<std::uint64_t>(entry.width) * static_cast<std::uint64_t>(entry.height);
        }
        result.pages.push_back(std::move(atlasPage));
    }

    result.utilization =
        pageArea == 0 ? 0.f : static_cast<float>(static_cast<double>(regionArea) / static_cast<double>(pageArea));
    result.ok = true;
    return result;
}

CookResult AssetCooker::Cook(const AssetDatabase& database, const CookOptions& options)
{
    CookResult result;

    std::vector<std::string> validationErrors;
    if (!database.Validate(&validationErrors))
    {
        result.errors = std::move(validationErrors);
        return result;
    }
    if (options.outputDir.empty())
    {
        result.errors.push_back("cook output directory is empty");
        return result;
    }

    const fs::path output = fs::path(options.outputDir).lexically_normal();
    std::error_code ec;
    fs::create_directories(output, ec);
    if (ec)
    {
        result.errors.push_back("cannot create output directory: " + ec.message());
        return result;
    }

    struct CookedFile
    {
        std::string id;
        std::string relative;
        fs::path absolute;
    };
    std::vector<CookedFile> files;
    std::vector<std::string> order = database.TopologicalOrder(&validationErrors);
    if (!validationErrors.empty())
    {
        result.errors = std::move(validationErrors);
        return result;
    }

    nlohmann::json cooked = nlohmann::json::object();
    cooked["textures"] = nlohmann::json::object();
    cooked["fonts"] = nlohmann::json::object();
    cooked["sounds"] = nlohmann::json::object();
    cooked["music"] = nlohmann::json::object();
    cooked["data"] = nlohmann::json::object();
    cooked["pipeline"] = {{"version", 1}, {"sourceManifest", database.ManifestPath()}};

    const auto addFile = [&](const std::string& id, const fs::path& absolute)
    {
        CookedFile file;
        file.id = id;
        file.relative = RelativeTo(absolute, output);
        file.absolute = absolute;
        files.push_back(std::move(file));
        std::error_code sizeError;
        const auto size = fs::file_size(absolute, sizeError);
        if (!sizeError)
        {
            result.bytes += static_cast<std::uint64_t>(size);
        }
    };

    const auto copyAsset = [&](const AssetImportSettings& settings, const std::string& kindDir,
                               const std::string& extension) -> std::string
    {
        const fs::path source = database.ResolvedPath(settings.id);
        const std::string relative =
            SafeOutputPath(settings.output.empty() ? std::string{} : settings.output, settings.id, extension);
        fs::path destination = output / kindDir / relative;
        if (settings.output.empty())
        {
            destination = output / kindDir / (Vanity::SanitizeFileName(settings.id) + extension);
        }
        fs::create_directories(destination.parent_path(), ec);
        if (ec)
        {
            result.errors.push_back("cannot create directory for '" + settings.id + "': " + ec.message());
            return {};
        }
        fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            result.errors.push_back("cannot copy '" + settings.id + "': " + ec.message());
            return {};
        }
        const std::string rel = RelativeTo(destination, output);
        addFile(settings.id, destination);
        return rel;
    };

    // Textures are grouped by atlas name. A non-atlas texture keeps its source
    // bytes when no import transform applies, so a cooker run is cheap and the
    // GPU sees exactly the authored PNG.
    std::map<std::string, std::vector<AtlasInput>> atlasGroups;
    std::map<std::string, std::vector<AssetImportSettings>> atlasSettings;
    for (const std::string& id : order)
    {
        const AssetImportSettings* settings = database.Find(id);
        if (!settings || settings->kind != AssetKind::Texture)
        {
            continue;
        }
        const fs::path source = database.ResolvedPath(id);
        sf::Image image;
        if (!image.loadFromFile(source))
        {
            result.errors.push_back("cannot load texture '" + id + "': " + source.string());
            continue;
        }
        if (!settings->atlas.empty())
        {
            AtlasInput input;
            input.id = id;
            input.image = PrepareTexture(image, *settings);
            atlasGroups[settings->atlas].push_back(std::move(input));
            atlasSettings[settings->atlas].push_back(*settings);
            ++result.textures;
            continue;
        }

        ++result.textures;
        const bool transformed = settings->premultiplyAlpha || settings->powerOfTwo || settings->maxSize > 0;
        if (!transformed)
        {
            const std::string rel = copyAsset(*settings, "textures", source.extension().string());
            if (!rel.empty())
            {
                cooked["textures"][id] = {{"path", rel}, {"smooth", settings->smooth}};
            }
            continue;
        }

        sf::Image prepared = PrepareTexture(image, *settings);
        const std::string relative = SafeOutputPath(settings->output, id, ".png");
        const fs::path destination = output / "textures" / relative;
        fs::create_directories(destination.parent_path(), ec);
        if (ec)
        {
            result.errors.push_back("cannot create texture directory for '" + id + "': " + ec.message());
            continue;
        }
        if (!prepared.saveToFile(destination))
        {
            result.errors.push_back("cannot write texture '" + id + "': " + destination.string());
            continue;
        }
        addFile(id, destination);
        cooked["textures"][id] = {{"path", RelativeTo(destination, output)}, {"smooth", settings->smooth}};
    }

    for (const auto& [group, inputs] : atlasGroups)
    {
        AtlasOptions atlasOptions;
        atlasOptions.padding = inputs.empty() ? 2 : atlasSettings[group].front().padding;
        atlasOptions.trim = inputs.empty() ? false : atlasSettings[group].front().trim;
        const AtlasResult packed = AtlasPacker::Pack(inputs, atlasOptions);
        if (!packed.ok)
        {
            result.errors.push_back("atlas '" + group + "': " + packed.error);
            continue;
        }
        result.atlases += packed.pages.size();
        const std::string safeGroup = Vanity::SanitizeFileName(group, "atlas");
        for (std::size_t pageIndex = 0; pageIndex < packed.pages.size(); ++pageIndex)
        {
            const AtlasPage& page = packed.pages[pageIndex];
            const std::string relative = "atlases/" + safeGroup + "_" + std::to_string(pageIndex) + ".png";
            const fs::path destination = output / relative;
            fs::create_directories(destination.parent_path(), ec);
            if (ec)
            {
                result.errors.push_back("cannot create atlas directory: " + ec.message());
                break;
            }
            if (!page.image.saveToFile(destination))
            {
                result.errors.push_back("cannot write atlas: " + destination.string());
                break;
            }
            addFile("atlas:" + group + ":" + std::to_string(pageIndex), destination);
        }

        for (const AtlasRegion& region : packed.regions)
        {
            const AssetImportSettings* settings = database.Find(region.id);
            const std::string relative = "atlases/" + safeGroup + "_" + std::to_string(region.page) + ".png";
            cooked["textures"][region.id] = {
                {"path", relative},
                {"rect", {region.x, region.y, region.width, region.height}},
                {"sourceSize", {region.sourceWidth, region.sourceHeight}},
                {"offset", {region.offsetX, region.offsetY}},
                {"smooth", settings ? settings->smooth : true},
            };
        }
    }

    for (const std::string& id : order)
    {
        const AssetImportSettings* settings = database.Find(id);
        if (!settings || settings->kind == AssetKind::Texture)
        {
            continue;
        }
        const fs::path source = database.ResolvedPath(id);
        const std::string extension = source.extension().string();
        const char* group = nullptr;
        switch (settings->kind)
        {
            case AssetKind::Font:
                group = "fonts";
                ++result.fonts;
                break;
            case AssetKind::Sound:
                group = "sounds";
                ++result.sounds;
                break;
            case AssetKind::Music:
                group = "music";
                ++result.music;
                break;
            case AssetKind::Data:
                group = "data";
                ++result.data;
                break;
            default:
                continue;
        }
        const std::string rel = copyAsset(*settings, group, extension);
        if (!rel.empty())
        {
            cooked[group][id] = rel;
        }
    }

    // Hashes let the next cook report exactly what changed without re-reading
    // every file in the editor.
    nlohmann::json cache = {
        {"version", 1}, {"sourceManifest", database.ManifestPath()}, {"assets", nlohmann::json::object()}};
    for (const auto& [id, settings] : database.Assets())
    {
        cache["assets"][id] = {{"hash", AssetDatabase::HashHex(database.SourceHash(id))},
                               {"kind", AssetKindName(settings.kind)},
                               {"path", database.ResolvedPath(id)}};
    }
    const Result<void> cacheWritten = Vanity::WriteJson((output / "cooked.json").string(), cache);
    if (!cacheWritten)
    {
        result.errors.push_back("cannot write cooked.json: " + cacheWritten.Error());
    }

    result.manifestPath = (output / "manifest.json").string();
    const Result<void> manifestWritten = Vanity::WriteJson(result.manifestPath, cooked);
    if (!manifestWritten)
    {
        result.errors.push_back("cannot write cooked manifest: " + manifestWritten.Error());
    }

    result.filesPacked = files.size();
    if (options.createPack && result.errors.empty())
    {
        std::vector<ResourcePack::Source> packFiles;
        packFiles.reserve(files.size() + 2);
        for (const CookedFile& file : files)
        {
            // The extracted pack must mirror the cooked output tree, not the
            // build machine's directory layout. A manifest that says
            // "textures/player.png" then works after extraction unchanged.
            packFiles.push_back(ResourcePack::Source{file.id, file.absolute.string(), file.relative});
        }
        // Include the manifest and cache report: a runtime can mount the pack
        // without needing a loose manifest beside it.
        packFiles.push_back(ResourcePack::Source{"manifest.json", result.manifestPath, "manifest.json"});
        packFiles.push_back(ResourcePack::Source{"cooked.json", (output / "cooked.json").string(), "cooked.json"});

        ResourcePack pack;
        const std::string packPath = (output / "assets.fempak").string();
        if (!pack.Create(packPath, packFiles))
        {
            result.errors.push_back("cannot create resource pack: " + packPath);
        }
        else
        {
            result.packPath = packPath;
            result.filesPacked = packFiles.size();
        }
    }

    result.assets = database.Assets().size();
    result.ok = result.errors.empty();
    if (options.verbose)
    {
        BOY_INFO("AssetPipeline", "cooked " + std::to_string(result.assets) + " asset(s) into " + output.string() +
                                      " (" + std::to_string(result.atlases) + " atlas page(s))");
    }
    return result;
}

} // namespace Fem
