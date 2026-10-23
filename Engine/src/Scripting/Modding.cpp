// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Modding.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Vanity.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace Fem
{

namespace
{
// Recursive merge: objects merge key by key, everything else replaces.
void MergeInto(nlohmann::json& target, const nlohmann::json& patch)
{
    if (!target.is_object() || !patch.is_object())
    {
        target = patch;
        return;
    }
    for (const auto& [k, v] : patch.items())
    {
        if (v.is_object() && target.contains(k) && target[k].is_object())
        {
            MergeInto(target[k], v);
        }
        else
        {
            target[k] = v;
        }
    }
}

bool ReadJsonFile(const std::string& path, nlohmann::json& out)
{
    auto parsed = Vanity::ReadJson(path);
    if (!parsed)
    {
        BOY_ERROR("Modding", path + ": " + parsed.Error());
        return false;
    }
    out = std::move(parsed.Value());
    return true;
}

bool WriteJsonFile(const std::string& path, const nlohmann::json& data)
{
    auto written = Vanity::WriteJson(path, data);
    if (!written)
    {
        BOY_ERROR("Modding", "cannot write " + path + ": " + written.Error());
        return false;
    }
    return true;
}

// Mod names come from disk today, but the API is public and a name such as
// "../../secrets" would escape the mods directory.
bool IsSafeModName(const std::string& name)
{
    if (name.empty())
    {
        return false;
    }
    if (name == "." || name == "..")
    {
        return false;
    }
    for (const char c : name)
    {
        if (c == '/' || c == '\\' || c == ':')
        {
            return false;
        }
    }
    return true;
}
} // namespace

ModManager& ModManager::Instance()
{
    static ModManager m;
    return m;
}

void ModManager::AddModPath(const std::string& path)
{
    if (path.empty())
    {
        return;
    }
    if (std::find(modPaths_.begin(), modPaths_.end(), path) == modPaths_.end())
    {
        modPaths_.push_back(path);
    }
}

void ModManager::ScanMods()
{
    namespace fs = std::filesystem;
    BOY_INFO("Modding", "scanning mods...");
    mods_.clear();

    for (const auto& p : modPaths_)
    {
        std::error_code ec;
        if (!fs::exists(p, ec) || ec)
        {
            continue;
        }

        for (const auto& e : fs::directory_iterator(p, ec))
        {
            if (ec)
            {
                break;
            }
            if (!e.is_directory())
            {
                continue;
            }

            // A folder only counts as a mod if it has a manifest, so stray
            // directories (backups, .git, caches) are not reported as mods.
            const std::string name = e.path().filename().string();
            if (name.empty() || name[0] == '.')
            {
                continue;
            }

            std::error_code mec;
            const bool hasManifest = fs::exists(e.path() / "mod.json", mec) || fs::exists(e.path() / "mod.femboy", mec);
            if (!hasManifest)
            {
                BOY_DEBUG("Modding", "skipping directory without manifest: " + name);
                continue;
            }

            if (std::find(mods_.begin(), mods_.end(), name) == mods_.end())
            {
                mods_.push_back(name);
                BOY_INFO("Modding", "mod found: " + name);
            }
        }
    }
}

bool ModManager::InjectData(const std::string& target, const nlohmann::json& patch)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(target, ec) || ec)
    {
        BOY_WARN("Modding", "target not found: " + target);
        return false;
    }

    nlohmann::json data;
    if (!ReadJsonFile(target, data))
    {
        return false;
    }

    MergeInto(data, patch);

    if (!WriteJsonFile(target, data))
    {
        return false;
    }
    BOY_INFO("Modding", "injected data into: " + target);
    return true;
}

void ModManager::InjectScript(const std::string& name, const nlohmann::json& program)
{
    if (name.empty())
    {
        BOY_WARN("Modding", "injectScript: empty name");
        return;
    }
    scripts_[name] = program;
    BOY_INFO("Modding", "injected script: " + name);
}

const nlohmann::json* ModManager::GetScript(const std::string& name) const
{
    auto it = scripts_.find(name);
    return (it == scripts_.end()) ? nullptr : &it->second;
}

std::vector<std::string> ModManager::ScriptNames() const
{
    std::vector<std::string> out;
    out.reserve(scripts_.size());
    for (const auto& [name, _] : scripts_)
    {
        out.push_back(name);
    }
    return out;
}

bool ModManager::InjectWidget(const std::string& uiFile, const nlohmann::json& widget)
{
    namespace fs = std::filesystem;

    nlohmann::json arr = nlohmann::json::array();
    std::error_code ec;
    if (fs::exists(uiFile, ec) && !ec)
    {
        nlohmann::json existing;
        if (!ReadJsonFile(uiFile, existing))
        {
            return false;
        }

        // Accept both a single widget object and an array of widgets.
        if (existing.is_array())
        {
            arr = std::move(existing);
        }
        else if (existing.is_object())
        {
            arr.push_back(std::move(existing));
        }
        else
        {
            BOY_ERROR("Modding", "UI file is neither an object nor an array: " + uiFile);
            return false;
        }
    }

    arr.push_back(widget);
    if (!WriteJsonFile(uiFile, arr))
    {
        return false;
    }
    BOY_INFO("Modding", "injected widget into: " + uiFile);
    return true;
}

std::vector<std::string> ModManager::ModFiles(const std::string& modName, const std::string& subDir) const
{
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    if (!IsSafeModName(modName))
    {
        BOY_WARN("Modding", "modFiles: rejected unsafe mod name: " + modName);
        return out;
    }

    for (const auto& p : modPaths_)
    {
        const fs::path d = fs::path(p) / modName / subDir;
        std::error_code ec;
        if (!fs::exists(d, ec) || ec)
        {
            continue;
        }

        for (const auto& e : fs::directory_iterator(d, ec))
        {
            if (ec)
            {
                break;
            }
            if (e.is_regular_file(ec))
            {
                out.push_back(e.path().string());
            }
        }
    }
    return out;
}

} // namespace Fem
