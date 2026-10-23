// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Window.hpp>
#include <string>
#include <vector>
#include <map>
#include "FemboyEngine/Core.hpp"

namespace Fem
{

enum class ControllerButton
{
    A,
    B,
    X,
    Y,
    LB,
    RB,
    Back,
    Start,
    LStick,
    RStick,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Guide
};

class Controller
{
public:
    static Controller& Instance();
    void Update();
    // detection
    int ControllerCount() const;
    bool IsConnected(int id) const;
    std::string Name(int id) const;
    // axes
    float Axis(int id, sf::Joystick::Axis axis) const;
    float LeftStickX(int id) const;
    float LeftStickY(int id) const;
    float RightStickX(int id) const;
    float RightStickY(int id) const;
    float LeftTrigger(int id) const;
    float RightTrigger(int id) const;
    // Trigger values are normalized [0,1] for analog triggers
    // buttons
    bool ButtonPressed(int id, unsigned int button) const;
    bool ButtonJustPressed(int id, unsigned int button) const;
    // action mapping (gamepad)
    void BindAction(const std::string& action, int button);
    bool IsActionDown(const std::string& action, int controllerId = 0) const;
    bool IsActionPressed(const std::string& action, int controllerId = 0) const;
    void EndFrame();

private:
    Controller() = default;
    std::map<std::string, unsigned int> actionMap_;
    std::vector<std::vector<bool>> prevButtons_;
    std::vector<std::vector<bool>> currButtons_;
    void EnsureSize(int id);
};

} // namespace Fem
