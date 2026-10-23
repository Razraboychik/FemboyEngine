// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <SFML/Graphics/Image.hpp>

namespace Fem
{

// Asset pipeline
// AssetDatabase is the authored description of a project's content. It reads
// both the original AssetManager manifest (textures/fonts/sounds/music maps of
// id -> path) and the pipeline format (assets + dependencies + import settings).
// AssetCooker validates the graph, applies import settings, packs greedy texture
// atlases and writes a cooked manifest plus an optional .fempak.
//
// This is deliberately CPU-side and renderer-agnostic: the editor and a build
// script can cook without creating a window or a GL context.

enum class AssetKind
{
    Unknown,
    Texture,
    Font,
    Sound,
    Music,
    Data
};

const char* AssetKindName(AssetKind kind);
AssetKind AssetKindFromString(const std::string& value);

struct AssetImportSettings
{
    std::string id;
    AssetKind kind = AssetKind::Unknown;
    std::string path;   // relative to the manifest, or absolute
    std::string output; // optional relative cooked path
    std::string atlas;  // non-empty groups textures into one page set

    bool smooth = true;
    bool premultiplyAlpha = false;
    bool powerOfTwo = false;
    int maxSize = 0;   // 0 = no downscale limit
    int padding = 2;   // atlas gutter in pixels
    bool trim = false; // trim fully transparent atlas borders

    // Unknown keys are preserved so a project can add its own importer options
    // without the engine dropping them on save.
    nlohmann::json extra = nlohmann::json::object();

    nlohmann::json ToJson() const;
    static AssetImportSettings FromJson(const std::string& id, const nlohmann::json& value);
};

class AssetDatabase
{
public:
    bool Load(const std::string& manifestPath);
    bool Save(const std::string& path) const;
    void Clear();

    // Reports missing files, unknown dependency ids, empty ids and cycles.
    bool Validate(std::vector<std::string>* errors = nullptr) const;

    const AssetImportSettings* Find(const std::string& id) const;
    const std::map<std::string, AssetImportSettings>& Assets() const
    {
        return assets_;
    }
    const std::vector<std::string>& Dependencies(const std::string& id) const;
    bool DependsOn(const std::string& id, const std::string& dependency) const;

    // Dependencies before dependants. An empty result with `errors` populated
    // means the graph contains a cycle.
    std::vector<std::string> TopologicalOrder(std::vector<std::string>* errors = nullptr) const;

    // Absolute source path resolved from the manifest directory.
    std::string ResolvedPath(const std::string& id) const;

    // FNV-1a 64 of the source file. 0 means the file is missing or unreadable.
    std::uint64_t SourceHash(const std::string& id) const;
    static std::string HashHex(std::uint64_t hash);

    // Ids whose source hash differs from the last cooked.json in outputDir.
    std::vector<std::string> Stale(const std::string& outputDir) const;

    const std::string& ManifestPath() const
    {
        return manifestPath_;
    }
    const std::string& ManifestDirectory() const
    {
        return manifestDirectory_;
    }
    const std::vector<std::string>& LoadErrors() const
    {
        return loadErrors_;
    }
    const nlohmann::json& Raw() const
    {
        return raw_;
    }

private:
    std::string manifestPath_;
    std::string manifestDirectory_;
    std::map<std::string, AssetImportSettings> assets_;
    std::map<std::string, std::vector<std::string>> dependencies_;
    std::vector<std::string> loadErrors_;
    nlohmann::json raw_ = nlohmann::json::object();
};

struct AtlasOptions
{
    int maxSize = 4096;
    int padding = 2;
    bool powerOfTwo = true;
    bool trim = false;
    bool extrude = false;
    int minSize = 64;
};

struct AtlasInput
{
    std::string id;
    sf::Image image;
};

struct AtlasRegion
{
    std::string id;
    int page = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int offsetX = 0;
    int offsetY = 0;
};

struct AtlasPage
{
    int width = 0;
    int height = 0;
    sf::Image image;
};

struct AtlasResult
{
    bool ok = false;
    std::string error;
    std::vector<AtlasPage> pages;
    std::vector<AtlasRegion> regions;
    float utilization = 0.f;
};

// Deterministic shelf packer. Inputs are sorted by height (then width, then id)
// so the same content always produces the same pages. The page width is grown
// from an area estimate up to maxSize; a full page starts a new page.
class AtlasPacker
{
public:
    static AtlasResult Pack(const std::vector<AtlasInput>& inputs, const AtlasOptions& options = {});
    static int NextPowerOfTwo(int value);
};

struct CookOptions
{
    std::string outputDir;
    bool createPack = true;
    bool verbose = false;
};

struct CookResult
{
    bool ok = false;
    std::string manifestPath;
    std::string packPath;
    std::size_t assets = 0;
    std::size_t textures = 0;
    std::size_t atlases = 0;
    std::size_t fonts = 0;
    std::size_t sounds = 0;
    std::size_t music = 0;
    std::size_t data = 0;
    std::size_t filesPacked = 0;
    std::uint64_t bytes = 0;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

class AssetCooker
{
public:
    static CookResult Cook(const AssetDatabase& database, const CookOptions& options);
};

} // namespace Fem
