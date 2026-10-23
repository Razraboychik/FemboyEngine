// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "FemboyEngine/Core.hpp"
#ifdef FEMBOY_HAS_ENET
#include <enet/enet.h>
#endif

namespace Fem
{

// Token bucket used by the receive path. It is public so a project can share
// the same limiter between a socket loop and a test, and so the editor can show
// exactly how many packets were dropped.
class PacketRateLimiter
{
public:
    // packetsPerSecond <= 0 disables the limiter. `burst` is the maximum number
    // of packets that may arrive back-to-back after an idle period.
    void Configure(double packetsPerSecond, double burst = 0.0);
    bool Enabled() const
    {
        return packetsPerSecond_ > 0.0;
    }
    double PacketsPerSecond() const
    {
        return packetsPerSecond_;
    }
    bool Allow();
    void Reset();

private:
    double packetsPerSecond_ = 0.0;
    double burst_ = 0.0;
    double tokens_ = 0.0;
    std::chrono::steady_clock::time_point last_{};
    bool primed_ = false;
};

// A snapshot of the socket's counters. RTT is the mean over connected peers and
// is 0 when no ENet peer exists. All byte counts are payload bytes (the
// transport's own headers are not included).
struct NetworkStats
{
    bool initialized = false;
    bool server = false;
    bool connected = false;
    bool connecting = false;
    std::size_t peers = 0;
    std::uint64_t packetsSent = 0;
    std::uint64_t packetsReceived = 0;
    std::uint64_t bytesSent = 0;
    std::uint64_t bytesReceived = 0;
    std::uint64_t packetsDropped = 0;
    float rttMs = 0.f;
    std::string lastError;
};

// Networking module: client-server over UDP (ENet).
//
// A Network instance is either a server (hosts many peers) or a client (one
// peer). Messages are framed as "channel|json".
class Network
{
public:
    // ENet channel 0 carries the existing textual/json protocol. Channel 1 is
    // reserved for binary payloads (currently Opus voice) so a large JSON
    // message can never head-of-line block a 20 ms voice frame.
    static constexpr std::uint8_t kJsonChannel = 0;
    static constexpr std::uint8_t kVoiceChannel = 1;
    static constexpr std::size_t kChannelCount = 2;

    static Network& Instance();
    bool Initialize();
    void Shutdown();

    // Server
    bool StartServer(unsigned short port, size_t maxClients = 32);
    void StopServer();
    bool IsServer() const
    {
        return isServer_;
    }

    // Client
    bool Connect(const std::string& host, unsigned short port);
    // Starts the handshake without blocking. Poll() advances it; watch
    // isConnected()/isConnecting() (a failed handshake clears both).
    bool ConnectAsync(const std::string& host, unsigned short port);
    bool IsConnecting() const
    {
        return connecting_;
    }
    void Disconnect();
    bool IsConnected() const
    {
        return connected_;
    }

    // Send/receive. Packet-level functions return false when the payload was
    // not handed to at least one peer (no connection, unknown peer, oversized).
    bool Send(const std::string& channel, const nlohmann::json& data, bool reliable = true);
    // Sends to one peer only. peerId is the stable session id returned by
    // peerIds()/the receive callback, not the raw ENet index (which reuses 0 for
    // the first client). Returns false when the peer is unknown.
    bool SendTo(uint32_t peerId, const std::string& channel, const nlohmann::json& data, bool reliable = true);
    bool Broadcast(const std::string& channel, const nlohmann::json& data, bool reliable = true);

    // Binary path. Voice packets are unreliable by default: a late frame is
    // worth less than the frame already waiting in the jitter buffer.
    bool SendBinaryTo(uint32_t peerId, std::uint8_t channel, const void* data, std::size_t size, bool reliable = false);
    // On a client this sends to the server; on a server it sends to every peer
    // except `exceptPeerId` (0 = nobody excluded). `channel` must be in
    // [0, kChannelCount).
    bool BroadcastBinary(std::uint8_t channel, const void* data, std::size_t size, bool reliable = false,
                         uint32_t exceptPeerId = 0);
    void Poll(); // call each frame

    void SetReceiveCallback(std::function<void(const std::string&, const nlohmann::json&, uint32_t peerId)> cb)
    {
        recvCb_ = std::move(cb);
    }

    // Receives raw packets from every channel except kJsonChannel. The pointer
    // is valid only for the duration of the call.
    void SetBinaryReceiveCallback(
        std::function<void(uint32_t peerId, std::uint8_t channel, const std::uint8_t* data, std::size_t size)> cb)
    {
        binRecvCb_ = std::move(cb);
    }

    // Fired when a peer disconnects (server side) or the connection drops
    // (client side, peerId 0). Keeps hosting code from polling peerCount().
    void SetPeerDisconnectedCallback(std::function<void(uint32_t peerId)> cb)
    {
        discCb_ = std::move(cb);
    }

    // Fired when a server-side handshake completes, before any game message can
    // arrive. Hosting uses it to arm the idle timeout for silent clients.
    void SetPeerConnectedCallback(std::function<void(uint32_t peerId)> cb)
    {
        connCb_ = std::move(cb);
    }

    // Peer management
    size_t PeerCount() const
    {
        return peers_.size();
    }
    // Ids of currently connected peers (server side).
    std::vector<uint32_t> PeerIds() const;
    // Drops every peer: used when a server shuts down or a client is kicked.
    void DisconnectAll();
    // Disconnects one peer by session id (server side). Used for idle timeouts.
    void Kick(uint32_t peerId);
    void SetLatencySimulation(int ms)
    {
        latencySim_ = ms;
    }
    int LatencySimulation() const
    {
        return latencySim_;
    }

    // Server-side flood guard. It is deliberately opt-in: a project that sends
    // large tooling snapshots should raise the budget, while a competitive game
    // can set e.g. 240 packets/s with a 480-packet burst. Dropped incoming
    // packets are counted in stats().
    void SetReceiveRateLimit(double packetsPerSecond, double burst = 0.0)
    {
        receiveLimiter_.Configure(packetsPerSecond, burst);
    }
    const PacketRateLimiter& ReceiveRateLimiter() const
    {
        return receiveLimiter_;
    }

    NetworkStats GetStats() const;
    void ResetStats();
    const std::string& LastError() const
    {
        return lastError_;
    }

private:
    void NoteSent(std::size_t bytes);
    void NoteReceived(std::size_t bytes);
    void SetError(const std::string& error);

    Network() = default;
    // Shared setup for Connect()/ConnectAsync(): creates the client host and
    // starts the ENet handshake.
    bool BeginConnect(const std::string& host, unsigned short port);

    bool isServer_ = false;
    bool connected_ = false;
    bool connecting_ = false;
    bool initialized_ = false;
    int latencySim_ = 0;
    PacketRateLimiter receiveLimiter_;
    std::uint64_t packetsSent_ = 0;
    std::uint64_t packetsReceived_ = 0;
    std::uint64_t bytesSent_ = 0;
    std::uint64_t bytesReceived_ = 0;
    std::uint64_t packetsDropped_ = 0;
    std::string lastError_;
    std::function<void(const std::string&, const nlohmann::json&, uint32_t)> recvCb_;
    std::function<void(uint32_t, std::uint8_t, const std::uint8_t*, std::size_t)> binRecvCb_;
    std::function<void(uint32_t)> discCb_;
    std::function<void(uint32_t)> connCb_;
#ifdef FEMBOY_HAS_ENET
    ENetHost* host_ = nullptr;
    ENetPeer* pendingPeer_ = nullptr;
    std::vector<ENetPeer*> peers_;
    // ENet assigns incomingPeerID per host: a server's first client gets 0,
    // which is exactly the id the host uses for "the server". Session ids are
    // therefore allocated separately, starting at 1.
    std::unordered_map<ENetPeer*, uint32_t> peerIds_;
    uint32_t nextPeerId_ = 1;
    uint32_t PeerIdOf(ENetPeer* peer) const;
#endif
};

} // namespace Fem
