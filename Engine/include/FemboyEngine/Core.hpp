// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include "FemboyEngine/FemObject.hpp"
#include "FemboyEngine/Interface.hpp"

namespace Fem
{

using Json = nlohmann::json;

class DrawList;
// Compatibility alias for the old, metaphor-only name. New code uses DrawList.
using Skirt = DrawList;

// Module system
// IInterface lives in Interface.hpp (UE5-style interface system).

class IModule : public IInterface
{
public:
    FEMCLASS(IModule, IInterface, true)
    virtual ~IModule() = default;
    static const char* InterfaceName()
    {
        return "IModule";
    }
    virtual const char* Name() const = 0;
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual void Update(float dt)
    {
    }
    const char* GetInterfaceName() const override
    {
        return "IModule";
    }
    bool ImplementsInterface(const std::string& name) const override
    {
        // Chain to the base so InterfaceCast<IInterface>() still succeeds and
        // derived implementations that use FEMBOY_IMPLEMENTS can chain too.
        return name == "IModule" || IInterface::ImplementsInterface(name);
    }
};

class ModuleRegistry
{
public:
    static ModuleRegistry& Instance();
    void RegisterModule(std::shared_ptr<IModule> m);
    void UnregisterModule(const std::string& name);
    std::shared_ptr<IModule> Get(const std::string& name) const;
    void InitializeAll();
    void ShutdownAll();
    void UpdateAll(float dt);
    std::vector<std::shared_ptr<IModule>> Modules() const;

private:
    ModuleRegistry() = default;
    mutable std::mutex mtx_;
    std::vector<std::shared_ptr<IModule>> modules_;
};

// Layer
class Layer : public FemObject
{
public:
    FEMCLASS(Layer, FemObject, true)
    virtual ~Layer() = default;
    virtual void OnAttach()
    {
    }
    virtual void OnDetach()
    {
    }
    virtual void OnUpdate(float dt)
    {
    }
    virtual void OnEvent(const std::string& event)
    {
    }
    // Called once per frame after the engine submitted its own visuals. Layers
    // push sprites/text/shapes into the single draw list instead of drawing to
    // the window directly, so depth ordering stays global.
    virtual void OnRender(Skirt& FrameDrawList)
    {
        (void)FrameDrawList;
    }
    virtual const char* Name() const = 0;
};

class LayerStack
{
public:
    void Push(std::shared_ptr<Layer> l);
    void Pop(const std::string& name);
    void Update(float dt);
    void Render(Skirt& FrameDrawList);
    // Detaches every layer, in reverse attach order.
    void Clear();
    const std::vector<std::shared_ptr<Layer>>& Layers() const
    {
        return layers_;
    }

private:
    std::vector<std::shared_ptr<Layer>> layers_;
};

// Application
class Application
{
public:
    static Application& Instance();
    bool Initialize(const std::string& title, int width, int height);
    void Run();
    void Update(float dt);
    void Render(Skirt& FrameDrawList);
    void Quit();
    // Detaches all layers and clears the running flag. Safe to call twice.
    void Shutdown();
    bool IsRunning() const
    {
        return running_;
    }
    void PushLayer(std::shared_ptr<Layer> l);
    void PopLayer(const std::string& name);
    LayerStack& Layers()
    {
        return layers_;
    }
    const std::string& Title() const
    {
        return title_;
    }

private:
    Application() = default;
    bool running_ = false;
    std::string title_;
    int width_ = 0, height_ = 0;
    LayerStack layers_;
};

} // namespace Fem

// Transitional compatibility alias. New code uses Fem::; existing games and
// tools can keep Fem:: until the rename is complete.
namespace Femboy = Fem;
