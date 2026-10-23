// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Heels.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Profiler.hpp"
#include <algorithm>
#include <string>

namespace Fem
{

const char* Heels::PhaseName(Phase phase)
{
    switch (phase)
    {
        case Phase::PreUpdate:
            return "PreUpdate";
        case Phase::FixedUpdate:
            return "FixedUpdate";
        case Phase::PostUpdate:
            return "PostUpdate";
    }
    return "Unknown";
}

void Heels::Add(System system)
{
    if (system.name.empty())
    {
        system.name = "unnamed";
    }
    systems_.push_back(std::move(system));
    // Stable order inside a phase: ties keep registration order.
    std::stable_sort(systems_.begin(), systems_.end(),
                     [](const System& a, const System& b)
                     {
                         if (a.phase != b.phase)
                         {
                             return static_cast<int>(a.phase) < static_cast<int>(b.phase);
                         }
                         return a.order < b.order;
                     });
}

void Heels::Clear()
{
    systems_.clear();
    initialized_ = false;
}

bool Heels::InitializeAll()
{
    if (initialized_)
    {
        return true;
    }

    std::vector<size_t> started;
    started.reserve(systems_.size());

    for (size_t i = 0; i < systems_.size(); ++i)
    {
        System& s = systems_[i];
        if (!s.initialize)
        {
            continue;
        }
        if (!s.initialize())
        {
            BOY_ERROR("Heels", "system failed to initialize: " + s.name);
            // Roll back what already started, newest first. Only systems whose
            // initialize() actually ran are shut down.
            for (auto it = started.rbegin(); it != started.rend(); ++it)
            {
                if (systems_[*it].shutdown)
                {
                    systems_[*it].shutdown();
                }
            }
            return false;
        }
        started.push_back(i);
        BOY_INFO("Heels", "system initialized: " + s.name + " (" + PhaseName(s.phase) + ")");
    }
    initialized_ = true;
    return true;
}

void Heels::RunPhase(Phase phase, float dt, float realDt)
{
    for (auto& s : systems_)
    {
        if (s.phase != phase)
        {
            continue;
        }
        if (s.update)
        {
            // The scope name contains the phase so a system can be registered in
            // more than one phase (and the editor can tell the rows apart).
            const std::string scopeName = std::string(PhaseName(phase)) + "/" + s.name;
            ProfileScope scope(scopeName);
            s.update(s.unscaled ? realDt : dt);
        }
    }
}

void Heels::Tick(float dt)
{
    Tick(dt, dt);
}

void Heels::Tick(float dt, float realDt)
{
    RunPhase(Phase::PreUpdate, dt, realDt);
}

void Heels::FixedTick(float dt)
{
    RunPhase(Phase::FixedUpdate, dt, dt);
}

void Heels::PostTick(float dt)
{
    RunPhase(Phase::PostUpdate, dt, dt);
}

void Heels::ShutdownAll()
{
    // Reverse phase/order: systems that came up last go down first.
    for (auto it = systems_.rbegin(); it != systems_.rend(); ++it)
    {
        if (!it->shutdown)
        {
            continue;
        }
        it->shutdown();
        BOY_INFO("Heels", "system shut down: " + it->name);
    }
    initialized_ = false;
}

} // namespace Fem
