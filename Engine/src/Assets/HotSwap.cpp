// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/HotSwap.hpp"
#include "FemboyEngine/Boy.hpp"

namespace Fem
{

HotSwap& HotSwap::Instance()
{
    static HotSwap h;
    return h;
}

void HotSwap::Watch(const std::string& path)
{
    namespace fs = std::filesystem;
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& w : watched_)
    {
        if (w.path == path)
        {
            return;
        }
    }

    WatchedFile wf;
    wf.path = path;
    std::error_code ec;
    if (fs::exists(path, ec) && !ec)
    {
        std::error_code tec;
        wf.lastWrite = fs::last_write_time(path, tec);
        wf.missing = (bool)tec;
    }
    else
    {
        // Register anyway so the file is detected when it appears.
        wf.missing = true;
    }
    watched_.push_back(wf);
}

void HotSwap::Unwatch(const std::string& path)
{
    std::lock_guard<std::mutex> lk(mtx_);
    for (size_t i = 0; i < watched_.size(); ++i)
    {
        if (watched_[i].path == path)
        {
            watched_.erase(watched_.begin() + i);
            break;
        }
    }
}

void HotSwap::Poll()
{
    namespace fs = std::filesystem;
    std::vector<std::string> events;
    std::function<void(const std::string&)> cb;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        changed_.clear();
        cb = callback_;

        for (auto& w : watched_)
        {
            std::error_code ec;
            const bool exists = fs::exists(w.path, ec);
            if (ec)
            {
                // Transient error (sharing violation): keep watching, retry later.
                continue;
            }

            if (!exists)
            {
                if (!w.missing)
                {
                    w.missing = true;
                    changed_.push_back(w.path + " (deleted)");
                    events.push_back(w.path);
                    BOY_WARN("HotSwap", "deleted: " + w.path);
                }
                continue; // keep the watch alive for a possible recreation
            }

            if (w.missing)
            {
                // Recreated.
                w.missing = false;
                std::error_code tec;
                w.lastWrite = fs::last_write_time(w.path, tec);
                changed_.push_back(w.path);
                events.push_back(w.path);
                BOY_INFO("HotSwap", "recreated: " + w.path);
                continue;
            }

            std::error_code tec;
            auto t = fs::last_write_time(w.path, tec);
            if (tec)
            {
                continue; // transient: keep watching
            }
            if (t != w.lastWrite)
            {
                w.lastWrite = t;
                changed_.push_back(w.path);
                events.push_back(w.path);
                BOY_INFO("HotSwap", "changed: " + w.path);
            }
        }
    }

    // Callbacks run outside the lock. A reload handler commonly calls back into
    // watch()/unwatch(), and invoking it while holding mtx_ deadlocked the
    // watcher thread permanently.
    if (cb)
    {
        for (const auto& path : events)
        {
            cb(path);
        }
    }
}

void HotSwap::SetCallback(std::function<void(const std::string&)> cb)
{
    std::lock_guard<std::mutex> lk(mtx_);
    callback_ = std::move(cb);
}

std::vector<std::string> HotSwap::ChangedFiles() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return changed_;
}

void HotSwap::ClearChanged()
{
    std::lock_guard<std::mutex> lk(mtx_);
    changed_.clear();
}

} // namespace Fem
