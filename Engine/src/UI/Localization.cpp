// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Localization.hpp"
#include "FemboyEngine/Boy.hpp"
#include <fstream>
#include <algorithm>

namespace Fem
{

Localization& Localization::Instance()
{
    static Localization l;
    return l;
}

void Localization::SetLocale(const std::string& locale)
{
    if (locale.empty())
    {
        BOY_WARN("Localization", "ignoring empty locale");
        return;
    }
    std::unique_lock<std::shared_mutex> lk(mtx_);
    locale_ = locale;
    BOY_INFO("Localization", "locale set to: " + locale);
}

void Localization::AddFallback(const std::string& locale)
{
    if (locale.empty())
    {
        return;
    }
    std::unique_lock<std::shared_mutex> lk(mtx_);
    if (std::find(fallbacks_.begin(), fallbacks_.end(), locale) == fallbacks_.end())
    {
        fallbacks_.push_back(locale);
    }
}

bool Localization::AddTable(const std::string& locale, const std::unordered_map<std::string, std::string>& table)
{
    if (locale.empty())
    {
        return false;
    }
    std::unique_lock<std::shared_mutex> lk(mtx_);
    auto& target = tables_[locale];
    for (const auto& [k, v] : table)
    {
        target[k] = v;
    }
    if (std::find(supported_.begin(), supported_.end(), locale) == supported_.end())
    {
        supported_.push_back(locale);
    }
    return true;
}

bool Localization::LoadTable(const std::string& locale, const std::string& path)
{
    if (locale.empty())
    {
        BOY_ERROR("Localization", "locale must not be empty");
        return false;
    }

    std::ifstream f(path);
    if (!f.is_open())
    {
        BOY_WARN("Localization", "cannot open table: " + path);
        return false;
    }

    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (const std::exception& e)
    {
        BOY_ERROR("Localization", std::string("parse error: ") + e.what());
        return false;
    }

    if (!j.is_object())
    {
        BOY_ERROR("Localization", "table root must be an object: " + path);
        return false;
    }

    std::unordered_map<std::string, std::string> table;
    for (const auto& [k, v] : j.items())
    {
        if (v.is_string())
        {
            table[k] = v.get<std::string>();
        }
        else
        {
            BOY_WARN("Localization", "skipping non-string value for key: " + k);
        }
    }

    AddTable(locale, table);
    BOY_INFO("Localization", "loaded table: " + locale + " (" + std::to_string(table.size()) + " keys)");
    return true;
}

std::string Localization::Tr(const std::string& key) const
{
    std::shared_lock<std::shared_mutex> lk(mtx_);

    const auto tryLocale = [&](const std::string& loc) -> const std::string*
    {
        auto it = tables_.find(loc);
        if (it == tables_.end())
        {
            return nullptr;
        }
        auto kt = it->second.find(key);
        if (kt == it->second.end())
        {
            return nullptr;
        }
        return &kt->second;
    };

    if (const std::string* p = tryLocale(locale_))
    {
        return *p;
    }
    for (const auto& fb : fallbacks_)
    {
        if (const std::string* p = tryLocale(fb))
        {
            return *p;
        }
    }
    if (locale_ != "en")
    {
        if (const std::string* p = tryLocale("en"))
        {
            return *p;
        }
    }
    return key; // missing key: return it unchanged so the problem is visible
}

void Localization::RegisterVoice(const std::string& key, const std::string& audioId)
{
    if (key.empty())
    {
        return;
    }
    std::unique_lock<std::shared_mutex> lk(mtx_);
    voices_[key] = audioId;
}

std::string Localization::VoiceId(const std::string& key) const
{
    std::shared_lock<std::shared_mutex> lk(mtx_);
    auto it = voices_.find(key);
    return (it == voices_.end()) ? std::string{} : it->second;
}

size_t Localization::KeyCount(const std::string& locale) const
{
    std::shared_lock<std::shared_mutex> lk(mtx_);
    auto it = tables_.find(locale);
    return (it == tables_.end()) ? 0 : it->second.size();
}

void Localization::Clear()
{
    std::unique_lock<std::shared_mutex> lk(mtx_);
    tables_.clear();
    voices_.clear();
    fallbacks_.clear();
    supported_.clear();
    locale_ = "en";
}

} // namespace Fem
