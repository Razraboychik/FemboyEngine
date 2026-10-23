// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/Boy.hpp"
#include <algorithm>
#include <chrono>
#include <thread>

namespace Fem
{

ModuleRegistry& ModuleRegistry::Instance()
{
    static ModuleRegistry r;
    return r;
}

void ModuleRegistry::RegisterModule(std::shared_ptr<IModule> m)
{
    if (!m)
    {
        return;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    modules_.push_back(m);
}

void ModuleRegistry::UnregisterModule(const std::string& name)
{
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto it = modules_.begin(); it != modules_.end(); ++it)
    {
        if (*it && std::string((*it)->Name()) == name)
        {
            modules_.erase(it);
            return;
        }
    }
}

std::shared_ptr<IModule> ModuleRegistry::Get(const std::string& name) const
{
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& m : modules_)
    {
        if (m && std::string(m->Name()) == name)
        {
            return m;
        }
    }
    return nullptr;
}

void ModuleRegistry::InitializeAll()
{
    // Snapshot so a module may register/unregister during its own update.
    auto snapshot = Modules();
    for (auto& m : snapshot)
    {
        if (m && !m->Initialize())
        {
            BOY_ERROR("Core", std::string("module failed to initialize: ") + m->Name());
        }
    }
}

void ModuleRegistry::ShutdownAll()
{
    auto snapshot = Modules();
    for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it)
    {
        if (*it)
        {
            (*it)->Shutdown();
        }
    }
}

void ModuleRegistry::UpdateAll(float dt)
{
    auto snapshot = Modules();
    for (auto& m : snapshot)
    {
        if (m)
        {
            m->Update(dt);
        }
    }
}

std::vector<std::shared_ptr<IModule>> ModuleRegistry::Modules() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return modules_;
}

void LayerStack::Push(std::shared_ptr<Layer> l)
{
    if (l)
    {
        l->OnAttach();
        layers_.push_back(l);
    }
}

void LayerStack::Pop(const std::string& name)
{
    for (auto it = layers_.begin(); it != layers_.end(); ++it)
    {
        if (*it && (*it)->Name() == name)
        {
            (*it)->OnDetach();
            layers_.erase(it);
            break;
        }
    }
}

void LayerStack::Update(float dt)
{
    // Snapshot: a layer may push or pop layers from inside onUpdate(), which
    // would invalidate the iterator and skip or double-update entries.
    const auto snapshot = layers_;
    for (const auto& l : snapshot)
    {
        if (!l)
        {
            continue;
        }
        // Skip layers detached by an earlier callback in this same pass.
        const bool stillAttached = std::find(layers_.begin(), layers_.end(), l) != layers_.end();
        if (stillAttached)
        {
            l->OnUpdate(dt);
        }
    }
}

void LayerStack::Render(Skirt& FrameDrawList)
{
    const auto snapshot = layers_;
    for (const auto& l : snapshot)
    {
        if (!l)
        {
            continue;
        }
        const bool stillAttached = std::find(layers_.begin(), layers_.end(), l) != layers_.end();
        if (stillAttached)
        {
            l->OnRender(FrameDrawList);
        }
    }
}

void LayerStack::Clear()
{
    // Reverse attach order, matching pop().
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)
    {
        if (*it)
        {
            (*it)->OnDetach();
        }
    }
    layers_.clear();
}

Application& Application::Instance()
{
    static Application a;
    return a;
}

bool Application::Initialize(const std::string& title, int w, int h)
{
    title_ = title;
    width_ = w;
    height_ = h;
    running_ = true;
    BOY_INFO("Application", "initialized: " + title + " " + std::to_string(w) + "x" + std::to_string(h));
    return true;
}

void Application::Update(float dt)
{
    layers_.Update(dt);
    ModuleRegistry::Instance().UpdateAll(dt);
}

void Application::Render(Skirt& FrameDrawList)
{
    layers_.Render(FrameDrawList);
}

void Application::Run()
{
    using Clock = std::chrono::steady_clock;
    auto prev = Clock::now();
    while (running_)
    {
        const auto now = Clock::now();
        const float dt = std::chrono::duration<float>(now - prev).count();
        prev = now;
        Update(dt);
        // Headless loop: yield so a pure logic application does not spin a core.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Application::Quit()
{
    running_ = false;
}

void Application::Shutdown()
{
    layers_.Clear();
    running_ = false;
}

void Application::PushLayer(std::shared_ptr<Layer> l)
{
    layers_.Push(l);
}

void Application::PopLayer(const std::string& name)
{
    layers_.Pop(name);
}

} // namespace Fem
