// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/ECS.hpp"

namespace Fem
{

// Scene: serializable collection of entities + components.
//
// A scene is intentionally additive-capable: loading a streamed chunk or a
// prefab-authored set of entities must not clear the world. `LoadAdditive()`
// keeps the registry and gives every entity a fresh id when the saved id is
// already occupied, while `ApplyTo()` remains the replace-the-world path used by
// save games.
class Scene
{
public:
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
    bool LoadAdditive(const std::string& path, Registry& reg);
    void ApplyTo(Registry& reg) const;
    void ApplyTo(Registry& reg, bool additive) const;
    void CaptureFrom(const Registry& reg);

    // Current on-disk schema. Scenes written by this version contain
    // "version": 1; a file without it is the legacy pre-versioning format and
    // is migrated on load.
    static int CurrentVersion()
    {
        return 1;
    }
    // Mutates `data` in place. Returns false when the scene is malformed or was
    // written by a newer engine. `report` receives human-readable migration
    // notes for the editor/log.
    static bool Migrate(nlohmann::json& data, std::vector<std::string>* report = nullptr);
    const nlohmann::json& Data() const
    {
        return data_;
    }
    nlohmann::json& Data()
    {
        return data_;
    }
    const std::string& Name() const
    {
        return name_;
    }
    void SetName(const std::string& n)
    {
        name_ = n;
    }

private:
    std::string name_ = "Untitled";
    nlohmann::json data_ = nlohmann::json::object();
};

} // namespace Fem
