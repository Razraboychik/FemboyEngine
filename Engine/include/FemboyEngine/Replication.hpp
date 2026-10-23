// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include "FemboyEngine/Core.hpp"

namespace Fem
{

// Lowest-level replication layer.
//
// Properties are plain JSON values registered on both sides in the same order,
// so a 32-bit handle is enough to identify them on the wire. The server owns the
// authoritative values and sends dirty-only deltas; clients apply them and fire
// a change callback. RPCs travel on their own channel and are dispatched by
// name, which keeps the layer usable without generated code.
//
// This class knows nothing about sockets: GameServer/GameClient below wire it to
// Network, and tests can drive buildDelta()/applyDelta() directly.

enum class ReplicationRole
{
    None,
    Server,
    Client
};

struct ReplicatedProperty
{
    uint32_t id = 0;
    std::string owner;
    std::string name;
    nlohmann::json value = nullptr;
    bool dirty = true;
    // Client-authoritative properties may be written by their owning client.
    bool clientAuthoritative = false;
};

class Replication
{
public:
    static Replication& Instance();

    // Clears properties, RPCs and the role. Call before loading a new level.
    void Reset();

    ReplicationRole Role() const;
    void SetRole(ReplicationRole r);

    // The id is derived from owner + name (stable FNV-1a hash), so peers agree
    // on it without registering in the same order. Returns 0 on an empty name or
    // a hash collision.
    uint32_t RegisterProperty(const std::string& owner, const std::string& name,
                              const nlohmann::json& initial = nullptr, bool clientAuthoritative = false);

    uint32_t FindProperty(const std::string& owner, const std::string& name) const;
    // Copies the property description. Returning a pointer into the internal
    // vector would dangle as soon as another property is registered.
    bool Property(uint32_t id, ReplicatedProperty& out) const;
    std::vector<ReplicatedProperty> Properties() const;

    // Writes a value locally. Only the authority (server, or the owning client
    // for a client-authoritative property) should call this.
    bool SetProperty(uint32_t id, const nlohmann::json& value);
    bool SetProperty(const std::string& owner, const std::string& name, const nlohmann::json& value);

    // Copies the current value into `out`. Returns false for unknown ids.
    bool GetProperty(uint32_t id, nlohmann::json& out) const;
    bool GetProperty(const std::string& owner, const std::string& name, nlohmann::json& out) const;
    // Convenience read that never fails.
    nlohmann::json GetPropertyOr(uint32_t id, const nlohmann::json& fallback = nullptr) const;

    // Fired after a value arrives from the network (never for local writes).
    void SetPropertyChangedCallback(std::function<void(uint32_t, const nlohmann::json&)> cb);

    // Marks a property as needing replication (used after mutating a value in
    // place through getProperty()).
    void MarkDirty(uint32_t id);
    void MarkAllDirty();

    // Serializes the dirty properties without touching their flags. Use this
    // together with CommitDelta() when a send can fail: clearing dirty state
    // before the packet reaches a peer loses the update forever.
    nlohmann::json PeekDelta() const;
    // Serializes every property (late-join snapshot). Does not change flags.
    nlohmann::json SnapshotAll() const;
    // Clears the dirty flag of every property whose value still matches the
    // delta. Call after a successful send (and only then).
    void CommitDelta(const nlohmann::json& delta);

    // Serializes and clears the dirty flags (peek + commit in one call).
    nlohmann::json BuildDelta();
    // Client side: applies a delta. Returns the number of applied properties.
    size_t ApplyDelta(const nlohmann::json& delta);

    // RPCs. A handler receives the sender peer id (0 on a listen server) and the
    // argument object.
    void RegisterRpc(const std::string& name, std::function<void(uint32_t senderPeer, const nlohmann::json& args)> fn);
    bool HasRpc(const std::string& name) const;
    // Dispatches an RPC locally. Returns false when no handler is registered.
    bool DispatchRpc(const std::string& name, uint32_t senderPeer, const nlohmann::json& args);

    // Statistics for the profiler.
    size_t PropertyCount() const;
    size_t DirtyCount() const;
    uint64_t DeltaCount() const
    {
        return deltaCount_;
    }
    uint64_t BytesSent() const
    {
        return bytesSent_;
    }

private:
    Replication() = default;

    mutable std::mutex mtx_;
    ReplicationRole role_ = ReplicationRole::None;
    std::vector<ReplicatedProperty> properties_;
    std::map<std::string, std::function<void(uint32_t, const nlohmann::json&)>> rpcs_;
    std::function<void(uint32_t, const nlohmann::json&)> changed_;
    uint64_t deltaCount_ = 0;
    uint64_t bytesSent_ = 0;
};

} // namespace Fem
