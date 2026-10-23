// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Networking.hpp"
#include "FemboyEngine/Boy.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace Fem
{

namespace
{
// Maximum size of one framed message. ENet fragments larger packets, but a
// 16 MB JSON blob is a bug in the caller, not a valid payload.
constexpr size_t kMaxMessageSize = 16u * 1024u * 1024u;
// Voice frames are tiny. A 64 KiB ceiling leaves room for future binary
// payloads while rejecting an accidental blob that would fragment badly.
constexpr size_t kMaxBinarySize = 64u * 1024u;
} // namespace

void PacketRateLimiter::Configure(double packetsPerSecond, double burst)
{
    packetsPerSecond_ = std::isfinite(packetsPerSecond) && packetsPerSecond > 0.0 ? packetsPerSecond : 0.0;
    // A limiter without an explicit burst still needs to admit at least one
    // packet; using one second's budget is the least surprising default.
    burst_ = burst > 0.0 && std::isfinite(burst) ? burst : (std::max)(1.0, packetsPerSecond_);
    tokens_ = burst_;
    primed_ = false;
    last_ = {};
}

bool PacketRateLimiter::Allow()
{
    if (!Enabled())
    {
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!primed_)
    {
        primed_ = true;
        last_ = now;
        tokens_ = burst_;
    }
    else
    {
        const double elapsed = std::chrono::duration<double>(now - last_).count();
        tokens_ = (std::min)(burst_, tokens_ + elapsed * packetsPerSecond_);
        last_ = now;
    }

    if (tokens_ < 1.0)
    {
        return false;
    }
    tokens_ -= 1.0;
    return true;
}

void PacketRateLimiter::Reset()
{
    tokens_ = burst_;
    primed_ = false;
    last_ = {};
}

Network& Network::Instance()
{
    static Network n;
    return n;
}

void Network::NoteSent(std::size_t bytes)
{
    ++packetsSent_;
    bytesSent_ += bytes;
}

void Network::NoteReceived(std::size_t bytes)
{
    ++packetsReceived_;
    bytesReceived_ += bytes;
}

void Network::SetError(const std::string& error)
{
    lastError_ = error;
    BOY_WARN("Network", error);
}

NetworkStats Network::GetStats() const
{
    NetworkStats out;
    out.initialized = initialized_;
    out.server = isServer_;
    out.connected = connected_;
    out.connecting = connecting_;
    out.peers = peers_.size();
    out.packetsSent = packetsSent_;
    out.packetsReceived = packetsReceived_;
    out.bytesSent = bytesSent_;
    out.bytesReceived = bytesReceived_;
    out.packetsDropped = packetsDropped_;
    out.lastError = lastError_;
#ifdef FEMBOY_HAS_ENET
    if (!peers_.empty())
    {
        double total = 0.0;
        std::size_t count = 0;
        for (const ENetPeer* peer : peers_)
        {
            if (!peer)
            {
                continue;
            }
            total += static_cast<double>(peer->roundTripTime);
            ++count;
        }
        if (count > 0)
        {
            out.rttMs = static_cast<float>(total / static_cast<double>(count));
        }
    }
#endif
    return out;
}

void Network::ResetStats()
{
    packetsSent_ = 0;
    packetsReceived_ = 0;
    bytesSent_ = 0;
    bytesReceived_ = 0;
    packetsDropped_ = 0;
}

#ifdef FEMBOY_HAS_ENET
uint32_t Network::PeerIdOf(ENetPeer* peer) const
{
    if (!peer)
    {
        return 0;
    }
    const auto it = peerIds_.find(peer);
    return it == peerIds_.end() ? 0 : it->second;
}
#endif

bool Network::Initialize()
{
#ifdef FEMBOY_HAS_ENET
    if (initialized_)
    {
        return true;
    }
    if (enet_initialize() != 0)
    {
        SetError("enet init failed");
        return false;
    }
    initialized_ = true;
    BOY_INFO("Network", "ENet initialized");
    return true;
#else
    BOY_WARN("Network", "ENet not available (stub)");
    initialized_ = true;
    return true;
#endif
}

void Network::Shutdown()
{
#ifdef FEMBOY_HAS_ENET
    if (host_)
    {
        // Politely disconnect every peer before destroying the host so clients
        // do not have to wait for a timeout.
        for (auto* p : peers_)
        {
            if (p)
            {
                enet_peer_disconnect_later(p, 0);
            }
        }
        enet_host_flush(host_);
        enet_host_destroy(host_);
        host_ = nullptr;
    }
    peers_.clear();
    peerIds_.clear();
    nextPeerId_ = 1;
    pendingPeer_ = nullptr;
    connecting_ = false;
    if (initialized_)
    {
        enet_deinitialize();
        initialized_ = false;
    }
#else
    initialized_ = false;
#endif
    connected_ = false;
    isServer_ = false;
    BOY_INFO("Network", "shutdown");
}

bool Network::StartServer(unsigned short port, size_t maxClients)
{
#ifdef FEMBOY_HAS_ENET
    if (!initialized_ && !Initialize())
    {
        return false;
    }
    if (host_)
    {
        BOY_WARN("Network", "startServer called while a host already exists; stopping it first");
        StopServer();
    }
    if (maxClients == 0)
    {
        maxClients = 1;
    }

    ENetAddress addr{};
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    host_ = enet_host_create(&addr, maxClients, kChannelCount, 0, 0);
    if (!host_)
    {
        SetError("server create failed (port " + std::to_string(port) + ")");
        return false;
    }
    isServer_ = true;
    connected_ = false;
    connecting_ = false;
    pendingPeer_ = nullptr;
    peers_.clear();
    peerIds_.clear();
    nextPeerId_ = 1;
    BOY_INFO("Network", "server started on port " + std::to_string(port));
    return true;
#else
    (void)port;
    (void)maxClients;
    BOY_WARN("Network", "ENet not available");
    return false;
#endif
}

void Network::StopServer()
{
#ifdef FEMBOY_HAS_ENET
    if (host_)
    {
        for (auto* p : peers_)
        {
            if (p)
            {
                enet_peer_disconnect_later(p, 0);
            }
        }
        enet_host_flush(host_);
        enet_host_destroy(host_);
        host_ = nullptr;
    }
    peers_.clear();
    peerIds_.clear();
    nextPeerId_ = 1;
    pendingPeer_ = nullptr;
    connecting_ = false;
#endif
    isServer_ = false;
    connected_ = false;
    BOY_INFO("Network", "server stopped");
}

bool Network::Connect(const std::string& host, unsigned short port)
{
#ifdef FEMBOY_HAS_ENET
    if (!BeginConnect(host, port))
    {
        return false;
    }

    // Poll instead of blocking inside enet_host_service(): the handshake needs
    // the same event pump as the rest of the frame, and a stuck handshake must
    // not freeze the window (or the editor) for five seconds.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (connecting_ && !connected_ && std::chrono::steady_clock::now() < deadline)
    {
        Poll();
        if (connecting_ && !connected_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    if (connected_)
    {
        return true;
    }

    if (connecting_)
    {
        SetError("connection timeout: " + host + ":" + std::to_string(port));
    }
    Disconnect();
    return false;
#else
    (void)host;
    (void)port;
    BOY_WARN("Network", "ENet not available");
    return false;
#endif
}

bool Network::ConnectAsync(const std::string& host, unsigned short port)
{
#ifdef FEMBOY_HAS_ENET
    return BeginConnect(host, port);
#else
    (void)host;
    (void)port;
    BOY_WARN("Network", "ENet not available");
    return false;
#endif
}

bool Network::BeginConnect(const std::string& host, unsigned short port)
{
#ifdef FEMBOY_HAS_ENET
    if (!initialized_ && !Initialize())
    {
        return false;
    }
    if (isServer_)
    {
        BOY_ERROR("Network", "cannot connect while acting as a server");
        return false;
    }
    if (connected_ || connecting_)
    {
        Disconnect();
    }
    if (host_)
    {
        enet_host_destroy(host_);
        host_ = nullptr;
    }

    host_ = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
    if (!host_)
    {
        BOY_ERROR("Network", "client host create failed");
        return false;
    }

    ENetAddress addr{};
    if (enet_address_set_host(&addr, host.c_str()) != 0)
    {
        SetError("cannot resolve host: " + host);
        enet_host_destroy(host_);
        host_ = nullptr;
        return false;
    }
    addr.port = port;

    ENetPeer* peer = enet_host_connect(host_, &addr, kChannelCount, 0);
    if (!peer)
    {
        BOY_ERROR("Network", "connect failed");
        enet_host_destroy(host_);
        host_ = nullptr;
        return false;
    }

    peers_.clear();
    peerIds_.clear();
    nextPeerId_ = 1;
    pendingPeer_ = peer;
    connecting_ = true;
    BOY_INFO("Network", "connecting to " + host + ":" + std::to_string(port));
    return true;
#else
    (void)host;
    (void)port;
    BOY_WARN("Network", "ENet not available");
    return false;
#endif
}

void Network::Disconnect()
{
#ifdef FEMBOY_HAS_ENET
    if (connected_)
    {
        for (auto* p : peers_)
        {
            if (p)
            {
                enet_peer_disconnect(p, 0);
            }
        }
        // Give the disconnect packets a chance to leave before tearing the host
        // down. A pending handshake has no peers to notify, so it tears down
        // immediately instead of blocking for up to 1.6 seconds.
        if (host_)
        {
            ENetEvent event{};
            int guard = 32;
            while (guard-- > 0 && enet_host_service(host_, &event, 50) > 0)
            {
                if (event.type == ENET_EVENT_TYPE_DISCONNECT)
                {
                    break;
                }
                if (event.type == ENET_EVENT_TYPE_RECEIVE)
                {
                    enet_packet_destroy(event.packet);
                }
            }
        }
    }
    if (host_)
    {
        enet_host_destroy(host_);
        host_ = nullptr;
    }
    peers_.clear();
    peerIds_.clear();
    pendingPeer_ = nullptr;
#endif
    connecting_ = false;
    connected_ = false;
    BOY_INFO("Network", "disconnected");
}

void Network::DisconnectAll()
{
#ifdef FEMBOY_HAS_ENET
    for (auto* p : peers_)
    {
        if (p)
        {
            enet_peer_disconnect_later(p, 0);
        }
    }
    if (host_)
    {
        enet_host_flush(host_);
    }
#endif
    peers_.clear();
    peerIds_.clear();
    pendingPeer_ = nullptr;
    connecting_ = false;
    connected_ = false;
}

void Network::Kick(uint32_t peerId)
{
#ifdef FEMBOY_HAS_ENET
    for (auto* p : peers_)
    {
        if (p && PeerIdOf(p) == peerId)
        {
            enet_peer_disconnect_later(p, 0);
            if (host_)
            {
                enet_host_flush(host_);
            }
            return;
        }
    }
#else
    (void)peerId;
#endif
}

bool Network::Send(const std::string& channel, const nlohmann::json& data, bool reliable)
{
    if (peers_.empty())
    {
        return false;
    }
#ifdef FEMBOY_HAS_ENET
    return SendTo(PeerIdOf(peers_.front()), channel, data, reliable);
#else
    (void)channel;
    (void)data;
    (void)reliable;
    return false;
#endif
}

bool Network::SendTo(uint32_t peerId, const std::string& channel, const nlohmann::json& data, bool reliable)
{
    const std::string msg = channel + "|" + data.dump();
    if (msg.size() > kMaxMessageSize)
    {
        BOY_WARN("Network", "sendTo: payload too large, dropped");
        return false;
    }

#ifdef FEMBOY_HAS_ENET
    if (!host_ || peers_.empty())
    {
        return false;
    }

    ENetPeer* target = nullptr;
    for (auto* p : peers_)
    {
        if (p && PeerIdOf(p) == peerId)
        {
            target = p;
            break;
        }
    }
    if (!target)
    {
        BOY_WARN("Network", "sendTo: unknown peer " + std::to_string(peerId));
        return false;
    }

    // No trailing NUL: the length is carried by ENet itself, and the old
    // "+1" made receivers parse a string with an embedded terminator.
    ENetPacket* packet = enet_packet_create(msg.data(), msg.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!packet)
    {
        return false;
    }

    if (enet_peer_send(target, kJsonChannel, packet) != 0)
    {
        enet_packet_destroy(packet);
        return false;
    }
    NoteSent(msg.size());
    return true;
#else
    (void)peerId;
    (void)reliable;
    return false;
#endif
}

bool Network::Broadcast(const std::string& channel, const nlohmann::json& data, bool reliable)
{
    if (!isServer_)
    {
        return Send(channel, data, reliable);
    }

#ifdef FEMBOY_HAS_ENET
    if (!host_ || peers_.empty())
    {
        return false;
    }

    const std::string msg = channel + "|" + data.dump();
    if (msg.size() > kMaxMessageSize)
    {
        BOY_WARN("Network", "broadcast: payload too large, dropped");
        return false;
    }

    // One packet per peer: enet_host_broadcast would take ownership of a single
    // packet, and a failed send would then destroy data other peers still use.
    bool sentAny = false;
    for (auto* p : peers_)
    {
        if (!p)
        {
            continue;
        }
        ENetPacket* packet = enet_packet_create(msg.data(), msg.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
        if (!packet)
        {
            continue;
        }
        if (enet_peer_send(p, kJsonChannel, packet) != 0)
        {
            enet_packet_destroy(packet);
        }
        else
        {
            NoteSent(msg.size());
            sentAny = true;
        }
    }
    if (sentAny)
    {
        enet_host_flush(host_);
    }
    return sentAny;
#else
    (void)channel;
    (void)data;
    (void)reliable;
    return false;
#endif
}

bool Network::SendBinaryTo(uint32_t peerId, std::uint8_t channel, const void* data, std::size_t size, bool reliable)
{
    if (!data || size == 0 || size > kMaxBinarySize || channel >= kChannelCount)
    {
        return false;
    }
#ifdef FEMBOY_HAS_ENET
    if (!host_ || peers_.empty())
    {
        return false;
    }

    ENetPeer* target = nullptr;
    for (auto* p : peers_)
    {
        if (p && PeerIdOf(p) == peerId)
        {
            target = p;
            break;
        }
    }
    if (!target)
    {
        return false;
    }

    ENetPacket* packet = enet_packet_create(data, size, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!packet)
    {
        return false;
    }
    if (enet_peer_send(target, channel, packet) != 0)
    {
        enet_packet_destroy(packet);
        return false;
    }
    NoteSent(size);
    // Voice is latency sensitive. Flushing here costs one syscall per 20 ms
    // frame and removes a full frame of delay compared with waiting for the
    // next poll().
    enet_host_flush(host_);
    return true;
#else
    (void)peerId;
    (void)reliable;
    return false;
#endif
}

bool Network::BroadcastBinary(std::uint8_t channel, const void* data, std::size_t size, bool reliable,
                              uint32_t exceptPeerId)
{
    if (!data || size == 0 || size > kMaxBinarySize || channel >= kChannelCount)
    {
        return false;
    }
    if (!isServer_)
    {
        return SendBinaryTo(0, channel, data, size, reliable);
    }

#ifdef FEMBOY_HAS_ENET
    if (!host_ || peers_.empty())
    {
        return false;
    }

    bool sentAny = false;
    for (auto* p : peers_)
    {
        if (!p || PeerIdOf(p) == exceptPeerId)
        {
            continue;
        }
        ENetPacket* packet = enet_packet_create(data, size, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
        if (!packet)
        {
            continue;
        }
        if (enet_peer_send(p, channel, packet) != 0)
        {
            enet_packet_destroy(packet);
        }
        else
        {
            NoteSent(size);
            sentAny = true;
        }
    }
    if (sentAny)
    {
        enet_host_flush(host_);
    }
    return sentAny;
#else
    (void)reliable;
    (void)exceptPeerId;
    return false;
#endif
}

std::vector<uint32_t> Network::PeerIds() const
{
    std::vector<uint32_t> out;
    out.reserve(peers_.size());
#ifdef FEMBOY_HAS_ENET
    for (auto* p : peers_)
    {
        if (p)
        {
            out.push_back(PeerIdOf(p));
        }
    }
#endif
    return out;
}

void Network::Poll()
{
#ifdef FEMBOY_HAS_ENET
    if (!host_)
    {
        return;
    }

    ENetEvent event{};
    while (enet_host_service(host_, &event, 0) > 0)
    {
        switch (event.type)
        {
            case ENET_EVENT_TYPE_CONNECT:
            {
                if (isServer_)
                {
                    if (!event.peer)
                    {
                        break;
                    }
                    // Allocate the session id before the player list is built from
                    // it. Never reuse 0: on the client side 0 means "the server".
                    const uint32_t id = nextPeerId_++;
                    peerIds_[event.peer] = id;
                    peers_.push_back(event.peer);
                    BOY_INFO("Network", "peer connected (id " + std::to_string(id) + ")");
                    auto connectedCb = connCb_;
                    if (connectedCb)
                    {
                        connectedCb(id);
                    }
                    // The callback may have kicked the peer or torn the host down.
                    if (!host_)
                    {
                        return;
                    }
                }
                else
                {
                    if (event.peer)
                    {
                        peers_.clear();
                        peers_.push_back(event.peer);
                        peerIds_[event.peer] = 0;
                    }
                    pendingPeer_ = nullptr;
                    connecting_ = false;
                    connected_ = true;
                    BOY_INFO("Network", "handshake complete");
                }
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE:
            {
                if (!event.packet)
                {
                    break;
                }

                // Rate-limit before touching payload. Connection/disconnection
                // events are never dropped; only data packets compete for tokens.
                if (receiveLimiter_.Enabled() && !receiveLimiter_.Allow())
                {
                    ++packetsDropped_;
                    enet_packet_destroy(event.packet);
                    event.packet = nullptr;
                    break;
                }
                NoteReceived(event.packet->dataLength);

                // The binary path has its own callback and must not be parsed as
                // "channel|json". Copy the bytes before destroying the ENet packet:
                // a callback may relay them or shut the network down, and the packet
                // memory is owned by the host.
                if (event.channelID != kJsonChannel)
                {
                    std::vector<std::uint8_t> payload(event.packet->data,
                                                      event.packet->data + event.packet->dataLength);
                    const std::uint32_t id = PeerIdOf(event.peer);
                    const std::uint8_t channel = event.channelID;
                    enet_packet_destroy(event.packet);
                    event.packet = nullptr;
                    auto binaryCb = binRecvCb_;
                    if (binaryCb)
                    {
                        binaryCb(id, channel, payload.data(), payload.size());
                    }
                    if (!host_)
                    {
                        return;
                    }
                    break;
                }

                // Use the explicit length; never assume a NUL terminator.
                std::string msg(reinterpret_cast<const char*>(event.packet->data), event.packet->dataLength);
                const size_t sep = msg.find('|');
                const std::string channel = (sep != std::string::npos) ? msg.substr(0, sep) : msg;

                nlohmann::json data = nlohmann::json::object();
                if (sep != std::string::npos)
                {
                    try
                    {
                        data = nlohmann::json::parse(msg.substr(sep + 1));
                    }
                    catch (const std::exception& e)
                    {
                        BOY_WARN("Network", std::string("bad packet payload: ") + e.what());
                    }
                }

                // Copy the callback first: it may replace itself while running.
                auto cb = recvCb_;
                const uint32_t id = PeerIdOf(event.peer);
                // Release the packet before invoking user code: a callback that
                // shuts the network down destroys the host, which frees packet
                // memory, and the later enet_packet_destroy() would double-free it.
                enet_packet_destroy(event.packet);
                event.packet = nullptr;
                if (cb)
                {
                    cb(channel, data, id);
                }
                // The callback may have destroyed host_; stop servicing a dead host.
                if (!host_)
                {
                    return;
                }
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT:
            {
                const uint32_t id = PeerIdOf(event.peer);
                peers_.erase(std::remove(peers_.begin(), peers_.end(), event.peer), peers_.end());
                peerIds_.erase(event.peer);
                if (!isServer_)
                {
                    connected_ = false;
                    connecting_ = false;
                    pendingPeer_ = nullptr;
                }
                BOY_INFO("Network", "peer disconnected (id " + std::to_string(id) + ")");

                auto cb = discCb_;
                if (cb)
                {
                    cb(id);
                }
                if (!host_)
                {
                    return;
                }
                break;
            }
            default:
                break;
        }
    }
#endif
}

} // namespace Fem
