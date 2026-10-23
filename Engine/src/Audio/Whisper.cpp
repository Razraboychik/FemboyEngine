// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Whisper.hpp"

namespace Fem
{

Whisper& Whisper::Instance()
{
    static Whisper e;
    return e;
}

Whisper::SubscriptionId Whisper::Subscribe(const std::string& event, Handler h)
{
    const SubscriptionId id = nextId_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(mtx_);
    handlers_[event].push_back({id, std::move(h)});
    return id;
}

void Whisper::Unsubscribe(const std::string& event, SubscriptionId id)
{
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = handlers_.find(event);
    if (it == handlers_.end())
    {
        return;
    }
    auto& vec = it->second;
    for (size_t i = 0; i < vec.size(); ++i)
    {
        if (vec[i].id == id)
        {
            vec.erase(vec.begin() + i);
            break;
        }
    }
    if (vec.empty())
    {
        handlers_.erase(it); // never keep dead event keys
    }
}

void Whisper::UnsubscribeAll(const std::string& event)
{
    std::lock_guard<std::mutex> lk(mtx_);
    handlers_.erase(event);
}

void Whisper::Publish(const std::string& event, const std::any& data)
{
    std::vector<Handler> snapshot;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = handlers_.find(event);
        if (it == handlers_.end())
        {
            return;
        }
        snapshot.reserve(it->second.size());
        for (auto& sub : it->second)
        {
            snapshot.push_back(sub.handler);
        }
    }
    // Dispatch outside the lock on a stable copy.
    for (auto& h : snapshot)
    {
        if (h)
        {
            h(data);
        }
    }
}

void Whisper::Clear()
{
    std::lock_guard<std::mutex> lk(mtx_);
    handlers_.clear();
}

} // namespace Fem
