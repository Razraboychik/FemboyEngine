// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Profiler.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace Fem
{

namespace
{
double ElapsedMs(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
} // namespace

ProfileScope::ProfileScope(std::string_view name) : name_(name)
{
    Profiler& profiler = Profiler::Instance();
    active_ = profiler.Enabled() && profiler.FrameOpen();
    if (active_)
    {
        start_ = std::chrono::steady_clock::now();
    }
}

ProfileScope::~ProfileScope()
{
    if (active_)
    {
        Profiler::Instance().Record(name_, ElapsedMs(start_));
    }
}

Profiler& Profiler::Instance()
{
    static Profiler p;
    return p;
}

void Profiler::SetEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = enabled;
    if (!enabled_)
    {
        frameOpen_ = false;
        current_ = FrameProfile{};
    }
}

bool Profiler::Enabled() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_;
}

void Profiler::BeginFrame()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_)
    {
        frameOpen_ = false;
        return;
    }

    // A previous frame that never reached endFrame() was interrupted (a tool
    // threw, a window closed mid-render). Discard it rather than folding its
    // samples into the next, unrelated frame.
    current_ = FrameProfile{};
    current_.frame = nextFrame_++;
    frameStart_ = std::chrono::steady_clock::now();
    frameOpen_ = true;
}

void Profiler::EndFrame()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || !frameOpen_)
    {
        return;
    }

    current_.frameMs = ElapsedMs(frameStart_);
    for (const ProfileSample& sample : current_.scopes)
    {
        if (sample.name == "PreUpdate")
        {
            current_.preUpdateMs += sample.milliseconds;
        }
        else if (sample.name == "FixedUpdate")
        {
            current_.fixedUpdateMs += sample.milliseconds;
        }
        else if (sample.name == "PostUpdate")
        {
            current_.postUpdateMs += sample.milliseconds;
        }
        else if (sample.name == "Render")
        {
            current_.renderMs += sample.milliseconds;
        }
    }

    history_.push_back(std::move(current_));
    current_ = FrameProfile{};
    frameOpen_ = false;
    if (history_.size() > maxHistory_)
    {
        history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(history_.size() - maxHistory_));
    }
}

bool Profiler::FrameOpen() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return frameOpen_;
}

void Profiler::Record(std::string_view name, double milliseconds)
{
    if (!std::isfinite(milliseconds) || milliseconds < 0.0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || !frameOpen_)
    {
        return;
    }
    // A runaway loop that instruments itself every iteration must not turn the
    // profiler into an unbounded allocation. 1024 named samples per frame is
    // already far more than a useful frame tree.
    if (current_.scopes.size() >= 1024)
    {
        return;
    }
    current_.scopes.push_back(ProfileSample{std::string(name), milliseconds});
}

void Profiler::SetRenderStats(std::size_t commands, std::size_t drawCalls, std::size_t textureBinds,
                              std::size_t vertices)
{
    std::lock_guard<std::mutex> lock(mutex_);
    current_.commands = commands;
    current_.drawCalls = drawCalls;
    current_.textureBinds = textureBinds;
    current_.vertices = vertices;
}

void Profiler::SetRegistryStats(std::size_t entities, std::size_t systems)
{
    std::lock_guard<std::mutex> lock(mutex_);
    current_.entities = entities;
    current_.systems = systems;
}

void Profiler::SetHistorySize(std::size_t frames)
{
    std::lock_guard<std::mutex> lock(mutex_);
    maxHistory_ = std::clamp<std::size_t>(frames, 1, 4096);
    if (history_.size() > maxHistory_)
    {
        history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(history_.size() - maxHistory_));
    }
}

std::size_t Profiler::HistorySize() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return history_.size();
}

FrameProfile Profiler::LastFrame() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return history_.empty() ? FrameProfile{} : history_.back();
}

std::vector<FrameProfile> Profiler::History() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return history_;
}

std::vector<ProfileSample> Profiler::AverageScopes(std::size_t frames) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (history_.empty() || frames == 0)
    {
        return {};
    }

    const std::size_t count = std::min(frames, history_.size());
    const std::size_t first = history_.size() - count;

    std::map<std::string, std::pair<double, std::size_t>> totals;
    for (std::size_t i = first; i < history_.size(); ++i)
    {
        for (const ProfileSample& sample : history_[i].scopes)
        {
            auto& entry = totals[sample.name];
            entry.first += sample.milliseconds;
            ++entry.second;
        }
    }

    std::vector<ProfileSample> out;
    out.reserve(totals.size());
    for (const auto& [name, total] : totals)
    {
        out.push_back(ProfileSample{name, total.first / static_cast<double>(total.second)});
    }

    std::sort(out.begin(), out.end(),
              [](const ProfileSample& a, const ProfileSample& b)
              {
                  if (a.milliseconds != b.milliseconds)
                  {
                      return a.milliseconds > b.milliseconds;
                  }
                  return a.name < b.name;
              });
    return out;
}

double Profiler::AverageFrameMs(std::size_t frames) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (history_.empty() || frames == 0)
    {
        return 0.0;
    }

    const std::size_t count = std::min(frames, history_.size());
    double total = 0.0;
    for (std::size_t i = history_.size() - count; i < history_.size(); ++i)
    {
        total += history_[i].frameMs;
    }
    return total / static_cast<double>(count);
}

double Profiler::AverageFps(std::size_t frames) const
{
    const double ms = AverageFrameMs(frames);
    return ms > 0.0 ? 1000.0 / ms : 0.0;
}

void Profiler::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    history_.clear();
    current_ = FrameProfile{};
    frameOpen_ = false;
    nextFrame_ = 1;
}

nlohmann::json Profiler::ToJson(std::size_t maxFrames) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t count = std::min(maxFrames, history_.size());
    nlohmann::json frames = nlohmann::json::array();
    for (std::size_t i = history_.size() - count; i < history_.size(); ++i)
    {
        const FrameProfile& frame = history_[i];
        nlohmann::json scopes = nlohmann::json::array();
        for (const ProfileSample& sample : frame.scopes)
        {
            scopes.push_back({{"name", sample.name}, {"ms", sample.milliseconds}});
        }
        frames.push_back({{"frame", frame.frame},
                          {"frameMs", frame.frameMs},
                          {"preUpdateMs", frame.preUpdateMs},
                          {"fixedUpdateMs", frame.fixedUpdateMs},
                          {"postUpdateMs", frame.postUpdateMs},
                          {"renderMs", frame.renderMs},
                          {"commands", frame.commands},
                          {"drawCalls", frame.drawCalls},
                          {"textureBinds", frame.textureBinds},
                          {"vertices", frame.vertices},
                          {"entities", frame.entities},
                          {"systems", frame.systems},
                          {"scopes", std::move(scopes)}});
    }
    return {{"version", 1}, {"frames", std::move(frames)}};
}

} // namespace Fem
