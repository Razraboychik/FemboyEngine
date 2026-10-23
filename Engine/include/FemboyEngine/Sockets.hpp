// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

#include "FemboyEngine/ECS.hpp"

namespace Fem
{

// A named anchor on an object: a muzzle, a hand, a hardpoint, a hat.
//
// A socket stores a local offset, rotation and scale. It does not own anything:
// whichever system wants to attach an object reads the socket's world transform
// and writes it into the child. That is the same model as a bone socket in a 3D
// engine and it survives ECS archetype moves because the data stays in plain
// components.
struct Socket
{
    std::string name;
    sf::Vector2f offset{0.f, 0.f};
    float rotation = 0.f;
    sf::Vector2f scale{1.f, 1.f};
};

struct SocketHost
{
    std::vector<Socket> sockets;

    Socket* Find(const std::string& socketName)
    {
        for (Socket& socket : sockets)
        {
            if (socket.name == socketName)
            {
                return &socket;
            }
        }
        return nullptr;
    }
    const Socket* Find(const std::string& socketName) const
    {
        for (const Socket& socket : sockets)
        {
            if (socket.name == socketName)
            {
                return &socket;
            }
        }
        return nullptr;
    }
    Socket& Add(const std::string& socketName, sf::Vector2f offset = {}, float rotation = 0.f,
                sf::Vector2f scale = {1.f, 1.f})
    {
        if (Socket* existing = Find(socketName))
        {
            existing->offset = offset;
            existing->rotation = rotation;
            existing->scale = scale;
            return *existing;
        }
        sockets.push_back(Socket{socketName, offset, rotation, scale});
        return sockets.back();
    }
    bool Remove(const std::string& socketName);
};

// A child entity locked to a socket on another entity.
struct SocketAttachment
{
    Entity parent = entt::null;
    std::string socket;
    // Extra local transform applied after the socket. A muzzle flash wants a
    // small forward offset; a hat wants none.
    sf::Vector2f offset{0.f, 0.f};
    float rotation = 0.f;
    bool inheritScale = true;
    // Set by SocketSystem when the parent or socket is missing, so gameplay can
    // hide the child instead of leaving it at its last world position.
    bool resolved = false;
};

// World-space result of evaluating a socket. Separate from Socket because it is
// what an attachment or a spawner actually consumes.
struct SocketWorld
{
    sf::Vector2f position{0.f, 0.f};
    float rotation = 0.f;
    sf::Vector2f scale{1.f, 1.f};
};

// Evaluates a socket against a parent transform. Pure math, usable outside the
// ECS (the game's player is not an entt entity).
SocketWorld MakeSocketWorld(const Socket& socket, sf::Vector2f parentPosition, float parentRotation,
                            sf::Vector2f parentScale = {1.f, 1.f});

// ECS glue: attaches/detaches children and writes their TransformComponent.
class SocketSystem
{
public:
    // Creates or replaces the attachment of `child` to `socketName` on `parent`.
    static bool Attach(Registry& registry, Entity child, Entity parent, const std::string& socketName,
                       sf::Vector2f offset = {}, float rotation = 0.f);
    static void Detach(Registry& registry, Entity child);

    // Evaluates every attachment. Call after gameplay movement and before
    // rendering, so the child's transform is the one the renderer sees.
    static void Update(Registry& registry);

    // Convenience for spawning a projectile at a socket without an entity child:
    // returns the socket world transform or `fallback` when the host/socket is
    // missing.
    static SocketWorld Evaluate(const Registry& registry, Entity parent, const std::string& socketName,
                                const SocketWorld& fallback = {});
};

} // namespace Fem
