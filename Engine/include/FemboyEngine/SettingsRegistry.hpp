// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "FemboyEngine/FemObject.hpp"

namespace Fem
{

// A small, UE-style settings schema.
//
// A game or an engine subsystem describes a setting once: its stable JSON key,
// category, human label, type, default, range and validation callback. The same
// description can then drive serialization, a generated settings page, a
// developer console or a mod editor. Values are kept in one registry and are
// addressed by their dotted key.
//
// This is deliberately data-oriented rather than reflection-based. C++ has no
// portable field reflection, and a macro-heavy "magic serialization" layer is
// harder to debug than a plain descriptor. A typed settings struct (like the
// game's Neon::Settings) can still sit on top and call Get()/Set() when it
// wants to; the registry is the shared catalogue, not a replacement for a
// convenient game-side facade.
enum class SettingType
{
    Bool,
    Int,
    Float,
    Enum,
    String,
    Color // "#RRGGBB[AA]" or [r, g, b, a]
};

// Optional metadata flags. They do not affect validation; a generated UI can
// use them to group engine/game/advanced settings.
enum SettingFlags : std::uint32_t
{
    SettingFlag_None = 0,
    SettingFlag_Engine = 1u << 0,
    SettingFlag_Game = 1u << 1,
    SettingFlag_UserVisible = 1u << 2,
    SettingFlag_Advanced = 1u << 3,
    SettingFlag_RestartRequired = 1u << 4
};

struct SettingDef
{
    std::string key;      // "engine.display.gamma"
    std::string category; // "Display"
    std::string label;    // English source string; a UI can localize it
    std::string tooltip;
    SettingType type = SettingType::Float;
    nlohmann::json defaultValue = nullptr;
    nlohmann::json minValue = nullptr;
    nlohmann::json maxValue = nullptr;
    nlohmann::json step = nullptr;
    std::vector<std::string> enumLabels;
    std::uint32_t flags = SettingFlag_UserVisible;

    // Called after a value has been accepted. This is how an engine subsystem
    // can push a changed value into a renderer immediately. The callback must
    // not mutate the same setting recursively.
    std::function<void(const nlohmann::json&)> onChange;
};

class SettingsRegistry : public FemObject
{
public:
    FEMCLASS_NO_CREATE(SettingsRegistry, FemObject, false)
    static SettingsRegistry& Instance();


    // Registering the same key twice is rejected. Re-registering is what a hot
    // reload would do, so callers should guard their registration once.
    bool Add(SettingDef definition);
    bool Has(const std::string& key) const;
    const SettingDef* Definition(const std::string& key) const;
    const std::vector<SettingDef>& Definitions() const
    {
        return definitions_;
    }
    std::vector<std::string> Categories() const;
    std::vector<const SettingDef*> InCategory(const std::string& category) const;

    // Returns null when the key is unknown. Unknown keys loaded from disk are
    // preserved in the stored JSON/ToJson(), which lets a newer build keep a setting a
    // mod or another subsystem added.
    nlohmann::json GetJson(const std::string& key) const;

    template <typename T> T Get(const std::string& key, const T& fallback = T{}) const
    {
        const nlohmann::json value = GetJson(key);
        if (value.is_null())
        {
            return fallback;
        }
        try
        {
            return value.get<T>();
        }
        catch (...)
        {
            return fallback;
        }
    }

    // Validates and clamps the value. Returns false for an unknown key or a
    // value of the wrong JSON type.
    bool Set(const std::string& key, const nlohmann::json& value, bool notify = true);

    bool Reset(const std::string& key, bool notify = true);
    void ResetAll(bool notify = true);

    // Accepts either a bare {key: value} object, the wrapper written by
    // ToJson() ({"schema":1,"values":{...}}) or {"settings":{...}}. Unknown keys
    // are kept verbatim. Returns false only when the root is not an object.
    bool LoadJson(const nlohmann::json& json, bool notify = true);
    nlohmann::json ToJson() const;

    template <typename T, typename Owner>
    void Bind(const std::string& key, Owner* owner, T Owner::* member)
    {
        if (!owner || !Has(key))
        {
            return;
        }
        bindings_.push_back(Binding{key,
                                    [owner, member](const nlohmann::json& value)
                                    {
                                        if (value.is_null())
                                        {
                                            return;
                                        }
                                        try
                                        {
                                            owner->*member = value.get<T>();
                                        }
                                        catch (...)
                                        {
                                        }
                                    },
                                    [owner, member]()
                                    {
                                        return nlohmann::json(owner->*member);
                                    }});
    }

    void SyncFromRegistry();
    void SyncToRegistry();

    bool LoadFile(const std::string& path, bool notify = true);
    bool SaveFile(const std::string& path) const;

    std::uint64_t Revision() const
    {
        return revision_;
    }

    // Mostly useful for tests and for embedding a fresh schema in a tool.
    void Clear();

private:
    struct Binding
    {
        std::string key;
        std::function<void(const nlohmann::json&)> apply;
        std::function<nlohmann::json()> collect;
    };

    SettingsRegistry() = default;

    bool ApplyValue(const SettingDef& def, const nlohmann::json& value, nlohmann::json& out) const;
    void Notify(const SettingDef& def) const;
    void Bump();

    std::vector<SettingDef> definitions_;
    std::unordered_map<std::string, std::size_t> index_;
    nlohmann::json values_ = nlohmann::json::object();
    std::uint64_t revision_ = 1;
    std::vector<Binding> bindings_;
};

} // namespace Fem
