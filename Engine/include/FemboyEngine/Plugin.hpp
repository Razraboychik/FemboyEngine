// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <functional>
#include "FemboyEngine/Core.hpp"

namespace Fem
{

// Plugin API version for compatibility checks.
#define FEMBOY_PLUGIN_API_VERSION 1

class Engine;

// Base interface for all plugins.
class IPlugin : public IInterface
{
public:
    FEMCLASS(IPlugin, IInterface, true)
    virtual ~IPlugin() = default;
    static const char* InterfaceName()
    {
        return "IPlugin";
    }
    virtual const char* Name() const = 0;
    virtual const char* Version() const = 0;
    virtual int ApiVersion() const
    {
        return FEMBOY_PLUGIN_API_VERSION;
    }
    virtual bool Initialize(Engine& engine) = 0;
    virtual void Shutdown() = 0;
    // Optional hooks
    virtual void OnUpdate(float dt)
    {
    }
    virtual void OnRender()
    {
    }
    const char* GetInterfaceName() const override
    {
        return "IPlugin";
    }
    bool ImplementsInterface(const std::string& name) const override
    {
        return name == "IPlugin";
    }
};

// Plugin manager: load/unload plugins, support replacement of subsystems.
class PluginManager
{
public:
    static PluginManager& Instance();

    // Registers a plugin. Duplicate names and API mismatches are rejected.
    bool RegisterPlugin(std::shared_ptr<IPlugin> p);
    bool LoadPlugin(const std::string& path); // dynamic lib
    void UnloadPlugin(const std::string& name);

    std::shared_ptr<IPlugin> Get(const std::string& name) const;
    std::vector<std::shared_ptr<IPlugin>> Plugins() const;

    void InitializeAll(Engine& engine);
    void ShutdownAll();
    void UpdateAll(float dt);

    // Replacement rules: a plugin can register a replacement for a subsystem by name.
    void RegisterReplacement(const std::string& subsystem, std::shared_ptr<IPlugin> plugin);
    std::shared_ptr<IPlugin> Replacement(const std::string& subsystem) const;

private:
    PluginManager() = default;
    mutable std::mutex mtx_;
    std::vector<std::shared_ptr<IPlugin>> plugins_;
    std::map<std::string, std::shared_ptr<IPlugin>> replacements_;
};

} // namespace Fem
