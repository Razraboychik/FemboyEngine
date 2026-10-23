// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Scene.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/ClassDefaults.hpp"
#include "FemboyEngine/Mirror.hpp"
#include "FemboyEngine/Vanity.hpp"
#include <fstream>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace Fem
{

bool Scene::Migrate(nlohmann::json& data, std::vector<std::string>* report)
{
    if (!data.is_object())
    {
        if (report)
        {
            report->push_back("scene root is not an object");
        }
        return false;
    }

    int version = 0;
    if (data.contains("version") && data["version"].is_number_integer())
    {
        version = data["version"].get<int>();
    }
    else if (data.contains("version") && data["version"].is_number_unsigned())
    {
        version = static_cast<int>(data["version"].get<unsigned>());
    }

    if (version > CurrentVersion())
    {
        if (report)
        {
            report->push_back("scene version " + std::to_string(version) + " is newer than " +
                              std::to_string(CurrentVersion()));
        }
        return false;
    }

    if (version < 1)
    {
        // Legacy scenes had no version. A few early tools used "objects" for the
        // entity array; accept both spellings instead of refusing to open the
        // project that produced them.
        if (!data.contains("entities") && data.contains("objects"))
        {
            data["entities"] = std::move(data["objects"]);
            data.erase("objects");
            if (report)
            {
                report->push_back("renamed legacy 'objects' to 'entities'");
            }
        }
        if (!data.contains("entities"))
        {
            data["entities"] = nlohmann::json::array();
        }
        if (!data["entities"].is_array())
        {
            if (report)
            {
                report->push_back("'entities' is not an array");
            }
            return false;
        }
        data["version"] = 1;
        if (report)
        {
            report->push_back("migrated legacy scene to version 1");
        }
    }

    if (!data.contains("entities"))
    {
        data["entities"] = nlohmann::json::array();
    }
    if (!data["entities"].is_array())
    {
        if (report)
        {
            report->push_back("'entities' is not an array");
        }
        return false;
    }

    return true;
}

bool Scene::Load(const std::string& path)
{
    auto parsed = Vanity::ReadJson(path);
    if (!parsed)
    {
        BOY_ERROR("Scene", parsed.Error());
        return false;
    }
    if (!parsed.Value().is_object())
    {
        BOY_ERROR("Scene", "root value must be an object: " + path);
        return false;
    }
    data_ = std::move(parsed.Value());
    std::vector<std::string> migration;
    if (!Migrate(data_, &migration))
    {
        for (const std::string& note : migration)
        {
            BOY_ERROR("Scene", note);
        }
        return false;
    }
    for (const std::string& note : migration)
    {
        BOY_INFO("Scene", note);
    }
    // json::value() throws a type_error when the stored value has the wrong
    // type; a hand-edited or corrupted scene must not abort the load.
    name_ = (data_.contains("name") && data_["name"].is_string()) ? data_["name"].get<std::string>()
                                                                  : std::string("Untitled");
    BOY_INFO("Scene", "loaded: " + path);
    return true;
}

bool Scene::LoadAdditive(const std::string& path, Registry& reg)
{
    Scene loaded;
    if (!loaded.Load(path))
    {
        return false;
    }
    loaded.ApplyTo(reg, true);
    return true;
}

bool Scene::Save(const std::string& path) const
{
    // Vanity writes atomically: a half-written scene file must never destroy
    // the last good version on disk.
    auto written = Vanity::WriteJson(path, data_);
    if (!written)
    {
        BOY_ERROR("Scene", "save failed: " + written.Error());
        return false;
    }
    BOY_INFO("Scene", "saved: " + path);
    return true;
}

void Scene::CaptureFrom(const Registry& reg)
{
    data_ = nlohmann::json::object();
    data_["name"] = name_;
    data_["version"] = CurrentVersion();
    nlohmann::json ents = nlohmann::json::array();
    auto* estorage = reg.storage<entt::entity>();
    if (!estorage)
    {
        // Always emit the key so a scene captured from an empty registry round
        // trips through save/load instead of looking like a malformed file.
        data_["entities"] = ents;
        return;
    }
    for (auto e : *estorage)
    {
        nlohmann::json je = nlohmann::json::object();
        je["id"] = (uint32_t)e;
        // Every reflected component describes its own JSON; adding a component
        // to the engine no longer means editing this function.
        for (const auto& comp : Mirror::Instance().Components())
        {
            if (comp.has(reg, e))
            {
                comp.capture(reg, e, je);
            }
        }
        ents.push_back(je);
    }
    data_["entities"] = ents;
}

void Scene::ApplyTo(Registry& reg) const
{
    ApplyTo(reg, false);
}

void Scene::ApplyTo(Registry& reg, bool additive) const
{
    if (!additive)
    {
        reg.clear();
    }
    if (!data_.contains("entities") || !data_["entities"].is_array())
    {
        return;
    }

    for (const auto& je : data_["entities"])
    {
        if (!je.is_object())
        {
            continue;
        }

        // Restore the saved entity id so cross-entity references (parents,
        // targets, save-game links) stay valid after loading. EnTT refuses to
        // reuse a live id, and a duplicate/absent id falls back to a fresh one.
        Entity e = entt::null;
        if (je.contains("id") && je["id"].is_number_unsigned())
        {
            const auto raw = je["id"].get<std::uint32_t>();
            if (raw != static_cast<std::uint32_t>(entt::null) && !reg.valid(static_cast<Entity>(raw)))
            {
                e = reg.create(static_cast<Entity>(raw));
            }
        }
        if (e == entt::null)
        {
            e = reg.create();
        }

        for (const auto& comp : Mirror::Instance().Components())
        {
            if (!je.contains(comp.jsonKey))
            {
                continue;
            }
            const auto& value = je[comp.jsonKey];
            if (!comp.scalar && !value.is_object())
            {
                continue;
            }
            // Additive loading can target an entity that already owns the
            // component. In that case write only the fields present in the
            // scene; applying the full default set would silently reset fields
            // the additively loaded scene never mentioned.
            if (comp.has(reg, e))
            {
                comp.apply(reg, e, value);
            }
            else
            {
                ClassDefaults::Instance().ApplyTo(reg, e, comp, value);
            }
        }
    }
    BOY_INFO("Scene", additive ? "applied additively to registry" : "applied to registry");
}

} // namespace Fem
