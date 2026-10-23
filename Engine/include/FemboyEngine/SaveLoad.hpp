// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/ECS.hpp"

namespace Fem
{

// Persistent save/load system for game state.
class SaveSystem
{
public:
    static SaveSystem& Instance();
    void SetSaveDir(const std::string& dir)
    {
        saveDir_ = dir;
    }
    // save registry + custom data to a slot
    bool Save(const std::string& slot, const Registry& reg, const nlohmann::json& extra = {});
    // load
    bool Load(const std::string& slot, Registry& reg, nlohmann::json* extra = nullptr);
    bool HasSave(const std::string& slot) const;
    void DeleteSave(const std::string& slot);
    std::vector<std::string> ListSaves() const;
    const std::string& SaveDir() const
    {
        return saveDir_;
    }

private:
    SaveSystem() = default;
    std::string saveDir_ = "saves";
};

} // namespace Fem
