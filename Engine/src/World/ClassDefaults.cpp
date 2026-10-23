// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/ClassDefaults.hpp"

#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Vanity.hpp"

namespace Fem
{

ClassDefaults& ClassDefaults::Instance()
{
    static ClassDefaults defaults;
    return defaults;
}

nlohmann::json& ClassDefaults::For(const std::string& componentKey)
{
    if (!data_.is_object())
    {
        data_ = nlohmann::json::object();
    }
    if (!data_.contains(componentKey) || !data_[componentKey].is_object())
    {
        data_[componentKey] = nlohmann::json::object();
    }
    return data_[componentKey];
}

const nlohmann::json* ClassDefaults::Find(const std::string& componentKey) const
{
    if (!data_.is_object())
    {
        return nullptr;
    }
    const auto it = data_.find(componentKey);
    if (it == data_.end() || !it->is_object())
    {
        return nullptr;
    }
    return &*it;
}

void ClassDefaults::Remove(const std::string& componentKey)
{
    if (data_.is_object())
    {
        data_.erase(componentKey);
    }
}

void ClassDefaults::Clear()
{
    data_ = nlohmann::json::object();
}

nlohmann::json ClassDefaults::Resolve(const ComponentReflection& component) const
{
    nlohmann::json resolved = nlohmann::json::object();
    if (!component.fields)
    {
        return resolved;
    }

    for (const Field& field : *component.fields)
    {
        const nlohmann::json value = field.defaultValue;
        if (field.flattened && value.is_object())
        {
            for (const auto& [key, entry] : value.items())
            {
                resolved[key] = entry;
            }
        }
        else
        {
            resolved[field.name] = value;
        }
    }

    if (const nlohmann::json* override = Find(component.jsonKey))
    {
        // RFC 7386-style merge: objects merge recursively, arrays and scalars
        // replace. This matches Prefab::ResolvedData() so a class default and a
        // prefab override compose predictably.
        resolved.merge_patch(*override);
    }
    return resolved;
}

bool ClassDefaults::ApplyTo(Registry& registry, Entity entity, const ComponentReflection& component,
                            const nlohmann::json& value) const
{
    if (!component.has || !component.apply)
    {
        return false;
    }
    if (component.scalar)
    {
        nlohmann::json scalar = value;
        if (scalar.is_null() && component.fields && !component.fields->empty())
        {
            scalar = component.fields->front().defaultValue;
        }
        component.apply(registry, entity, scalar);
        return true;
    }

    nlohmann::json merged = Resolve(component);
    if (value.is_object())
    {
        merged.merge_patch(value);
    }
    component.apply(registry, entity, merged);
    return true;
}

bool ClassDefaults::ResetField(Registry& registry, Entity entity, const ComponentReflection& component,
                               const std::string& fieldName) const
{
    if (!component.has || !component.fields || !registry.valid(entity))
    {
        return false;
    }
    void* object = component.getMutable(registry, entity);
    if (!object)
    {
        return false;
    }
    for (const Field& field : *component.fields)
    {
        if (field.name == fieldName)
        {
            field.FromJson(object, field.defaultValue);
            return true;
        }
    }
    return false;
}

nlohmann::json ClassDefaults::ToJson() const
{
    return data_;
}

bool ClassDefaults::FromJson(const nlohmann::json& json)
{
    if (!json.is_object())
    {
        BOY_ERROR("ClassDefaults", "root must be an object");
        return false;
    }
    data_ = json;
    return true;
}

bool ClassDefaults::Load(const std::string& path)
{
    const Result<nlohmann::json> result = Vanity::ReadJson(path);
    if (!result)
    {
        BOY_WARN("ClassDefaults", "cannot load " + path + ": " + result.Error());
        return false;
    }
    if (!FromJson(result.Value()))
    {
        return false;
    }
    BOY_INFO("ClassDefaults", "loaded: " + path);
    return true;
}

bool ClassDefaults::Save(const std::string& path) const
{
    const Result<void> result = Vanity::WriteJson(path, data_, 2);
    if (!result)
    {
        BOY_ERROR("ClassDefaults", "write failed: " + path + " (" + result.Error() + ")");
        return false;
    }
    BOY_INFO("ClassDefaults", "saved: " + path);
    return true;
}

const ComponentReflection* FindReflectedComponent(const std::string& nameOrKey)
{
    return Mirror::Instance().Find(nameOrKey);
}

} // namespace Fem
