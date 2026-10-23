// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/ECS.hpp"

namespace Fem
{

// GameInstance: global game state, persists across levels.
class GameInstance
{
public:
    static GameInstance& Instance();
    void Initialize();
    void Shutdown();

    // global data
    void Set(const std::string& key, const std::string& value);
    std::string Get(const std::string& key, const std::string& def = "") const;
    bool Has(const std::string& key) const;
    void Clear();

    // level management
    // Empty names are rejected: there is no meaningful "level with no name".
    bool OpenLevel(const std::string& levelName);
    std::string CurrentLevel() const;

    // Invoked after the level name changes; useful for streaming and UI.
    void SetLevelChangedCallback(std::function<void(const std::string&)> cb);

private:
    GameInstance() = default;
    std::map<std::string, std::string> data_;
    std::string currentLevel_;
    std::function<void(const std::string&)> levelChanged_;
};

// GameState: per-level state (score, time, etc.)
class GameState
{
public:
    virtual ~GameState() = default;
    virtual void BeginPlay()
    {
    }
    virtual void EndPlay()
    {
    }

    // Ticks the clock and then the derived state. Paused state does not advance.
    virtual void Tick(float dt);

    void Reset();
    void Pause();
    void Resume();

    int score = 0;
    float elapsedTime = 0;
    bool isGameOver = false;
    bool isPaused = false;
};

// GameMode: rules of the game (win/lose conditions, spawning).
class GameMode
{
public:
    virtual ~GameMode() = default;
    virtual void Init()
    {
    }
    virtual void BeginPlay()
    {
    }
    virtual void EndPlay()
    {
    }
    virtual void Tick(float dt)
    {
    }
    virtual void OnPlayerDeath()
    {
    }
    virtual void OnPlayerWin()
    {
    }
    GameState* gameState = nullptr;
};

// Level: a playable level/scene.
class Level
{
public:
    virtual ~Level() = default;
    virtual void Load()
    {
    }
    virtual void Unload()
    {
    }
    virtual void BeginPlay()
    {
    }
    virtual void Tick(float dt)
    {
    }
    std::string name;
    std::vector<Entity> entities;
};

// Character: base for player/AI characters.
struct CharacterComponent
{
    float health = 100.f;
    float maxHealth = 100.f;
    float speed = 200.f;
    bool isAlive = true;
    std::string characterType;

    // Clamps health into [0, maxHealth] and updates isAlive.
    void ApplyDamage(float amount);
    void Heal(float amount);
};

// PlayerController: handles input for a player.
class PlayerController
{
public:
    virtual ~PlayerController() = default;
    virtual void ProcessInput(float dt)
    {
    }
    virtual void Tick(float dt)
    {
    }
    Entity controlledEntity = entt::null;
    int playerIndex = 0;
};

// HUD: heads-up display.
class HUD
{
public:
    virtual ~HUD() = default;
    virtual void Draw()
    {
    }
    virtual void Tick(float dt)
    {
    }
    bool visible = true;
};

// Camera: view management.
struct CameraComponent
{
    float x = 0, y = 0;
    float zoom = 1.f;
    float rotation = 0;
    bool followTarget = false;
    Entity target = entt::null;
};

} // namespace Fem
