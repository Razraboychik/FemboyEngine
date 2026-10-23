// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <random>
#include <nlohmann/json.hpp>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/Skirt.hpp"

namespace Fem
{

struct Sparkle
{
    sf::Vector2f pos{0, 0};
    sf::Vector2f vel{0, 0};
    sf::Vector2f acc{0, 0};
    sf::Color color = sf::Color::White;
    sf::Color colorEnd = sf::Color::Transparent;
    float life = 1.f;
    float lifeMax = 1.f;
    float size = 4.f;
    float sizeEnd = 0.f;
    float rotation = 0.f;
    float rotSpeed = 0.f;
    bool alive = true;
};

struct EmitterConfig
{
    sf::Vector2f position{0, 0};
    sf::Vector2f velocityMin{-50, -50};
    sf::Vector2f velocityMax{50, 50};
    sf::Color colorStart = sf::Color::White;
    sf::Color colorEnd = sf::Color::Transparent;
    float lifeMin = 0.5f;
    float lifeMax = 1.5f;
    float sizeMin = 2.f;
    float sizeMax = 6.f;
    float sizeEnd = 0.f;
    float spawnRate = 100.f; // sparkles per second
    float gravity = 0.f;
    int burstCount = 0; // 0 = continuous
    bool looping = true;
    bool enabled = true; // emitter on/off switch

    nlohmann::json ToJson() const;
    static EmitterConfig FromJson(const nlohmann::json& j);
};

class Sparkles
{
public:
    static Sparkles& Instance();
    void Update(float dt);
    void Render(sf::RenderTarget& target);
    // Submits every live particle to the shared draw list (DrawLayer::Particles).
    void Render(Skirt& FrameDrawList);

    // emitters
    int CreateEmitter(const EmitterConfig& cfg);
    void RemoveEmitter(int id);
    // Returns a mutable config. If the id is unknown the returned reference is a
    // detached scratch object (never a dangling reference into the emitter list).
    EmitterConfig& GetEmitter(int id);
    const EmitterConfig* FindEmitter(int id) const;
    void SetEmitterEnabled(int id, bool enabled);
    bool IsEmitterEnabled(int id) const;
    void Burst(int id, int count);
    void Clear();

    size_t GetSparkleCount() const;
    // Kept for source compatibility with the original API.
    size_t SparkleCount() const
    {
        return GetSparkleCount();
    }
    size_t EmitterCount() const
    {
        return emitters_.size();
    }

    // Particles are independent, so the integration pass is embarrassingly
    // parallel. The crossover depends on the machine: waking 24 workers costs
    // more than integrating a few thousand particles. The threshold is therefore
    // measured, not hard-coded: the first update calibrates the pool's dispatch
    // cost and derives "items worth one dispatch" from it. setParallelThreshold
    // overrides the automatic value for a project that has profiled its own
    // particle workload.
    void SetMultithreaded(bool enabled)
    {
        multithreaded_ = enabled;
    }
    bool Multithreaded() const
    {
        return multithreaded_;
    }
    void SetParallelThreshold(size_t particles);
    size_t ParallelThreshold() const
    {
        return parallelThreshold_;
    }
    // Re-runs the calibration (thread count change, laptop power-plan change).
    void RefreshParallelThreshold();
    // Historical conservative default; the measured threshold usually lands
    // near it on a 16-24 thread desktop.
    static constexpr size_t kParallelParticles = 65536;

private:
    Sparkles() = default;
    struct Emitter
    {
        int id = 0;
        EmitterConfig cfg;
        std::vector<Sparkle> sparkles;
        float spawnAccum = 0.f;
        bool active = true;
    };

    Emitter* Find(int id);
    const Emitter* Find(int id) const;
    float Rnd(float a, float b);
    void Spawn(Emitter& e);

    std::vector<Emitter> emitters_;
    int nextId_ = 1;
    std::mt19937 rng_{12345};
    bool multithreaded_ = true;
    size_t parallelThreshold_ = kParallelParticles;
    bool thresholdCalibrated_ = false;
    size_t calibratedThreads_ = 0;
};

} // namespace Fem
