// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Controller.hpp"
#include <cstring>
#include <algorithm>

namespace Fem
{

Controller& Controller::Instance()
{
    static Controller c;
    return c;
}

void Controller::EnsureSize(int id)
{
    while ((int)currButtons_.size() <= id)
    {
        currButtons_.push_back({});
        prevButtons_.push_back({});
    }
}

void Controller::Update()
{
    for (unsigned int i = 0; i < sf::Joystick::Count; ++i)
    {
        if (!sf::Joystick::isConnected(i))
        {
            // A disconnected pad must not keep reporting stale buttons as held.
            if (i < currButtons_.size())
            {
                std::fill(currButtons_[i].begin(), currButtons_[i].end(), false);
                std::fill(prevButtons_[i].begin(), prevButtons_[i].end(), false);
            }
            continue;
        }
        EnsureSize((int)i);
        // SFML 3: getButtonCount
        unsigned int bc = sf::Joystick::getButtonCount(i);
        // Resize in both directions. Growing was handled before, but when a pad
        // reported fewer buttons the tail entries kept their last value and
        // "just pressed" could fire on a button that no longer exists.
        if (currButtons_[i].size() != bc)
        {
            currButtons_[i].resize(bc, false);
            prevButtons_[i].resize(bc, false);
        }
        for (unsigned int b = 0; b < bc; ++b)
        {
            prevButtons_[i][b] = currButtons_[i][b];
            currButtons_[i][b] = sf::Joystick::isButtonPressed(i, b);
        }
    }
}

int Controller::ControllerCount() const
{
    int c = 0;
    for (unsigned int i = 0; i < sf::Joystick::Count; ++i)
    {
        if (sf::Joystick::isConnected(i))
        {
            c++;
        }
    }
    return c;
}

bool Controller::IsConnected(int id) const
{
    if (id < 0 || id >= (int)sf::Joystick::Count)
    {
        return false;
    }
    return sf::Joystick::isConnected((unsigned int)id);
}

std::string Controller::Name(int id) const
{
    if (!IsConnected(id))
    {
        return "";
    }
    return sf::Joystick::getIdentification((unsigned int)id).name;
}

float Controller::Axis(int id, sf::Joystick::Axis ax) const
{
    if (!IsConnected(id))
    {
        return 0.f;
    }
    return sf::Joystick::getAxisPosition((unsigned int)id, ax) / 100.f;
}

float Controller::LeftStickX(int id) const
{
    return Axis(id, sf::Joystick::Axis::X);
}
float Controller::LeftStickY(int id) const
{
    return Axis(id, sf::Joystick::Axis::Y);
}
float Controller::RightStickX(int id) const
{
    return Axis(id, sf::Joystick::Axis::U);
}
float Controller::RightStickY(int id) const
{
    return Axis(id, sf::Joystick::Axis::V);
}
float Controller::LeftTrigger(int id) const
{
    return Axis(id, sf::Joystick::Axis::Z);
}
float Controller::RightTrigger(int id) const
{
    return Axis(id, sf::Joystick::Axis::R);
}

bool Controller::ButtonPressed(int id, unsigned int button) const
{
    if (id < 0 || id >= (int)currButtons_.size())
    {
        return false;
    }
    if (button >= currButtons_[id].size())
    {
        return false;
    }
    return currButtons_[id][button];
}

bool Controller::ButtonJustPressed(int id, unsigned int button) const
{
    if (id < 0 || id >= (int)currButtons_.size())
    {
        return false;
    }
    if (button >= currButtons_[id].size())
    {
        return false;
    }
    return currButtons_[id][button] && !prevButtons_[id][button];
}

void Controller::BindAction(const std::string& action, int button)
{
    if (button < 0)
    {
        actionMap_.erase(action);
        return;
    }
    // Casting a negative button to unsigned produced a huge index that could
    // never match a real button.
    actionMap_[action] = static_cast<unsigned int>(button);
}

bool Controller::IsActionDown(const std::string& action, int controllerId) const
{
    auto it = actionMap_.find(action);
    if (it == actionMap_.end())
    {
        return false;
    }
    return ButtonPressed(controllerId, it->second);
}

bool Controller::IsActionPressed(const std::string& action, int controllerId) const
{
    auto it = actionMap_.find(action);
    if (it == actionMap_.end())
    {
        return false;
    }
    return ButtonJustPressed(controllerId, it->second);
}

void Controller::EndFrame()
{
    // prevButtons already updated in Update()
}

} // namespace Fem
