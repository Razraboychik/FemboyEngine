// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Replication.hpp"
#include "FemboyEngine/Boy.hpp"
#include <algorithm>

namespace Fem
{

namespace
{
// FNV-1a over "owner.name". The id has to be identical on every peer, so it
// cannot come from a local counter: two peers that register the same properties
// in a different order (level load, mods, late entities) would address different
// values with the same id and corrupt each other's state.
std::uint32_t StablePropertyKey(const std::string& owner, const std::string& name)
{
    std::uint32_t hash = 2166136261u;
    const auto mix = [&hash](const std::string& text)
    {
        for (const unsigned char c : text)
        {
            hash ^= c;
            hash *= 16777619u;
        }
    };
    mix(owner);
    hash ^= static_cast<std::uint32_t>('.');
    hash *= 16777619u;
    mix(name);
    // 0 means "no property" throughout the API.
    return hash == 0u ? 1u : hash;
}
} // namespace

Replication& Replication::Instance()
{
    static Replication r;
    return r;
}

void Replication::Reset()
{
    std::lock_guard<std::mutex> lk(mtx_);
    // Only session state is reset. RPC handlers and the change callback are
    // registered once by game code at startup; clearing them here meant hosting
    // or joining silently unregistered every RPC and dropped the handler.
    properties_.clear();
    role_ = ReplicationRole::None;
    deltaCount_ = 0;
    bytesSent_ = 0;
}

ReplicationRole Replication::Role() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return role_;
}

void Replication::SetRole(ReplicationRole r)
{
    std::lock_guard<std::mutex> lk(mtx_);
    role_ = r;
}

uint32_t Replication::RegisterProperty(const std::string& owner, const std::string& name, const nlohmann::json& initial,
                                       bool clientAuthoritative)
{
    if (owner.empty() || name.empty())
    {
        BOY_WARN("Replication", "registerProperty: empty owner or name");
        return 0;
    }

    std::lock_guard<std::mutex> lk(mtx_);

    // Re-registering the same property must not create a second id: that would
    // desynchronize client and server permanently.
    for (const auto& p : properties_)
    {
        if (p.owner == owner && p.name == name)
        {
            return p.id;
        }
    }

    const std::uint32_t id = StablePropertyKey(owner, name);

    // A collision would silently alias two properties onto one wire id, which
    // is unrecoverable at runtime. Refuse it loudly instead (and note that
    // probing would be order-dependent, i.e. exactly the bug hash ids fix).
    for (const auto& p : properties_)
    {
        if (p.id == id)
        {
            BOY_ERROR("Replication", "property id collision: " + owner + "." + name + " vs " + p.owner + "." + p.name);
            return 0;
        }
    }

    ReplicatedProperty p;
    p.id = id;
    p.owner = owner;
    p.name = name;
    p.value = initial;
    p.dirty = true;
    p.clientAuthoritative = clientAuthoritative;
    properties_.push_back(std::move(p));
    return properties_.back().id;
}

uint32_t Replication::FindProperty(const std::string& owner, const std::string& name) const
{
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& p : properties_)
    {
        if (p.owner == owner && p.name == name)
        {
            return p.id;
        }
    }
    return 0;
}

bool Replication::Property(uint32_t id, ReplicatedProperty& out) const
{
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& p : properties_)
    {
        if (p.id != id)
        {
            continue;
        }
        out = p;
        return true;
    }
    return false;
}

std::vector<ReplicatedProperty> Replication::Properties() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return properties_;
}

bool Replication::SetProperty(uint32_t id, const nlohmann::json& value)
{
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& p : properties_)
    {
        if (p.id != id)
        {
            continue;
        }
        if (p.value == value)
        {
            return true;
        }
        p.value = value;
        p.dirty = true;
        return true;
    }
    BOY_WARN("Replication", "setProperty: unknown id " + std::to_string(id));
    return false;
}

bool Replication::SetProperty(const std::string& owner, const std::string& name, const nlohmann::json& value)
{
    const uint32_t id = FindProperty(owner, name);
    if (id == 0)
    {
        BOY_WARN("Replication", "setProperty: unknown property " + owner + "." + name);
        return false;
    }
    return SetProperty(id, value);
}

bool Replication::GetProperty(uint32_t id, nlohmann::json& out) const
{
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& p : properties_)
    {
        if (p.id != id)
        {
            continue;
        }
        out = p.value;
        return true;
    }
    return false;
}

bool Replication::GetProperty(const std::string& owner, const std::string& name, nlohmann::json& out) const
{
    const uint32_t id = FindProperty(owner, name);
    if (id == 0)
    {
        return false;
    }
    return GetProperty(id, out);
}

nlohmann::json Replication::GetPropertyOr(uint32_t id, const nlohmann::json& fallback) const
{
    nlohmann::json out;
    return GetProperty(id, out) ? out : fallback;
}

void Replication::SetPropertyChangedCallback(std::function<void(uint32_t, const nlohmann::json&)> cb)
{
    std::lock_guard<std::mutex> lk(mtx_);
    changed_ = std::move(cb);
}

void Replication::MarkDirty(uint32_t id)
{
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& p : properties_)
    {
        if (p.id == id)
        {
            p.dirty = true;
            return;
        }
    }
}

void Replication::MarkAllDirty()
{
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& p : properties_)
    {
        p.dirty = true;
    }
}

nlohmann::json Replication::PeekDelta() const
{
    std::lock_guard<std::mutex> lk(mtx_);

    nlohmann::json delta;
    delta["props"] = nlohmann::json::array();

    for (const auto& p : properties_)
    {
        if (!p.dirty)
        {
            continue;
        }
        delta["props"].push_back({{"id", p.id}, {"v", p.value}});
    }

    if (delta["props"].empty())
    {
        return nlohmann::json();
    }
    return delta;
}

nlohmann::json Replication::SnapshotAll() const
{
    std::lock_guard<std::mutex> lk(mtx_);

    nlohmann::json delta;
    delta["props"] = nlohmann::json::array();
    for (const auto& p : properties_)
    {
        delta["props"].push_back({{"id", p.id}, {"v", p.value}});
    }
    return delta;
}

void Replication::CommitDelta(const nlohmann::json& delta)
{
    if (!delta.is_object() || !delta.contains("props") || !delta["props"].is_array())
    {
        return;
    }

    size_t committed = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (const auto& entry : delta["props"])
        {
            if (!entry.is_object() || !entry.contains("id") || !entry.contains("v"))
            {
                continue;
            }
            if (!entry["id"].is_number_unsigned() && !entry["id"].is_number_integer())
            {
                continue;
            }
            if (entry["id"].is_number_integer() && entry["id"].get<long long>() < 0)
            {
                continue;
            }

            const uint32_t id = entry["id"].get<uint32_t>();
            for (auto& p : properties_)
            {
                if (p.id != id)
                {
                    continue;
                }
                // The value may have changed between PeekDelta() and the send.
                // In that case the newer value is still dirty and goes out next.
                if (p.dirty && p.value == entry["v"])
                {
                    p.dirty = false;
                    ++committed;
                }
                break;
            }
        }
    }

    if (committed > 0)
    {
        ++deltaCount_;
        bytesSent_ += delta.dump().size();
    }
}

nlohmann::json Replication::BuildDelta()
{
    const nlohmann::json delta = PeekDelta();
    if (delta.is_null())
    {
        return delta;
    }
    CommitDelta(delta);
    return delta;
}

size_t Replication::ApplyDelta(const nlohmann::json& delta)
{
    if (!delta.is_object() || !delta.contains("props") || !delta["props"].is_array())
    {
        return 0;
    }

    std::vector<std::pair<uint32_t, nlohmann::json>> applied;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (const auto& entry : delta["props"])
        {
            if (!entry.is_object() || !entry.contains("id") || !entry.contains("v"))
            {
                continue;
            }
            if (!entry["id"].is_number_unsigned() && !entry["id"].is_number_integer())
            {
                continue;
            }
            // A negative integer converts to a huge uint32_t via get<uint32_t>(),
            // silently targeting an unrelated property.
            if (entry["id"].is_number_integer() && entry["id"].get<long long>() < 0)
            {
                continue;
            }

            const uint32_t id = entry["id"].get<uint32_t>();
            for (auto& p : properties_)
            {
                if (p.id != id)
                {
                    continue;
                }
                if (p.value != entry["v"])
                {
                    p.value = entry["v"];
                    p.dirty = false;
                    applied.push_back({id, p.value});
                }
                break;
            }
        }
    }

    // Callbacks run outside the lock: a handler may touch replication itself.
    std::function<void(uint32_t, const nlohmann::json&)> cb;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cb = changed_;
    }
    if (cb)
    {
        for (const auto& [id, v] : applied)
        {
            cb(id, v);
        }
    }
    return applied.size();
}

void Replication::RegisterRpc(const std::string& name,
                              std::function<void(uint32_t senderPeer, const nlohmann::json& args)> fn)
{
    if (name.empty() || !fn)
    {
        BOY_WARN("Replication", "registerRPC: empty name or handler");
        return;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    rpcs_[name] = std::move(fn);
}

bool Replication::HasRpc(const std::string& name) const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return rpcs_.find(name) != rpcs_.end();
}

bool Replication::DispatchRpc(const std::string& name, uint32_t senderPeer, const nlohmann::json& args)
{
    std::function<void(uint32_t, const nlohmann::json&)> fn;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = rpcs_.find(name);
        if (it == rpcs_.end())
        {
            return false;
        }
        fn = it->second;
    }

    try
    {
        fn(senderPeer, args);
    }
    catch (const std::exception& e)
    {
        BOY_ERROR("Replication", std::string("RPC '") + name + "' threw: " + e.what());
        return false;
    }
    catch (...)
    {
        BOY_ERROR("Replication", "RPC '" + name + "' threw");
        return false;
    }
    return true;
}

size_t Replication::PropertyCount() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return properties_.size();
}

size_t Replication::DirtyCount() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    size_t n = 0;
    for (const auto& p : properties_)
    {
        if (p.dirty)
        {
            ++n;
        }
    }
    return n;
}

} // namespace Fem
