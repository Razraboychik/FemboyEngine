// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.

#include "FemboyEngine/FemObject.hpp"

namespace Fem
{

void FemObject::RegisterForTick(FemObject* object)
{
    if (!object)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(tickMutex_);
    tickObjects_.push_back(object);
}

void FemObject::UnregisterForTick(FemObject* object)
{
    std::lock_guard<std::mutex> lock(tickMutex_);
    std::erase(tickObjects_, object);
}

void FemObject::TickAll(float dt)
{
    std::vector<FemObject*> snapshot;
    {
        std::lock_guard<std::mutex> lock(tickMutex_);
        snapshot = tickObjects_;
    }
    for (FemObject* object : snapshot)
    {
        if (object && object->IsTickEnabled())
        {
            object->Tick(dt);
        }
    }
}

} // namespace Fem
