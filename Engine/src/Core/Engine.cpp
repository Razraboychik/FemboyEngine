// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Engine.hpp"
#include "FemboyEngine/Accessibility.hpp"
#include "FemboyEngine/Assets.hpp"
#include "FemboyEngine/Audio.hpp"
#include "FemboyEngine/AudioMixer.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Console.hpp"
#include "FemboyEngine/Controller.hpp"
#include "FemboyEngine/CrashBoy.hpp"
#include "FemboyEngine/CrowdController.hpp"
#include "FemboyEngine/Display.hpp"
#include "FemboyEngine/Hosting.hpp"
#include "FemboyEngine/HotSwap.hpp"
#include "FemboyEngine/Input.hpp"
#include "FemboyEngine/IRenderer.hpp"
#include "FemboyEngine/Jobs.hpp"
#include "FemboyEngine/LiveResize.hpp"
#include "FemboyEngine/Localization.hpp"
#include "FemboyEngine/Modding.hpp"
#include "FemboyEngine/Networking.hpp"
#include "FemboyEngine/PostFX.hpp"
#include "FemboyEngine/Profiler.hpp"
#include "FemboyEngine/RandomSystem.hpp"
#include "FemboyEngine/SaveLoad.hpp"
#include "FemboyEngine/Sparkles.hpp"
#include "FemboyEngine/TextureOptimizer.hpp"

#include <functional>
#include "FemboyEngine/UI.hpp"
#include "FemboyEngine/VisualScript.hpp"
#include "FemboyEngine/VoiceChat.hpp"
#include "FemboyEngine/Whisper.hpp"
#include "Splash.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>

namespace Fem
{

namespace
{
// Fixed-step accumulator so gameplay is frame-rate independent.
constexpr float kFixedStep = 1.f / 120.f;
constexpr float kMaxFrameTime = 0.25f;
// Upper bound on catch-up work per frame. Excess time is dropped: simulating
// eight steps for several frames in a row looks like fast-forward, which is
// worse than slowing down briefly after a stall.
constexpr int kMaxFixedSteps = 8;

// Interpolates degrees along the shortest arc, so a sprite rotating from 350 to
// 10 degrees crosses 0 instead of spinning almost all the way around.
float LerpAngle(float from, float to, float t)
{
    float delta = std::fmod(to - from, 360.f);
    if (delta > 180.f)
    {
        delta -= 360.f;
    }
    else if (delta < -180.f)
    {
        delta += 360.f;
    }
    return from + delta * t;
}
} // namespace

Engine& Engine::Instance()
{
    static Engine e;
    return e;
}

// Subsystem accessors. They live here, not in the header, so Engine.hpp can
// forward-declare the subsystems instead of including every one of them.
Profiler& Engine::GetProfiler()
{
    return Profiler::Instance();
}

Input& Engine::GetInput()
{
    return Input::Instance();
}

AssetManager& Engine::Assets()
{
    return AssetManager::Instance();
}

Localization& Engine::GetLocalization()
{
    return Localization::Instance();
}

AudioSystem& Engine::Audio()
{
    return AudioSystem::Instance();
}

VoiceChat& Engine::Voice()
{
    return VoiceChat::Instance();
}

Sparkles& Engine::GetSparkles()
{
    return Sparkles::Instance();
}

JobSystem& Engine::Jobs()
{
    return JobSystem::Instance();
}

SaveSystem& Engine::Saves()
{
    return SaveSystem::Instance();
}

PostFX& Engine::GetPostFx()
{
    return PostFX::Instance();
}

AudioMixer& Engine::GetAudioMixer()
{
    return AudioMixer::Instance();
}

HotSwap& Engine::GetHotSwap()
{
    return HotSwap::Instance();
}

CrowdController& Engine::Crowd()
{
    return CrowdController::Instance();
}

Whisper& Engine::GetWhisper()
{
    return Whisper::Instance();
}

VisualScriptEditor& Engine::VisualScript()
{
    return VisualScriptEditor::Instance();
}

Network& Engine::GetNetwork()
{
    return Network::Instance();
}

Accessibility& Engine::GetAccessibility()
{
    return Accessibility::Instance();
}

Controller& Engine::GetController()
{
    return Controller::Instance();
}

TextureOptimizer& Engine::GetTextureOptimizer()
{
    return TextureOptimizer::Instance();
}

CrashBoy& Engine::GetCrashBoy()
{
    return CrashBoy::Instance();
}

PinkUI& Engine::Ui()
{
    return PinkUI::Instance();
}

ModManager& Engine::Mods()
{
    return ModManager::Instance();
}

bool Engine::IsLiveResizing() const
{
    return LiveResize::Instance().Active();
}

bool Engine::Initialize(const std::string& title, int w, int h, WindowMode mode)
{
    if (initialized_)
    {
        BOY_WARN("Engine", "initialize called twice; ignoring");
        return true;
    }

    BOY_INFO("Engine", "initializing: " + title);

    if (!Application::Instance().Initialize(title, w, h))
    {
        BOY_ERROR("Engine", "application init failed");
        return false;
    }

    if (!window_.Create(title, w, h, mode))
    {
        BOY_ERROR("Engine", "window creation failed");
        Shutdown();
        return false;
    }

    // A borderless or fullscreen window is the desktop resolution regardless of
    // the size that was asked for, so every later stage uses the client size.
    w = window_.Width();
    h = window_.Height();
    PinkUI::Instance().SetViewport(sf::FloatRect({0.f, 0.f}, {static_cast<float>(w), static_cast<float>(h)}));

    if (!canvas_.Initialize(w, h))
    {
        BOY_ERROR("Engine", "canvas init failed");
        Shutdown();
        return false;
    }

    // Single renderer path: the backend renders into the engine's window.
    renderer_ = std::make_unique<SFMLRenderer>();
    renderer_->SetExternalWindow(&window_.Handle());
    if (!renderer_->Initialize(w, h, title))
    {
        BOY_ERROR("Engine", "renderer backend init failed");
        Shutdown();
        return false;
    }

    registry_ = Registry{};
    camera_.SetViewportSize(static_cast<float>(window_.Width()), static_cast<float>(window_.Height()));

    // Systems
    // Everything that needs a per-frame callback is registered once, with an
    // explicit phase and order. Heels initializes in this order and shuts down in
    // reverse, so Engine::Run() no longer hard-codes the frame order and a game
    // can slot its own systems in without editing the engine.
    heels_.Clear();

    // Services first: they must be ready before the first tick.
    heels_.Add({"ModManager", Heels::Phase::PreUpdate, 0,
                []()
                {
                    ModManager::Instance().ScanMods();
                    return true;
                },
                nullptr, nullptr});
    heels_.Add({"JobSystem", Heels::Phase::PreUpdate, 1,
                []()
                {
                    JobSystem::Instance().Initialize();
                    return true;
                },
                nullptr, []() { JobSystem::Instance().Shutdown(); }});
    heels_.Add({"Network", Heels::Phase::PreUpdate, 2, []() { return Network::Instance().Initialize(); },
                // Network is a service, not a GameHost detail: voice and any
                // direct Network user must keep receiving even when the project
                // never calls GameHost::Join/host. GameHost::Tick also polls,
                // which is harmless and keeps its event ordering unchanged.
                [](float) { Network::Instance().Poll(); }, []() { Network::Instance().Shutdown(); }, true});
    heels_.Add({"CrashBoy", Heels::Phase::PreUpdate, 3,
                []()
                {
                    CrashBoy::Instance().Initialize("crashes");
                    return true;
                },
                nullptr, nullptr});
    // Asset watching is a service: assets loaded by the project after Engine
    // init are registered with HotSwap by AssetManager, and one poll per frame
    // is enough to debounce an editor save.
    heels_.Add({"HotSwap", Heels::Phase::PreUpdate, 4, nullptr,
                [](float)
                {
                    HotSwap& watch = HotSwap::Instance();
                    watch.Poll();
                    if (!watch.ChangedFiles().empty())
                    {
                        AssetManager::Instance().HotReload();
                        watch.ClearChanged();
                    }
                    // CPU decodes finish on worker threads; the GPU upload has
                    // to happen here, on the thread that owns the GL context.
                    AssetManager::Instance().PumpAsync();
                },
                nullptr});

    // Per-frame systems, in the order the frame needs them: controller state is
    // read before Input samples it, audio/particles/hosting follow gameplay
    // input, and the camera updates after the fixed step moved the world.
    heels_.Add({"Controller", Heels::Phase::PreUpdate, 10, nullptr, [](float) { Controller::Instance().Update(); },
                nullptr, true});
    heels_.Add({"Input", Heels::Phase::PreUpdate, 20,
                []()
                {
                    Input::Instance().Initialize();
                    return true;
                },
                [](float dt) { Input::Instance().Tick(dt); }, nullptr, true});
    heels_.Add({"Audio", Heels::Phase::PreUpdate, 30, nullptr, [](float) { AudioSystem::Instance().Update(); }, nullptr,
                true});
    // Centralized object tick. Only objects with ticking enabled are visited, so
    // a disabled FemObject costs one branch instead of a virtual call.
    heels_.Add({"FemObjects", Heels::Phase::PreUpdate, 25, nullptr,
                [](float dt) { Fem::FemObject::TickAll(dt); }, nullptr});
    // Voice capture and decode are real-time services: they stay unscaled while
    // the game is paused, so a menu does not turn into a wall of delayed audio.
    heels_.Add({"VoiceChat", Heels::Phase::PreUpdate, 35,
                []()
                {
                    const bool ok = VoiceChat::Instance().Initialize();
                    Network::Instance().SetBinaryReceiveCallback(
                        [](uint32_t peerId, uint8_t, const uint8_t* data, size_t size)
                        { VoiceChat::Instance().OnBinaryPacket(peerId, data, size); });
                    return ok;
                },
                [](float dt) { VoiceChat::Instance().Update(dt, &Network::Instance()); },
                []()
                {
                    Network::Instance().SetBinaryReceiveCallback({});
                    VoiceChat::Instance().Shutdown();
                },
                true});
    heels_.Add(
        {"Sparkles", Heels::Phase::PreUpdate, 40, nullptr, [](float dt) { Sparkles::Instance().Update(dt); }, nullptr});
    heels_.Add({"Hosting", Heels::Phase::PreUpdate, 50, nullptr, [](float dt) { GameHost::Instance().Tick(dt); },
                nullptr, true});
    heels_.Add({"Crowd", Heels::Phase::PreUpdate, 60,
                []()
                {
                    CrowdController::Instance().Initialize(10000);
                    return true;
                },
                [this](float dt)
                {
                    const sf::Vector2f center = camera_.Position();
                    const sf::Vector2f size = camera_.ViewportSize();
                    CrowdController::Instance().Update(dt, center, std::max(size.x, size.y) * 0.5f);
                },
                []() { CrowdController::Instance().Shutdown(); }});

    // Animation advances with the simulation, then writes the active frame into
    // the sprite's source rectangle. The renderer itself stays animation-aware
    // in exactly one place (the draw list).
    heels_.Add({"Animation", Heels::Phase::FixedUpdate, 5, nullptr,
                [this](float dt)
                {
                    auto view = registry_.view<AnimationComponent>();
                    for (auto e : view)
                    {
                        AnimationComponent& anim = view.get<AnimationComponent>(e);
                        anim.animator.Update(dt);
                        SpriteComponent* sprite = registry_.try_get<SpriteComponent>(e);
                        if (!sprite)
                        {
                            continue;
                        }
                        const sf::IntRect& rect = anim.animator.CurrentRect();
                        if (rect.size.x > 0 && rect.size.y > 0)
                        {
                            sprite->srcX = rect.position.x;
                            sprite->srcY = rect.position.y;
                            sprite->srcW = rect.size.x;
                            sprite->srcH = rect.size.y;
                        }
                    }
                },
                nullptr});

    // Simulation runs on a fixed step; presentation (camera) runs once per frame.
    heels_.Add({"Application", Heels::Phase::FixedUpdate, 10, nullptr,
                [](float dt) { Application::Instance().Update(dt); }, nullptr});
    heels_.Add({"Camera", Heels::Phase::PostUpdate, 10, nullptr, [this](float dt) { camera_.Update(dt, registry_); },
                nullptr});
    // Captions must expire even while gameplay is paused, so they live on the
    // real-time per-frame phase rather than FixedUpdate.
    heels_.Add({"Accessibility", Heels::Phase::PreUpdate, 5, nullptr,
                [](float dt) { Accessibility::Instance().UpdateSubtitles(dt); }, nullptr});

    if (!heels_.InitializeAll())
    {
        // initializeAll() already rolled back the systems that had started.
        BOY_ERROR("Engine", "system initialization failed");
        Shutdown();
        return false;
    }

    initialized_ = true;

    // Live resize: while the user drags a window border, the system's modal loop
    // parks Engine::Run(), so a timer renders frames from the last state. The
    // timer exists only for the duration of the drag.
    LiveResize::Instance().Attach(reinterpret_cast<void*>(window_.Handle().getNativeHandle()),
                                  [this]() { RenderFrame(); });

    BOY_INFO("Engine", "ready (threads: " + std::to_string(JobSystem::Instance().ThreadCount()) + ")");
    return true;
}

void Engine::Run()
{
    Application& app = Application::Instance();

    // The splash owns the window for its first couple of seconds. It consumes
    // input events itself, so a click that dismisses the logo can never reach a
    // weapon or a menu button on the first gameplay frame.
    if (splashEnabled_ && !splashShown_)
    {
        splashShown_ = true;

        // Sign-in cards the project supplied, in order, then the engine mark.
        std::vector<Splash::SplashCard> cards;
        if (!studioSplashPath_.empty() || !studioSplashCaption_.empty())
        {
            cards.push_back({studioSplashPath_, studioSplashCaption_, 1.5f, false});
        }
        if (!publisherSplashPath_.empty() || !publisherSplashCaption_.empty())
        {
            cards.push_back({publisherSplashPath_, publisherSplashCaption_, 1.5f, false});
        }

        if (!Splash::Play(window_, cards, splashDuration_))
        {
            BOY_INFO("Engine", "window closed during the boot splash");
            return;
        }
    }

    sf::Clock clock;

    while (app.IsRunning() && window_.IsOpen())
    {
        // Clamp the frame time: after a breakpoint or a stall the game must not
        // simulate minutes of physics in a single frame.
        float dt = std::min(clock.restart().asSeconds(), kMaxFrameTime);
        if (!std::isfinite(dt) || dt < 0.f)
        {
            dt = 0.f;
        }

        RandomSystem::BeginFrame(0);
        Profiler::Instance().BeginFrame();
        DisplaySettings::Instance().Update(dt, Input::Instance().AnyInputActive());

        while (const std::optional<sf::Event> event = window_.Handle().pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                app.Quit();
                window_.Handle().close();
                break;
            }
            // The developer console gets first refusal on keyboard events. It
            // consumes only while open (plus its toggle key), so gameplay input
            // is untouched during normal play.
            if (Console::Instance().ProcessEvent(*event))
            {
                continue;
            }
            // Any real input wakes a dissolved UI. Resizes and focus changes do
            // not count: the window settling is not the player asking to see it.
            if (!event->is<sf::Event::Resized>() && !event->is<sf::Event::FocusGained>() &&
                !event->is<sf::Event::FocusLost>())
            {
                DisplaySettings::Instance().NotifyActivity();
            }
            if (const auto* resized = event->getIf<sf::Event::Resized>())
            {
                window_.SetSize(static_cast<int>(resized->size.x), static_cast<int>(resized->size.y));
                camera_.SetViewportSize(static_cast<float>(window_.Width()), static_cast<float>(window_.Height()));
                // UI layouts are in design units; re-laying out on resize keeps
                // panels anchored to the real window instead of 1280x720.
                PinkUI::Instance().SetViewport(sf::FloatRect(
                    {0.f, 0.f}, {static_cast<float>(window_.Width()), static_cast<float>(window_.Height())}));
            }
            Input::Instance().Update(*event);
        }

        if (!window_.IsOpen())
        {
            break;
        }

        // Pausing freezes gameplay, particles, the crowd and the camera while
        // input, audio, networking and controller polling keep running.
        const float simDt = paused_ ? 0.f : dt * timeScale_;

        // Frame order comes from the scheduler, not from this loop.
        {
            ProfileScope scope("PreUpdate");
            heels_.Tick(simDt, dt);
        }

        accumulator_ += simDt;
        int steps = static_cast<int>(accumulator_ / kFixedStep);
        steps = std::min(steps, kMaxFixedSteps);
        {
            ProfileScope scope("FixedUpdate");
            for (int i = 0; i < steps; ++i)
            {
                // Snapshot only before the final step: interpolation then runs
                // between the last two simulated states, which keeps render latency
                // bounded by one fixed step instead of one whole frame.
                if (i == steps - 1)
                {
                    auto transforms = registry_.view<TransformComponent>();
                    for (auto e : transforms)
                    {
                        TransformComponent& t = transforms.get<TransformComponent>(e);
                        t.prevX = t.x;
                        t.prevY = t.y;
                        t.prevScaleX = t.scaleX;
                        t.prevScaleY = t.scaleY;
                        t.prevRotation = t.rotation;
                        t.hasPrev = true;
                    }
                }
                heels_.FixedTick(kFixedStep);
                accumulator_ -= kFixedStep;
            }
        }

        if (accumulator_ >= kFixedStep)
        {
            // The catch-up guard clipped a burst (a long stall). Drop the excess
            // instead of fast-forwarding the simulation on later frames.
            accumulator_ = std::fmod(accumulator_, kFixedStep);
            BOY_WARN("Engine", "fixed-step backlog dropped after a hitch");
        }

        {
            ProfileScope scope("PostUpdate");
            heels_.PostTick(simDt);
        }

        RenderFrame();

        // Attach the completed frame to the profiler. All counters describe the
        // same frame the scopes were recorded in; endFrame() closes it and
        // appends to the ring.
        Profiler& profiler = Profiler::Instance();
        if (renderer_)
        {
            const RenderStats stats = renderer_->GetRenderStats();
            profiler.SetRenderStats(stats.commands, stats.drawCalls, stats.textureBinds, stats.vertices);
        }
        const std::size_t entityCount = registry_.storage<entt::entity>().size();
        profiler.SetRegistryStats(entityCount, heels_.Systems().size());
        profiler.EndFrame();

        Input::Instance().EndFrame();
    }
}

void Engine::RenderFrame()
{
    if (!renderer_ || !window_.IsOpen())
    {
        return;
    }

    ProfileScope renderScope("Render");

    // During a live resize the window changes size without a Resized event
    // reaching the game loop (it is parked inside the system's modal loop), so
    // the cached client size is refreshed here as well.
    window_.SyncSize();
    const int width = window_.Width();
    const int height = window_.Height();
    if (width != renderedWidth_ || height != renderedHeight_)
    {
        renderedWidth_ = width;
        renderedHeight_ = height;
        camera_.SetViewportSize(static_cast<float>(width), static_cast<float>(height));
        PinkUI::Instance().SetViewport(
            sf::FloatRect({0.f, 0.f}, {static_cast<float>(width), static_cast<float>(height)}));
    }

    // Display comfort is an engine-wide concern: the game only edits the values,
    // and every backend receives the same frame parameters.
    DisplaySettings& display = DisplaySettings::Instance();
    renderer_->SetPostProcess(display.PostProcess());
    renderer_->SetUiPixelShift(display.UiPixelShift());
    renderer_->SetUiDissolve(display.UiDissolve(), display.UiDissolveSeed());

    // One draw list for the whole frame: everything below submits data, the
    // backend sorts by (layer, z) and batches once.
    renderer_->BeginFrame();
    renderer_->SetCamera(camera_.View());
    renderer_->SetWorldTransform(camera_.WorldTransform());
    DrawList& FrameDrawList = renderer_->GetDrawList();

    // Interpolate between the last two fixed states so motion stays smooth
    // on displays whose refresh rate does not divide the fixed step.
    const float alpha = std::clamp(accumulator_ / kFixedStep, 0.f, 1.f);
    struct WorldTransform
    {
        float x = 0.f;
        float y = 0.f;
        float scaleX = 1.f;
        float scaleY = 1.f;
        float rotation = 0.f;
    };
    std::function<WorldTransform(Entity, int)> worldTransform = [&](Entity entity, int depth) -> WorldTransform
    {
        WorldTransform local;
        if (const TransformComponent* t = registry_.try_get<TransformComponent>(entity))
        {
            local.x = t->x;
            local.y = t->y;
            local.scaleX = t->scaleX;
            local.scaleY = t->scaleY;
            local.rotation = t->rotation;
            if (t->hasPrev)
            {
                local.x = t->prevX + (t->x - t->prevX) * alpha;
                local.y = t->prevY + (t->y - t->prevY) * alpha;
                local.scaleX = t->prevScaleX + (t->scaleX - t->prevScaleX) * alpha;
                local.scaleY = t->prevScaleY + (t->scaleY - t->prevScaleY) * alpha;
                local.rotation = LerpAngle(t->prevRotation, t->rotation, alpha);
            }
        }
        if (depth > 64)
        {
            return local;
        }
        const HierarchyComponent* hierarchy = registry_.try_get<HierarchyComponent>(entity);
        if (hierarchy == nullptr || hierarchy->parent == kNoParent)
        {
            return local;
        }
        const Entity parent = static_cast<Entity>(hierarchy->parent);
        if (parent == entity || !registry_.valid(parent) ||
            !registry_.all_of<TransformComponent>(parent))
        {
            return local;
        }
        const WorldTransform parentWorld = worldTransform(parent, depth + 1);
        constexpr float pi = 3.14159265358979323846f;
        const float radians = parentWorld.rotation * pi / 180.f;
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        WorldTransform result;
        result.x = parentWorld.x + c * local.x * parentWorld.scaleX - s * local.y * parentWorld.scaleY;
        result.y = parentWorld.y + s * local.x * parentWorld.scaleX + c * local.y * parentWorld.scaleY;
        result.scaleX = parentWorld.scaleX * local.scaleX;
        result.scaleY = parentWorld.scaleY * local.scaleY;
        result.rotation = parentWorld.rotation + local.rotation;
        return result;
    };

    auto view = registry_.view<TransformComponent, SpriteComponent>();
    for (auto e : view)
    {
        const auto& s = view.get<SpriteComponent>(e);
        const WorldTransform world = worldTransform(e, 0);

        const sf::FloatRect source({static_cast<float>(s.srcX), static_cast<float>(s.srcY)},
                                   {static_cast<float>(s.srcW), static_cast<float>(s.srcH)});
        FrameDrawList.Sprite(s.textureId, sf::Vector2f(world.x, world.y),
                             sf::Vector2f(world.scaleX, world.scaleY), world.rotation, s.tint, s.z,
                             DrawLayer::Entities, source, sf::Vector2f(s.originX, s.originY), s.flipX,
                             s.flipY);
    }

    // Game layers get the same draw list the engine uses. This is the public
    // extension point for custom world/HUD rendering.
    Application::Instance().Render(FrameDrawList);

    // Engine-wide subtitles. A game only calls Accessibility::AddSubtitle();
    // the overlay, scale, wrapping and expiry are handled here so accessibility
    // support is not something every project has to reimplement.
    Accessibility& accessibility = Accessibility::Instance();
    if (accessibility.SubtitlesEnabled() && !accessibility.Subtitles().empty())
    {
        const float scale = accessibility.SubtitleScale();
        const unsigned fontSize = static_cast<unsigned>(std::lround(24.f * scale));
        std::string caption;
        for (const Accessibility::Subtitle& subtitle : accessibility.Subtitles())
        {
            if (!caption.empty())
            {
                caption += "\n";
            }
            caption += subtitle.text;
        }
        const float maxWidth = static_cast<float>(width) * 0.74f;
        const unsigned lineCount = 1 + static_cast<unsigned>(std::count(caption.begin(), caption.end(), '\n'));
        const float boxHeight = (static_cast<float>(fontSize) * 1.35f * static_cast<float>(lineCount)) + 18.f;
        const float boxWidth = maxWidth;
        const sf::FloatRect box({(static_cast<float>(width) - boxWidth) * 0.5f,
                                 static_cast<float>(height) - boxHeight - 32.f * scale},
                                {boxWidth, boxHeight});
        FrameDrawList.Rect(box, sf::Color(0, 0, 0, 185), sf::Color::Transparent, DrawCommand::kNoThickness, 0.f,
                   DrawLayer::Overlay);
        FrameDrawList.DrawRichText(caption, "", {static_cast<float>(width) * 0.5f, box.position.y + 9.f}, fontSize,
                           sf::Color::White, 1.f, DrawLayer::Overlay, TextAlign::Center);
    }

    // Particles and the crowd go through the same list, so they are ordered
    // by layer instead of always landing under (or over) everything.
    Sparkles::Instance().Render(FrameDrawList);
    CrowdController::Instance().Render(FrameDrawList);

    if (profilerOverlay_)
    {
        DrawProfilerOverlay(FrameDrawList);
    }

    Console::Instance().Render(FrameDrawList, static_cast<float>(width), static_cast<float>(height));

    renderer_->EndFrame();

    // Capture after the draw list has been flushed but before the swap, so the
    // saved image is exactly the frame that is about to be shown.
    if (!screenshotPath_.empty() && --screenshotCountdown_ <= 0)
    {
        sf::Texture frame(
            sf::Vector2u(static_cast<unsigned>(std::max(1, width)), static_cast<unsigned>(std::max(1, height))));
        frame.update(window_.Handle());
        const bool saved = frame.copyToImage().saveToFile(screenshotPath_);
        BOY_INFO("Engine", std::string(saved ? "screenshot saved: " : "screenshot failed: ") + screenshotPath_);
        screenshotPath_.clear();
        screenshotCountdown_ = 0;
        Application::Instance().Quit();
    }

    renderer_->Present();
}

void Engine::DrawProfilerOverlay(Skirt& FrameDrawList)
{
    Profiler& profiler = Profiler::Instance();
    const FrameProfile frame = profiler.LastFrame();
    const std::vector<ProfileSample> scopes = profiler.AverageScopes(120);

    const float width = 430.f;
    const float height = 178.f;
    // The profiler is a debug overlay: it must sit above every game panel, not
    // disappear behind a menu that happens to use the same draw layer.
    FrameDrawList.Rect(sf::FloatRect({10.f, 10.f}, {width, height}), sf::Color(8, 10, 16, 220), sf::Color(90, 190, 255, 180),
               1.f, 10000, DrawLayer::Overlay);

    char line[256];
    std::snprintf(line, sizeof(line), "FPS %.1f   frame %.2f ms   draw calls %zu   verts %zu", profiler.AverageFps(120),
                  frame.frameMs, frame.drawCalls, frame.vertices);
    FrameDrawList.Text(line, "default", {18.f, 17.f}, 14, sf::Color(210, 240, 255), 10001, DrawLayer::Overlay);

    std::snprintf(line, sizeof(line), "pre %.2f  fixed %.2f  post %.2f  render %.2f ms", frame.preUpdateMs,
                  frame.fixedUpdateMs, frame.postUpdateMs, frame.renderMs);
    FrameDrawList.Text(line, "default", {18.f, 37.f}, 13, sf::Color(170, 200, 220), 10001, DrawLayer::Overlay);

    std::snprintf(line, sizeof(line), "entities %zu   systems %zu   textures %zu   commands %zu", frame.entities,
                  frame.systems, frame.textureBinds, frame.commands);
    FrameDrawList.Text(line, "default", {18.f, 55.f}, 13, sf::Color(170, 200, 220), 10001, DrawLayer::Overlay);

    float y = 79.f;
    std::size_t shown = 0;
    for (const ProfileSample& sample : scopes)
    {
        // Phase rows are already shown above; the useful list is the systems.
        if (sample.name == "PreUpdate" || sample.name == "FixedUpdate" || sample.name == "PostUpdate" ||
            sample.name == "Render")
        {
            continue;
        }
        std::snprintf(line, sizeof(line), "%-30s %6.2f ms", sample.name.c_str(), sample.milliseconds);
        FrameDrawList.Text(line, "default", {18.f, y}, 12, sf::Color(140, 230, 180), 10001, DrawLayer::Overlay);
        y += 17.f;
        if (++shown >= 5)
        {
            break;
        }
    }
    if (shown == 0)
    {
        FrameDrawList.Text("(no system samples yet)", "default", {18.f, y}, 12, sf::Color(130, 140, 150), 10001,
                   DrawLayer::Overlay);
    }
}

void Engine::RequestScreenshot(const std::string& path, int afterFrames)
{
    screenshotPath_ = path;
    screenshotCountdown_ = std::max(1, afterFrames);
}

void Engine::SetTimeScale(float scale)
{
    if (!std::isfinite(scale))
    {
        return;
    }
    timeScale_ = std::clamp(scale, 0.f, 8.f);
}

void Engine::SetSplashDuration(float seconds)
{
    if (!std::isfinite(seconds))
    {
        return;
    }
    splashDuration_ = std::clamp(seconds, 0.f, 10.f);
}

void Engine::SetAntiAliasing(AntiAliasingMode mode)
{
    antiAliasingMode_ = mode;
    if (renderer_)
    {
        renderer_->SetAntiAliasing(mode);
    }
    window_.SetAntiAliasingLevel(AntiAliasingSampleCount(mode));
}

void Engine::SetRenderScale(float scale)
{
    if (!std::isfinite(scale))
    {
        return;
    }
    if (renderer_)
    {
        renderer_->SetRenderScale(std::clamp(scale, 0.5f, 2.f));
    }
}

float Engine::RenderScale() const
{
    return renderer_ ? renderer_->RenderScale() : 1.f;
}

void Engine::SetFrameLimit(bool vsync, int fps)
{
    if (!window_.IsOpen())
    {
        return;
    }

    // Applied through the window object: callers that only need a limit must
    // not reach for the underlying RenderWindow.
    window_.SetVerticalSync(vsync);
    window_.SetFramerateLimit(vsync ? 0 : std::max(0, fps));
}

sf::Vector2u Engine::WindowSize() const
{
    return {static_cast<unsigned>(std::max(0, window_.Width())), static_cast<unsigned>(std::max(0, window_.Height()))};
}

void Engine::SetStudioSplash(const std::string& imagePath, const std::string& caption)
{
    studioSplashPath_ = imagePath;
    studioSplashCaption_ = caption;
}

void Engine::SetPublisherSplash(const std::string& imagePath, const std::string& caption)
{
    publisherSplashPath_ = imagePath;
    publisherSplashCaption_ = caption;
}

void Engine::Shutdown()
{
    // Idempotent: also called from the failure paths in initialize(), where the
    // scheduler may never have started.
    LiveResize::Instance().Detach();
    if (initialized_)
    {
        Application::Instance().Quit();

        // Stop gameplay systems before tearing down the subsystems they talk to.
        if (GameHost::Instance().IsJoined())
        {
            GameHost::Instance().Leave();
        }
        if (GameHost::Instance().IsHosting())
        {
            GameHost::Instance().StopHost();
        }
        AudioSystem::Instance().StopAllSounds();
        AudioSystem::Instance().StopMusic();
        Sparkles::Instance().Clear();

        // Systems go down in reverse phase/order.
        heels_.ShutdownAll();
    }
    heels_.Clear();

    if (renderer_)
    {
        renderer_->Shutdown();
        renderer_.reset();
    }
    canvas_.Shutdown();
    Application::Instance().Shutdown();
    if (window_.IsOpen())
    {
        window_.Handle().close();
    }
    initialized_ = false;
    BOY_INFO("Engine", "shutdown complete");
}

} // namespace Fem
