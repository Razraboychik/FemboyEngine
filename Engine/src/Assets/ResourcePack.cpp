// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/ResourcePack.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Vanity.hpp"
#include <fstream>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <algorithm>

namespace Fem
{

namespace
{
constexpr uint32_t kMaxManifestSize = 64u * 1024u * 1024u; // 64 MiB

void WriteU32Le(std::ostream& out, uint32_t v)
{
    const char bytes[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                           static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
    out.write(bytes, 4);
}

bool ReadU32Le(std::istream& in, uint32_t& v)
{
    unsigned char bytes[4]{};
    if (!in.read(reinterpret_cast<char*>(bytes), 4))
    {
        return false;
    }
    v = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
    return true;
}

// Returns a path relative to `root` that cannot escape it, or an empty
// string when the stored path is unsafe (absolute, drive letter, "..").
std::string SafeRelativePath(const std::string& stored)
{
    if (stored.empty())
    {
        return {};
    }
    namespace fs = std::filesystem;
    fs::path p(stored);
    if (p.is_absolute() || p.has_root_name() || p.has_root_directory())
    {
        return {};
    }

    fs::path result;
    for (const auto& part : p)
    {
        const std::string s = part.string();
        if (s == "..")
        {
            return {};
        }
        if (s == "." || s.empty())
        {
            continue;
        }
        result /= part;
    }
    if (result.empty())
    {
        return {};
    }
    // Generic separators: a pack written on Windows must extract into the same
    // nested tree on Linux and vice versa.
    return result.generic_string();
}

// Turns a source path into something safeRelativePath() accepts. Absolute paths
// are reduced to a path relative to the current directory when possible and to
// their filename otherwise, so a pack created from absolute paths can still be
// extracted.
std::string NormalizedStoredPath(const std::string& sourcePath)
{
    namespace fs = std::filesystem;
    if (sourcePath.empty())
    {
        return {};
    }

    std::error_code ec;
    fs::path p(sourcePath);
    if (!p.is_absolute() && !p.has_root_name() && !p.has_root_directory())
    {
        const std::string safe = SafeRelativePath(sourcePath);
        if (!safe.empty())
        {
            return safe;
        }
    }

    const fs::path cwd = fs::current_path(ec);
    if (!ec)
    {
        const fs::path rel = fs::relative(p, cwd, ec);
        if (!ec)
        {
            const std::string safe = SafeRelativePath(rel.string());
            if (!safe.empty())
            {
                return safe;
            }
        }
    }

    // Fall back to the bare filename: never absolute, never containing "..".
    const std::string name = p.filename().string();
    return SafeRelativePath(name);
}
} // namespace

void ResourcePack::Clear()
{
    sourcePath_.clear();
    entries_.clear();
    data_.clear();
}

bool ResourcePack::Create(const std::string& outputPath, const std::vector<std::pair<std::string, std::string>>& files)
{
    std::vector<Source> sources;
    sources.reserve(files.size());
    for (const auto& [id, filePath] : files)
    {
        sources.push_back(Source{id, filePath, {}});
    }
    return Create(outputPath, sources);
}

bool ResourcePack::Create(const std::string& outputPath, const std::vector<Source>& files)
{
    Clear();

    nlohmann::json manifest = nlohmann::json::array();
    std::vector<char> blob;

    for (const Source& source : files)
    {
        if (source.id.empty())
        {
            BOY_WARN("ResourcePack", "skipping entry with empty id: " + source.filePath);
            continue;
        }

        std::ifstream f(source.filePath, std::ios::binary | std::ios::ate);
        if (!f.is_open())
        {
            BOY_WARN("ResourcePack", "skip missing: " + source.filePath);
            continue;
        }

        const std::streamoff end = f.tellg();
        if (end < 0)
        {
            BOY_WARN("ResourcePack", "cannot size file: " + source.filePath);
            continue;
        }
        f.seekg(0, std::ios::beg);

        std::vector<char> buf(static_cast<size_t>(end));
        if (!buf.empty() && !f.read(buf.data(), static_cast<std::streamsize>(buf.size())))
        {
            BOY_WARN("ResourcePack", "read failed: " + source.filePath);
            continue;
        }

        // Reject packs that would overflow the 32-bit offsets used by the format.
        if (blob.size() + buf.size() > 0xFFFFFFFFull)
        {
            BOY_ERROR("ResourcePack", "pack too large");
            Clear();
            return false;
        }

        Entry e;
        e.id = source.id;
        // Store a path that is safe to extract later. The cooker passes the path
        // relative to its output directory; the legacy overload derives one
        // from the source file. Absolute paths would be rejected by extract().
        e.path =
            source.storedPath.empty() ? NormalizedStoredPath(source.filePath) : SafeRelativePath(source.storedPath);
        if (e.path.empty())
        {
            BOY_WARN("ResourcePack", "skipping entry with unsafe path: " + source.filePath);
            continue;
        }
        e.offset = blob.size();
        e.size = buf.size();
        entries_.push_back(e);
        manifest.push_back({{"id", e.id}, {"path", e.path}, {"offset", e.offset}, {"size", e.size}});
        blob.insert(blob.end(), buf.begin(), buf.end());
    }

    // Build into a sibling temp file and promote it atomically. Writing the
    // pack in place meant a crash (or a full disk) left a truncated file at the
    // final path, which load() then rejected forever.
    const std::string tempPath = outputPath + ".tmp.pack";
    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        BOY_ERROR("ResourcePack", "cannot write: " + tempPath);
        Clear();
        return false;
    }

    const char magic[4] = {'F', 'E', 'P', 'A'};
    out.write(magic, 4);

    const std::string mstr = manifest.dump();
    if (mstr.size() > kMaxManifestSize)
    {
        BOY_ERROR("ResourcePack", "manifest too large");
        out.close();
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        Clear();
        return false;
    }
    WriteU32Le(out, static_cast<uint32_t>(mstr.size()));
    out.write(mstr.data(), static_cast<std::streamsize>(mstr.size()));
    if (!blob.empty())
    {
        out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    }

    out.flush();
    if (!out)
    {
        BOY_ERROR("ResourcePack", "write failed: " + tempPath);
        out.close();
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        Clear();
        return false;
    }
    out.close();

    const auto promoted = Vanity::PromoteTemp(tempPath, outputPath);
    if (!promoted)
    {
        BOY_ERROR("ResourcePack", promoted.Error());
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        Clear();
        return false;
    }

    data_ = std::move(blob);
    sourcePath_ = outputPath;
    BOY_INFO("ResourcePack", "created: " + outputPath + " (" + std::to_string(entries_.size()) + " entries)");
    return true;
}

bool ResourcePack::Load(const std::string& path)
{
    Clear();

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open())
    {
        BOY_ERROR("ResourcePack", "cannot open: " + path);
        return false;
    }

    const std::streamoff end = in.tellg();
    if (end < 8)
    {
        BOY_ERROR("ResourcePack", "file too small: " + path);
        return false;
    }
    const size_t total = static_cast<size_t>(end);
    in.seekg(0, std::ios::beg);

    char magic[4]{};
    if (!in.read(magic, 4) || magic[0] != 'F' || magic[1] != 'E' || magic[2] != 'P' || magic[3] != 'A')
    {
        BOY_ERROR("ResourcePack", "bad magic: " + path);
        return false;
    }

    uint32_t msz = 0;
    if (!ReadU32Le(in, msz))
    {
        BOY_ERROR("ResourcePack", "cannot read manifest size");
        return false;
    }
    if (msz == 0 || msz > kMaxManifestSize || static_cast<size_t>(msz) > total - 8)
    {
        BOY_ERROR("ResourcePack", "invalid manifest size: " + std::to_string(msz));
        return false;
    }

    std::string mstr(msz, '\0');
    if (!in.read(mstr.data(), static_cast<std::streamsize>(msz)))
    {
        BOY_ERROR("ResourcePack", "truncated manifest");
        return false;
    }

    nlohmann::json manifest;
    try
    {
        manifest = nlohmann::json::parse(mstr);
    }
    catch (const std::exception& e)
    {
        BOY_ERROR("ResourcePack", std::string("manifest parse error: ") + e.what());
        return false;
    }
    if (!manifest.is_array())
    {
        BOY_ERROR("ResourcePack", "manifest must be an array");
        return false;
    }

    const size_t headerSize = 8 + static_cast<size_t>(msz);
    const size_t dataSize = total - headerSize;

    entries_.reserve(manifest.size());
    for (const auto& m : manifest)
    {
        if (!m.is_object() || !m.contains("id") || !m.contains("offset") || !m.contains("size"))
        {
            BOY_WARN("ResourcePack", "skipping malformed manifest entry");
            continue;
        }

        Entry e;
        try
        {
            e.id = m.at("id").get<std::string>();
            e.path = m.value("path", std::string(""));
            e.offset = m.at("offset").get<size_t>();
            e.size = m.at("size").get<size_t>();
        }
        catch (const std::exception&)
        {
            BOY_WARN("ResourcePack", "skipping entry with invalid types");
            continue;
        }

        // Bounds check: offset must be inside the blob and offset+size must not
        // overflow or run past the end. This is the check that used to be missing.
        if (e.offset > dataSize || e.size > dataSize - e.offset)
        {
            BOY_ERROR("ResourcePack", "entry out of bounds: " + e.id);
            Clear();
            return false;
        }

        entries_.push_back(std::move(e));
    }

    in.seekg(static_cast<std::streamoff>(headerSize), std::ios::beg);
    data_.resize(dataSize);
    if (dataSize > 0 && !in.read(data_.data(), static_cast<std::streamsize>(dataSize)))
    {
        BOY_ERROR("ResourcePack", "truncated data section");
        Clear();
        return false;
    }

    sourcePath_ = path;
    BOY_INFO("ResourcePack", "loaded: " + path + " (" + std::to_string(entries_.size()) + " entries)");
    return true;
}

bool ResourcePack::Has(const std::string& id) const
{
    for (const auto& e : entries_)
    {
        if (e.id == id)
        {
            return true;
        }
    }
    return false;
}

std::vector<char> ResourcePack::Read(const std::string& id) const
{
    for (const auto& e : entries_)
    {
        if (e.id != id)
        {
            continue;
        }
        if (e.offset > data_.size() || e.size > data_.size() - e.offset)
        {
            return {};
        }
        return std::vector<char>(data_.begin() + static_cast<std::ptrdiff_t>(e.offset),
                                 data_.begin() + static_cast<std::ptrdiff_t>(e.offset + e.size));
    }
    return {};
}

bool ResourcePack::Extract(const std::string& outputDir) const
{
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::create_directories(outputDir, ec);
    if (ec)
    {
        BOY_ERROR("ResourcePack", "cannot create output dir: " + outputDir);
        return false;
    }

    const fs::path root = fs::weakly_canonical(fs::path(outputDir), ec);
    if (ec)
    {
        BOY_ERROR("ResourcePack", "cannot resolve output dir: " + outputDir);
        return false;
    }

    bool allOk = true;
    for (const auto& e : entries_)
    {
        if (e.offset > data_.size() || e.size > data_.size() - e.offset)
        {
            BOY_ERROR("ResourcePack", "entry out of bounds while extracting: " + e.id);
            allOk = false;
            continue;
        }

        // Only the stored relative path is used, never the original absolute
        // source path, and traversal is rejected outright.
        const std::string rel = SafeRelativePath(e.path);
        if (rel.empty())
        {
            BOY_ERROR("ResourcePack", "refusing unsafe path: " + e.path);
            allOk = false;
            continue;
        }

        fs::path target = root / fs::path(rel);
        const fs::path canonicalTarget = fs::weakly_canonical(target, ec);
        if (ec)
        {
            BOY_ERROR("ResourcePack", "cannot resolve path: " + target.string());
            allOk = false;
            continue;
        }

        // Belt and braces: the final path must still live under the root. A
        // plain prefix compare accepted "C:/out" as a prefix of "C:/outside/x",
        // so require a separator (or exact equality).
        const std::string rootStr = root.string();
        const std::string targetStr = canonicalTarget.string();
        const bool underRoot =
            targetStr == rootStr ||
            (targetStr.size() > rootStr.size() && targetStr.compare(0, rootStr.size(), rootStr) == 0 &&
             (targetStr[rootStr.size()] == '/' || targetStr[rootStr.size()] == '\\'));
        if (!underRoot)
        {
            BOY_ERROR("ResourcePack", "path escapes output dir: " + targetStr);
            allOk = false;
            continue;
        }

        fs::create_directories(canonicalTarget.parent_path(), ec);
        std::ofstream f(canonicalTarget, std::ios::binary);
        if (!f.is_open())
        {
            BOY_ERROR("ResourcePack", "cannot write: " + canonicalTarget.string());
            allOk = false;
            continue;
        }
        f.write(data_.data() + e.offset, static_cast<std::streamsize>(e.size));
        f.flush();
        if (!f)
        {
            BOY_ERROR("ResourcePack", "write failed: " + canonicalTarget.string());
            allOk = false;
        }
    }

    BOY_INFO("ResourcePack", std::string(allOk ? "extracted to: " : "extracted with errors to: ") + outputDir);
    return allOk;
}

} // namespace Fem
