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

// Modding / injection system: load mods from folders, inject data/scripts/widgets.
class ModManager
{
public:
    static ModManager& Instance();

    void AddModPath(const std::string& path);
    void ScanMods();
    const std::vector<std::string>& Mods() const
    {
        return mods_;
    }

    // injection points
    // JSON merge patch: objects are merged recursively, other values replace.
    bool InjectData(const std::string& target, const nlohmann::json& patch);
    void InjectScript(const std::string& name, const nlohmann::json& program);
    bool InjectWidget(const std::string& uiFile, const nlohmann::json& widget);

    // Scripts injected at runtime, retrievable by name.
    const nlohmann::json* GetScript(const std::string& name) const;
    std::vector<std::string> ScriptNames() const;

    std::vector<std::string> ModFiles(const std::string& modName, const std::string& subDir) const;

private:
    ModManager() = default;
    std::vector<std::string> modPaths_;
    std::vector<std::string> mods_;
    std::map<std::string, nlohmann::json> scripts_;
};

} // namespace Fem
