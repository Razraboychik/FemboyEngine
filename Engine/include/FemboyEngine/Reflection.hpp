// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

// Reflection-lite: a small runtime type registry for enums, structs, member
// properties and function signatures.
//
// It is intentionally descriptive, not magical:
//   * FEMENUM / FEMENUM_VALUE describe an enum and its values;
//   * FEMSTRUCT / FEMSTRUCT_FIELD describe a struct, its fields and defaults;
//   * FEMPROPERTY / FEMFUNCTION describe members of a class.
//
// Registration happens through static objects, so a type becomes visible to the
// visual-script node picker, FemboyScript and FemInstancedStruct without any
// generated code. The macros are meant to be used at namespace scope in a .cpp
// file; a header would register the same type once per translation unit (the
// registries deduplicate, but the object still costs a little space).

#include <cstddef>
#include <cstdint>
#include <cctype>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace Fem
{

struct FemStructType;
class FemStructRegistry;

// Lightweight tooling metadata, in the spirit of UE's meta specifiers.
//
// It is deliberately a plain value stored in the registries: nothing in a hot
// path reads it, and FSValue never carries a copy. Tools (the editor, the
// visual-script picker, a future serializer) query it on demand.
//
//   FEMPROPERTY(Actor, int, Health,
//               meta = (category="Gameplay", display="Health",
//                       description="Current hit points", min=0, max=100, units=hp))
//
// Unknown keys are preserved, so a project can add its own without an engine
// change.
struct FemMeta
{
    std::string category;
    std::string displayName;
    std::string description;
    std::map<std::string, std::string> values;

    bool Empty() const
    {
        return category.empty() && displayName.empty() && description.empty() && values.empty();
    }

    bool Has(const std::string& key) const
    {
        return values.find(key) != values.end();
    }

    const std::string& Get(const std::string& key) const
    {
        static const std::string empty;
        const auto it = values.find(key);
        return it == values.end() ? empty : it->second;
    }

    std::optional<double> Number(const std::string& key) const
    {
        const auto it = values.find(key);
        if (it == values.end())
        {
            return std::nullopt;
        }
        try
        {
            std::size_t used = 0;
            const double value = std::stod(it->second, &used);
            if (used == 0)
            {
                return std::nullopt;
            }
            return value;
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }
    }

    std::optional<double> Min() const
    {
        return Number("min");
    }

    std::optional<double> Max() const
    {
        return Number("max");
    }

    bool AllowsNumber(double value) const
    {
        if (const auto min = Min(); min && value < *min)
        {
            return false;
        }
        if (const auto max = Max(); max && value > *max)
        {
            return false;
        }
        return true;
    }

    double ClampNumber(double value) const
    {
        if (const auto min = Min(); min && value < *min)
        {
            value = *min;
        }
        if (const auto max = Max(); max && value > *max)
        {
            value = *max;
        }
        return value;
    }
};

inline FemMeta MakeMeta(std::string category = {}, std::string displayName = {}, std::string description = {},
                        std::map<std::string, std::string> values = {})
{
    return FemMeta{std::move(category), std::move(displayName), std::move(description), std::move(values)};
}

namespace FemReflect
{
inline std::string MetaTrim(std::string_view text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
    {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

inline std::string MetaLower(std::string text)
{
    for (char& ch : text)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

inline std::string MetaUnquote(std::string text)
{
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') || (text.front() == '\'' && text.back() == '\'')))
    {
        text = text.substr(1, text.size() - 2);
    }
    // A quoted tooltip may contain an escaped quote; keep the text readable.
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\\' && i + 1 < text.size())
        {
            result.push_back(text[++i]);
        }
        else
        {
            result.push_back(text[i]);
        }
    }
    return result;
}

// Splits "category = \"Stats\", min = 0, tooltip = \"a, b\"" on top-level
// commas only. Braces, brackets and quotes protect a comma.
inline std::vector<std::string> MetaSplit(const std::string& text)
{
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;
    char quote = '\0';
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];
        if (quote != '\0')
        {
            current.push_back(ch);
            if (ch == '\\' && i + 1 < text.size())
            {
                current.push_back(text[++i]);
            }
            else if (ch == quote)
            {
                quote = '\0';
            }
            continue;
        }
        if (ch == '"' || ch == '\'')
        {
            quote = ch;
            current.push_back(ch);
            continue;
        }
        if (ch == '(' || ch == '[' || ch == '{')
        {
            ++depth;
            current.push_back(ch);
            continue;
        }
        if (ch == ')' || ch == ']' || ch == '}')
        {
            --depth;
            current.push_back(ch);
            continue;
        }
        if (ch == ',' && depth == 0)
        {
            parts.push_back(MetaTrim(current));
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    const std::string tail = MetaTrim(current);
    if (!tail.empty())
    {
        parts.push_back(tail);
    }
    return parts;
}

inline std::size_t MetaFindEquals(const std::string& text)
{
    int depth = 0;
    char quote = '\0';
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];
        if (quote != '\0')
        {
            if (ch == '\\')
            {
                ++i;
            }
            else if (ch == quote)
            {
                quote = '\0';
            }
            continue;
        }
        if (ch == '"' || ch == '\'')
        {
            quote = ch;
        }
        else if (ch == '(' || ch == '[' || ch == '{')
        {
            ++depth;
        }
        else if (ch == ')' || ch == ']' || ch == '}')
        {
            --depth;
        }
        else if (ch == '=' && depth == 0)
        {
            return i;
        }
    }
    return std::string::npos;
}
} // namespace FemReflect

// Parses the textual `meta = (...)` argument of the registration macros.
// Accepted forms:
//   meta = (category="Stats", display="Health", min=0, max=100, units=hp)
//   (Category = "Stats", ClampMin = 0, ClampMax = 100)
//   category = "Stats", tooltip = "Current hit points"
//
// `category`, `display`/`displayname`/`name`, `description`/`tooltip` fill the
// named fields. `min`/`clampmin` and `max`/`clampmax` are normalized, every
// other key is preserved verbatim (lower-cased). A bare flag becomes "true".
inline FemMeta FemMetaFromSpec(const char* specification)
{
    using namespace FemReflect;

    std::string text = MetaTrim(specification ? specification : "");
    if (text.empty())
    {
        return FemMeta{};
    }

    // Drop an optional leading "meta" and "=".
    const std::string lower = MetaLower(text);
    if (lower.rfind("meta", 0) == 0)
    {
        text = MetaTrim(std::string_view(text).substr(4));
        if (!text.empty() && text.front() == '=')
        {
            text = MetaTrim(std::string_view(text).substr(1));
        }
    }
    if (text.size() >= 2 && text.front() == '(' && text.back() == ')')
    {
        text = text.substr(1, text.size() - 2);
    }

    FemMeta meta;
    for (const std::string& part : MetaSplit(text))
    {
        if (part.empty())
        {
            continue;
        }
        const std::size_t equals = MetaFindEquals(part);
        std::string key;
        std::string value;
        if (equals == std::string::npos)
        {
            key = MetaLower(MetaTrim(part));
            value = "true";
        }
        else
        {
            key = MetaLower(MetaTrim(part.substr(0, equals)));
            value = MetaUnquote(MetaTrim(part.substr(equals + 1)));
        }
        if (key.empty())
        {
            continue;
        }

        if (key == "category")
        {
            meta.category = value;
        }
        else if (key == "display" || key == "displayname" || key == "name" || key == "title")
        {
            meta.displayName = value;
        }
        else if (key == "description" || key == "tooltip" || key == "desc" || key == "comment")
        {
            meta.description = value;
        }
        else
        {
            if (key == "clampmin" || key == "uimin")
            {
                key = "min";
            }
            else if (key == "clampmax" || key == "uimax")
            {
                key = "max";
            }
            meta.values[key] = value;
        }
    }
    return meta;
}

namespace FemReflect
{
template <typename T> struct AlwaysFalse : std::false_type
{
};

template <typename T> struct IsVector : std::false_type
{
};
template <typename T, typename A> struct IsVector<std::vector<T, A>> : std::true_type
{
    using value_type = T;
};

template <typename T> struct IsMap : std::false_type
{
};
template <typename K, typename V, typename C, typename A> struct IsMap<std::map<K, V, C, A>> : std::true_type
{
    using key_type = K;
    using mapped_type = V;
};

// A short, stable name for field metadata. The name is informational: the
// serialiser never parses it, it only shows it to tools.
template <typename T> std::string TypeName()
{
    if constexpr (std::is_same_v<T, std::string>)
    {
        return "string";
    }
    else if constexpr (std::is_same_v<T, bool>)
    {
        return "bool";
    }
    else if constexpr (std::is_integral_v<T>)
    {
        return "int";
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        return "float";
    }
    else if constexpr (std::is_enum_v<T>)
    {
        return "enum";
    }
    else
    {
        return typeid(T).name();
    }
}

template <typename T> nlohmann::json ToJson(const T& value);
template <typename T> bool FromJson(const nlohmann::json& json, T& value);

// Defined below, after FemStructRegistry: serialises any registered struct by
// type_index, which is what the generic ToJson/FromJson fallbacks call.
nlohmann::json StructToJson(const std::type_index& type, const void* object);
bool StructFromJson(const std::type_index& type, void* object, const nlohmann::json& json);
} // namespace FemReflect

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

struct FemEnumEntry
{
    std::string name;
    std::int64_t value = 0;
    FemMeta meta;
};

struct FemEnumType
{
    std::string name;
    std::vector<FemEnumEntry> values;
    FemMeta meta;

    const FemEnumEntry* FindName(const std::string& valueName) const
    {
        for (const auto& entry : values)
        {
            if (entry.name == valueName)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    const FemEnumEntry* FindValue(std::int64_t value) const
    {
        for (const auto& entry : values)
        {
            if (entry.value == value)
            {
                return &entry;
            }
        }
        return nullptr;
    }
};

class FemEnumRegistry
{
public:
    static FemEnumRegistry& Instance()
    {
        static FemEnumRegistry registry;
        return registry;
    }

    void Add(FemEnumType type)
    {
        if (!Find(type.name))
        {
            types_.push_back(std::move(type));
        }
    }

    void AddValue(const std::string& typeName, FemEnumEntry entry)
    {
        FemEnumType* type = FindMutable(typeName);
        if (!type)
        {
            types_.push_back(FemEnumType{typeName, {}});
            type = &types_.back();
        }
        if (!type->FindName(entry.name))
        {
            type->values.push_back(std::move(entry));
        }
    }

    const FemEnumType* Find(const std::string& name) const
    {
        for (const auto& type : types_)
        {
            if (type.name == name)
            {
                return &type;
            }
        }
        return nullptr;
    }

    const std::vector<FemEnumType>& All() const
    {
        return types_;
    }

    void Clear()
    {
        types_.clear();
    }

private:
    FemEnumType* FindMutable(const std::string& name)
    {
        for (auto& type : types_)
        {
            if (type.name == name)
            {
                return &type;
            }
        }
        return nullptr;
    }

    std::vector<FemEnumType> types_;
};

struct FemEnumRegistrar
{
    explicit FemEnumRegistrar(const char* name, FemMeta meta = {})
    {
        FemEnumRegistry::Instance().Add(FemEnumType{name ? name : "", {}, std::move(meta)});
    }
};

struct FemEnumValueRegistrar
{
    FemEnumValueRegistrar(const char* typeName, const char* valueName, std::int64_t value, FemMeta meta = {})
    {
        FemEnumRegistry::Instance().AddValue(typeName ? typeName : "",
                                             FemEnumEntry{valueName ? valueName : "", value, std::move(meta)});
    }
};

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------

struct FemStructField
{
    std::string name;
    std::string type;
    nlohmann::json defaultJson;
    std::function<nlohmann::json(const void* object)> get;
    std::function<bool(void* object, const nlohmann::json& value)> set;
    FemMeta meta;
};

struct FemStructType
{
    std::string name;
    std::type_index index = std::type_index(typeid(void));
    std::size_t size = 0;
    std::size_t alignment = 1;
    std::vector<FemStructField> fields;
    std::function<void*()> create;
    std::function<void(void*)> destroy;
    std::function<void*(const void*)> clone;
    FemMeta meta;

    const FemStructField* FindField(const std::string& fieldName) const
    {
        for (const auto& field : fields)
        {
            if (field.name == fieldName)
            {
                return &field;
            }
        }
        return nullptr;
    }
};

class FemStructRegistry
{
public:
    static FemStructRegistry& Instance()
    {
        static FemStructRegistry registry;
        return registry;
    }

    void Add(FemStructType type)
    {
        const std::string name = type.name;
        const std::type_index index = type.index;
        auto it = byName_.find(name);
        if (it == byName_.end())
        {
            byType_[index] = name;
            byName_.emplace(name, std::move(type));
            return;
        }
        // A duplicate registration (for example the same header included in two
        // translation units) must not wipe fields that are already known.
        byType_[index] = name;
    }

    void AddField(const std::string& typeName, FemStructField field)
    {
        auto it = byName_.find(typeName);
        if (it == byName_.end())
        {
            FemStructType type;
            type.name = typeName;
            byName_.emplace(typeName, std::move(type));
            it = byName_.find(typeName);
        }
        for (auto& existing : it->second.fields)
        {
            if (existing.name == field.name)
            {
                existing = std::move(field);
                return;
            }
        }
        it->second.fields.push_back(std::move(field));
    }

    const FemStructType* Find(const std::string& name) const
    {
        auto it = byName_.find(name);
        return it == byName_.end() ? nullptr : &it->second;
    }

    const FemStructType* Find(const std::type_index& index) const
    {
        auto it = byType_.find(index);
        return it == byType_.end() ? nullptr : Find(it->second);
    }

    std::vector<const FemStructType*> All() const
    {
        std::vector<const FemStructType*> result;
        result.reserve(byName_.size());
        for (const auto& [name, type] : byName_)
        {
            result.push_back(&type);
        }
        return result;
    }

    std::size_t Size() const
    {
        return byName_.size();
    }

    // Creates a value and applies every field's declared default. Returns null
    // when the type is unknown or was not registered with FEMSTRUCT.
    void* CreateDefault(const std::string& name) const
    {
        const FemStructType* type = Find(name);
        if (!type || !type->create)
        {
            return nullptr;
        }
        void* object = type->create();
        if (!object)
        {
            return nullptr;
        }
        for (const auto& field : type->fields)
        {
            if (!field.defaultJson.is_null() && field.set)
            {
                field.set(object, field.defaultJson);
            }
        }
        return object;
    }

    nlohmann::json FieldsToJson(const FemStructType& type, const void* object) const
    {
        nlohmann::json fields = nlohmann::json::object();
        for (const auto& field : type.fields)
        {
            if (field.get)
            {
                fields[field.name] = field.get(object);
            }
        }
        return fields;
    }

    bool FieldsFromJson(const FemStructType& type, void* object, const nlohmann::json& fields) const
    {
        if (!fields.is_object())
        {
            return false;
        }
        bool ok = true;
        for (const auto& field : type.fields)
        {
            auto it = fields.find(field.name);
            if (it != fields.end() && field.set)
            {
                ok = field.set(object, *it) && ok;
            }
        }
        return ok;
    }

    nlohmann::json ToJson(const FemStructType& type, const void* object) const
    {
        return {{"$type", "struct"}, {"name", type.name}, {"fields", FieldsToJson(type, object)}};
    }

    bool FromJson(const FemStructType& type, void* object, const nlohmann::json& json) const
    {
        if (!json.is_object())
        {
            return false;
        }
        if (json.contains("fields"))
        {
            return FieldsFromJson(type, object, json["fields"]);
        }
        return FieldsFromJson(type, object, json);
    }

    void Clear()
    {
        byName_.clear();
        byType_.clear();
    }

private:
    std::map<std::string, FemStructType> byName_;
    std::map<std::type_index, std::string> byType_;
};

template <typename T> struct FemStructRegistrar
{
    explicit FemStructRegistrar(const char* name, FemMeta meta = {})
    {
        FemStructType type;
        type.name = name ? name : "";
        type.index = std::type_index(typeid(T));
        type.size = sizeof(T);
        type.alignment = alignof(T);
        type.meta = std::move(meta);
        type.create = []() -> void* { return new T{}; };
        type.destroy = [](void* object) { delete static_cast<T*>(object); };
        type.clone = [](const void* object) -> void*
        { return new T(*static_cast<const T*>(object)); };
        FemStructRegistry::Instance().Add(std::move(type));
    }
};

template <typename S, typename F> struct FemStructFieldRegistrar
{
    FemStructFieldRegistrar(const char* typeName, const char* fieldName, F S::*member, const F& defaultValue,
                            FemMeta meta = {})
    {
        FemStructField field;
        field.name = fieldName ? fieldName : "";
        field.type = FemReflect::TypeName<F>();
        field.defaultJson = FemReflect::ToJson(defaultValue);
        field.meta = std::move(meta);
        field.get = [member](const void* object) -> nlohmann::json
        { return FemReflect::ToJson(static_cast<const S*>(object)->*member); };
        field.set = [member](void* object, const nlohmann::json& value) -> bool
        {
            F parsed{};
            if (!FemReflect::FromJson(value, parsed))
            {
                return false;
            }
            static_cast<S*>(object)->*member = std::move(parsed);
            return true;
        };
        FemStructRegistry::Instance().AddField(typeName ? typeName : "", std::move(field));
    }
};

// ---------------------------------------------------------------------------
// Properties and functions
// ---------------------------------------------------------------------------

struct FemProperty
{
    std::string className;
    std::string type;
    std::string name;
    std::function<nlohmann::json(const void* object)> get;
    std::function<bool(void* object, const nlohmann::json& value)> set;
    FemMeta meta;
};

class FemPropertyRegistry
{
public:
    static FemPropertyRegistry& Instance()
    {
        static FemPropertyRegistry registry;
        return registry;
    }

    void Add(FemProperty property)
    {
        for (auto& existing : properties_)
        {
            if (existing.className == property.className && existing.name == property.name)
            {
                existing = std::move(property);
                return;
            }
        }
        properties_.push_back(std::move(property));
    }

    const FemProperty* Find(const std::string& className, const std::string& name) const
    {
        for (const auto& property : properties_)
        {
            if (property.className == className && property.name == name)
            {
                return &property;
            }
        }
        return nullptr;
    }

    const std::vector<FemProperty>& All() const
    {
        return properties_;
    }

    void Clear()
    {
        properties_.clear();
    }

private:
    std::vector<FemProperty> properties_;
};

template <typename C, typename M> struct FemPropertyRegistrar
{
    FemPropertyRegistrar(const char* className, const char* typeName, const char* name, M C::*member,
                         FemMeta meta = {})
    {
        FemProperty property;
        property.className = className ? className : "";
        property.type = typeName ? typeName : FemReflect::TypeName<M>();
        property.name = name ? name : "";
        property.meta = std::move(meta);
        property.get = [member](const void* object) -> nlohmann::json
        { return FemReflect::ToJson(static_cast<const C*>(object)->*member); };
        property.set = [member](void* object, const nlohmann::json& value) -> bool
        {
            M parsed{};
            if (!FemReflect::FromJson(value, parsed))
            {
                return false;
            }
            static_cast<C*>(object)->*member = std::move(parsed);
            return true;
        };
        FemPropertyRegistry::Instance().Add(std::move(property));
    }
};

struct FemFunction
{
    std::string className;
    std::string returnType;
    std::string name;
    std::string arguments;
    FemMeta meta;
};

class FemFunctionRegistry
{
public:
    static FemFunctionRegistry& Instance()
    {
        static FemFunctionRegistry registry;
        return registry;
    }

    void Add(FemFunction function)
    {
        for (auto& existing : functions_)
        {
            if (existing.className == function.className && existing.name == function.name &&
                existing.arguments == function.arguments)
            {
                existing = std::move(function);
                return;
            }
        }
        functions_.push_back(std::move(function));
    }

    const FemFunction* Find(const std::string& className, const std::string& name) const
    {
        for (const auto& function : functions_)
        {
            if (function.className == className && function.name == name)
            {
                return &function;
            }
        }
        return nullptr;
    }

    const std::vector<FemFunction>& All() const
    {
        return functions_;
    }

    void Clear()
    {
        functions_.clear();
    }

private:
    std::vector<FemFunction> functions_;
};

struct FemFunctionRegistrar
{
    FemFunctionRegistrar(const char* className, const char* returnType, const char* name, FemMeta meta = {})
    {
        std::string arguments = meta.Get("args");
        if (arguments.empty())
        {
            arguments = meta.Get("parameters");
        }
        FemFunctionRegistry::Instance().Add(FemFunction{className ? className : "", returnType ? returnType : "",
                                                         name ? name : "", std::move(arguments), std::move(meta)});
    }
};

// ---------------------------------------------------------------------------
// Generic value <-> JSON
// ---------------------------------------------------------------------------

namespace FemReflect
{
template <typename T> nlohmann::json ToJson(const T& value)
{
    using Value = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<Value, std::string>)
    {
        return value;
    }
    else if constexpr (std::is_same_v<Value, bool>)
    {
        return value;
    }
    else if constexpr (std::is_enum_v<Value>)
    {
        return static_cast<std::int64_t>(value);
    }
    else if constexpr (std::is_integral_v<Value>)
    {
        return static_cast<std::int64_t>(value);
    }
    else if constexpr (std::is_floating_point_v<Value>)
    {
        return static_cast<double>(value);
    }
    else if constexpr (IsVector<Value>::value)
    {
        nlohmann::json array = nlohmann::json::array();
        for (const auto& item : value)
        {
            array.push_back(ToJson(item));
        }
        return array;
    }
    else if constexpr (IsMap<Value>::value)
    {
        nlohmann::json object = nlohmann::json::object();
        for (const auto& [key, item] : value)
        {
            if constexpr (std::is_same_v<typename IsMap<Value>::key_type, std::string>)
            {
                object[key] = ToJson(item);
            }
            else
            {
                object[std::to_string(key)] = ToJson(item);
            }
        }
        return object;
    }
    else
    {
        return StructToJson(std::type_index(typeid(Value)), &value);
    }
}

template <typename T> bool FromJson(const nlohmann::json& json, T& value)
{
    using Value = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<Value, std::string>)
    {
        if (!json.is_string())
        {
            return false;
        }
        value = json.get<std::string>();
        return true;
    }
    else if constexpr (std::is_same_v<Value, bool>)
    {
        if (json.is_boolean())
        {
            value = json.get<bool>();
            return true;
        }
        if (json.is_number())
        {
            value = json.get<double>() != 0.0;
            return true;
        }
        return false;
    }
    else if constexpr (std::is_enum_v<Value>)
    {
        if (!json.is_number())
        {
            return false;
        }
        value = static_cast<Value>(json.get<std::int64_t>());
        return true;
    }
    else if constexpr (std::is_integral_v<Value>)
    {
        if (!json.is_number())
        {
            return false;
        }
        value = static_cast<Value>(json.get<std::int64_t>());
        return true;
    }
    else if constexpr (std::is_floating_point_v<Value>)
    {
        if (!json.is_number())
        {
            return false;
        }
        value = static_cast<Value>(json.get<double>());
        return true;
    }
    else if constexpr (IsVector<Value>::value)
    {
        if (!json.is_array())
        {
            return false;
        }
        Value result;
        result.reserve(json.size());
        for (const auto& item : json)
        {
            typename IsVector<Value>::value_type element{};
            if (!FromJson(item, element))
            {
                return false;
            }
            result.push_back(std::move(element));
        }
        value = std::move(result);
        return true;
    }
    else if constexpr (IsMap<Value>::value)
    {
        if (!json.is_object())
        {
            return false;
        }
        Value result;
        for (const auto& [key, item] : json.items())
        {
            typename IsMap<Value>::mapped_type element{};
            if (!FromJson(item, element))
            {
                return false;
            }
            using Key = typename IsMap<Value>::key_type;
            if constexpr (std::is_same_v<Key, std::string>)
            {
                result.emplace(key, std::move(element));
            }
            else if constexpr (std::is_integral_v<Key>)
            {
                result.emplace(static_cast<Key>(std::stoll(key)), std::move(element));
            }
            else
            {
                return false;
            }
        }
        value = std::move(result);
        return true;
    }
    else
    {
        return StructFromJson(std::type_index(typeid(Value)), &value, json);
    }
}

inline nlohmann::json StructToJson(const std::type_index& type, const void* object)
{
    const FemStructType* info = FemStructRegistry::Instance().Find(type);
    if (!info || !object)
    {
        return nullptr;
    }
    return FemStructRegistry::Instance().ToJson(*info, object);
}

inline bool StructFromJson(const std::type_index& type, void* object, const nlohmann::json& json)
{
    const FemStructType* info = FemStructRegistry::Instance().Find(type);
    if (!info || !object)
    {
        return false;
    }
    return FemStructRegistry::Instance().FromJson(*info, object, json);
}
} // namespace FemReflect

} // namespace Fem

// ---------------------------------------------------------------------------
// Registration macros
// ---------------------------------------------------------------------------
//
// Use them once, at namespace scope, in a .cpp file:
//
//   enum class EState { Idle, Running };
//   FEMENUM(EState, meta = (category="Gameplay", display="State"));
//   FEMENUM_VALUE(EState, Idle, EState::Idle, meta = ());
//   FEMENUM_VALUE(EState, Running, EState::Running, meta = (display="Running"));
//
//   struct FStats { int health = 100; std::string name = "player"; };
//   FEMSTRUCT(FStats, meta = (category="Gameplay", display="Stats"));
//   FEMSTRUCT_FIELD(FStats, health, 100,
//                   meta = (category="Stats", display="Health", min=0, max=100, units=hp));
//   FEMSTRUCT_FIELD(FStats, name, std::string("player"), meta = ());
//
// The default value is both a fallback used by FemInstancedStruct::SetDefault
// and the value written into JSON metadata.
//
// Every FEM_* macro has an explicit `meta` parameter, written in UE style as
// `meta = (...)`. The preprocessor turns the whole token sequence into a string
// literal and FemMetaFromSpec() parses it once during static registration;
// metadata never enters values or per-frame code. Use `meta = ()` for none.

#define FEMBOY_META_ARG(meta) ::Fem::FemMetaFromSpec(#meta)

// Unique internal names, so the same registration macro can be used in more
// than one translation unit and with namespaced type names.
#define FEMBOY_DETAIL_CONCAT_(Left, Right) Left##Right
#define FEMBOY_DETAIL_CONCAT(Left, Right) FEMBOY_DETAIL_CONCAT_(Left, Right)
#define FEMBOY_REGISTRAR(Prefix) FEMBOY_DETAIL_CONCAT(Prefix, __COUNTER__)

#define FEMENUM(EnumName, meta)                                                                                        \
    [[maybe_unused]] static const ::Fem::FemEnumRegistrar FEMBOY_REGISTRAR(fem_enum_registrar_)(                    \
        #EnumName, FEMBOY_META_ARG(meta))

#define FEMENUM_VALUE(EnumName, ValueName, Value, meta)                                                                \
    [[maybe_unused]] static const ::Fem::FemEnumValueRegistrar FEMBOY_REGISTRAR(fem_enum_value_registrar_)(         \
        #EnumName, #ValueName, static_cast<std::int64_t>(Value), FEMBOY_META_ARG(meta))

#define FEMSTRUCT(StructName, meta)                                                                                    \
    [[maybe_unused]] static const ::Fem::FemStructRegistrar<StructName> FEMBOY_REGISTRAR(fem_struct_registrar_)(    \
        #StructName, FEMBOY_META_ARG(meta))

#define FEMSTRUCT_FIELD(StructName, Name, Default, meta)                                                               \
    [[maybe_unused]] static const ::Fem::FemStructFieldRegistrar<                                                   \
        StructName, decltype(std::declval<StructName&>().Name)>                                                        \
        FEMBOY_REGISTRAR(fem_struct_field_registrar_)(#StructName, #Name, &StructName::Name, Default,                  \
                                                     FEMBOY_META_ARG(meta))

#define FEMPROPERTY(Class, Type, Name, meta)                                                                           \
    [[maybe_unused]] static const ::Fem::FemPropertyRegistrar<                                                      \
        Class, decltype(std::declval<Class&>().Name)>                                                                  \
        FEMBOY_REGISTRAR(fem_property_registrar_)(#Class, #Type, #Name, &Class::Name, FEMBOY_META_ARG(meta))

#define FEMFUNCTION(Class, ReturnType, Name, meta)                                                                     \
    [[maybe_unused]] static const ::Fem::FemFunctionRegistrar FEMBOY_REGISTRAR(fem_function_registrar_)(           \
        #Class, #ReturnType, #Name, FEMBOY_META_ARG(meta))
