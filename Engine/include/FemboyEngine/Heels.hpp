// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace Fem
{

// Heels: the engine's system scheduler.
//
// Engine::Run() used to hard-code which subsystem ran when, and in which order.
// That made the frame order implicit, made it easy to forget a system (the crowd
// controller was initialised but never ticked) and impossible for a game to slot
// its own systems into the right phase.
//
// Systems are registered once with a phase and an order. Heels guarantees:
//   * PreUpdate -> FixedUpdate (0..N times) -> PostUpdate ordering;
//   * initialize() in phase/order order (the order systems run), shutdown() in
//     reverse;
//   * one place to inspect the frame order (systems()).
class Heels
{
public:
    enum class Phase
    {
        PreUpdate,   // variable dt, once per frame, before physics/gameplay
        FixedUpdate, // constant dt, may run several times per frame
        PostUpdate   // variable dt, once per frame, after gameplay
    };

    static const char* PhaseName(Phase phase);

    struct System
    {
        std::string name;
        Phase phase = Phase::PreUpdate;
        int order = 0;
        std::function<bool()> initialize;  // return false to abort startup
        std::function<void(float)> update; // dt is fixed for FixedUpdate
        std::function<void()> shutdown;
        // Unscaled systems keep receiving real time while the simulation is
        // paused or slowed down (input, networking, audio, controller polling).
        bool unscaled = false;
    };

    void Add(System system);
    void Clear();

    // Runs initialize() in phase/order order. Stops at the first failure and
    // shuts down the systems that already started.
    bool InitializeAll();

    void Tick(float dt); // PreUpdate phase
    // Like Tick(), but unscaled systems receive realDt instead of the scaled dt.
    void Tick(float dt, float realDt);
    void FixedTick(float dt); // FixedUpdate phase
    void PostTick(float dt);  // PostUpdate phase
    void ShutdownAll();       // reverse phase/order

    const std::vector<System>& Systems() const
    {
        return systems_;
    }
    bool Initialized() const
    {
        return initialized_;
    }

private:
    void RunPhase(Phase phase, float dt, float realDt);

    std::vector<System> systems_;
    bool initialized_ = false;
};

} // namespace Fem
