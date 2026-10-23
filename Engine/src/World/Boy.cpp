// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Boy.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace Fem
{

Boy& Boy::Instance()
{
    // Process-lifetime logger: another singleton's destructor may still log
    // while static objects are being torn down.
    static Boy* b = new Boy();
    return *b;
}

void Boy::SetLogFile(const std::string& path)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (file_.is_open())
    {
        file_.close();
    }
    file_.open(path, std::ios::app);
}

void Boy::Log(LogLevel level, const std::string& category, const std::string& msg)
{
    if (level < minLevel_.load(std::memory_order_relaxed))
    {
        return;
    }

    // The filter is consulted before anything is formatted or copied, so a
    // filtered-out category really does cost one indirect call and a branch.
    if (const auto filter = std::atomic_load(&categoryFilter_))
    {
        if (*filter && !(*filter)(category))
        {
            return;
        }
    }

    LogEntry e{level, category, msg, std::chrono::system_clock::now()};
    std::function<void(const LogEntry&)> cb;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        entries_.push_back(e);
        if (entries_.size() > maxEntries_)
        {
            entries_.erase(entries_.begin(), entries_.begin() + (entries_.size() - maxEntries_));
        }
        if (file_.is_open())
        {
            auto t = std::chrono::system_clock::to_time_t(e.time);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            file_ << std::put_time(&tm, "%H:%M:%S") << " [" << LevelName(level) << "] " << category << ": " << msg
                  << "\n";
            file_.flush();
        }
        cb = callback_;
    }

    // Callback and console output happen outside the entries lock so a handler
    // may log again without deadlocking.
    if (cb)
    {
        cb(e);
    }
    {
        std::lock_guard<std::mutex> io(ioMtx_);
        std::cout << "[" << LevelName(level) << "] " << category << ": " << msg << "\n";
    }
}

std::vector<LogEntry> Boy::EntriesSnapshot() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return entries_;
}

void Boy::Clear()
{
    std::lock_guard<std::mutex> lk(mtx_);
    entries_.clear();
}

void Boy::SetCallback(std::function<void(const LogEntry&)> cb)
{
    std::lock_guard<std::mutex> lk(mtx_);
    callback_ = std::move(cb);
}

void Boy::SetCategoryFilter(std::function<bool(const std::string&)> filter)
{
    auto shared = std::make_shared<const std::function<bool(const std::string&)>>(std::move(filter));
    std::atomic_store(&categoryFilter_, std::move(shared));
}

void Boy::ClearCategoryFilter()
{
    std::shared_ptr<const std::function<bool(const std::string&)>> none;
    std::atomic_store(&categoryFilter_, std::move(none));
}

const char* Boy::LevelName(LogLevel l)
{
    switch (l)
    {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
    }
    return "?";
}

ImVec4 Boy::LevelColor(LogLevel l)
{
    switch (l)
    {
        case LogLevel::Trace:
            return ImVec4(0.6f, 0.6f, 0.6f, 1.f);
        case LogLevel::Debug:
            return ImVec4(0.5f, 0.7f, 1.f, 1.f);
        case LogLevel::Info:
            return ImVec4(0.4f, 1.f, 0.5f, 1.f);
        case LogLevel::Warn:
            return ImVec4(1.f, 0.8f, 0.3f, 1.f);
        case LogLevel::Error:
            return ImVec4(1.f, 0.3f, 0.3f, 1.f);
        case LogLevel::Fatal:
            return ImVec4(1.f, 0.1f, 0.6f, 1.f);
    }
    return ImVec4(1, 1, 1, 1);
}

} // namespace Fem
