// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Prefab.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/ClassDefaults.hpp"
#include "FemboyEngine/Mirror.hpp"
#include "FemboyEngine/Vanity.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace Fem
{
namespace
{
bool IsMetaKey(const std::string& key)
{
    return key == "name" || key == "description" || key == "parent" || key == "extends" || key == "overrides" ||
           key == "children" || key == "version" || key == "guid";
}

// Objects merge recursively; arrays and scalars replace. A prefab that changes
// an animation list or a socket array intends the new list, not a concatenation.
void DeepMerge(nlohmann::json& target, const nlohmann::json& patch)
{
    if (!patch.is_object() || !target.is_object())
    {
        target = patch;
        return;
    }
    for (const auto& [key, value] : patch.items())
    {
        if (target.contains(key) && target[key].is_object() && value.is_object())
        {
            DeepMerge(target[key], value);
        }
        else
        {
            target[key] = value;
        }
    }
}

void MergeComponentFields(nlohmann::json& out, const nlohmann::json& data)
{
    if (!data.is_object())
    {
        return;
    }
    for (const auto& [key, value] : data.items())
    {
        if (IsMetaKey(key))
        {
            continue;
        }
        DeepMerge(out[key], value);
    }
}

std::string ParentNameOf(const nlohmann::json& data)
{
    for (const char* key : {"parent", "extends"})
    {
        if (!data.contains(key))
        {
            continue;
        }
        const auto& value = data[key];
        if (value.is_string())
        {
            return value.get<std::string>();
        }
        if (value.is_object())
        {
            if (value.contains("prefab") && value["prefab"].is_string())
            {
                return value["prefab"].get<std::string>();
            }
            if (value.contains("name") && value["name"].is_string())
            {
                return value["name"].get<std::string>();
            }
        }
    }
    return {};
}

nlohmann::json ParentOverridesOf(const nlohmann::json& data)
{
    for (const char* key : {"parent", "extends"})
    {
        if (!data.contains(key) || !data[key].is_object())
        {
            continue;
        }
        if (data[key].contains("overrides") && data[key]["overrides"].is_object())
        {
            return data[key]["overrides"];
        }
    }
    return nlohmann::json::object();
}

struct ResolveContext
{
    std::vector<std::string>* errors = nullptr;
    std::set<std::string> stack;
    int depth = 0;
};

void ResolveInto(const Prefab& prefab, nlohmann::json& out, ResolveContext& context)
{
    constexpr int kMaxDepth = 32;
    if (context.depth++ > kMaxDepth)
    {
        if (context.errors)
        {
            context.errors->push_back("prefab nesting exceeds " + std::to_string(kMaxDepth) + " levels");
        }
        return;
    }

    const std::string ownName = prefab.Name();
    if (context.stack.count(ownName) > 0)
    {
        if (context.errors)
        {
            context.errors->push_back("prefab parent cycle at '" + ownName + "'");
        }
        return;
    }
    context.stack.insert(ownName);

    const std::string parentName = prefab.ParentName();
    if (!parentName.empty())
    {
        Prefab* parent = PrefabLibrary::Instance().Get(parentName);
        if (!parent)
        {
            if (context.errors)
            {
                context.errors->push_back("prefab '" + ownName + "' inherits missing '" + parentName + "'");
            }
        }
        else
        {
            ResolveInto(*parent, out, context);
        }
        DeepMerge(out, ParentOverridesOf(prefab.Data()));
    }

    MergeComponentFields(out, prefab.Data());
    if (prefab.Data().contains("overrides") && prefab.Data()["overrides"].is_object())
    {
        DeepMerge(out, prefab.Data()["overrides"]);
    }

    context.stack.erase(ownName);
    --context.depth;
}

void ApplyResolved(const nlohmann::json& resolved, Registry& reg, Entity e)
{
    for (const auto& comp : Mirror::Instance().Components())
    {
        if (!resolved.contains(comp.jsonKey))
        {
            continue;
        }
        const auto& value = resolved[comp.jsonKey];
        if (!comp.scalar && !value.is_object())
        {
            continue;
        }
        ClassDefaults::Instance().ApplyTo(reg, e, comp, value);
    }
}

void SpawnChildren(const Prefab& prefab, Registry& reg, Entity parent, std::vector<std::string>* errors)
{
    if (!prefab.Data().contains("children") || !prefab.Data()["children"].is_array())
    {
        return;
    }

    sf::Vector2f parentPosition{0.f, 0.f};
    if (const TransformComponent* transform = reg.try_get<TransformComponent>(parent))
    {
        parentPosition = {transform->x, transform->y};
    }

    for (const auto& child : prefab.Data()["children"])
    {
        std::string childName;
        nlohmann::json overrides = nlohmann::json::object();
        sf::Vector2f offset{0.f, 0.f};

        if (child.is_string())
        {
            childName = child.get<std::string>();
        }
        else if (child.is_object())
        {
            if (child.contains("prefab") && child["prefab"].is_string())
            {
                childName = child["prefab"].get<std::string>();
            }
            else if (child.contains("name") && child["name"].is_string())
            {
                childName = child["name"].get<std::string>();
            }

            // Explicit fields on the child entry are overrides on the referenced
            // prefab. They are merged before the child's own `overrides`, so the
            // latter wins.
            for (const auto& [key, value] : child.items())
            {
                if (key == "prefab" || key == "name" || key == "offset" || key == "position" || key == "overrides")
                {
                    continue;
                }
                DeepMerge(overrides[key], value);
            }
            if (child.contains("overrides") && child["overrides"].is_object())
            {
                DeepMerge(overrides, child["overrides"]);
            }

            const auto readOffset = [&offset](const nlohmann::json& value)
            {
                if (!value.is_object())
                {
                    return;
                }
                if (value.contains("x") && value["x"].is_number())
                {
                    offset.x = value["x"].get<float>();
                }
                if (value.contains("y") && value["y"].is_number())
                {
                    offset.y = value["y"].get<float>();
                }
            };
            if (child.contains("offset"))
            {
                readOffset(child["offset"]);
            }
            if (child.contains("position"))
            {
                readOffset(child["position"]);
            }
        }

        if (childName.empty())
        {
            if (errors)
            {
                errors->push_back("prefab child has no prefab name");
            }
            continue;
        }

        Entity childEntity = PrefabLibrary::Instance().Instantiate(childName, reg, overrides);
        if (childEntity == entt::null)
        {
            if (errors)
            {
                errors->push_back("prefab child references missing prefab '" + childName + "'");
            }
            continue;
        }

        // A child prefab is authored in local space: its own transform is the
        // local offset from the parent, and the child entry's offset is added on
        // top. Applying the parent position only when the offset was non-zero
        // made a child at (0, 0) stay at the world origin.
        TransformComponent* transform = reg.try_get<TransformComponent>(childEntity);
        if (!transform)
        {
            transform = &reg.emplace<TransformComponent>(childEntity);
            transform->x = parentPosition.x + offset.x;
            transform->y = parentPosition.y + offset.y;
        }
        else
        {
            transform->x += parentPosition.x + offset.x;
            transform->y += parentPosition.y + offset.y;
        }
    }
}
} // namespace

bool Prefab::Load(const std::string& path)
{
    auto parsed = Vanity::ReadJson(path);
    if (!parsed)
    {
        BOY_ERROR("Prefab", parsed.Error());
        return false;
    }
    if (!parsed.Value().is_object())
    {
        BOY_ERROR("Prefab", "root must be an object: " + path);
        return false;
    }

    data_ = std::move(parsed.Value());
    if (data_.contains("name") && data_["name"].is_string())
    {
        name_ = data_["name"].get<std::string>();
    }
    else
    {
        name_ = std::filesystem::path(path).stem().string();
    }
    BOY_INFO("Prefab", "loaded: " + path);
    return true;
}

bool Prefab::Save(const std::string& path) const
{
    // Atomic replace, so an interrupted write cannot corrupt the prefab.
    auto written = Vanity::WriteJson(path, data_);
    if (!written)
    {
        BOY_ERROR("Prefab", "save failed: " + written.Error());
        return false;
    }
    BOY_INFO("Prefab", "saved: " + path);
    return true;
}

nlohmann::json Prefab::ResolvedData(std::vector<std::string>* errors) const
{
    nlohmann::json out = nlohmann::json::object();
    ResolveContext context;
    context.errors = errors;
    ResolveInto(*this, out, context);
    return out;
}

bool Prefab::HasParent() const
{
    return !ParentNameOf(data_).empty();
}

std::string Prefab::ParentName() const
{
    return ParentNameOf(data_);
}

Entity Prefab::Instantiate(Registry& reg) const
{
    return Instantiate(reg, nlohmann::json::object());
}

Entity Prefab::Instantiate(Registry& reg, const nlohmann::json& overrides) const
{
    std::vector<std::string> errors;
    nlohmann::json resolved = ResolvedData(&errors);
    if (overrides.is_object())
    {
        DeepMerge(resolved, overrides);
    }

    for (const std::string& error : errors)
    {
        BOY_WARN("Prefab", error);
    }

    const Entity e = reg.create();
    ApplyResolved(resolved, reg, e);
    SpawnChildren(*this, reg, e, &errors);
    for (const std::string& error : errors)
    {
        BOY_WARN("Prefab", error);
    }
    return e;
}

void Prefab::ApplyTo(Registry& reg, Entity e) const
{
    std::vector<std::string> errors;
    const nlohmann::json resolved = ResolvedData(&errors);
    for (const std::string& error : errors)
    {
        BOY_WARN("Prefab", error);
    }
    if (reg.valid(e))
    {
        ApplyResolved(resolved, reg, e);
    }
}

void Prefab::CaptureFrom(Registry& reg, Entity e)
{
    data_ = nlohmann::json::object();
    data_["name"] = name_;

    if (!reg.valid(e))
    {
        return;
    }

    for (const auto& comp : Mirror::Instance().Components())
    {
        if (comp.has(reg, e))
        {
            comp.capture(reg, e, data_);
        }
    }
}

PrefabLibrary& PrefabLibrary::Instance()
{
    static PrefabLibrary p;
    return p;
}

Entity PrefabLibrary::Instantiate(const std::string& name, Registry& reg, const nlohmann::json& overrides) const
{
    auto it = prefabs_.find(name);
    if (it == prefabs_.end())
    {
        BOY_WARN("Prefab", "instantiate: unknown prefab '" + name + "'");
        return entt::null;
    }
    return it->second.Instantiate(reg, overrides);
}

void PrefabLibrary::LoadFromDir(const std::string& dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec)
    {
        return;
    }

    // Directory iteration order is filesystem-specific. Sorting makes a spawn
    // that looks up by name deterministic across machines and runs.
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (ec)
        {
            break;
        }
        if (entry.is_regular_file() && entry.path().extension() == ".prefab")
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    for (const fs::path& path : files)
    {
        Prefab p;
        if (p.Load(path.string()))
        {
            Add(path.stem().string(), p);
        }
    }
}

Prefab* PrefabLibrary::Get(const std::string& name)
{
    auto it = prefabs_.find(name);
    if (it == prefabs_.end())
    {
        return nullptr;
    }
    return &it->second;
}

void PrefabLibrary::Add(const std::string& name, const Prefab& p)
{
    prefabs_[name] = p;
    if (std::find(names_.begin(), names_.end(), name) == names_.end())
    {
        names_.push_back(name);
    }
}

void PrefabLibrary::Clear()
{
    prefabs_.clear();
    names_.clear();
}

} // namespace Fem
