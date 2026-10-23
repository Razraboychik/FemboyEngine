// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/Networking.hpp"

namespace Fem
{

// A player as seen by the host.
struct NetPlayer
{
    uint32_t peerId = 0;
    std::string name;
    bool ready = false;
    // Server-assigned, stable for the lifetime of the session.
    int playerIndex = -1;
};

// Out-of-the-box server hosting.
//
// GameHost wraps Network + Replication so a game only needs:
//     GameHost::Instance().Host(7777);
// or
//     GameHost::Instance().Join("127.0.0.1", 7777);
// and then calls Tick(dt) once per frame. Everything else (player list,
// replication deltas, RPC routing, late-join snapshots) is handled here.
class GameHost
{
public:
    static GameHost& Instance();

    // Hosting
    // Starts a listen server. Returns false when the port is taken.
    bool Host(unsigned short port, size_t maxPlayers = 16, const std::string& serverName = "FemboyServer");
    void StopHost();
    bool IsHosting() const
    {
        return hosting_;
    }

    // Joining
    // Connects to a server. Blocks for at most a few seconds while handshaking.
    bool Join(const std::string& address, unsigned short port, const std::string& playerName = "Player");
    // Non-blocking join: returns true once the handshake has started. Poll
    // Tick() as usual; IsJoining() clears when the connection is established or
    // fails. The hello/player-list exchange happens automatically.
    bool JoinAsync(const std::string& address, unsigned short port, const std::string& playerName = "Player");
    bool IsJoining() const
    {
        return joining_;
    }
    void Leave();
    bool IsJoined() const
    {
        return joined_;
    }

    // Reconnection
    // A client that loses the link retries with exponential backoff. The
    // policy is off by default so a single-player build never opens a socket
    // behind the player's back; opt in after the first successful join.
    struct ReconnectPolicy
    {
        bool enabled = false;
        float initialDelay = 1.f;
        float maxDelay = 30.f;
        float backoffMultiplier = 2.f;
        // 0 = retry forever.
        int maxAttempts = 0;
    };
    void SetReconnectPolicy(const ReconnectPolicy& policy);
    const ReconnectPolicy& GetReconnectPolicy() const
    {
        return reconnectPolicy_;
    }
    bool IsReconnecting() const
    {
        return reconnecting_;
    }
    int ReconnectAttempts() const
    {
        return reconnectAttempts_;
    }
    float ReconnectDelayRemaining() const
    {
        return reconnectTimer_;
    }
    NetworkStats GetNetworkStats() const
    {
        return Network::Instance().GetStats();
    }

    // Frame
    // Polls the socket, sends replication deltas at a fixed rate and processes
    // timeouts. Safe to call when neither hosting nor joined (does nothing).
    void Tick(float dt);

    // Replication
    // Sends one delta immediately instead of waiting for the next tick.
    void FlushReplication();
    // Forces a full state send to every client (used when a player joins).
    void SendFullState();

    // RPC
    // Sends an RPC to the server (client) or to every client (server).
    void Rpc(const std::string& name, const nlohmann::json& args = nullptr);
    // Sends an RPC to a single peer (server only).
    bool RpcTo(uint32_t peerId, const std::string& name, const nlohmann::json& args = nullptr);

    // Players
    const std::vector<NetPlayer>& Players() const
    {
        return players_;
    }
    const NetPlayer* Player(uint32_t peerId) const;
    const NetPlayer* LocalPlayer() const;
    size_t PlayerCount() const
    {
        return players_.size();
    }
    // Session id of the local player (0 when hosting or not connected).
    uint32_t LocalPeerId() const
    {
        return localPeerId_;
    }

    // Lobby readiness
    // A client sends its ready flag to the host; the host updates the player
    // entry and broadcasts the full list to every client. A listen-server host
    // is part of the same list, so allPlayersReady() also waits for it.
    bool SetReady(bool ready);
    bool IsReady() const
    {
        const NetPlayer* self = LocalPlayer();
        return self != nullptr && self->ready;
    }
    bool AllPlayersReady() const;
    size_t ReadyCount() const;

    // Fired when a player joins, leaves or changes readiness (server side, and
    // for the local player list on clients).
    void SetPlayerListChangedCallback(std::function<void()> cb);

    const std::string& ServerName() const
    {
        return serverName_;
    }
    float TickRate() const
    {
        return tickRate_;
    }
    void SetTickRate(float hz);
    float LatencyMs() const
    {
        return latencyMs_;
    }

    // Snapshot of the last replication delta size, for the profiler.
    size_t LastDeltaBytes() const
    {
        return lastDeltaBytes_;
    }

private:
    GameHost() = default;

    void OnPacket(const std::string& channel, const nlohmann::json& data, uint32_t peerId);
    void OnPeerDisconnected(uint32_t peerId);
    void HandleHello(uint32_t peerId, const nlohmann::json& data);
    void HandlePlayerList(const nlohmann::json& data);
    void HandleReady(uint32_t peerId, const nlohmann::json& data);
    void HandleState(uint32_t peerId, const nlohmann::json& data);
    nlohmann::json PlayerListJson() const;
    void SendPlayerListTo(uint32_t peerId);
    void BroadcastPlayerList();
    // Sends a full property snapshot to one peer without disturbing the dirty
    // flags that other clients still need.
    void SendFullStateTo(uint32_t peerId);

    void InstallCallbacks();
    void ScheduleReconnect();
    void TickReconnect(float dt);

    bool hosting_ = false;
    bool joined_ = false;
    bool joining_ = false;
    bool reconnecting_ = false;
    bool callbackInstalled_ = false;
    float sinceReplication_ = 0.f;
    float tickRate_ = 20.f;
    float latencyMs_ = 0.f;
    float reconnectTimer_ = 0.f;
    int reconnectAttempts_ = 0;
    ReconnectPolicy reconnectPolicy_{};
    size_t lastDeltaBytes_ = 0;
    unsigned short port_ = 0;
    std::string serverName_ = "FemboyServer";
    std::string localName_ = "Player";
    std::string serverAddress_;

    std::vector<NetPlayer> players_;
    std::map<uint32_t, float> lastSeen_;
    std::function<void()> playerListChanged_;
    int nextPlayerIndex_ = 0;
    uint32_t localPeerId_ = 0;
};

} // namespace Fem
