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
#include "FemboyEngine/ECS.hpp"

namespace Fem
{

// Prefab: reusable entity template.
class Prefab
{
public:
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;

    // A prefab is a component patch, not a closed copy. `parent` (also accepted
    // as `extends`) names another prefab whose components are inherited;
    // `overrides` is applied after this prefab's own fields, and `children`
    // spawns additional prefabs. The format stays plain JSON so an editor, a
    // script or a hand edit all produce the same result.
    //
    // Meta keys are never treated as components:
    //   name, description, parent, extends, overrides, children, version, guid
    Entity Instantiate(Registry& reg) const;
    Entity Instantiate(Registry& reg, const nlohmann::json& overrides) const;

    // Applies the resolved prefab to an existing entity. Used by the editor to
    // re-apply a changed prefab without destroying the entity's runtime links.
    void ApplyTo(Registry& reg, Entity e) const;

    // Parent chain plus overrides flattened into one component object. It is
    // deterministic and is what Instantiate()/ApplyTo() consume.
    nlohmann::json ResolvedData(std::vector<std::string>* errors = nullptr) const;

    bool HasParent() const;
    std::string ParentName() const;

    void CaptureFrom(Registry& reg, Entity e);
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
    std::string name_ = "Prefab";
    nlohmann::json data_ = nlohmann::json::object();
};

// Prefab library
class PrefabLibrary
{
public:
    static PrefabLibrary& Instance();
    void LoadFromDir(const std::string& dir);
    Prefab* Get(const std::string& name);
    // Convenience used by scene tools and the editor. Returns entt::null when
    // the prefab is missing instead of throwing.
    Entity Instantiate(const std::string& name, Registry& reg, const nlohmann::json& overrides = {}) const;
    const std::vector<std::string>& Names() const
    {
        return names_;
    }
    void Add(const std::string& name, const Prefab& p);
    void Clear();

private:
    PrefabLibrary() = default;
    std::map<std::string, Prefab> prefabs_;
    std::vector<std::string> names_;
};

} // namespace Fem
