// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>
#include "FemboyEngine/Core.hpp"

namespace Fem
{

// Simple resource package (.fempak): JSON manifest + concatenated file data.
// Format: [4 bytes magic "FEPA"][4 bytes LE manifest size][manifest JSON][file data blobs]
//
// All sizes and offsets read from disk are validated before use, and extract()
// refuses paths that would escape the output directory.
class ResourcePack
{
public:
    struct Entry
    {
        std::string id;
        std::string path;
        size_t offset;
        size_t size;
    };

    // Source entry for Create(). `storedPath` is the path inside the extracted
    // pack. The cooker supplies the path relative to its output directory, so
    // `assets.fempak` can be extracted into a cache and the cooked manifest's
    // relative texture/font/sound paths keep working. An empty storedPath falls
    // back to the legacy behaviour (a path derived from the source file).
    struct Source
    {
        std::string id;
        std::string filePath;
        std::string storedPath;
    };

    bool Create(const std::string& outputPath,
                const std::vector<std::pair<std::string, std::string>>& files); // id, filePath
    bool Create(const std::string& outputPath, const std::vector<Source>& files);
    bool Load(const std::string& path);
    bool Extract(const std::string& outputDir) const;
    std::vector<char> Read(const std::string& id) const;
    const std::vector<Entry>& Entries() const
    {
        return entries_;
    }
    bool Has(const std::string& id) const;
    const std::string& SourcePath() const
    {
        return sourcePath_;
    }
    void Clear();

private:
    std::string sourcePath_;
    std::vector<Entry> entries_;
    std::vector<char> data_;
};

} // namespace Fem
