// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <nlohmann/json.hpp>
#include <string>

#include "FemboyEngine/ECS.hpp"
#include "FemboyEngine/Mirror.hpp"

namespace Fem
{

// Project-level defaults for reflected components.
//
// The inheritance chain is:
//
//   C++ field default -> ClassDefaults -> prefab parent -> prefab -> instance
//
// ClassDefaults only stores the fields a designer changed; everything else comes
// from the component's captured Field::defaultValue. That keeps the file small
// and lets an engine upgrade change a C++ default without rewriting every
// project that never touched the field.
class ClassDefaults
{
public:
    static ClassDefaults& Instance();

    // Component key is the Mirror JSON key ("health") or the type name
    // ("HealthComponent"). An unknown key creates an empty override object.
    nlohmann::json& For(const std::string& componentKey);
    const nlohmann::json* Find(const std::string& componentKey) const;
    void Remove(const std::string& componentKey);
    void Clear();

    // Field defaults plus the project override, merged into one component JSON
    // value. Returns an empty object for a scalar component.
    nlohmann::json Resolve(const ComponentReflection& component) const;

    // Applies defaults first and then the supplied component value. This is the
    // single path Scene, Prefab and the editor use, so adding a component
    // through any of them has identical behaviour.
    bool ApplyTo(Registry& registry, Entity entity, const ComponentReflection& component,
                 const nlohmann::json& value = nlohmann::json()) const;

    // Resets one field to its registered default. Returns false when the field
    // is unknown.
    bool ResetField(Registry& registry, Entity entity, const ComponentReflection& component,
                    const std::string& fieldName) const;

    nlohmann::json ToJson() const;
    bool FromJson(const nlohmann::json& json);
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;

    const nlohmann::json& Data() const
    {
        return data_;
    }
    nlohmann::json& Data()
    {
        return data_;
    }

private:
    ClassDefaults() = default;
    nlohmann::json data_ = nlohmann::json::object();
};

// Finds a component by JSON key or type name. Returns nullptr for unknown names.
const ComponentReflection* FindReflectedComponent(const std::string& nameOrKey);

} // namespace Fem
