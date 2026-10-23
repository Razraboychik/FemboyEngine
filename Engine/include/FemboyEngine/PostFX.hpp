// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <string>
#include <map>
#include <memory>

namespace Fem
{

// Post-processing effects using SFML shaders.
class PostFX
{
public:
    static PostFX& Instance();
    bool LoadShader(const std::string& name, const std::string& fragPath);
    void Apply(const std::string& name, sf::RenderTarget& target, const sf::Texture& input);
    void SetParam(const std::string& shader, const std::string& param, float v);
    void SetParam(const std::string& shader, const std::string& param, const sf::Vector2f& v);
    bool Has(const std::string& name) const;
    void Remove(const std::string& name);
    void Clear();

private:
    PostFX() = default;
    struct ShaderEntry
    {
        std::unique_ptr<sf::Shader> shader;
        std::map<std::string, float> floats;
        std::map<std::string, sf::Vector2f> vecs;
    };
    std::map<std::string, ShaderEntry> shaders_;
};

} // namespace Fem
