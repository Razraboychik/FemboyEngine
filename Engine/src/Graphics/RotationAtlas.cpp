// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/RotationAtlas.hpp"

#include "FemboyEngine/Boy.hpp"

#include <algorithm>
#include <cmath>

namespace Fem
{

RotationAtlas& RotationAtlas::Instance()
{
    static RotationAtlas atlas;
    return atlas;
}

void RotationAtlas::SetAngleCount(int angles)
{
    angles_ = std::clamp(angles, 4, 128);
    Clear();
}

int RotationAtlas::FrameIndex(float degrees, int angles)
{
    if (angles <= 1)
    {
        return 0;
    }
    const float normalized = std::fmod(degrees, 360.f);
    const float positive = normalized < 0.f ? normalized + 360.f : normalized;
    int index = static_cast<int>(std::floor(positive / 360.f * static_cast<float>(angles) + 0.5f));
    return index % angles;
}

bool RotationAtlas::Bake(Atlas& atlas, const sf::Texture& sourceTexture, const sf::FloatRect& source, int angles)
{
    if (angles < 2 || sourceTexture.getSize().x == 0 || sourceTexture.getSize().y == 0)
    {
        return false;
    }

    sf::FloatRect src = source;
    if (src.size.x <= 0.f || src.size.y <= 0.f)
    {
        src = sf::FloatRect(
            {0.f, 0.f}, {static_cast<float>(sourceTexture.getSize().x), static_cast<float>(sourceTexture.getSize().y)});
    }

    const int w = std::max(1, static_cast<int>(std::ceil(src.size.x)));
    const int h = std::max(1, static_cast<int>(std::ceil(src.size.y)));
    const int cell = std::max(w, h) + static_cast<int>(std::ceil(std::sqrt(2.0) * 2.0));
    const int diagonal = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(w * w + h * h)))) + 2;
    const int size = std::max(cell, diagonal);

    sf::RenderTexture render;
    if (!render.resize({static_cast<unsigned>(size), static_cast<unsigned>(size)}))
    {
        BOY_WARN("RotationAtlas", "cannot allocate render texture; using the source texture");
        return false;
    }
    render.setSmooth(false);

    sf::Sprite sprite(sourceTexture);
    sprite.setOrigin({src.position.x + src.size.x * 0.5f, src.position.y + src.size.y * 0.5f});
    sprite.setPosition({static_cast<float>(size) * 0.5f, static_cast<float>(size) * 0.5f});

    sf::Image sheet;
    sheet.resize({static_cast<unsigned>(size * angles), static_cast<unsigned>(size)}, sf::Color::Transparent);

    for (int i = 0; i < angles; ++i)
    {
        render.clear(sf::Color::Transparent);
        sprite.setRotation(sf::degrees(360.f * static_cast<float>(i) / static_cast<float>(angles)));
        render.draw(sprite);
        render.display();

        const sf::Image image = render.getTexture().copyToImage();
        if (!sheet.copy(image, {static_cast<unsigned>(i * size), 0u}))
        {
            BOY_WARN("RotationAtlas", "could not copy a rotation frame into the atlas");
            return false;
        }
    }

    if (!atlas.texture.resize({static_cast<unsigned>(size * angles), static_cast<unsigned>(size)}))
    {
        return false;
    }
    atlas.texture.setSmooth(false);
    atlas.texture.update(sheet);

    atlas.angles = angles;
    atlas.cell = size;
    atlas.frames.clear();
    atlas.frames.reserve(static_cast<std::size_t>(angles));
    for (int i = 0; i < angles; ++i)
    {
        atlas.frames.emplace_back(sf::Vector2f(static_cast<float>(i * size), 0.f),
                                  sf::Vector2f(static_cast<float>(size), static_cast<float>(size)));
    }
    return true;
}

RotationAtlas::Frame RotationAtlas::MakeFrame(const std::string& textureId, const sf::Texture& texture,
                                              const sf::FloatRect& source, float degrees, int angles)
{
    const int count = std::clamp(angles > 0 ? angles : angles_, 4, 128);
    const sf::Vector2f texSize(static_cast<float>(texture.getSize().x), static_cast<float>(texture.getSize().y));
    sf::FloatRect src = source;
    if (src.size.x <= 0.f || src.size.y <= 0.f)
    {
        src = sf::FloatRect({0.f, 0.f}, texSize);
    }

    // The imaged source is part of the key: a hot-reloaded texture can reuse an
    // id while its contents (and therefore its pixels) change.
    const std::string key = textureId + "|" + std::to_string(reinterpret_cast<std::uintptr_t>(&texture)) + "|" +
                            std::to_string(static_cast<int>(src.position.x)) + "," +
                            std::to_string(static_cast<int>(src.position.y)) + "," +
                            std::to_string(static_cast<int>(src.size.x)) + "," +
                            std::to_string(static_cast<int>(src.size.y)) + "|" + std::to_string(count);

    auto it = atlases_.find(key);
    if (it == atlases_.end())
    {
        Atlas atlas;
        if (!Bake(atlas, texture, src, count))
        {
            // Cache the failure as an empty atlas; otherwise every frame would
            // retry a RenderTexture allocation in a headless session.
            Atlas fallback;
            fallback.angles = 0;
            it = atlases_.emplace(key, std::move(fallback)).first;
        }
        else
        {
            it = atlases_.emplace(key, std::move(atlas)).first;
        }
    }

    if (it->second.angles <= 1 || it->second.frames.empty())
    {
        Frame fallback;
        fallback.texture = &texture;
        fallback.source = src;
        fallback.origin = {0.5f, 0.5f};
        fallback.baked = false;
        return fallback;
    }

    const int index = FrameIndex(degrees, it->second.angles);
    Frame result;
    result.texture = &it->second.texture;
    result.source = it->second.frames[static_cast<std::size_t>(index)];
    result.origin = {0.5f, 0.5f};
    result.baked = true;
    return result;
}

void RotationAtlas::Clear()
{
    atlases_.clear();
}

} // namespace Fem
