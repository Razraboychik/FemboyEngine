// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "FemboyEngine/ECS.hpp"
#include "FemboyEngine/Tags.hpp"

namespace Fem
{

// Mirror: the engine's reflection registry.
//
// Components used to be marshalled by hand in three places (Scene, Prefab,
// the editor inspector) and every new component had to be added to all of
// them. Mirror describes a component once - its JSON key and its fields - and
// the same description drives:
//   * Scene save/load,
//   * Prefab capture/instantiate,
//   * the editor inspector,
//   * replication property registration.
//
// A field is stored as two type-erased callables, so any member type the
// engine cares about (arithmetic, bool, std::string, sf::Color) works without
// the JSON layer knowing about it.
// Field type, so a generic UI (the editor inspector) can pick a widget without
// knowing the component it belongs to.
enum class FieldKind
{
    Float,
    Int,
    Bool,
    String,
    Color,
    // A tag set. Added at the end so the existing kinds keep their values: they
    // are written into editor layouts and, through them, into saved projects.
    Tags,
    // Tooling-only kinds. The storage type is still the member type (an int for
    // an enum, a string for an asset id), so scene and prefab JSON stay exactly
    // what they were; the kind only tells a generic property editor which
    // widget to draw.
    Enum,
    Asset
};

// How a reflected property may be edited. This is the data-side analogue of
// UE's EditAnywhere / EditDefaultsOnly / VisibleAnywhere specifiers.
enum class EditFlags
{
    EditAnywhere,
    EditDefaultsOnly,
    VisibleAnywhere,
    NoEdit
};

struct EnumEntry
{
    std::string name;
    std::int64_t value = 0;
};

// Fluent descriptors for a reflected field. The defaults reproduce the old
// AddField(name, member) behaviour, so existing registrations keep working.
struct FieldOptions
{
    std::string displayName;
    std::string category;
    std::string tooltip;
    std::string assetType; // "texture", "font", "sound", "prefab", ...
    EditFlags edit = EditFlags::EditAnywhere;
    bool advanced = false;
    bool multiline = false;
    bool flattened = false;
    std::optional<double> min;
    std::optional<double> max;
    std::optional<double> step;
    std::vector<EnumEntry> enumValues;
    nlohmann::json defaultValue = nullptr;

    FieldOptions& Display(std::string value)
    {
        displayName = std::move(value);
        return *this;
    }
    FieldOptions& Category(std::string value)
    {
        category = std::move(value);
        return *this;
    }
    FieldOptions& Tooltip(std::string value)
    {
        tooltip = std::move(value);
        return *this;
    }
    FieldOptions& Asset(std::string type)
    {
        assetType = std::move(type);
        return *this;
    }
    FieldOptions& Edit(EditFlags value)
    {
        edit = value;
        return *this;
    }
    FieldOptions& DefaultsOnly()
    {
        edit = EditFlags::EditDefaultsOnly;
        return *this;
    }
    FieldOptions& ReadOnly()
    {
        edit = EditFlags::VisibleAnywhere;
        return *this;
    }
    FieldOptions& Advanced(bool value = true)
    {
        advanced = value;
        return *this;
    }
    FieldOptions& Multiline(bool value = true)
    {
        multiline = value;
        return *this;
    }
    FieldOptions& Flattened(bool value = true)
    {
        flattened = value;
        return *this;
    }
    FieldOptions& Range(double lo, double hi, double increment = 0.0)
    {
        min = lo;
        max = hi;
        if (increment > 0.0)
        {
            step = increment;
        }
        return *this;
    }
    FieldOptions& Enum(std::vector<EnumEntry> values)
    {
        enumValues = std::move(values);
        return *this;
    }
    FieldOptions& Default(nlohmann::json value)
    {
        defaultValue = std::move(value);
        return *this;
    }
};

struct Field
{
    std::string name;
    // A flattened field merges its object into the component object instead of
    // nesting under `name`. sf::Color uses this to keep the historic flat
    // r/g/b/a layout that existing scene and prefab files already use.
    bool flattened = false;
    FieldKind kind = FieldKind::Float;
    std::string displayName;
    std::string category;
    std::string tooltip;
    std::string assetType;
    EditFlags edit = EditFlags::EditAnywhere;
    bool advanced = false;
    bool multiline = false;
    std::optional<double> min;
    std::optional<double> max;
    std::optional<double> step;
    std::vector<EnumEntry> enumValues;
    // Captured from a default-constructed component at registration time, unless
    // the caller supplied an explicit default.
    nlohmann::json defaultValue = nullptr;
    std::function<nlohmann::json(const void* object)> ToJson;
    std::function<void(void* object, const nlohmann::json& value)> FromJson;

    std::string Label() const
    {
        return displayName.empty() ? name : displayName;
    }
};

namespace Detail
{
template <typename> inline constexpr bool alwaysFalse = false;

template <typename M> nlohmann::json FieldToJson(const M& value)
{
    if constexpr (std::is_same_v<M, std::string>)
    {
        return value;
    }
    else if constexpr (std::is_same_v<M, sf::Color>)
    {
        return {{"r", value.r}, {"g", value.g}, {"b", value.b}, {"a", value.a}};
    }
    else if constexpr (std::is_same_v<M, TagContainer>)
    {
        return value.ToJson();
    }
    else if constexpr (std::is_same_v<M, GameplayTags>)
    {
        return value.tags.ToJson();
    }
    else if constexpr (std::is_same_v<M, bool>)
    {
        return value;
    }
    else if constexpr (std::is_integral_v<M>)
    {
        return static_cast<std::int64_t>(value);
    }
    else if constexpr (std::is_floating_point_v<M>)
    {
        return static_cast<double>(value);
    }
    else
    {
        static_assert(alwaysFalse<M>, "Mirror: unsupported field type");
    }
}

template <typename M> void FieldFromJson(const nlohmann::json& in, M& value)
{
    if constexpr (std::is_same_v<M, std::string>)
    {
        if (in.is_string())
        {
            value = in.get<std::string>();
        }
    }
    else if constexpr (std::is_same_v<M, sf::Color>)
    {
        // Channel access matches the old hand-written loader: missing or
        // malformed channels fall back to 255 (opaque white) instead of
        // wrapping around through a static_cast.
        if (!in.is_object())
        {
            return;
        }
        const auto channel = [&in](const char* key)
        {
            if (!in.contains(key) || !in[key].is_number())
            {
                return static_cast<std::uint8_t>(255);
            }
            const double d = in[key].get<double>();
            if (!std::isfinite(d))
            {
                return static_cast<std::uint8_t>(255);
            }
            return static_cast<std::uint8_t>(std::clamp(d, 0.0, 255.0));
        };
        value = sf::Color(channel("r"), channel("g"), channel("b"), channel("a"));
    }
    else if constexpr (std::is_same_v<M, TagContainer>)
    {
        // Names, through the container's own JSON, so a scene file stays
        // readable and a rename keeps resolving through the registry's aliases.
        value.FromJson(in);
    }
    else if constexpr (std::is_same_v<M, GameplayTags>)
    {
        value.tags.FromJson(in);
    }
    else if constexpr (std::is_same_v<M, bool>)
    {
        if (in.is_boolean())
        {
            value = in.get<bool>();
        }
        else if (in.is_number())
        {
            value = in.get<double>() != 0.0;
        }
    }
    else if constexpr (std::is_integral_v<M>)
    {
        if (!in.is_number())
        {
            return;
        }
        const double d = in.get<double>();
        if (!std::isfinite(d))
        {
            return;
        }
        value = static_cast<M>(std::clamp(d, static_cast<double>(std::numeric_limits<M>::lowest()),
                                          static_cast<double>(std::numeric_limits<M>::max())));
    }
    else if constexpr (std::is_floating_point_v<M>)
    {
        if (!in.is_number())
        {
            return;
        }
        const double d = in.get<double>();
        if (std::isfinite(d))
        {
            value = static_cast<M>(d);
        }
    }
    else
    {
        static_assert(alwaysFalse<M>, "Mirror: unsupported field type");
    }
}
} // namespace Detail

// Collects fields for a component type while it is being registered.
class FieldList
{
public:
    template <typename C, typename M> void Add(const char* name, M C::* member, bool flattened = false)
    {
        FieldOptions options;
        options.flattened = flattened;
        Add(name, member, std::move(options));
    }

    template <typename C, typename M> void Add(const char* name, M C::* member, FieldOptions options)
    {
        Field f;
        f.name = name;
        f.flattened = options.flattened;
        f.displayName = std::move(options.displayName);
        f.category = std::move(options.category);
        f.tooltip = std::move(options.tooltip);
        f.assetType = std::move(options.assetType);
        f.edit = options.edit;
        f.advanced = options.advanced;
        f.multiline = options.multiline;
        f.min = options.min;
        f.max = options.max;
        f.step = options.step;
        f.enumValues = std::move(options.enumValues);
        f.defaultValue = std::move(options.defaultValue);

        if constexpr (std::is_same_v<M, std::string>)
        {
            f.kind = FieldKind::String;
        }
        else if constexpr (std::is_same_v<M, sf::Color>)
        {
            f.kind = FieldKind::Color;
        }
        else if constexpr (std::is_same_v<M, TagContainer> || std::is_same_v<M, GameplayTags>)
        {
            f.kind = FieldKind::Tags;
        }
        else if constexpr (std::is_same_v<M, bool>)
        {
            f.kind = FieldKind::Bool;
        }
        else if constexpr (std::is_integral_v<M>)
        {
            f.kind = FieldKind::Int;
        }
        else
        {
            f.kind = FieldKind::Float;
        }

        // The editor-only kinds are storage-compatible: an enum is an integer
        // and an asset reference is a string id. Overriding the widget kind here
        // keeps the marshalling lambdas untouched.
        if (!f.assetType.empty() && std::is_same_v<M, std::string>)
        {
            f.kind = FieldKind::Asset;
        }
        else if (!f.enumValues.empty() && std::is_integral_v<M>)
        {
            f.kind = FieldKind::Enum;
        }

        f.ToJson = [member](const void* object) { return Detail::FieldToJson(static_cast<const C*>(object)->*member); };
        f.FromJson = [member](void* object, const nlohmann::json& value)
        { Detail::FieldFromJson(value, static_cast<C*>(object)->*member); };
        fields_.push_back(std::move(f));
    }

    std::vector<Field> Take()
    {
        return std::move(fields_);
    }

private:
    std::vector<Field> fields_;
};

class Mirror;

// Fluent registration: mirror.component<T>("Name", "jsonKey").AddField(...).Done().
template <typename T> class ComponentBuilder
{
public:
    ComponentBuilder(Mirror& mirror, std::string name, std::string jsonKey, bool scalar)
        : mirror_(mirror), name_(std::move(name)), jsonKey_(std::move(jsonKey)), scalar_(scalar)
    {
    }

    template <typename M> ComponentBuilder& AddField(const char* name, M T::* member, bool flattened = false)
    {
        fields_.Add(name, member, flattened);
        return *this;
    }

    template <typename M> ComponentBuilder& AddField(const char* name, M T::* member, FieldOptions options)
    {
        fields_.Add(name, member, std::move(options));
        return *this;
    }

    void Done();

private:
    Mirror& mirror_;
    std::string name_;
    std::string jsonKey_;
    bool scalar_;
    FieldList fields_;
};

// One reflected component type.
struct ComponentReflection
{
    std::string name;    // "TransformComponent"
    std::string jsonKey; // "transform"
    bool scalar = false; // true: the component marshals as a bare value, not an object

    std::function<bool(const Registry&, Entity)> has;
    // Raw component pointers for generic consumers (inspector, replication).
    std::function<void*(Registry&, Entity)> getMutable;
    std::function<const void*(const Registry&, Entity)> getConst;
    // Writes out[jsonKey]; leaves the rest of the object untouched.
    std::function<void(const Registry&, Entity, nlohmann::json&)> capture;
    // Reads the component's JSON value (already extracted from the entity object).
    std::function<void(Registry&, Entity, const nlohmann::json&)> apply;
    // Removes the component (used by the editor's component menu).
    std::function<void(Registry&, Entity)> remove;

    std::shared_ptr<const std::vector<Field>> fields;
};

class Mirror
{
public:
    static Mirror& Instance();

    template <typename T>
    ComponentBuilder<T> Component(const std::string& name, const std::string& jsonKey, bool scalar = false)
    {
        return ComponentBuilder<T>(*this, name, jsonKey, scalar);
    }

    void Add(ComponentReflection component);

    // Lookup by JSON key ("transform") or by type name ("TransformComponent").
    const ComponentReflection* Find(const std::string& nameOrKey) const;

    const std::vector<ComponentReflection>& Components() const
    {
        return components_;
    }

    void Clear();

private:
    Mirror();
    std::vector<ComponentReflection> components_;
};

template <typename T> void ComponentBuilder<T>::Done()
{
    std::vector<Field> taken = fields_.Take();
    // Every field gets a baseline value even when the registration did not spell
    // one out. ClassDefaults and the inspector's "Reset to default" then have a
    // real value to fall back to instead of guessing from a widget's zero.
    if constexpr (std::is_default_constructible_v<T>)
    {
        T defaults{};
        for (Field& field : taken)
        {
            if (field.defaultValue.is_null())
            {
                field.defaultValue = field.ToJson(&defaults);
            }
        }
    }
    auto fields = std::make_shared<std::vector<Field>>(std::move(taken));

    ComponentReflection c;
    c.name = name_;
    c.jsonKey = jsonKey_;
    c.scalar = scalar_;
    c.fields = fields;

    c.has = [](const Registry& reg, Entity e) { return reg.all_of<T>(e); };
    c.getMutable = [](Registry& reg, Entity e) -> void*
    { return reg.all_of<T>(e) ? static_cast<void*>(&reg.get<T>(e)) : nullptr; };
    c.getConst = [](const Registry& reg, Entity e) -> const void*
    { return reg.all_of<T>(e) ? static_cast<const void*>(&reg.get<T>(e)) : nullptr; };

    const std::string key = jsonKey_;
    c.capture = [key, fields, scalar = scalar_](const Registry& reg, Entity e, nlohmann::json& out)
    {
        const T& object = reg.get<T>(e);
        if (scalar && !fields->empty())
        {
            out[key] = fields->front().ToJson(&object);
            return;
        }
        nlohmann::json value = nlohmann::json::object();
        for (const auto& f : *fields)
        {
            nlohmann::json fieldValue = f.ToJson(&object);
            if (f.flattened && fieldValue.is_object())
            {
                for (const auto& [k, v] : fieldValue.items())
                {
                    value[k] = v;
                }
            }
            else
            {
                value[f.name] = std::move(fieldValue);
            }
        }
        out[key] = std::move(value);
    };

    c.apply = [fields, scalar = scalar_](Registry& reg, Entity e, const nlohmann::json& in)
    {
        T& object = reg.all_of<T>(e) ? reg.get<T>(e) : reg.emplace<T>(e);
        if (scalar)
        {
            if (!fields->empty())
            {
                fields->front().FromJson(&object, in);
            }
            return;
        }
        if (!in.is_object())
        {
            return;
        }
        for (const auto& f : *fields)
        {
            if (f.flattened)
            {
                f.FromJson(&object, in);
            }
            else if (in.contains(f.name))
            {
                f.FromJson(&object, in[f.name]);
            }
        }
    };

    c.remove = [](Registry& reg, Entity e) { reg.remove<T>(e); };

    mirror_.Add(std::move(c));
}

} // namespace Fem
