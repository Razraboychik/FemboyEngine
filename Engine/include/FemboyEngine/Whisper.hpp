// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <any>
#include <mutex>
#include <atomic>
#include "FemboyEngine/Core.hpp"

namespace Fem
{

// Message bus for decoupled communication between systems.
// Dispatch uses a snapshot of the handler list, so a handler may safely
// subscribe/unsubscribe while an event is being published.
class Whisper
{
public:
    static Whisper& Instance();
    using Handler = std::function<void(const std::any&)>;
    using SubscriptionId = size_t;
    SubscriptionId Subscribe(const std::string& event, Handler h);
    void Unsubscribe(const std::string& event, SubscriptionId id);
    void UnsubscribeAll(const std::string& event);
    void Publish(const std::string& event, const std::any& data = {});
    void Clear();

private:
    Whisper() = default;
    struct Sub
    {
        SubscriptionId id;
        Handler handler;
    };
    mutable std::mutex mtx_;
    std::map<std::string, std::vector<Sub>> handlers_;
    std::atomic<SubscriptionId> nextId_{1};
};

} // namespace Fem
