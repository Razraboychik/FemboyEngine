// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <mutex>
#include <nlohmann/json.hpp>
#include "FemboyEngine/Core.hpp"

namespace Fem
{

class Config
{
public:
    static Config& Instance();
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
    nlohmann::json& Data()
    {
        return data_;
    }
    const nlohmann::json& Data() const
    {
        return data_;
    }
    template <typename T> T Get(const std::string& key, const T& def = T{}) const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (data_.is_object() && data_.contains(key))
        {
            try
            {
                return data_[key].get<T>();
            }
            catch (...)
            {
                return def;
            }
        }
        return def;
    }
    template <typename T> void Set(const std::string& key, const T& v)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        data_[key] = v;
    }
    void Clear();

private:
    Config() = default;
    mutable std::mutex mtx_;
    nlohmann::json data_ = nlohmann::json::object();
};

} // namespace Fem
