// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace Fem
{

// Lightweight frame profiler.
//
// The engine already knows the frame order (Heels) and the renderer already
// reports draw-call telemetry. Profiler joins the two: Heels opens a scope for
// every system, Engine opens one for each phase and for rendering, and the
// renderer's counters are attached to the completed frame. The editor reads the
// ring directly; a game can enable the in-engine overlay with
// Engine::SetProfilerOverlay(true).
//
// Recording is deliberately opt-in-friendly: a disabled profiler still lets
// Engine and Heels call into it, but no samples are retained. Enabling or
// disabling it at runtime never invalidates a frame that is already open.
struct ProfileSample
{
    std::string name;
    double milliseconds = 0.0;
};

struct FrameProfile
{
    std::uint64_t frame = 0;
    double frameMs = 0.0;
    double preUpdateMs = 0.0;
    double fixedUpdateMs = 0.0;
    double postUpdateMs = 0.0;
    double renderMs = 0.0;

    std::size_t commands = 0;
    std::size_t drawCalls = 0;
    std::size_t textureBinds = 0;
    std::size_t vertices = 0;
    std::size_t entities = 0;
    std::size_t systems = 0;

    // Named scopes in the order they completed. Nested scopes are kept: a
    // system that instruments its own expensive section appears twice, once as
    // the system and once as the section.
    std::vector<ProfileSample> scopes;
};

// RAII timing scope. Construct it at the start of the work and let it fall out
// of scope; when recording is disabled the constructor still succeeds and the
// destructor is a cheap no-op.
class ProfileScope
{
public:
    explicit ProfileScope(std::string_view name);
    ~ProfileScope();

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    std::string_view name_;
    std::chrono::steady_clock::time_point start_;
    bool active_ = false;
};

class Profiler
{
public:
    static Profiler& Instance();

    // Default is enabled: a developer build should be diagnosable without a
    // restart. Shipping games can turn it off after initialization.
    void SetEnabled(bool enabled);
    bool Enabled() const;

    // A frame is a begin/end pair. endFrame() without a matching beginFrame()
    // is ignored, so a tool that only calls one of them cannot corrupt history.
    void BeginFrame();
    void EndFrame();
    bool FrameOpen() const;

    // Manual sample. Usually produced by ProfileScope, but game systems may also
    // record a measurement from a worker thread under a stable name.
    void Record(std::string_view name, double milliseconds);

    // Renderer telemetry for the frame that is being recorded.
    void SetRenderStats(std::size_t commands, std::size_t drawCalls, std::size_t textureBinds, std::size_t vertices);
    void SetRegistryStats(std::size_t entities, std::size_t systems);

    // Number of completed frames retained. Clamped to [1, 4096].
    void SetHistorySize(std::size_t frames);
    std::size_t HistorySize() const;

    FrameProfile LastFrame() const;
    std::vector<FrameProfile> History() const;

    // Aggregated samples over the newest `frames` completed frames, sorted by
    // total time descending. Names are unique; duplicate system names are
    // summed.
    std::vector<ProfileSample> AverageScopes(std::size_t frames = 120) const;
    double AverageFrameMs(std::size_t frames = 120) const;
    double AverageFps(std::size_t frames = 120) const;

    void Reset();
    nlohmann::json ToJson(std::size_t maxFrames = 300) const;

private:
    Profiler() = default;

    mutable std::mutex mutex_;
    bool enabled_ = true;
    bool frameOpen_ = false;
    std::uint64_t nextFrame_ = 1;
    std::chrono::steady_clock::time_point frameStart_{};
    FrameProfile current_{};
    std::vector<FrameProfile> history_;
    std::size_t maxHistory_ = 300;
};

// Convenience macro. The name is a string literal, so it adds no allocation at
// the call site; passing a dynamic string is still supported by the class.
#define FB_PROFILE_SCOPE_DETAIL(name, line) ::Fem::ProfileScope femboy_profile_scope_##line(name)
#define FB_PROFILE_SCOPE_EXPAND(name, line) FB_PROFILE_SCOPE_DETAIL(name, line)
#define FB_PROFILE_SCOPE(name) FB_PROFILE_SCOPE_EXPAND(name, __LINE__)

} // namespace Fem
