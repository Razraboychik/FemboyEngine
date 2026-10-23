// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Config.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Vanity.hpp"
#include <fstream>

namespace Fem
{

Config& Config::Instance()
{
    static Config c;
    return c;
}

bool Config::Load(const std::string& path)
{
    auto parsed = Vanity::ReadJson(path);
    if (!parsed)
    {
        BOY_WARN("Config", parsed.Error());
        return false;
    }
    if (!parsed.Value().is_object())
    {
        BOY_ERROR("Config", "root value must be a JSON object: " + path);
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        data_ = std::move(parsed.Value());
    }
    BOY_INFO("Config", "loaded: " + path);
    return true;
}

bool Config::Save(const std::string& path) const
{
    std::lock_guard<std::mutex> lk(mtx_);
    // Settings are user data too: replace atomically instead of truncating.
    auto written = Vanity::WriteJson(path, data_);
    if (!written)
    {
        BOY_ERROR("Config", "save failed: " + written.Error());
        return false;
    }
    BOY_INFO("Config", "saved: " + path);
    return true;
}

void Config::Clear()
{
    std::lock_guard<std::mutex> lk(mtx_);
    data_ = nlohmann::json::object();
}

} // namespace Fem
