// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Sockets.hpp"

#include <algorithm>
#include <cmath>

namespace Fem
{

namespace
{
sf::Vector2f Rotate(sf::Vector2f v, float degrees)
{
    const float rad = degrees * 3.14159265358979323846f / 180.f;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}
} // namespace

bool SocketHost::Remove(const std::string& socketName)
{
    const auto it = std::remove_if(sockets.begin(), sockets.end(),
                                   [&socketName](const Socket& socket) { return socket.name == socketName; });
    if (it == sockets.end())
    {
        return false;
    }
    sockets.erase(it, sockets.end());
    return true;
}

SocketWorld MakeSocketWorld(const Socket& socket, sf::Vector2f parentPosition, float parentRotation,
                            sf::Vector2f parentScale)
{
    SocketWorld world;
    const sf::Vector2f scaled{socket.offset.x * parentScale.x, socket.offset.y * parentScale.y};
    world.position = parentPosition + Rotate(scaled, parentRotation);
    world.rotation = parentRotation + socket.rotation;
    world.scale = {parentScale.x * socket.scale.x, parentScale.y * socket.scale.y};
    return world;
}

bool SocketSystem::Attach(Registry& registry, Entity child, Entity parent, const std::string& socketName,
                          sf::Vector2f offset, float rotation)
{
    if (!registry.valid(child) || !registry.valid(parent) || socketName.empty())
    {
        return false;
    }

    SocketAttachment attachment;
    attachment.parent = parent;
    attachment.socket = socketName;
    attachment.offset = offset;
    attachment.rotation = rotation;
    registry.emplace_or_replace<SocketAttachment>(child, attachment);
    return true;
}

void SocketSystem::Detach(Registry& registry, Entity child)
{
    if (registry.valid(child))
    {
        registry.remove<SocketAttachment>(child);
    }
}

void SocketSystem::Update(Registry& registry)
{
    auto view = registry.view<SocketAttachment, TransformComponent>();
    for (const Entity child : view)
    {
        SocketAttachment& attachment = view.get<SocketAttachment>(child);
        TransformComponent& transform = view.get<TransformComponent>(child);

        if (!registry.valid(attachment.parent))
        {
            attachment.resolved = false;
            continue;
        }

        const auto* parentTransform = registry.try_get<TransformComponent>(attachment.parent);
        const auto* host = registry.try_get<SocketHost>(attachment.parent);
        const Socket* socket = host ? host->Find(attachment.socket) : nullptr;
        if (!parentTransform || !socket)
        {
            attachment.resolved = false;
            continue;
        }

        SocketWorld base = MakeSocketWorld(*socket, {parentTransform->x, parentTransform->y}, parentTransform->rotation,
                                           {parentTransform->scaleX, parentTransform->scaleY});
        base.position += Rotate(attachment.offset, base.rotation);
        base.rotation += attachment.rotation;

        transform.x = base.position.x;
        transform.y = base.position.y;
        transform.rotation = base.rotation;
        if (attachment.inheritScale)
        {
            transform.scaleX = base.scale.x;
            transform.scaleY = base.scale.y;
        }
        // Attachments are kinematic: interpolating from their previous world
        // position would make a hat lag one frame behind a rotating head.
        transform.prevX = transform.x;
        transform.prevY = transform.y;
        transform.prevRotation = transform.rotation;
        transform.prevScaleX = transform.scaleX;
        transform.prevScaleY = transform.scaleY;
        transform.hasPrev = true;
        attachment.resolved = true;
    }
}

SocketWorld SocketSystem::Evaluate(const Registry& registry, Entity parent, const std::string& socketName,
                                   const SocketWorld& fallback)
{
    if (!registry.valid(parent))
    {
        return fallback;
    }

    const auto* parentTransform = registry.try_get<TransformComponent>(parent);
    const auto* host = registry.try_get<SocketHost>(parent);
    const Socket* socket = host ? host->Find(socketName) : nullptr;
    if (!parentTransform || !socket)
    {
        return fallback;
    }

    return MakeSocketWorld(*socket, {parentTransform->x, parentTransform->y}, parentTransform->rotation,
                           {parentTransform->scaleX, parentTransform->scaleY});
}

} // namespace Fem
