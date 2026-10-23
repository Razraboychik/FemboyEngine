// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Interface.hpp"
#include <algorithm>

namespace Fem
{

InterfaceRegistry& InterfaceRegistry::Instance()
{
    static InterfaceRegistry r;
    return r;
}

void InterfaceRegistry::RegisterInterface(const std::string& name)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (std::find(interfaces_.begin(), interfaces_.end(), name) == interfaces_.end())
    {
        interfaces_.push_back(name);
    }
}

bool InterfaceRegistry::IsRegistered(const std::string& name) const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return std::find(interfaces_.begin(), interfaces_.end(), name) != interfaces_.end();
}

std::vector<std::string> InterfaceRegistry::Interfaces() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return interfaces_;
}

} // namespace Fem
