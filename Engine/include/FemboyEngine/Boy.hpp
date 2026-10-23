// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <memory>
#include <sstream>
#include <functional>
#include <imgui.h>

namespace Fem
{

enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

struct LogEntry
{
    LogLevel level;
    std::string category;
    std::string message;
    std::chrono::system_clock::time_point time;
};

// Boy: the logging tool. "Boy" speaks log messages to you.
class Boy
{
public:
    static Boy& Instance();
    void SetLevel(LogLevel minLevel)
    {
        minLevel_.store(minLevel, std::memory_order_relaxed);
    }
    LogLevel Level() const
    {
        return minLevel_.load(std::memory_order_relaxed);
    }
    void SetLogFile(const std::string& path);
    void Log(LogLevel level, const std::string& category, const std::string& msg);
    // convenience
    void Trace(const std::string& cat, const std::string& msg)
    {
        Log(LogLevel::Trace, cat, msg);
    }
    void Debug(const std::string& cat, const std::string& msg)
    {
        Log(LogLevel::Debug, cat, msg);
    }
    void Info(const std::string& cat, const std::string& msg)
    {
        Log(LogLevel::Info, cat, msg);
    }
    void Warn(const std::string& cat, const std::string& msg)
    {
        Log(LogLevel::Warn, cat, msg);
    }
    void Error(const std::string& cat, const std::string& msg)
    {
        Log(LogLevel::Error, cat, msg);
    }
    void Fatal(const std::string& cat, const std::string& msg)
    {
        Log(LogLevel::Fatal, cat, msg);
    }

    // Category filter, for keeping a busy log readable. A null filter accepts
    // everything, which is the default. The filter is only consulted after the
    // level check, so a filtered-out category costs nothing to write.
    void SetCategoryFilter(std::function<bool(const std::string&)> filter);
    void ClearCategoryFilter();

    // Thread-safe snapshot for UI iteration.
    std::vector<LogEntry> EntriesSnapshot() const;
    void Clear();
    void SetCallback(std::function<void(const LogEntry&)> cb);
    static const char* LevelName(LogLevel l);
    static ImVec4 LevelColor(LogLevel l);

private:
    Boy() = default;
    std::atomic<LogLevel> minLevel_{LogLevel::Trace};
    std::vector<LogEntry> entries_;
    mutable std::mutex mtx_;
    std::mutex ioMtx_;
    std::ofstream file_;
    std::function<void(const LogEntry&)> callback_;
    // Swapped atomically rather than read under mtx_: log() is on the hot path
    // of a busy frame and must not take a lock to decide to stay silent.
    std::shared_ptr<const std::function<bool(const std::string&)>> categoryFilter_;
    size_t maxEntries_ = 4096;
};

// Macros for convenience
#define BOY_TRACE(cat, msg) Fem::Boy::Instance().Trace(cat, msg)
#define BOY_DEBUG(cat, msg) Fem::Boy::Instance().Debug(cat, msg)
#define BOY_INFO(cat, msg) Fem::Boy::Instance().Info(cat, msg)
#define BOY_WARN(cat, msg) Fem::Boy::Instance().Warn(cat, msg)
#define BOY_ERROR(cat, msg) Fem::Boy::Instance().Error(cat, msg)
#define BOY_FATAL(cat, msg) Fem::Boy::Instance().Fatal(cat, msg)

} // namespace Fem
