// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Plugin.hpp"
#include "FemboyEngine/Engine.hpp"
#include "FemboyEngine/Boy.hpp"
#include <algorithm>

namespace Fem
{

PluginManager& PluginManager::Instance()
{
    static PluginManager p;
    return p;
}

bool PluginManager::RegisterPlugin(std::shared_ptr<IPlugin> p)
{
    if (!p)
    {
        BOY_WARN("Plugin", "registerPlugin: null plugin");
        return false;
    }
    if (p->ApiVersion() != FEMBOY_PLUGIN_API_VERSION)
    {
        BOY_ERROR("Plugin", std::string("API version mismatch: ") + p->Name());
        return false;
    }

    std::lock_guard<std::mutex> lk(mtx_);
    const std::string name = p->Name() ? p->Name() : "";

    // Never register the same name twice: shutdownAll would call shutdown twice
    // on the same instance and unloadPlugin would erase the wrong entry.
    for (const auto& existing : plugins_)
    {
        if (existing && name == existing->Name())
        {
            BOY_WARN("Plugin", "already registered: " + name);
            return false;
        }
    }

    plugins_.push_back(std::move(p));
    BOY_INFO("Plugin", "registered: " + name);
    return true;
}

bool PluginManager::LoadPlugin(const std::string& path)
{
    // Dynamic loading via LoadLibrary/dlopen would go here. It is deliberately
    // not faked: returning true without loading anything would be a lie.
    BOY_WARN("Plugin", "dynamic loading is not supported in this build: " + path);
    return false;
}

void PluginManager::UnloadPlugin(const std::string& name)
{
    std::shared_ptr<IPlugin> victim;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = std::find_if(plugins_.begin(), plugins_.end(),
                               [&](const std::shared_ptr<IPlugin>& p) { return p && name == p->Name(); });
        if (it == plugins_.end())
        {
            return;
        }
        victim = *it;
        plugins_.erase(it);

        // Drop any replacement entries pointing at the unloaded plugin.
        for (auto rit = replacements_.begin(); rit != replacements_.end();)
        {
            rit = (rit->second == victim) ? replacements_.erase(rit) : std::next(rit);
        }
    }

    // Shutdown outside the lock: a plugin may call back into the manager.
    if (victim)
    {
        victim->Shutdown();
    }
    BOY_INFO("Plugin", "unloaded: " + name);
}

std::shared_ptr<IPlugin> PluginManager::Get(const std::string& name) const
{
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& p : plugins_)
    {
        if (p && name == p->Name())
        {
            return p;
        }
    }
    return nullptr;
}

std::vector<std::shared_ptr<IPlugin>> PluginManager::Plugins() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return plugins_;
}

void PluginManager::InitializeAll(Engine& engine)
{
    // Snapshot: plugins may register or unregister others while initializing.
    for (const auto& p : Plugins())
    {
        if (!p)
        {
            continue;
        }
        if (!p->Initialize(engine))
        {
            BOY_ERROR("Plugin", std::string("initialize failed: ") + p->Name());
        }
    }
}

void PluginManager::ShutdownAll()
{
    for (const auto& p : Plugins())
    {
        if (p)
        {
            p->Shutdown();
        }
    }
}

void PluginManager::UpdateAll(float dt)
{
    for (const auto& p : Plugins())
    {
        if (p)
        {
            p->OnUpdate(dt);
        }
    }
}

void PluginManager::RegisterReplacement(const std::string& subsystem, std::shared_ptr<IPlugin> plugin)
{
    if (subsystem.empty() || !plugin)
    {
        return;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    replacements_[subsystem] = std::move(plugin);
    BOY_INFO("Plugin", "replacement registered for: " + subsystem);
}

std::shared_ptr<IPlugin> PluginManager::Replacement(const std::string& subsystem) const
{
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = replacements_.find(subsystem);
    return (it == replacements_.end()) ? nullptr : it->second;
}

} // namespace Fem
