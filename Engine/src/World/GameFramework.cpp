// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/GameFramework.hpp"
#include "FemboyEngine/Boy.hpp"
#include <algorithm>
#include <cmath>

namespace Fem
{

GameInstance& GameInstance::Instance()
{
    static GameInstance g;
    return g;
}

void GameInstance::Initialize()
{
    BOY_INFO("GameInstance", "initialized");
}

void GameInstance::Shutdown()
{
    data_.clear();
    currentLevel_.clear();
    levelChanged_ = nullptr;
    BOY_INFO("GameInstance", "shutdown");
}

void GameInstance::Set(const std::string& key, const std::string& value)
{
    if (key.empty())
    {
        BOY_WARN("GameInstance", "set: empty key ignored");
        return;
    }
    data_[key] = value;
}

std::string GameInstance::Get(const std::string& key, const std::string& def) const
{
    auto it = data_.find(key);
    return it == data_.end() ? def : it->second;
}

bool GameInstance::Has(const std::string& key) const
{
    return data_.find(key) != data_.end();
}

void GameInstance::Clear()
{
    data_.clear();
}

bool GameInstance::OpenLevel(const std::string& levelName)
{
    if (levelName.empty())
    {
        BOY_WARN("GameInstance", "openLevel: empty level name rejected");
        return false;
    }
    if (levelName == currentLevel_)
    {
        BOY_DEBUG("GameInstance", "openLevel: already in " + levelName);
        return true;
    }

    const std::string previous = currentLevel_;
    currentLevel_ = levelName;
    BOY_INFO("GameInstance", "opening level: " + levelName + " (from " + (previous.empty() ? "none" : previous) + ")");

    // Copy the callback: it may reset itself while running.
    auto cb = levelChanged_;
    if (cb)
    {
        cb(currentLevel_);
    }
    return true;
}

std::string GameInstance::CurrentLevel() const
{
    return currentLevel_;
}

void GameInstance::SetLevelChangedCallback(std::function<void(const std::string&)> cb)
{
    levelChanged_ = std::move(cb);
}

void GameState::Tick(float dt)
{
    if (isPaused || isGameOver)
    {
        return;
    }
    if (!std::isfinite(dt) || dt <= 0.f)
    {
        return;
    }
    elapsedTime += dt;
}

void GameState::Reset()
{
    score = 0;
    elapsedTime = 0.f;
    isGameOver = false;
    isPaused = false;
}

void GameState::Pause()
{
    isPaused = true;
}

void GameState::Resume()
{
    isPaused = false;
}

void CharacterComponent::ApplyDamage(float amount)
{
    if (!std::isfinite(amount) || amount <= 0.f)
    {
        return;
    }
    health = std::clamp(health - amount, 0.f, std::max(0.f, maxHealth));
    isAlive = health > 0.f;
}

void CharacterComponent::Heal(float amount)
{
    if (!std::isfinite(amount) || amount <= 0.f)
    {
        return;
    }
    health = std::clamp(health + amount, 0.f, std::max(0.f, maxHealth));
    isAlive = health > 0.f;
}

} // namespace Fem
