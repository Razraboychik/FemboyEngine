// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include "FemboyEngine/Core.hpp"

namespace Fem
{

// Decoupled localization: string tables per locale, fallback chain, voice acting separate.
class Localization
{
public:
    static Localization& Instance();

    void SetLocale(const std::string& locale); // e.g. "en", "ru"
    // Returned by value: a reference into the table would dangle as soon as
    // another thread calls setLocale().
    std::string Locale() const
    {
        std::shared_lock<std::shared_mutex> lk(mtx_);
        return locale_;
    }
    void AddFallback(const std::string& locale);                        // fallback chain
    bool LoadTable(const std::string& locale, const std::string& path); // JSON
    bool AddTable(const std::string& locale, const std::unordered_map<std::string, std::string>& table);
    std::string Tr(const std::string& key) const; // translate

    // voice acting: key -> audio asset id, decoupled from text
    void RegisterVoice(const std::string& key, const std::string& audioId);
    std::string VoiceId(const std::string& key) const;

    std::vector<std::string> SupportedLocales() const
    {
        std::shared_lock<std::shared_mutex> lk(mtx_);
        return supported_;
    }
    size_t KeyCount(const std::string& locale) const;
    void Clear();

private:
    Localization() = default;
    mutable std::shared_mutex mtx_;
    std::string locale_ = "en";
    std::vector<std::string> fallbacks_;
    std::vector<std::string> supported_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> tables_; // locale -> key -> text
    std::unordered_map<std::string, std::string> voices_;                                  // key -> audio id
};

// convenience
inline std::string _(const std::string& key)
{
    return Localization::Instance().Tr(key);
}

} // namespace Fem
