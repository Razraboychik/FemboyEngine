// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <string>
#include <map>
#include <vector>
#include <set>
#include <functional>
#include <nlohmann/json.hpp>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/FemObject.hpp"

namespace Fem
{

// Engine-facing names for raw input codes. They alias the windowing library's
// enums so a game binds keys without including a windowing header of its own,
// and so the engine can swap that library without touching gameplay code.
using Key = sf::Keyboard::Key;
using MouseButton = sf::Mouse::Button;

// Input System
// Action/context based input with modifiers and triggers.
// Raw events are fed through Update(), actions are evaluated once per frame in
// Tick(), and per-frame trigger flags are cleared in endFrame().

enum class InputValueType
{
    Bool,
    Float,
    Vector2D,
    Vector3D
};
enum class InputTriggerType
{
    Pressed,
    Released,
    Held,
    Tap,
    Chorded,
    Pulse,
    Down
};
enum class InputModifierType
{
    None,
    DeadZone,
    Negate,
    Swizzle,
    Scale,
    Smooth,
    Clamp
};

struct InputAction
{
    std::string name;
    InputValueType valueType = InputValueType::Bool;
    // runtime value
    bool boolValue = false;
    float floatValue = 0.f;
    sf::Vector2f vector2Value{0, 0};
    sf::Vector3f vector3Value{0, 0, 0};
};

struct InputModifier
{
    InputModifierType type = InputModifierType::None;
    float scale = 1.f;
    float deadZone = 0.1f;
    bool negateX = false, negateY = false, negateZ = false;
    int swizzleOrder = 0; // 0=XYZ, 1=XZY, 2=YXZ, 3=YZX, 4=ZXY, 5=ZYX
    float smoothSpeed = 10.f;
    float clampMin = -1.f, clampMax = 1.f;
};

struct InputTrigger
{
    InputTriggerType type = InputTriggerType::Pressed;
    float heldThreshold = 0.5f;              // seconds for Held
    float tapThreshold = 0.2f;               // seconds for Tap
    float pulseInterval = 0.5f;              // seconds for Pulse
    std::vector<std::string> chordedActions; // for Chorded
};

struct InputMapping
{
    std::string actionName;
    // binding
    int keyCode = -1;       // keyboard key (-1 = none)
    int mouseButton = -1;   // mouse button
    int gamepadButton = -1; // gamepad button
    int gamepadAxis = -1;   // gamepad axis (0=LX, 1=LY, 2=RX, 3=RY)
    bool gamepadAxisNegative = false;
    // modifiers and triggers
    std::vector<InputModifier> modifiers;
    std::vector<InputTrigger> triggers;
    // context
    std::string context;
    int priority = 0;
};

struct InputMappingContext
{
    std::string name;
    std::vector<InputMapping> mappings;
    bool enabled = true;
    int priority = 0;
};

class Input : public FemObject
{
public:
    FEMCLASS_NO_CREATE(Input, FemObject, false)
    static Input& Instance();


    // lifecycle
    void Initialize();
    void Update(const sf::Event& e);
    void EndFrame();
    void Tick(float dt);

    // action registration
    InputAction& CreateAction(const std::string& name, InputValueType type = InputValueType::Bool);
    InputAction* GetAction(const std::string& name);
    const InputAction* GetAction(const std::string& name) const;

    // mapping contexts
    void AddContext(const InputMappingContext& ctx);
    void RemoveContext(const std::string& name);
    void EnableContext(const std::string& name, bool enabled);
    bool IsContextEnabled(const std::string& name) const;

    // value queries
    bool GetBool(const std::string& action) const;
    float GetFloat(const std::string& action) const;
    sf::Vector2f GetVector2(const std::string& action) const;
    sf::Vector3f GetVector3(const std::string& action) const;

    // trigger queries
    bool WasTriggered(const std::string& action, InputTriggerType trigger) const;
    bool WasPressed(const std::string& action) const;
    bool WasReleased(const std::string& action) const;
    bool IsHeld(const std::string& action) const;

    // input buffering (for fighting games)
    void SetBufferSize(int frames);
    bool ConsumeBufferedInput(const std::string& action);

    // rebinding
    void Rebind(const std::string& actionName, int newKey);
    bool SaveBindings(const std::string& path) const;
    bool LoadBindings(const std::string& path);

    // raw input (low-level)
    bool IsKeyDown(sf::Keyboard::Key k) const;
    bool IsKeyPressed(sf::Keyboard::Key k) const;
    bool IsMouseButtonDown(sf::Mouse::Button b) const;
    bool IsMouseButtonPressed(sf::Mouse::Button b) const;
    // True while any key, mouse button, wheel delta or mapped action is active.
    // Used by engine display comfort to distinguish "watching" from "playing";
    // mouse movement alone is handled by the event wake-up in Engine.
    bool AnyInputActive() const;
    sf::Vector2i MousePosition() const
    {
        return mousePos_;
    }
    // Accumulated wheel delta for this frame, in notches (positive = up). Reset
    // by endFrame(), like every other per-frame edge.
    float MouseWheel() const
    {
        return wheel_;
    }
    // UTF-8 characters delivered by TextEntered events since the previous
    // EndFrame(). Used by editable text fields; gameplay actions are separate.
    const std::string& TextEntered() const
    {
        return textEntered_;
    }

    // dead zone helpers
    static float ApplyDeadZone(float value, float threshold);
    static sf::Vector2f ApplyDeadZone2D(const sf::Vector2f& v, float threshold);

private:
    Input() = default;
    void ProcessMapping(const InputMapping& m, float dt, int priority);
    void EvaluateTriggers(const std::string& name, const std::vector<InputTrigger>& triggers, float dt);
    void UpdateEdges(float dt);
    void ApplyModifiers(sf::Vector2f& v, const std::vector<InputModifier>& mods, float dt, const std::string& key);
    float ApplyModifiersFloat(float v, const std::vector<InputModifier>& mods, float dt, const std::string& key);
    static sf::Vector3f Swizzle(const sf::Vector3f& v, int order);

    std::map<std::string, InputAction> actions_;
    std::vector<InputMappingContext> contexts_;

    // raw state
    std::set<sf::Keyboard::Key> keysDown_;
    std::set<sf::Keyboard::Key> keysPressed_;
    std::set<sf::Mouse::Button> mouseDown_;
    std::set<sf::Mouse::Button> mousePressed_;
    std::set<sf::Mouse::Button> mouseReleased_;
    sf::Vector2i mousePos_{0, 0};
    float wheel_ = 0.f;
    std::string textEntered_;

    // trigger state
    std::map<std::string, std::map<InputTriggerType, bool>> triggered_;
    std::map<std::string, float> heldTime_;
    std::map<std::string, bool> prevBool_;
    std::map<std::string, bool> pressedEdge_;
    std::map<std::string, bool> releasedEdge_;
    std::map<std::string, float> pulseTimer_;
    // Highest mapping priority that wrote an action this frame. Lower-priority
    // contexts must not overwrite a value produced by a higher-priority one.
    std::map<std::string, int> appliedPriority_;

    // modifier smoothing state (keyed by action + modifier index)
    std::map<std::string, sf::Vector2f> smooth2_;
    std::map<std::string, float> smooth1_;

    // input buffer
    int bufferSize_ = 0;
    struct BufferedInput
    {
        std::string action;
        int frame;
    };
    std::vector<BufferedInput> inputBuffer_;
    int currentFrame_ = 0;
};

} // namespace Fem
