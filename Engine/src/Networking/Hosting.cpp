// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Hosting.hpp"
#include "FemboyEngine/Networking.hpp"
#include "FemboyEngine/Replication.hpp"
#include "FemboyEngine/Boy.hpp"
#include <algorithm>
#include <cmath>

namespace Fem
{

namespace
{
const char* kChannelHello = "hello";
const char* kChannelPlayers = "players";
const char* kChannelRepl = "repl";
const char* kChannelRpc = "rpc";
const char* kChannelState = "state";
const char* kChannelReady = "ready";

// A client that has been silent for this long is considered gone.
constexpr float kTimeoutSeconds = 10.f;
} // namespace

GameHost& GameHost::Instance()
{
    static GameHost h;
    return h;
}

void GameHost::InstallCallbacks()
{
    if (callbackInstalled_)
    {
        return;
    }

    Network& net = Network::Instance();
    net.SetReceiveCallback([this](const std::string& channel, const nlohmann::json& data, uint32_t peerId)
                           { OnPacket(channel, data, peerId); });
    net.SetPeerDisconnectedCallback([this](uint32_t peerId) { OnPeerDisconnected(peerId); });
    net.SetPeerConnectedCallback(
        [this](uint32_t peerId)
        {
            // Arm the idle timeout immediately; a client
            // that connects and never sends hello must not
            // linger forever.
            if (hosting_)
            {
                lastSeen_[peerId] = 0.f;
            }
        });
    callbackInstalled_ = true;
}

void GameHost::SetReconnectPolicy(const ReconnectPolicy& policy)
{
    reconnectPolicy_ = policy;
    if (!std::isfinite(reconnectPolicy_.initialDelay) || reconnectPolicy_.initialDelay < 0.1f)
    {
        reconnectPolicy_.initialDelay = 0.1f;
    }
    if (!std::isfinite(reconnectPolicy_.maxDelay) || reconnectPolicy_.maxDelay < reconnectPolicy_.initialDelay)
    {
        reconnectPolicy_.maxDelay = reconnectPolicy_.initialDelay;
    }
    if (!std::isfinite(reconnectPolicy_.backoffMultiplier) || reconnectPolicy_.backoffMultiplier < 1.f)
    {
        reconnectPolicy_.backoffMultiplier = 1.f;
    }
    if (reconnectPolicy_.maxAttempts < 0)
    {
        reconnectPolicy_.maxAttempts = 0;
    }
    if (!reconnectPolicy_.enabled)
    {
        reconnecting_ = false;
        reconnectTimer_ = 0.f;
    }
}

void GameHost::ScheduleReconnect()
{
    if (!reconnectPolicy_.enabled || hosting_ || serverAddress_.empty())
    {
        reconnecting_ = false;
        reconnectTimer_ = 0.f;
        return;
    }
    if (reconnectPolicy_.maxAttempts > 0 && reconnectAttempts_ >= reconnectPolicy_.maxAttempts)
    {
        reconnecting_ = false;
        reconnectTimer_ = 0.f;
        BOY_WARN("Hosting", "reconnect attempts exhausted");
        return;
    }

    const double exponent = static_cast<double>(reconnectAttempts_);
    const double delay = static_cast<double>(reconnectPolicy_.initialDelay) *
                         std::pow(static_cast<double>(reconnectPolicy_.backoffMultiplier), exponent);
    reconnectTimer_ = static_cast<float>(std::min<double>(delay, static_cast<double>(reconnectPolicy_.maxDelay)));
    reconnecting_ = true;
    BOY_INFO("Hosting", "reconnect in " + std::to_string(reconnectTimer_) + " s");
}

void GameHost::TickReconnect(float dt)
{
    if (!reconnectPolicy_.enabled)
    {
        reconnecting_ = false;
        reconnectTimer_ = 0.f;
        return;
    }

    reconnectTimer_ -= dt;
    if (reconnectTimer_ > 0.f)
    {
        return;
    }

    if (reconnectPolicy_.maxAttempts > 0 && reconnectAttempts_ >= reconnectPolicy_.maxAttempts)
    {
        reconnecting_ = false;
        BOY_WARN("Hosting", "reconnect attempts exhausted");
        return;
    }

    ++reconnectAttempts_;
    BOY_INFO("Hosting", "reconnect attempt " + std::to_string(reconnectAttempts_) + " to " + serverAddress_ + ":" +
                            std::to_string(port_));
    if (!Network::Instance().ConnectAsync(serverAddress_, port_))
    {
        ScheduleReconnect();
        return;
    }

    // The hello and player-list exchange happen in Tick() once the handshake
    // completes, exactly as for a manual JoinAsync().
    joined_ = true;
    joining_ = true;
    hosting_ = false;
    reconnecting_ = false;
    reconnectTimer_ = 0.f;
}

bool GameHost::Host(unsigned short port, size_t maxPlayers, const std::string& serverName)
{
    if (hosting_)
    {
        BOY_WARN("Hosting", "already hosting; stopping previous session");
        StopHost();
    }
    if (joined_ || joining_ || reconnecting_)
    {
        Leave();
    }

    Network& net = Network::Instance();
    if (!net.Initialize())
    {
        return false;
    }
    if (!net.StartServer(port, maxPlayers))
    {
        BOY_ERROR("Hosting", "could not start server on port " + std::to_string(port));
        return false;
    }

    InstallCallbacks();

    hosting_ = true;
    joined_ = false;
    joining_ = false;
    reconnecting_ = false;
    reconnectTimer_ = 0.f;
    serverAddress_.clear();
    port_ = port;
    serverName_ = serverName;
    players_.clear();
    lastSeen_.clear();

    // The host is a player too. Without this entry localPlayer() returned null
    // while hosting, and the first joining client was handed player index 0.
    NetPlayer hostPlayer;
    hostPlayer.peerId = 0;
    hostPlayer.name = serverName.empty() ? std::string("Host") : serverName;
    hostPlayer.playerIndex = 0;
    hostPlayer.ready = true;
    players_.push_back(hostPlayer);
    nextPlayerIndex_ = 1;

    sinceReplication_ = 0.f;

    Replication::Instance().Reset();
    Replication::Instance().SetRole(ReplicationRole::Server);

    BOY_INFO("Hosting", "listening on port " + std::to_string(port) + " as '" + serverName + "'");
    return true;
}

void GameHost::StopHost()
{
    if (!hosting_)
    {
        return;
    }

    Network::Instance().DisconnectAll();
    Network::Instance().StopServer();

    hosting_ = false;
    joined_ = false;
    joining_ = false;
    reconnecting_ = false;
    reconnectTimer_ = 0.f;
    reconnectAttempts_ = 0;
    serverAddress_.clear();
    players_.clear();
    lastSeen_.clear();
    localPeerId_ = 0;
    Replication::Instance().SetRole(ReplicationRole::None);
    BOY_INFO("Hosting", "server stopped");
}

bool GameHost::Join(const std::string& address, unsigned short port, const std::string& playerName)
{
    if (joined_ || joining_ || reconnecting_)
    {
        Leave();
    }

    Network& net = Network::Instance();
    if (!net.Initialize())
    {
        return false;
    }

    InstallCallbacks();

    if (!net.Connect(address, port))
    {
        return false;
    }

    joined_ = true;
    joining_ = false;
    reconnecting_ = false;
    reconnectTimer_ = 0.f;
    reconnectAttempts_ = 0;
    hosting_ = false;
    port_ = port;
    serverAddress_ = address;
    localName_ = playerName;

    Replication::Instance().Reset();
    Replication::Instance().SetRole(ReplicationRole::Client);

    // Announce ourselves; the server replies with the player list and a full
    // state snapshot so late joiners are immediately in sync.
    net.Send(kChannelHello, {{"name", playerName}});

    BOY_INFO("Hosting", "joined " + address + ":" + std::to_string(port) + " as '" + playerName + "'");
    return true;
}

bool GameHost::JoinAsync(const std::string& address, unsigned short port, const std::string& playerName)
{
    if (joined_ || joining_ || reconnecting_)
    {
        Leave();
    }

    Network& net = Network::Instance();
    if (!net.Initialize())
    {
        return false;
    }

    InstallCallbacks();

    if (!net.ConnectAsync(address, port))
    {
        return false;
    }

    // joined_ is true while the handshake is in flight so Tick() keeps polling;
    // joining_ distinguishes it from a live session.
    joined_ = true;
    joining_ = true;
    reconnecting_ = false;
    reconnectTimer_ = 0.f;
    reconnectAttempts_ = 0;
    hosting_ = false;
    port_ = port;
    serverAddress_ = address;
    localName_ = playerName;

    Replication::Instance().Reset();
    Replication::Instance().SetRole(ReplicationRole::Client);

    BOY_INFO("Hosting", "connecting to " + address + ":" + std::to_string(port) + " as '" + playerName + "'");
    return true;
}

void GameHost::Leave()
{
    if (!joined_ && !joining_ && !reconnecting_)
    {
        return;
    }

    Network::Instance().Disconnect();
    joined_ = false;
    joining_ = false;
    reconnecting_ = false;
    reconnectTimer_ = 0.f;
    reconnectAttempts_ = 0;
    players_.clear();
    lastSeen_.clear();
    localPeerId_ = 0;
    Replication::Instance().SetRole(ReplicationRole::None);
    BOY_INFO("Hosting", "left the session");
}

void GameHost::SetTickRate(float hz)
{
    if (!std::isfinite(hz) || hz <= 0.f)
    {
        return;
    }
    tickRate_ = std::clamp(hz, 1.f, 120.f);
}

const NetPlayer* GameHost::Player(uint32_t peerId) const
{
    for (const auto& p : players_)
    {
        if (p.peerId == peerId)
        {
            return &p;
        }
    }
    return nullptr;
}

const NetPlayer* GameHost::LocalPlayer() const
{
    if (hosting_)
    {
        for (const auto& p : players_)
        {
            if (p.playerIndex == 0)
            {
                return &p;
            }
        }
        return nullptr;
    }
    return Player(localPeerId_);
}

void GameHost::SetPlayerListChangedCallback(std::function<void()> cb)
{
    playerListChanged_ = std::move(cb);
}

bool GameHost::AllPlayersReady() const
{
    if (players_.empty())
    {
        return false;
    }
    for (const NetPlayer& p : players_)
    {
        if (!p.ready)
        {
            return false;
        }
    }
    return true;
}

size_t GameHost::ReadyCount() const
{
    size_t count = 0;
    for (const NetPlayer& p : players_)
    {
        count += p.ready ? 1u : 0u;
    }
    return count;
}

bool GameHost::SetReady(bool ready)
{
    if (hosting_)
    {
        for (NetPlayer& p : players_)
        {
            if (p.playerIndex == 0)
            {
                p.ready = ready;
            }
        }
        BroadcastPlayerList();
        if (playerListChanged_)
        {
            playerListChanged_();
        }
        return true;
    }
    if (!joined_)
    {
        return false;
    }

    // Update the local copy immediately so a UI reacts without waiting for the
    // server round trip; the authoritative list replaces it on the next packet.
    for (NetPlayer& p : players_)
    {
        if (p.peerId == localPeerId_)
        {
            p.ready = ready;
        }
    }
    const bool sent = Network::Instance().Send(kChannelReady, {{"ready", ready}});
    if (playerListChanged_)
    {
        playerListChanged_();
    }
    return sent;
}

nlohmann::json GameHost::PlayerListJson() const
{
    nlohmann::json list = nlohmann::json::array();
    for (const NetPlayer& p : players_)
    {
        list.push_back({{"id", p.peerId}, {"name", p.name}, {"index", p.playerIndex}, {"ready", p.ready}});
    }
    return list;
}

void GameHost::SendPlayerListTo(uint32_t peerId)
{
    // "you" lets the client identify its own entry without matching display
    // names (two players may use the same name, and the host is also listed).
    Network::Instance().SendTo(peerId, kChannelPlayers,
                               {{"players", PlayerListJson()}, {"server", serverName_}, {"you", peerId}});
}

void GameHost::BroadcastPlayerList()
{
    if (!hosting_)
    {
        return;
    }
    for (const NetPlayer& p : players_)
    {
        if (p.peerId != 0)
        {
            SendPlayerListTo(p.peerId);
        }
    }
}

void GameHost::HandleReady(uint32_t peerId, const nlohmann::json& data)
{
    if (!data.is_object() || !data.contains("ready") || !data["ready"].is_boolean())
    {
        return;
    }
    const bool ready = data["ready"].get<bool>();
    for (NetPlayer& p : players_)
    {
        if (p.peerId == peerId)
        {
            p.ready = ready;
            break;
        }
    }
    BroadcastPlayerList();
    if (playerListChanged_)
    {
        playerListChanged_();
    }
}

void GameHost::Tick(float dt)
{
    if (!hosting_ && !joined_ && !reconnecting_)
    {
        return;
    }
    if (!std::isfinite(dt) || dt < 0.f)
    {
        return;
    }

    Network::Instance().Poll();

    if (reconnecting_)
    {
        TickReconnect(dt);
        return;
    }

    // Complete or fail a non-blocking join. The hello has to wait until the
    // handshake finished; sending it from JoinAsync() would race the connect.
    if (joining_)
    {
        if (Network::Instance().IsConnected())
        {
            joining_ = false;
            reconnectAttempts_ = 0;
            Network::Instance().Send(kChannelHello, {{"name", localName_}});
            BOY_INFO("Hosting", "connected to server");
        }
        else if (!Network::Instance().IsConnecting())
        {
            joining_ = false;
            joined_ = false;
            players_.clear();
            localPeerId_ = 0;
            BOY_WARN("Hosting", "async join failed");
            // A retry policy turns a failed handshake into the same backoff
            // path as a link that dropped after a successful join.
            ScheduleReconnect();
            return;
        }
    }

    if (hosting_)
    {
        // Drop peers that went silent; the player entry has to go with them or
        // the lobby would keep showing ghosts. Timeouts advance with real time
        // and are checked every tick, not only when a replication delta is due.
        std::vector<uint32_t> timedOut;
        for (auto it = lastSeen_.begin(); it != lastSeen_.end();)
        {
            it->second += dt;
            if (it->second > kTimeoutSeconds)
            {
                timedOut.push_back(it->first);
                it = lastSeen_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (const uint32_t peerId : timedOut)
        {
            // Removing the player entry is not enough: the ENet peer is still
            // connected and would keep a slot (and the ghost) alive.
            Network::Instance().Kick(peerId);
            OnPeerDisconnected(peerId);
        }
    }

    const float interval = 1.f / tickRate_;
    sinceReplication_ += dt;
    if (sinceReplication_ < interval)
    {
        return;
    }
    // Keep the remainder instead of zeroing: at a 20 Hz tick rate and a 144 Hz
    // frame rate, zeroing would make the effective send rate drift (and with
    // large dt would send only one delta for several intervals worth of time).
    sinceReplication_ = std::fmod(sinceReplication_, interval);

    if (hosting_)
    {
        const nlohmann::json delta = Replication::Instance().PeekDelta();
        if (!delta.is_null())
        {
            if (Network::Instance().Broadcast(kChannelRepl, delta))
            {
                Replication::Instance().CommitDelta(delta);
                lastDeltaBytes_ = delta.dump().size();
            }
            // On failure the properties stay dirty and are retried next tick.
        }
        else
        {
            lastDeltaBytes_ = 0;
        }
    }
    else
    {
        // Clients push their own client-authoritative properties.
        const nlohmann::json delta = Replication::Instance().PeekDelta();
        if (!delta.is_null())
        {
            if (Network::Instance().Send(kChannelRepl, delta))
            {
                Replication::Instance().CommitDelta(delta);
                lastDeltaBytes_ = delta.dump().size();
            }
        }
    }
}

void GameHost::FlushReplication()
{
    const nlohmann::json delta = Replication::Instance().PeekDelta();
    if (delta.is_null())
    {
        return;
    }

    bool sent = false;
    if (hosting_)
    {
        sent = Network::Instance().Broadcast(kChannelRepl, delta);
    }
    else if (joined_)
    {
        sent = Network::Instance().Send(kChannelRepl, delta);
    }

    if (sent)
    {
        Replication::Instance().CommitDelta(delta);
        lastDeltaBytes_ = delta.dump().size();
    }
}

void GameHost::SendFullState()
{
    if (!hosting_)
    {
        return;
    }

    // Snapshot without consuming pending dirty flags: clients that are already
    // in sync would otherwise make the next delta disappear for everyone.
    const nlohmann::json snapshot = Replication::Instance().SnapshotAll();
    if (!snapshot["props"].empty())
    {
        Network::Instance().Broadcast(kChannelRepl, snapshot);
    }
}

void GameHost::SendFullStateTo(uint32_t peerId)
{
    if (!hosting_)
    {
        return;
    }

    const nlohmann::json snapshot = Replication::Instance().SnapshotAll();
    if (!snapshot["props"].empty())
    {
        Network::Instance().SendTo(peerId, kChannelRepl, snapshot);
    }
}

void GameHost::Rpc(const std::string& name, const nlohmann::json& args)
{
    if (name.empty())
    {
        return;
    }
    const nlohmann::json payload = {{"n", name}, {"a", args}};

    if (hosting_)
    {
        // A host is also a player: run the handler locally, then forward.
        Replication::Instance().DispatchRpc(name, 0, args);
        Network::Instance().Broadcast(kChannelRpc, payload);
    }
    else if (joined_)
    {
        Network::Instance().Send(kChannelRpc, payload);
    }
}

bool GameHost::RpcTo(uint32_t peerId, const std::string& name, const nlohmann::json& args)
{
    if (!hosting_ || name.empty())
    {
        return false;
    }
    // Peer 0 is the host itself; deliver locally instead of failing as an
    // unknown remote peer.
    if (peerId == 0)
    {
        return Replication::Instance().DispatchRpc(name, 0, args);
    }

    const nlohmann::json payload = {{"n", name}, {"a", args}};
    return Network::Instance().SendTo(peerId, kChannelRpc, payload);
}

void GameHost::OnPacket(const std::string& channel, const nlohmann::json& data, uint32_t peerId)
{
    // Any traffic from a peer proves it is alive.
    if (hosting_ && peerId != 0)
    {
        lastSeen_[peerId] = 0.f;
    }

    if (channel == kChannelHello)
    {
        if (hosting_)
        {
            HandleHello(peerId, data);
        }
        return;
    }
    if (channel == kChannelPlayers)
    {
        if (joined_)
        {
            HandlePlayerList(data);
        }
        return;
    }
    if (channel == kChannelReady)
    {
        if (hosting_)
        {
            HandleReady(peerId, data);
        }
        return;
    }
    if (channel == kChannelState)
    {
        if (joined_)
        {
            HandleState(peerId, data);
        }
        return;
    }
    if (channel == kChannelRepl)
    {
        if (hosting_)
        {
            // Only apply properties the sender is allowed to write.
            nlohmann::json filtered;
            filtered["props"] = nlohmann::json::array();
            if (data.is_object() && data.contains("props") && data["props"].is_array())
            {
                for (const auto& entry : data["props"])
                {
                    if (!entry.is_object() || !entry.contains("id") || !entry.contains("v"))
                    {
                        continue;
                    }

                    // A malformed id must not throw out of the network pump.
                    uint32_t id = 0;
                    try
                    {
                        if (!entry["id"].is_number_integer() && !entry["id"].is_number_unsigned())
                        {
                            continue;
                        }
                        id = entry["id"].get<uint32_t>();
                    }
                    catch (const std::exception&)
                    {
                        continue;
                    }

                    ReplicatedProperty rp;
                    if (!Replication::Instance().Property(id, rp))
                    {
                        continue;
                    }
                    if (!rp.clientAuthoritative)
                    {
                        continue;
                    }
                    filtered["props"].push_back(entry);
                }
            }
            if (Replication::Instance().ApplyDelta(filtered) > 0)
            {
                // The value came from one client; fan it out to everyone else
                // on the next replication tick.
                for (const auto& entry : filtered["props"])
                {
                    if (!entry.is_object() || !entry.contains("id"))
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
                    Replication::Instance().MarkDirty(entry["id"].get<uint32_t>());
                }
            }
        }
        else if (joined_)
        {
            Replication::Instance().ApplyDelta(data);
        }
        return;
    }
    if (channel == kChannelRpc)
    {
        if (!data.is_object() || !data.contains("n") || !data["n"].is_string())
        {
            return;
        }
        const std::string name = data["n"].get<std::string>();
        const nlohmann::json args = data.contains("a") ? data["a"] : nlohmann::json();
        if (hosting_)
        {
            Replication::Instance().DispatchRpc(name, peerId, args);
        }
        else if (joined_)
        {
            Replication::Instance().DispatchRpc(name, 0, args);
        }
        return;
    }
}

void GameHost::OnPeerDisconnected(uint32_t peerId)
{
    lastSeen_.erase(peerId);

    auto it =
        std::find_if(players_.begin(), players_.end(), [peerId](const NetPlayer& p) { return p.peerId == peerId; });
    if (it != players_.end())
    {
        BOY_INFO("Hosting", "player left: " + it->name);
        players_.erase(it);
        if (playerListChanged_)
        {
            playerListChanged_();
        }
    }

    if ((joined_ || joining_) && peerId == 0)
    {
        // The client lost the connection to the server.
        BOY_WARN("Hosting", "connection to server lost");
        joined_ = false;
        joining_ = false;
        players_.clear();
        localPeerId_ = 0;
        if (reconnectPolicy_.enabled)
        {
            reconnectAttempts_ = 0;
            ScheduleReconnect();
        }
    }
}

void GameHost::HandleHello(uint32_t peerId, const nlohmann::json& data)
{
    const std::string name = (data.is_object() && data.contains("name") && data["name"].is_string())
                                 ? data["name"].get<std::string>()
                                 : std::string("Player");

    // Re-connecting with the same peer id must not duplicate the entry.
    auto it =
        std::find_if(players_.begin(), players_.end(), [peerId](const NetPlayer& p) { return p.peerId == peerId; });
    if (it == players_.end())
    {
        NetPlayer p;
        p.peerId = peerId;
        p.name = name;
        p.playerIndex = nextPlayerIndex_++;
        players_.push_back(p);
        BOY_INFO("Hosting", "player joined: " + name + " (peer " + std::to_string(peerId) + ")");
    }
    else
    {
        it->name = name;
    }

    lastSeen_[peerId] = 0.f;

    // Every client, including the new one, gets the same authoritative list.
    BroadcastPlayerList();
    SendFullStateTo(peerId);

    if (playerListChanged_)
    {
        playerListChanged_();
    }
}

void GameHost::HandlePlayerList(const nlohmann::json& data)
{
    if (!data.is_object() || !data.contains("players") || !data["players"].is_array())
    {
        return;
    }

    std::vector<NetPlayer> next;
    for (const auto& p : data["players"])
    {
        if (!p.is_object())
        {
            continue;
        }
        // json::value() throws on a type mismatch; a malicious or corrupt
        // player list must not propagate an exception out of Network::Poll().
        NetPlayer np;
        if (p.contains("id") && p["id"].is_number_unsigned())
        {
            np.peerId = p["id"].get<uint32_t>();
        }
        else if (p.contains("id") && p["id"].is_number_integer() && p["id"].get<long long>() >= 0)
        {
            np.peerId = static_cast<uint32_t>(p["id"].get<long long>());
        }
        if (p.contains("name") && p["name"].is_string())
        {
            np.name = p["name"].get<std::string>();
        }
        else
        {
            np.name = "Player";
        }
        if (p.contains("index") && p["index"].is_number_integer())
        {
            np.playerIndex = p["index"].get<int>();
        }
        if (p.contains("ready") && p["ready"].is_boolean())
        {
            np.ready = p["ready"].get<bool>();
        }
        next.push_back(std::move(np));
    }

    players_ = std::move(next);

    // Prefer the server-assigned id: names are user input and can collide.
    localPeerId_ = 0;
    if (data.contains("you") && data["you"].is_number_unsigned())
    {
        localPeerId_ = data["you"].get<uint32_t>();
    }
    else if (data.contains("you") && data["you"].is_number_integer() && data["you"].get<long long>() >= 0)
    {
        localPeerId_ = static_cast<uint32_t>(data["you"].get<long long>());
    }
    else
    {
        // Older servers do not send "you"; fall back to the name match.
        for (const auto& p : players_)
        {
            if (p.name == localName_)
            {
                localPeerId_ = p.peerId;
            }
        }
    }

    if (playerListChanged_)
    {
        playerListChanged_();
    }
}

void GameHost::HandleState(uint32_t peerId, const nlohmann::json& data)
{
    (void)peerId;
    Replication::Instance().ApplyDelta(data);
}

} // namespace Fem
