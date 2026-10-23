// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/SaveLoad.hpp"
#include "FemboyEngine/Scene.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Vanity.hpp"
#include <filesystem>

namespace Fem
{

SaveSystem& SaveSystem::Instance()
{
    static SaveSystem s;
    return s;
}

bool SaveSystem::Save(const std::string& slot, const Registry& reg, const nlohmann::json& extra)
{
    auto dir = Vanity::EnsureDirectory(saveDir_);
    if (!dir)
    {
        BOY_ERROR("SaveSystem", dir.Error());
        return false;
    }

    const std::string path = saveDir_ + "/" + Vanity::SanitizeFileName(slot, "slot") + ".save";
    const std::string backupPath = path + ".bak";

    // Keep one generation of history. The atomic write protects against a
    // truncated file, but not against saving a broken world state over a good
    // one; the backup is the escape hatch (and load() falls back to it).
    if (Vanity::Exists(path))
    {
        auto previous = Vanity::ReadText(path);
        if (previous)
        {
            auto backup = Vanity::WriteText(backupPath, previous.Value());
            if (!backup)
            {
                BOY_WARN("SaveSystem", "could not write backup: " + backup.Error());
            }
        }
    }

    Scene sc;
    sc.SetName(slot);
    sc.CaptureFrom(reg);
    nlohmann::json data = sc.Data();
    data["saveVersion"] = 1;
    data["extra"] = extra;

    // Vanity writes to a temp file and renames, so a crash or a full disk
    // mid-write cannot destroy the player's previous save.
    auto written = Vanity::WriteJson(path, data);
    if (!written)
    {
        BOY_ERROR("SaveSystem", "save failed: " + written.Error());
        return false;
    }
    BOY_INFO("SaveSystem", "saved slot: " + slot);
    return true;
}

bool SaveSystem::Load(const std::string& slot, Registry& reg, nlohmann::json* extra)
{
    const std::string path = saveDir_ + "/" + Vanity::SanitizeFileName(slot, "slot") + ".save";
    const std::string backupPath = path + ".bak";
    auto parsed = Vanity::ReadJson(path);
    if (!parsed)
    {
        // A corrupt primary file should not make the slot unloadable when a
        // good previous generation exists.
        if (Vanity::Exists(backupPath))
        {
            auto backup = Vanity::ReadJson(backupPath);
            if (backup)
            {
                BOY_WARN("SaveSystem", "primary save unreadable, recovered backup: " + slot);
                parsed = std::move(backup);
            }
        }
        if (!parsed)
        {
            BOY_WARN("SaveSystem", "load failed: " + parsed.Error());
            return false;
        }
    }

    Scene sc;
    sc.Data() = std::move(parsed.Value());
    sc.SetName(slot);
    sc.ApplyTo(reg);
    if (extra && sc.Data().contains("extra"))
    {
        *extra = sc.Data()["extra"];
    }
    BOY_INFO("SaveSystem", "loaded slot: " + slot);
    return true;
}

bool SaveSystem::HasSave(const std::string& slot) const
{
    return Vanity::Exists(saveDir_ + "/" + Vanity::SanitizeFileName(slot, "slot") + ".save");
}

void SaveSystem::DeleteSave(const std::string& slot)
{
    const std::string path = saveDir_ + "/" + Vanity::SanitizeFileName(slot, "slot") + ".save";
    if (!Vanity::Exists(path))
    {
        return;
    }
    auto removed = Vanity::Remove(path);
    if (!removed)
    {
        BOY_ERROR("SaveSystem", "delete failed: " + removed.Error());
    }
    else
    {
        BOY_INFO("SaveSystem", "deleted: " + slot);
    }
}

std::vector<std::string> SaveSystem::ListSaves() const
{
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(saveDir_, ec) || ec)
    {
        return out;
    }
    for (auto& e : fs::directory_iterator(saveDir_, ec))
    {
        if (ec)
        {
            break;
        }
        if (e.is_regular_file() && e.path().extension() == ".save")
        {
            out.push_back(e.path().stem().string());
        }
    }
    return out;
}

} // namespace Fem
