// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/SettingsRegistry.hpp"

#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Vanity.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace Fem
{

namespace
{
bool IsColor(const nlohmann::json& value)
{
    if (value.is_string())
    {
        const std::string text = value.get<std::string>();
        if (text.size() != 7 && text.size() != 9)
        {
            return false;
        }
        if (text[0] != '#')
        {
            return false;
        }
        for (std::size_t i = 1; i < text.size(); ++i)
        {
            const char c = text[i];
            const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!hex)
            {
                return false;
            }
        }
        return true;
    }
    if (value.is_array())
    {
        if (value.size() != 3 && value.size() != 4)
        {
            return false;
        }
        for (const auto& channel : value)
        {
            if (!channel.is_number())
            {
                return false;
            }
            const double d = channel.get<double>();
            if (!std::isfinite(d) || d < 0.0 || d > 255.0)
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool ClampNumber(const nlohmann::json& minValue, const nlohmann::json& maxValue, double& value)
{
    if (minValue.is_number())
    {
        value = std::max(value, minValue.get<double>());
    }
    if (maxValue.is_number())
    {
        value = std::min(value, maxValue.get<double>());
    }
    if (!std::isfinite(value))
    {
        return false;
    }
    return true;
}
} // namespace

SettingsRegistry& SettingsRegistry::Instance()
{
    static SettingsRegistry registry;
    return registry;
}

void SettingsRegistry::Clear()
{
    definitions_.clear();
    index_.clear();
    values_ = nlohmann::json::object();
    Bump();
}

bool SettingsRegistry::Add(SettingDef definition)
{
    if (definition.key.empty())
    {
        BOY_WARN("Settings", "ignored a setting with an empty key");
        return false;
    }
    if (index_.find(definition.key) != index_.end())
    {
        BOY_WARN("Settings", "duplicate setting key: " + definition.key);
        return false;
    }
    if (definition.category.empty())
    {
        definition.category = "General";
    }
    if (definition.label.empty())
    {
        definition.label = definition.key;
    }

    // Every registered setting gets a value immediately. A missing initial
    // value is a schema bug, not an empty setting: callers must choose a
    // meaningful default.
    if (!definition.defaultValue.is_null())
    {
        values_[definition.key] = definition.defaultValue;
    }
    else
    {
        values_[definition.key] = nullptr;
    }

    index_.emplace(definition.key, definitions_.size());
    definitions_.push_back(std::move(definition));
    Bump();
    return true;
}

bool SettingsRegistry::Has(const std::string& key) const
{
    return index_.find(key) != index_.end();
}

const SettingDef* SettingsRegistry::Definition(const std::string& key) const
{
    const auto it = index_.find(key);
    if (it == index_.end())
    {
        return nullptr;
    }
    return &definitions_[it->second];
}

std::vector<std::string> SettingsRegistry::Categories() const
{
    std::vector<std::string> result;
    std::set<std::string> seen;
    for (const SettingDef& def : definitions_)
    {
        if (seen.insert(def.category).second)
        {
            result.push_back(def.category);
        }
    }
    return result;
}

std::vector<const SettingDef*> SettingsRegistry::InCategory(const std::string& category) const
{
    std::vector<const SettingDef*> result;
    for (const SettingDef& def : definitions_)
    {
        if (def.category == category)
        {
            result.push_back(&def);
        }
    }
    return result;
}

nlohmann::json SettingsRegistry::GetJson(const std::string& key) const
{
    const auto it = values_.find(key);
    return it == values_.end() ? nlohmann::json(nullptr) : *it;
}

bool SettingsRegistry::ApplyValue(const SettingDef& def, const nlohmann::json& value, nlohmann::json& out) const
{
    switch (def.type)
    {
        case SettingType::Bool:
            if (!value.is_boolean())
            {
                return false;
            }
            out = value.get<bool>();
            return true;

        case SettingType::Int:
        {
            if (!value.is_number())
            {
                return false;
            }
            double number = value.get<double>();
            if (!ClampNumber(def.minValue, def.maxValue, number))
            {
                return false;
            }
            out = static_cast<int>(std::lround(number));
            return true;
        }

        case SettingType::Float:
        {
            if (!value.is_number())
            {
                return false;
            }
            double number = value.get<double>();
            if (!ClampNumber(def.minValue, def.maxValue, number))
            {
                return false;
            }
            out = static_cast<float>(number);
            return true;
        }

        case SettingType::Enum:
        {
            if (!value.is_number())
            {
                return false;
            }
            double number = value.get<double>();
            const double maxIndex = def.enumLabels.empty()
                                        ? (def.maxValue.is_number() ? def.maxValue.get<double>() : 0.0)
                                        : static_cast<double>(def.enumLabels.size() - 1);
            number = std::clamp(number, def.minValue.is_number() ? def.minValue.get<double>() : 0.0, maxIndex);
            out = static_cast<int>(std::lround(number));
            return true;
        }

        case SettingType::String:
            if (!value.is_string())
            {
                return false;
            }
            out = value.get<std::string>();
            return true;

        case SettingType::Color:
            if (!IsColor(value))
            {
                return false;
            }
            out = value;
            return true;
    }
    return false;
}

void SettingsRegistry::Notify(const SettingDef& def) const
{
    if (!def.onChange)
    {
        return;
    }
    const auto it = values_.find(def.key);
    if (it == values_.end())
    {
        return;
    }
    try
    {
        def.onChange(*it);
    }
    catch (const std::exception& e)
    {
        BOY_ERROR("Settings", "onChange for '" + def.key + "' threw: " + e.what());
    }
}

void SettingsRegistry::Bump()
{
    ++revision_;
}

bool SettingsRegistry::Set(const std::string& key, const nlohmann::json& value, bool notifyChanges)
{
    const SettingDef* def = Definition(key);
    if (!def)
    {
        return false;
    }

    nlohmann::json accepted;
    if (!ApplyValue(*def, value, accepted))
    {
        return false;
    }

    if (values_[key] == accepted)
    {
        return true;
    }

    values_[key] = accepted;
    Bump();
    for (const Binding& binding : bindings_)
    {
        if (binding.key == key)
        {
            binding.apply(accepted);
        }
    }
    if (notifyChanges)
    {
        Notify(*def);
    }
    return true;
}

bool SettingsRegistry::Reset(const std::string& key, bool notifyChanges)
{
    const SettingDef* def = Definition(key);
    if (!def)
    {
        return false;
    }
    if (def->defaultValue.is_null())
    {
        return false;
    }
    return Set(key, def->defaultValue, notifyChanges);
}

void SettingsRegistry::ResetAll(bool notifyChanges)
{
    for (const SettingDef& def : definitions_)
    {
        if (!def.defaultValue.is_null())
        {
            Set(def.key, def.defaultValue, notifyChanges);
        }
    }
}

bool SettingsRegistry::LoadJson(const nlohmann::json& json, bool notifyChanges)
{
    if (!json.is_object())
    {
        return false;
    }

    const nlohmann::json* values = &json;
    if (json.contains("values") && json["values"].is_object())
    {
        values = &json["values"];
    }
    else if (json.contains("settings") && json["settings"].is_object())
    {
        values = &json["settings"];
    }

    std::vector<std::string> changed;
    for (auto it = values->begin(); it != values->end(); ++it)
    {
        const std::string key = it.key();
        if (const SettingDef* def = Definition(key))
        {
            nlohmann::json accepted;
            if (!ApplyValue(*def, it.value(), accepted))
            {
                BOY_WARN("Settings", "ignored invalid value for '" + key + "'");
                continue;
            }
            if (values_[key] != accepted)
            {
                values_[key] = accepted;
                changed.push_back(key);
            }
        }
        else
        {
            // Preserve extension keys. A newer build, a mod or a game-specific
            // subsystem may own them; dropping them on save is data loss.
            values_[key] = it.value();
        }
    }

    if (!changed.empty())
    {
        Bump();
    }
    if (notifyChanges)
    {
        for (const std::string& key : changed)
        {
            if (const SettingDef* def = Definition(key))
            {
                Notify(*def);
            }
        }
    }
    SyncFromRegistry();
    return true;
}

nlohmann::json SettingsRegistry::ToJson() const
{
    return nlohmann::json{{"schema", 1}, {"values", values_}};
}

bool SettingsRegistry::LoadFile(const std::string& path, bool notifyChanges)
{
    Result<nlohmann::json> parsed = Vanity::ReadJson(path);
    if (!parsed)
    {
        BOY_INFO("Settings", "settings file not found or unreadable: " + path);
        return false;
    }
    return LoadJson(parsed.Value(), notifyChanges);
}

bool SettingsRegistry::SaveFile(const std::string& path) const
{
    Result<void> written = Vanity::WriteJson(path, ToJson());
    if (!written)
    {
        BOY_ERROR("Settings", written.Error());
        return false;
    }
    return true;
}

void SettingsRegistry::SyncFromRegistry()
{
    for (const Binding& binding : bindings_)
    {
        const nlohmann::json value = GetJson(binding.key);
        if (!value.is_null())
        {
            binding.apply(value);
        }
    }
}

void SettingsRegistry::SyncToRegistry()
{
    for (const Binding& binding : bindings_)
    {
        Set(binding.key, binding.collect(), false);
    }
}

} // namespace Fem
