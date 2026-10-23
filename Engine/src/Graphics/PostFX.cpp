// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/PostFX.hpp"
#include "FemboyEngine/Boy.hpp"

namespace Fem
{

PostFX& PostFX::Instance()
{
    static PostFX p;
    return p;
}

bool PostFX::LoadShader(const std::string& name, const std::string& fragPath)
{
    if (name.empty())
    {
        BOY_ERROR("PostFX", "shader name must not be empty");
        return false;
    }

    auto shader = std::make_unique<sf::Shader>();
    if (!shader->loadFromFile(fragPath, sf::Shader::Type::Fragment))
    {
        BOY_ERROR("PostFX", "failed to load shader: " + fragPath);
        return false;
    }

    ShaderEntry e;
    e.shader = std::move(shader);

    // Preserve parameters already set for this name (hot reload use case).
    auto existing = shaders_.find(name);
    if (existing != shaders_.end())
    {
        e.floats = existing->second.floats;
        e.vecs = existing->second.vecs;
        for (const auto& [k, v] : e.floats)
        {
            e.shader->setUniform(k, v);
        }
        for (const auto& [k, v] : e.vecs)
        {
            e.shader->setUniform(k, v);
        }
        existing->second = std::move(e);
    }
    else
    {
        shaders_.emplace(name, std::move(e));
    }

    BOY_INFO("PostFX", "loaded shader: " + name);
    return true;
}

void PostFX::SetParam(const std::string& shader, const std::string& param, float v)
{
    auto it = shaders_.find(shader);
    if (it == shaders_.end() || !it->second.shader)
    {
        return;
    }
    it->second.floats[param] = v;
    it->second.shader->setUniform(param, v);
}

void PostFX::SetParam(const std::string& shader, const std::string& param, const sf::Vector2f& v)
{
    auto it = shaders_.find(shader);
    if (it == shaders_.end() || !it->second.shader)
    {
        return;
    }
    it->second.vecs[param] = v;
    it->second.shader->setUniform(param, v);
}

bool PostFX::Has(const std::string& name) const
{
    return shaders_.count(name) > 0;
}

void PostFX::Remove(const std::string& name)
{
    shaders_.erase(name);
}
void PostFX::Clear()
{
    shaders_.clear();
}

void PostFX::Apply(const std::string& name, sf::RenderTarget& target, const sf::Texture& input)
{
    auto it = shaders_.find(name);
    if (it == shaders_.end() || !it->second.shader)
    {
        return;
    }

    // Re-apply every stored parameter: a shader recompiled or re-bound loses the
    // uniforms that were set earlier.
    for (const auto& [k, v] : it->second.floats)
    {
        it->second.shader->setUniform(k, v);
    }
    for (const auto& [k, v] : it->second.vecs)
    {
        it->second.shader->setUniform(k, v);
    }

    sf::Sprite sp(input);
    sf::RenderStates states;
    states.shader = it->second.shader.get();
    target.draw(sp, states);
}

} // namespace Fem
