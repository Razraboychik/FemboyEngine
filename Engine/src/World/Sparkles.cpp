// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Sparkles.hpp"
#include "FemboyEngine/Jobs.hpp"
#include "FemboyEngine/Boy.hpp"
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace Fem
{

Sparkles& Sparkles::Instance()
{
    static Sparkles p;
    return p;
}

float Sparkles::Rnd(float a, float b)
{
    if (b < a)
    {
        std::swap(a, b);
    }
    std::uniform_real_distribution<float> d(a, b);
    return d(rng_);
}

EmitterConfig EmitterConfig::FromJson(const nlohmann::json& j)
{
    EmitterConfig c;
    if (!j.is_object())
    {
        return c;
    }

    // Array elements are type-checked too: get<float>() throws on a string.
    const auto readVec = [&j](const char* key, sf::Vector2f def)
    {
        if (!j.contains(key) || !j[key].is_array() || j[key].size() < 2)
        {
            return def;
        }
        const auto& a = j[key];
        if (!a[0].is_number() || !a[1].is_number())
        {
            return def;
        }
        const double x = a[0].get<double>();
        const double y = a[1].get<double>();
        if (!std::isfinite(x) || !std::isfinite(y))
        {
            return def;
        }
        return sf::Vector2f(static_cast<float>(std::clamp(x, -1.0e9, 1.0e9)),
                            static_cast<float>(std::clamp(y, -1.0e9, 1.0e9)));
    };

    c.position = readVec("position", c.position);
    c.velocityMin = readVec("velocityMin", c.velocityMin);
    c.velocityMax = readVec("velocityMax", c.velocityMax);

    // Colours were serialized nowhere, so any emitter saved to disk came back
    // white-to-transparent. Accept either an [r,g,b,a] array or an object.
    const auto readColor = [](const nlohmann::json& src, const char* key, sf::Color def)
    {
        if (!src.contains(key))
        {
            return def;
        }
        const auto& v = src[key];
        const auto channel = [](const nlohmann::json& n, std::uint8_t d) -> std::uint8_t
        {
            if (!n.is_number())
            {
                return d;
            }
            const double d2 = n.get<double>();
            if (!std::isfinite(d2))
            {
                return d;
            }
            return static_cast<std::uint8_t>(std::clamp(d2, 0.0, 255.0));
        };
        if (v.is_array() && v.size() >= 3)
        {
            return sf::Color(channel(v[0], def.r), channel(v[1], def.g), channel(v[2], def.b),
                             v.size() >= 4 ? channel(v[3], def.a) : def.a);
        }
        if (v.is_object())
        {
            return sf::Color(
                v.contains("r") ? channel(v["r"], def.r) : def.r, v.contains("g") ? channel(v["g"], def.g) : def.g,
                v.contains("b") ? channel(v["b"], def.b) : def.b, v.contains("a") ? channel(v["a"], def.a) : def.a);
        }
        return def;
    };

    c.colorStart = readColor(j, "colorStart", c.colorStart);
    c.colorEnd = readColor(j, "colorEnd", c.colorEnd);

    // json::value() throws on a type mismatch; an emitter config is data-driven
    // and must not take the game down.
    const auto readFloat = [&j](const char* key, float def)
    {
        if (!j.contains(key) || !j[key].is_number())
        {
            return def;
        }
        const double d = j[key].get<double>();
        if (!std::isfinite(d))
        {
            return def;
        }
        return static_cast<float>(std::clamp(d, -1.0e9, 1.0e9));
    };
    const auto readInt = [&j](const char* key, int def)
    {
        if (!j.contains(key) || !j[key].is_number())
        {
            return def;
        }
        const double d = j[key].get<double>();
        if (!std::isfinite(d))
        {
            return def;
        }
        return static_cast<int>(std::clamp(d, -1000000.0, 1000000.0));
    };
    const auto readBool = [&j](const char* key, bool def)
    {
        if (!j.contains(key) || !j[key].is_boolean())
        {
            return def;
        }
        return j[key].get<bool>();
    };

    c.spawnRate = readFloat("spawnRate", c.spawnRate);
    c.lifeMin = readFloat("lifeMin", c.lifeMin);
    c.lifeMax = readFloat("lifeMax", c.lifeMax);
    c.sizeMin = readFloat("sizeMin", c.sizeMin);
    c.sizeMax = readFloat("sizeMax", c.sizeMax);
    c.sizeEnd = readFloat("sizeEnd", c.sizeEnd);
    c.gravity = readFloat("gravity", c.gravity);
    c.looping = readBool("looping", c.looping);
    c.burstCount = readInt("burstCount", c.burstCount);
    c.enabled = readBool("enabled", c.enabled);

    // Sanitize so spawn() can never divide by zero or produce inverted ranges.
    if (c.lifeMax < c.lifeMin)
    {
        std::swap(c.lifeMax, c.lifeMin);
    }
    if (c.sizeMax < c.sizeMin)
    {
        std::swap(c.sizeMax, c.sizeMin);
    }
    if (c.spawnRate < 0.f)
    {
        c.spawnRate = 0.f;
    }
    if (c.burstCount < 0)
    {
        c.burstCount = 0;
    }
    if (c.lifeMax <= 0.f)
    {
        c.lifeMax = 0.001f;
    }
    if (c.lifeMin <= 0.f)
    {
        c.lifeMin = 0.001f;
    }

    return c;
}

nlohmann::json EmitterConfig::ToJson() const
{
    return {{"position", {position.x, position.y}},
            {"velocityMin", {velocityMin.x, velocityMin.y}},
            {"velocityMax", {velocityMax.x, velocityMax.y}},
            {"colorStart", {colorStart.r, colorStart.g, colorStart.b, colorStart.a}},
            {"colorEnd", {colorEnd.r, colorEnd.g, colorEnd.b, colorEnd.a}},
            {"spawnRate", spawnRate},
            {"lifeMin", lifeMin},
            {"lifeMax", lifeMax},
            {"sizeMin", sizeMin},
            {"sizeMax", sizeMax},
            {"sizeEnd", sizeEnd},
            {"gravity", gravity},
            {"looping", looping},
            {"burstCount", burstCount},
            {"enabled", enabled}};
}

Sparkles::Emitter* Sparkles::Find(int id)
{
    for (auto& e : emitters_)
    {
        if (e.id == id)
        {
            return &e;
        }
    }
    return nullptr;
}

const Sparkles::Emitter* Sparkles::Find(int id) const
{
    for (const auto& e : emitters_)
    {
        if (e.id == id)
        {
            return &e;
        }
    }
    return nullptr;
}

int Sparkles::CreateEmitter(const EmitterConfig& cfg)
{
    Emitter e;
    e.id = nextId_++;
    e.cfg = cfg;
    e.active = cfg.enabled;

    // Normalize ranges defensively even if the caller built the config by hand.
    if (e.cfg.lifeMax < e.cfg.lifeMin)
    {
        std::swap(e.cfg.lifeMax, e.cfg.lifeMin);
    }
    if (e.cfg.sizeMax < e.cfg.sizeMin)
    {
        std::swap(e.cfg.sizeMax, e.cfg.sizeMin);
    }
    if (e.cfg.lifeMax <= 0.f)
    {
        e.cfg.lifeMax = 0.001f;
    }
    if (e.cfg.spawnRate < 0.f)
    {
        e.cfg.spawnRate = 0.f;
    }

    if (cfg.burstCount > 0)
    {
        e.sparkles.reserve(static_cast<size_t>(cfg.burstCount));
        for (int i = 0; i < cfg.burstCount; ++i)
        {
            Spawn(e);
        }
    }

    // Remember the id before the move: reading emitters_.back().id after a
    // reallocation is fine, but capturing it up front keeps the contract clear
    // even if the container is later changed.
    const int id = e.id;
    emitters_.push_back(std::move(e));
    return id;
}

void Sparkles::RemoveEmitter(int id)
{
    for (size_t i = 0; i < emitters_.size(); ++i)
    {
        if (emitters_[i].id == id)
        {
            emitters_.erase(emitters_.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

EmitterConfig& Sparkles::GetEmitter(int id)
{
    if (Emitter* e = Find(id))
    {
        return e->cfg;
    }
    // Unknown id: return a stable scratch object instead of a reference that
    // could dangle after a later createEmitter() reallocates the vector.
    static thread_local EmitterConfig scratch;
    scratch = EmitterConfig{};
    return scratch;
}

const EmitterConfig* Sparkles::FindEmitter(int id) const
{
    const Emitter* e = Find(id);
    return e ? &e->cfg : nullptr;
}

void Sparkles::SetEmitterEnabled(int id, bool enabled)
{
    if (Emitter* e = Find(id))
    {
        e->cfg.enabled = enabled;
        e->active = enabled;
        if (!enabled)
        {
            e->spawnAccum = 0.f;
        }
    }
}

bool Sparkles::IsEmitterEnabled(int id) const
{
    const Emitter* e = Find(id);
    return e ? e->active : false;
}

void Sparkles::Burst(int id, int count)
{
    if (count <= 0)
    {
        return;
    }
    if (Emitter* e = Find(id))
    {
        e->sparkles.reserve(e->sparkles.size() + static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            Spawn(*e);
        }
    }
}

void Sparkles::Clear()
{
    emitters_.clear();
    nextId_ = 1;
}

void Sparkles::SetParallelThreshold(size_t particles)
{
    parallelThreshold_ = std::clamp<size_t>(particles, 64, 1u << 24);
    thresholdCalibrated_ = true;
    calibratedThreads_ = JobSystem::Instance().ThreadCount();
}

void Sparkles::RefreshParallelThreshold()
{
    thresholdCalibrated_ = false;
    calibratedThreads_ = 0;
}

size_t Sparkles::GetSparkleCount() const
{
    size_t n = 0;
    for (const auto& e : emitters_)
    {
        n += e.sparkles.size();
    }
    return n;
}

void Sparkles::Spawn(Emitter& e)
{
    Sparkle p;
    p.pos = e.cfg.position;
    p.vel = {Rnd(e.cfg.velocityMin.x, e.cfg.velocityMax.x), Rnd(e.cfg.velocityMin.y, e.cfg.velocityMax.y)};
    p.lifeMax = std::max(0.001f, Rnd(e.cfg.lifeMin, e.cfg.lifeMax));
    p.life = p.lifeMax;
    p.size = Rnd(e.cfg.sizeMin, e.cfg.sizeMax);
    p.sizeEnd = e.cfg.sizeEnd;
    p.color = e.cfg.colorStart;
    p.colorEnd = e.cfg.colorEnd;
    p.acc = {0.f, e.cfg.gravity};
    e.sparkles.push_back(p);
}

void Sparkles::Update(float dt)
{
    if (dt < 0.f)
    {
        dt = 0.f;
    }
    // A long frame must not spawn thousands of particles at once.
    const float safeDt = std::min(dt, 0.1f);

    JobSystem& jobs = JobSystem::Instance();
    const size_t workers = jobs.ThreadCount();
    if (multithreaded_ && jobs.IsRunning() && (!thresholdCalibrated_ || calibratedThreads_ != workers))
    {
        // Calibration is a handful of trivial dispatches. Doing it here instead
        // of at boot keeps Sparkles usable in tools that never initialise the
        // pool, and re-runs automatically after a thread-count change.
        const double dispatchNs = jobs.CalibrateDispatch();
        if (dispatchNs > 0.0)
        {
            // ~2.5 ns of integration work per particle on a current desktop.
            // The factor of three covers the sub-linear speedup of a fork-join
            // dispatch: at the crossover the pool is not yet faster, it only
            // stops being slower. With no calibration the core-count rule of
            // thumb below lands in the same range (24 threads -> ~65k).
            const double items = 3.0 * dispatchNs / 2.5;
            parallelThreshold_ = std::clamp<size_t>(static_cast<size_t>(items), 4096, 1u << 20);
        }
        else
        {
            parallelThreshold_ = std::clamp<size_t>(workers * 2730, 4096, 1u << 20);
        }
        thresholdCalibrated_ = true;
        calibratedThreads_ = workers;
    }

    for (auto& e : emitters_)
    {
        if (!e.active)
        {
            continue;
        }

        // A burst emitter spawns its particles once, at creation/burst() time.
        // Re-running the continuous spawner for it would duplicate the burst
        // every frame because spawnRate defaults to a non-zero value.
        if (e.cfg.looping && e.cfg.burstCount == 0 && e.cfg.spawnRate > 0.f)
        {
            e.spawnAccum += e.cfg.spawnRate * safeDt;
            // Cap the per-frame spawn so a stall cannot allocate unbounded memory.
            int budget = 256;
            while (e.spawnAccum >= 1.f && budget-- > 0)
            {
                Spawn(e);
                e.spawnAccum -= 1.f;
            }
            if (e.spawnAccum >= 1.f)
            {
                e.spawnAccum = 0.f;
            }
        }

        // Particles never touch each other, so the integration pass is
        // embarrassingly parallel: same loop, claimed in chunks by the pool. It
        // only pays above a few thousand particles; below that it runs inline.
        const auto integrate = [&e, safeDt](size_t i)
        {
            Sparkle& p = e.sparkles[i];
            if (!p.alive)
            {
                return;
            }
            p.vel.x += p.acc.x * safeDt;
            p.vel.y += p.acc.y * safeDt;
            p.pos.x += p.vel.x * safeDt;
            p.pos.y += p.vel.y * safeDt;
            p.life -= safeDt;
            p.rotation += p.rotSpeed * safeDt;
            if (p.life <= 0.f)
            {
                p.alive = false;
            }
        };

        const bool parallel =
            multithreaded_ && e.sparkles.size() >= parallelThreshold_ && JobSystem::Instance().IsRunning();
        if (parallel)
        {
            JobSystem::Instance().ParallelFor(0, e.sparkles.size(), integrate, 256);
        }
        else
        {
            for (size_t i = 0; i < e.sparkles.size(); ++i)
            {
                integrate(i);
            }
        }

        e.sparkles.erase(
            std::remove_if(e.sparkles.begin(), e.sparkles.end(), [](const Sparkle& p) { return !p.alive; }),
            e.sparkles.end());
    }
}

namespace
{
// One triangle fan per particle, all of them in a single vertex array per
// emitter. The old path built an sf::CircleShape and issued a draw call for
// every particle; at 20k particles that was tens of milliseconds in driver
// overhead alone, dwarfing the integration pass this file is trying to
// parallelise.
constexpr unsigned kCircleSegments = 10;

sf::Color ParticleColor(const Sparkle& p, float t)
{
    sf::Color c;
    c.r = static_cast<std::uint8_t>(p.color.r + (p.colorEnd.r - p.color.r) * t);
    c.g = static_cast<std::uint8_t>(p.color.g + (p.colorEnd.g - p.color.g) * t);
    c.b = static_cast<std::uint8_t>(p.color.b + (p.colorEnd.b - p.color.b) * t);
    c.a = static_cast<std::uint8_t>(p.color.a + (p.colorEnd.a - p.color.a) * t);
    return c;
}

} // namespace

void Sparkles::Render(sf::RenderTarget& target)
{
    for (const auto& e : emitters_)
    {
        sf::VertexArray batch(sf::PrimitiveType::Triangles);
        batch.resize(e.sparkles.size() * kCircleSegments * 3);
        std::size_t cursor = 0;
        for (const auto& p : e.sparkles)
        {
            if (!p.alive)
            {
                continue;
            }
            const float inv = (p.lifeMax > 0.f) ? (1.f - p.life / p.lifeMax) : 1.f;
            const float t = std::clamp(inv, 0.f, 1.f);
            const float sz = std::max(0.f, p.size + (p.sizeEnd - p.size) * t);
            if (sz <= 0.f)
            {
                continue;
            }
            const sf::Color c = ParticleColor(p, t);
            const float step = 6.283185307179586f / static_cast<float>(kCircleSegments);
            const float phase = p.rotation * 3.14159265358979323846f / 180.f;
            for (unsigned i = 0; i < kCircleSegments; ++i)
            {
                const float a0 = phase + step * static_cast<float>(i);
                const float a1 = phase + step * static_cast<float>(i + 1);
                batch[cursor++] = sf::Vertex{p.pos, c};
                batch[cursor++] = sf::Vertex{{p.pos.x + std::cos(a0) * sz, p.pos.y + std::sin(a0) * sz}, c};
                batch[cursor++] = sf::Vertex{{p.pos.x + std::cos(a1) * sz, p.pos.y + std::sin(a1) * sz}, c};
            }
        }
        batch.resize(cursor);
        if (cursor > 0)
        {
            target.draw(batch);
        }
    }
}

void Sparkles::Render(Skirt& FrameDrawList)
{
    // Same particles, submitted to the shared draw list. They land on
    // DrawLayer::Particles, i.e. above the world instead of under every sprite.
    // One pre-built vertex array per emitter keeps the draw list small: the
    // backend can draw thousands of particles with a single command.
    for (const auto& e : emitters_)
    {
        sf::VertexArray batch(sf::PrimitiveType::Triangles);
        batch.resize(e.sparkles.size() * kCircleSegments * 3);
        std::size_t cursor = 0;
        for (const auto& p : e.sparkles)
        {
            if (!p.alive)
            {
                continue;
            }
            const float inv = (p.lifeMax > 0.f) ? (1.f - p.life / p.lifeMax) : 1.f;
            const float t = std::clamp(inv, 0.f, 1.f);
            const float sz = std::max(0.f, p.size + (p.sizeEnd - p.size) * t);
            if (sz <= 0.f)
            {
                continue;
            }
            const sf::Color c = ParticleColor(p, t);
            const float step = 6.283185307179586f / static_cast<float>(kCircleSegments);
            const float phase = p.rotation * 3.14159265358979323846f / 180.f;
            for (unsigned i = 0; i < kCircleSegments; ++i)
            {
                const float a0 = phase + step * static_cast<float>(i);
                const float a1 = phase + step * static_cast<float>(i + 1);
                batch[cursor++] = sf::Vertex{p.pos, c};
                batch[cursor++] = sf::Vertex{{p.pos.x + std::cos(a0) * sz, p.pos.y + std::sin(a0) * sz}, c};
                batch[cursor++] = sf::Vertex{{p.pos.x + std::cos(a1) * sz, p.pos.y + std::sin(a1) * sz}, c};
            }
        }
        batch.resize(cursor);
        if (cursor > 0)
        {
            FrameDrawList.Geometry(batch, "", 0, DrawLayer::Particles);
        }
    }
}

} // namespace Fem
