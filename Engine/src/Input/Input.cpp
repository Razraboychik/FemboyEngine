// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Input.hpp"
#include "FemboyEngine/Controller.hpp"
#include "FemboyEngine/Boy.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <limits>

namespace Fem
{

namespace
{
// An action counts as "active" for trigger purposes when it carries a
// non-zero value. Bool actions were the only ones that ever worked before,
// because the old code tested boolValue even for float/vector actions.
bool ActionActive(const InputAction& a)
{
    switch (a.valueType)
    {
        case InputValueType::Bool:
            return a.boolValue;
        case InputValueType::Float:
            return std::abs(a.floatValue) > 0.0001f;
        case InputValueType::Vector2D:
            return std::abs(a.vector2Value.x) > 0.0001f || std::abs(a.vector2Value.y) > 0.0001f;
        case InputValueType::Vector3D:
            return std::abs(a.vector3Value.x) > 0.0001f || std::abs(a.vector3Value.y) > 0.0001f ||
                   std::abs(a.vector3Value.z) > 0.0001f;
    }
    return false;
}

void AppendUtf8(std::string& out, char32_t codepoint)
{
    const auto c = static_cast<std::uint32_t>(codepoint);
    if (c <= 0x7F)
    {
        out.push_back(static_cast<char>(c));
    }
    else if (c <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
    else if (c <= 0xFFFF)
    {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}
} // namespace

Input& Input::Instance()
{
    static Input i;
    return i;
}

void Input::Initialize()
{
    actions_.clear();
    contexts_.clear();
    keysDown_.clear();
    keysPressed_.clear();
    mouseDown_.clear();
    mousePressed_.clear();
    mouseReleased_.clear();
    triggered_.clear();
    heldTime_.clear();
    prevBool_.clear();
    pressedEdge_.clear();
    releasedEdge_.clear();
    pulseTimer_.clear();
    smooth2_.clear();
    smooth1_.clear();
    appliedPriority_.clear();
    inputBuffer_.clear();
    textEntered_.clear();
    currentFrame_ = 0;
    BOY_INFO("Input", "initialized input system");
}

void Input::Update(const sf::Event& e)
{
    if (const auto* kp = e.getIf<sf::Event::KeyPressed>())
    {
        // Ignore auto-repeat so "pressed" stays a real edge.
        if (!keysDown_.count(kp->code))
        {
            keysPressed_.insert(kp->code);
        }
        keysDown_.insert(kp->code);
    }
    if (const auto* kr = e.getIf<sf::Event::KeyReleased>())
    {
        keysDown_.erase(kr->code);
    }
    if (const auto* mb = e.getIf<sf::Event::MouseButtonPressed>())
    {
        if (!mouseDown_.count(mb->button))
        {
            mousePressed_.insert(mb->button);
        }
        mouseDown_.insert(mb->button);
    }
    if (const auto* mr = e.getIf<sf::Event::MouseButtonReleased>())
    {
        mouseDown_.erase(mr->button);
        mouseReleased_.insert(mr->button);
    }
    if (const auto* mm = e.getIf<sf::Event::MouseMoved>())
    {
        mousePos_ = {mm->position.x, mm->position.y};
    }
    if (const auto* ws = e.getIf<sf::Event::MouseWheelScrolled>())
    {
        wheel_ += ws->delta;
    }
    if (const auto* te = e.getIf<sf::Event::TextEntered>())
    {
        // Control characters are handled as editing keys, not as text. This
        // keeps backspace/enter from inserting glyphs into a search field.
        if (te->unicode >= 32 && te->unicode != 127)
        {
            AppendUtf8(textEntered_, te->unicode);
        }
    }
    if (e.is<sf::Event::FocusLost>())
    {
        // No KeyReleased/MouseButtonReleased arrives once the window loses
        // focus, so held state would stick forever. Drop it here.
        keysDown_.clear();
        mouseDown_.clear();
    }
}

void Input::EndFrame()
{
    keysPressed_.clear();
    mousePressed_.clear();
    mouseReleased_.clear();
    triggered_.clear();
    wheel_ = 0.f;
    textEntered_.clear();
    currentFrame_++;
}

bool Input::AnyInputActive() const
{
    if (!keysDown_.empty() || !mouseDown_.empty() || std::abs(wheel_) > 0.0001f)
    {
        return true;
    }

    // Controller/axis input is exposed through mapped actions, so checking the
    // action values covers gamepads without reaching into Controller here.
    for (const auto& [name, action] : actions_)
    {
        if (ActionActive(action))
        {
            return true;
        }
    }
    return false;
}

void Input::Tick(float dt)
{
    if (dt < 0.f)
    {
        dt = 0.f;
    }

    // Remember the previous frame's state before clearing the current values.
    prevBool_.clear();
    for (auto& [name, action] : actions_)
    {
        prevBool_[name] = ActionActive(action);
        action.boolValue = false;
        action.floatValue = 0.f;
        action.vector2Value = {0, 0};
        action.vector3Value = {0, 0, 0};
    }
    appliedPriority_.clear();

    // Higher-priority contexts are evaluated last so they win on shared actions.
    std::vector<const InputMappingContext*> ordered;
    ordered.reserve(contexts_.size());
    for (auto& ctx : contexts_)
    {
        if (ctx.enabled)
        {
            ordered.push_back(&ctx);
        }
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const InputMappingContext* a, const InputMappingContext* b)
                     { return a->priority < b->priority; });

    // Pass 1: sample every mapping. Lower priorities run first, and a mapping
    // may only write an action when no higher-priority mapping already did.
    for (const InputMappingContext* ctx : ordered)
    {
        for (const auto& m : ctx->mappings)
        {
            ProcessMapping(m, dt, std::max(ctx->priority, m.priority));
        }
    }

    // Pass 2: derive edges and held time from the finished values. Doing this
    // before the triggers run means Pressed/Released/Held fire on the frame the
    // input actually happened instead of one frame late.
    UpdateEdges(dt);

    // Pass 3: triggers. They are evaluated once per action: running them per
    // mapping advanced a Pulse timer (and appended to the input buffer) once for
    // every mapping bound to the action, so an action with two keys pulsed at
    // double rate and buffered duplicates.
    {
        std::map<std::string, std::vector<InputTrigger>> byAction;
        std::vector<std::string> order;
        for (const InputMappingContext* ctx : ordered)
        {
            for (const auto& m : ctx->mappings)
            {
                if (m.actionName.empty() || !GetAction(m.actionName))
                {
                    continue;
                }
                auto it = byAction.find(m.actionName);
                if (it == byAction.end())
                {
                    order.push_back(m.actionName);
                    it = byAction.emplace(m.actionName, std::vector<InputTrigger>{}).first;
                }
                it->second.insert(it->second.end(), m.triggers.begin(), m.triggers.end());
            }
        }
        for (const auto& name : order)
        {
            EvaluateTriggers(name, byAction[name], dt);
        }
    }
}

void Input::UpdateEdges(float dt)
{
    for (auto& [name, action] : actions_)
    {
        const bool now = ActionActive(action);
        const bool prev = prevBool_.count(name) ? prevBool_[name] : false;
        pressedEdge_[name] = now && !prev;
        releasedEdge_[name] = !now && prev;
        if (now)
        {
            heldTime_[name] += dt;
        }
        else
        {
            heldTime_[name] = 0.f;
        }
    }
}

void Input::ProcessMapping(const InputMapping& m, float dt, int priority)
{
    if (m.actionName.empty())
    {
        return;
    }
    InputAction* action = GetAction(m.actionName);
    if (!action)
    {
        return;
    }

    // A lower-priority mapping must not clobber a value a higher-priority
    // context already produced this frame.
    const auto it = appliedPriority_.find(m.actionName);
    const int previous = (it == appliedPriority_.end()) ? std::numeric_limits<int>::min() : it->second;
    if (priority < previous)
    {
        return;
    }
    const bool replace = priority > previous;
    appliedPriority_[m.actionName] = priority;

    // Determine the raw value from the bindings. Multiple bindings for the same
    // action accumulate, which is what allows WASD + stick to drive one vector.
    float rawFloat = 0.f;
    sf::Vector2f rawVec2{0, 0};
    sf::Vector3f rawVec3{0, 0, 0};
    bool rawBool = false;

    if (m.keyCode >= 0)
    {
        const auto key = static_cast<sf::Keyboard::Key>(m.keyCode);
        if (IsKeyDown(key))
        {
            rawBool = true;
            rawFloat = 1.f;
            rawVec2 = {1.f, 0.f};
            rawVec3 = {1.f, 0.f, 0.f};
        }
    }
    if (m.mouseButton >= 0)
    {
        if (IsMouseButtonDown(static_cast<sf::Mouse::Button>(m.mouseButton)))
        {
            rawBool = true;
            rawFloat = 1.f;
            rawVec2 = {1.f, 0.f};
            rawVec3 = {1.f, 0.f, 0.f};
        }
    }
    if (m.gamepadButton >= 0)
    {
        if (Controller::Instance().ButtonPressed(0, static_cast<unsigned int>(m.gamepadButton)))
        {
            rawBool = true;
            rawFloat = 1.f;
            rawVec2 = {1.f, 0.f};
            rawVec3 = {1.f, 0.f, 0.f};
        }
    }
    if (m.gamepadAxis >= 0)
    {
        float ax = 0.f;
        switch (m.gamepadAxis)
        {
            case 0:
                ax = Controller::Instance().LeftStickX(0);
                break;
            case 1:
                ax = Controller::Instance().LeftStickY(0);
                break;
            case 2:
                ax = Controller::Instance().RightStickX(0);
                break;
            case 3:
                ax = Controller::Instance().RightStickY(0);
                break;
            default:
                break;
        }
        if (m.gamepadAxisNegative)
        {
            ax = -ax;
        }
        rawFloat += ax;
        rawBool = rawBool || std::abs(ax) > 0.5f;
        if (m.gamepadAxis == 0 || m.gamepadAxis == 2)
        {
            rawVec2.x += ax;
        }
        else
        {
            rawVec2.y += ax;
        }
        if (m.gamepadAxis == 0)
        {
            rawVec3.x += ax;
        }
        else if (m.gamepadAxis == 1)
        {
            rawVec3.y += ax;
        }
        else if (m.gamepadAxis == 2)
        {
            rawVec3.z += ax;
        }
        else if (m.gamepadAxis == 3)
        {
            rawVec3.y += ax;
        }
    }

    // Apply modifiers.
    const std::string modKey = m.actionName + "|" + m.context;
    switch (action->valueType)
    {
        case InputValueType::Bool:
            if (replace)
            {
                action->boolValue = rawBool;
            }
            else
            {
                action->boolValue = action->boolValue || rawBool;
            }
            break;
        case InputValueType::Float:
        {
            const float v = ApplyModifiersFloat(rawFloat, m.modifiers, dt, modKey);
            // Accumulate like the vector types do. Taking the larger magnitude
            // silently dropped a second binding's contribution and, worse, stopped
            // two opposing bindings (a "W"/"S" float axis) from cancelling to zero.
            if (replace)
            {
                action->floatValue = 0.f;
            }
            action->floatValue += v;
            break;
        }
        case InputValueType::Vector2D:
        {
            sf::Vector2f v = rawVec2;
            ApplyModifiers(v, m.modifiers, dt, modKey);
            if (replace)
            {
                action->vector2Value = {0, 0};
            }
            action->vector2Value += v;
            break;
        }
        case InputValueType::Vector3D:
        {
            sf::Vector3f v = rawVec3;
            // 2D modifiers operate on the XY plane; scale/clamp apply to all axes.
            sf::Vector2f xy{v.x, v.y};
            ApplyModifiers(xy, m.modifiers, dt, modKey);
            v.x = xy.x;
            v.y = xy.y;
            for (const auto& mod : m.modifiers)
            {
                if (mod.type == InputModifierType::Scale)
                {
                    v.z *= mod.scale;
                }
                else if (mod.type == InputModifierType::Negate && mod.negateZ)
                {
                    v.z = -v.z;
                }
                else if (mod.type == InputModifierType::Clamp)
                {
                    v.z = std::max(mod.clampMin, std::min(mod.clampMax, v.z));
                }
                else if (mod.type == InputModifierType::Swizzle)
                {
                    v = Swizzle(v, mod.swizzleOrder);
                }
            }
            if (replace)
            {
                action->vector3Value = {0, 0, 0};
            }
            action->vector3Value += v;
            break;
        }
    }
}

void Input::EvaluateTriggers(const std::string& name, const std::vector<InputTrigger>& triggers, float dt)
{
    if (name.empty())
    {
        return;
    }
    InputAction* action = GetAction(name);
    if (!action)
    {
        return;
    }

    const bool down = ActionActive(*action);
    const bool pressed = pressedEdge_.count(name) ? pressedEdge_[name] : false;
    const bool released = releasedEdge_.count(name) ? releasedEdge_[name] : false;
    const float held = heldTime_.count(name) ? heldTime_[name] : 0.f;

    for (const auto& trig : triggers)
    {
        bool fired = false;
        switch (trig.type)
        {
            case InputTriggerType::Pressed:
                fired = pressed;
                break;
            case InputTriggerType::Released:
                fired = released;
                break;
            case InputTriggerType::Down:
                fired = down;
                break;
            case InputTriggerType::Held:
                fired = down && held >= trig.heldThreshold;
                break;
            case InputTriggerType::Tap:
                fired = released && held > 0.f && held < trig.tapThreshold;
                break;
            case InputTriggerType::Pulse:
            {
                if (!down)
                {
                    pulseTimer_[name] = 0.f;
                    break;
                }
                // A zero/negative interval would fire every frame; keep it sane.
                const float interval = std::max(0.01f, trig.pulseInterval);
                pulseTimer_[name] += dt;
                if (pulseTimer_[name] >= interval)
                {
                    // Keep the remainder so the cadence survives frame jitter.
                    pulseTimer_[name] = std::fmod(pulseTimer_[name], interval);
                    fired = true;
                }
                break;
            }
            case InputTriggerType::Chorded:
            {
                if (!down)
                {
                    break;
                }
                bool all = true;
                for (const auto& ca : trig.chordedActions)
                {
                    const InputAction* other = GetAction(ca);
                    if (!other || !ActionActive(*other))
                    {
                        all = false;
                        break;
                    }
                }
                fired = all;
                break;
            }
        }
        if (fired)
        {
            triggered_[name][trig.type] = true;
        }
    }

    // Input buffering: record a fresh press once.
    if (bufferSize_ > 0 && pressed)
    {
        inputBuffer_.push_back({name, currentFrame_});
        while (static_cast<int>(inputBuffer_.size()) > bufferSize_)
        {
            inputBuffer_.erase(inputBuffer_.begin());
        }
    }
}

InputAction& Input::CreateAction(const std::string& name, InputValueType type)
{
    InputAction& a = actions_[name];
    a.name = name;
    a.valueType = type;
    a.boolValue = false;
    a.floatValue = 0.f;
    a.vector2Value = {0, 0};
    a.vector3Value = {0, 0, 0};
    return a;
}

InputAction* Input::GetAction(const std::string& name)
{
    auto it = actions_.find(name);
    return it == actions_.end() ? nullptr : &it->second;
}

const InputAction* Input::GetAction(const std::string& name) const
{
    auto it = actions_.find(name);
    return it == actions_.end() ? nullptr : &it->second;
}

void Input::AddContext(const InputMappingContext& ctx)
{
    // Stamp the owning context name onto each mapping. The Smooth modifier keys
    // its state by action+context, and an empty context made two contexts that
    // share an action name share one smoothing state.
    InputMappingContext normalized = ctx;
    for (auto& m : normalized.mappings)
    {
        if (m.context.empty())
        {
            m.context = normalized.name;
        }
    }

    // Replace an existing context with the same name instead of duplicating it.
    for (auto& c : contexts_)
    {
        if (c.name == normalized.name)
        {
            c = std::move(normalized);
            return;
        }
    }
    contexts_.push_back(std::move(normalized));
}

void Input::RemoveContext(const std::string& name)
{
    for (size_t i = 0; i < contexts_.size(); ++i)
    {
        if (contexts_[i].name == name)
        {
            contexts_.erase(contexts_.begin() + i);
            return;
        }
    }
}

void Input::EnableContext(const std::string& name, bool enabled)
{
    for (auto& c : contexts_)
    {
        if (c.name == name)
        {
            c.enabled = enabled;
        }
    }
}

bool Input::IsContextEnabled(const std::string& name) const
{
    for (const auto& c : contexts_)
    {
        if (c.name == name)
        {
            return c.enabled;
        }
    }
    return false;
}

bool Input::GetBool(const std::string& action) const
{
    const InputAction* a = GetAction(action);
    return a ? ActionActive(*a) : false;
}

float Input::GetFloat(const std::string& action) const
{
    const InputAction* a = GetAction(action);
    return a ? a->floatValue : 0.f;
}

sf::Vector2f Input::GetVector2(const std::string& action) const
{
    const InputAction* a = GetAction(action);
    return a ? a->vector2Value : sf::Vector2f{0, 0};
}

sf::Vector3f Input::GetVector3(const std::string& action) const
{
    const InputAction* a = GetAction(action);
    return a ? a->vector3Value : sf::Vector3f{0, 0, 0};
}

bool Input::WasTriggered(const std::string& action, InputTriggerType trigger) const
{
    auto it = triggered_.find(action);
    if (it == triggered_.end())
    {
        return false;
    }
    auto it2 = it->second.find(trigger);
    return it2 != it->second.end() && it2->second;
}

bool Input::WasPressed(const std::string& action) const
{
    if (WasTriggered(action, InputTriggerType::Pressed))
    {
        return true;
    }
    auto it = pressedEdge_.find(action);
    return it != pressedEdge_.end() && it->second;
}

bool Input::WasReleased(const std::string& action) const
{
    if (WasTriggered(action, InputTriggerType::Released))
    {
        return true;
    }
    auto it = releasedEdge_.find(action);
    return it != releasedEdge_.end() && it->second;
}

bool Input::IsHeld(const std::string& action) const
{
    if (WasTriggered(action, InputTriggerType::Held))
    {
        return true;
    }
    if (WasTriggered(action, InputTriggerType::Down))
    {
        return true;
    }
    return GetBool(action);
}

void Input::SetBufferSize(int frames)
{
    bufferSize_ = std::max(0, frames);
    while (static_cast<int>(inputBuffer_.size()) > bufferSize_)
    {
        inputBuffer_.erase(inputBuffer_.begin());
    }
}

bool Input::ConsumeBufferedInput(const std::string& action)
{
    for (size_t i = 0; i < inputBuffer_.size(); ++i)
    {
        if (inputBuffer_[i].action == action)
        {
            inputBuffer_.erase(inputBuffer_.begin() + i);
            return true;
        }
    }
    return false;
}

void Input::Rebind(const std::string& actionName, int newKey)
{
    for (auto& ctx : contexts_)
    {
        for (auto& m : ctx.mappings)
        {
            if (m.actionName != actionName)
            {
                continue;
            }
            // Rebinding must actually replace the binding. Leaving the old
            // mouse/gamepad binding live meant the new key never took effect
            // for actions that had been bound to a mouse button or pad.
            m.keyCode = newKey;
            m.mouseButton = -1;
            m.gamepadButton = -1;
            m.gamepadAxis = -1;
            m.gamepadAxisNegative = false;
        }
    }
}

bool Input::SaveBindings(const std::string& path) const
{
    nlohmann::json j = nlohmann::json::object();
    for (const auto& ctx : contexts_)
    {
        nlohmann::json cj = nlohmann::json::array();
        for (const auto& m : ctx.mappings)
        {
            cj.push_back({{"action", m.actionName},
                          {"key", m.keyCode},
                          {"mouse", m.mouseButton},
                          {"gpBtn", m.gamepadButton},
                          {"gpAxis", m.gamepadAxis},
                          {"gpAxisNeg", m.gamepadAxisNegative},
                          {"priority", m.priority}});
        }
        j[ctx.name] = cj;
    }

    std::ofstream f(path);
    if (!f.is_open())
    {
        BOY_ERROR("Input", "cannot write bindings: " + path);
        return false;
    }
    f << j.dump(4);
    f.flush();
    if (!f)
    {
        BOY_ERROR("Input", "bindings write failed: " + path);
        return false;
    }
    BOY_INFO("Input", "bindings saved: " + path);
    return true;
}

bool Input::LoadBindings(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        BOY_WARN("Input", "cannot open bindings: " + path);
        return false;
    }

    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (const std::exception& e)
    {
        BOY_ERROR("Input", std::string("bindings parse error: ") + e.what());
        return false;
    }
    if (!j.is_object())
    {
        return false;
    }

    for (const auto& [ctxName, mappings] : j.items())
    {
        if (!mappings.is_array())
        {
            continue;
        }
        for (const auto& m : mappings)
        {
            if (!m.is_object())
            {
                continue;
            }
            if (!m.contains("action") || !m["action"].is_string())
            {
                continue;
            }
            const std::string action = m["action"].get<std::string>();

            // Each value is type-checked: json::value() throws on a type
            // mismatch, which used to escape the parse try/catch above and
            // abort the frame when a bindings file was hand-edited.
            const auto readInt = [&](const char* key, int def)
            {
                if (!m.contains(key) || !m[key].is_number_integer())
                {
                    return def;
                }
                return m[key].get<int>();
            };
            const auto readBool = [&](const char* key, bool def)
            {
                if (!m.contains(key) || !m[key].is_boolean())
                {
                    return def;
                }
                return m[key].get<bool>();
            };

            for (auto& ctx : contexts_)
            {
                if (ctx.name != ctxName)
                {
                    continue;
                }
                for (auto& mm : ctx.mappings)
                {
                    if (mm.actionName != action)
                    {
                        continue;
                    }
                    mm.keyCode = readInt("key", mm.keyCode);
                    mm.mouseButton = readInt("mouse", mm.mouseButton);
                    mm.gamepadButton = readInt("gpBtn", mm.gamepadButton);
                    mm.gamepadAxis = readInt("gpAxis", mm.gamepadAxis);
                    mm.gamepadAxisNegative = readBool("gpAxisNeg", mm.gamepadAxisNegative);
                    // saveBindings() writes "priority" but the loader ignored it,
                    // so a round trip silently reset every mapping's priority.
                    mm.priority = readInt("priority", mm.priority);
                    if (mm.context.empty())
                    {
                        mm.context = ctx.name;
                    }
                }
            }
        }
    }
    BOY_INFO("Input", "bindings loaded: " + path);
    return true;
}

bool Input::IsKeyDown(sf::Keyboard::Key k) const
{
    return keysDown_.count(k) > 0;
}
bool Input::IsKeyPressed(sf::Keyboard::Key k) const
{
    return keysPressed_.count(k) > 0;
}
bool Input::IsMouseButtonDown(sf::Mouse::Button b) const
{
    return mouseDown_.count(b) > 0;
}

bool Input::IsMouseButtonPressed(sf::Mouse::Button b) const
{
    return mousePressed_.count(b) > 0;
}

sf::Vector3f Input::Swizzle(const sf::Vector3f& v, int order)
{
    switch (order)
    {
        case 1:
            return {v.x, v.z, v.y};
        case 2:
            return {v.y, v.x, v.z};
        case 3:
            return {v.y, v.z, v.x};
        case 4:
            return {v.z, v.x, v.y};
        case 5:
            return {v.z, v.y, v.x};
        default:
            return v;
    }
}

void Input::ApplyModifiers(sf::Vector2f& v, const std::vector<InputModifier>& mods, float dt, const std::string& key)
{
    for (size_t i = 0; i < mods.size(); ++i)
    {
        const auto& m = mods[i];
        switch (m.type)
        {
            case InputModifierType::DeadZone:
                v = ApplyDeadZone2D(v, m.deadZone);
                break;
            case InputModifierType::Negate:
                if (m.negateX)
                {
                    v.x = -v.x;
                }
                if (m.negateY)
                {
                    v.y = -v.y;
                }
                break;
            case InputModifierType::Scale:
                v.x *= m.scale;
                v.y *= m.scale;
                break;
            case InputModifierType::Swizzle:
                v = sf::Vector2f(v.y, v.x);
                break;
            case InputModifierType::Smooth:
            {
                const std::string k = key + "#" + std::to_string(i);
                sf::Vector2f& prev = smooth2_[k];
                const float t = std::clamp(m.smoothSpeed * dt, 0.f, 1.f);
                prev += (v - prev) * t;
                v = prev;
                break;
            }
            case InputModifierType::Clamp:
                v.x = std::max(m.clampMin, std::min(m.clampMax, v.x));
                v.y = std::max(m.clampMin, std::min(m.clampMax, v.y));
                break;
            default:
                break;
        }
    }
}

float Input::ApplyModifiersFloat(float v, const std::vector<InputModifier>& mods, float dt, const std::string& key)
{
    for (size_t i = 0; i < mods.size(); ++i)
    {
        const auto& m = mods[i];
        switch (m.type)
        {
            case InputModifierType::DeadZone:
                v = ApplyDeadZone(v, m.deadZone);
                break;
            case InputModifierType::Negate:
                v = -v;
                break;
            case InputModifierType::Scale:
                v *= m.scale;
                break;
            case InputModifierType::Smooth:
            {
                const std::string k = key + "#" + std::to_string(i);
                float& prev = smooth1_[k];
                const float t = std::clamp(m.smoothSpeed * dt, 0.f, 1.f);
                prev += (v - prev) * t;
                v = prev;
                break;
            }
            case InputModifierType::Clamp:
                v = std::max(m.clampMin, std::min(m.clampMax, v));
                break;
            default:
                break;
        }
    }
    return v;
}

float Input::ApplyDeadZone(float value, float threshold)
{
    threshold = std::clamp(threshold, 0.f, 1.f);
    if (std::abs(value) < threshold)
    {
        return 0.f;
    }
    if (threshold >= 1.f)
    {
        return 0.f;
    }
    // Rescale so the value ramps from 0 at the threshold to 1 at full tilt.
    const float sign = value < 0.f ? -1.f : 1.f;
    const float mag = (std::abs(value) - threshold) / (1.f - threshold);
    return sign * std::clamp(mag, 0.f, 1.f);
}

sf::Vector2f Input::ApplyDeadZone2D(const sf::Vector2f& v, float threshold)
{
    threshold = std::clamp(threshold, 0.f, 1.f);
    const float mag = std::sqrt(v.x * v.x + v.y * v.y);
    if (mag < threshold || mag <= 0.000001f)
    {
        return {0, 0};
    }
    if (threshold >= 1.f)
    {
        return {0, 0};
    }
    const float scale = (mag - threshold) / (1.f - threshold);
    const float inv = std::clamp(scale, 0.f, 1.f) / mag;
    return {v.x * inv, v.y * inv};
}

} // namespace Fem
