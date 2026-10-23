// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
// The facade header is deliberately small: a game that includes Engine.hpp gets
// the engine object, not the 54 subsystem headers it can reach through it. Every
// getter that returns a subsystem is defined in Engine.cpp, so the types only
// need a forward declaration here.
#include "FemboyEngine/AntiAliasing.hpp"
#include "FemboyEngine/Camera2D.hpp"
#include "FemboyEngine/Canvas3D.hpp"
#include "FemboyEngine/ECS.hpp"
#include "FemboyEngine/FemboyScript.hpp"
#include "FemboyEngine/World.hpp"
#include "FemboyEngine/FemObject.hpp"
#include "FemboyEngine/Heels.hpp"
#include "FemboyEngine/IRenderer.hpp"
#include "FemboyEngine/Scripting.hpp"
#include "FemboyEngine/Window.hpp"
#include <memory>
#include <string>

namespace Fem
{

class Accessibility;
class AssetManager;
class AudioMixer;
class AudioSystem;
class Controller;
class CrashBoy;
class CrowdController;
class HotSwap;
class Input;
class JobSystem;
class Localization;
class ModManager;
class Network;
class PinkUI;
class PostFX;
class Profiler;
class SaveSystem;
class DrawList;
class Sparkles;
class TextureOptimizer;
class VisualScriptEditor;
class VoiceChat;
class Whisper;

// Engine facade: owns all subsystems.
class Engine : public FemObject
{
public:
    FEMCLASS_NO_CREATE(Engine, FemObject, false)
    static Engine& Instance();


    // `mode` defaults to borderless fullscreen so a project launches the way
    // players expect: the whole display, no frame, no video-mode switch.
    bool Initialize(const std::string& title, int w, int h, WindowMode mode = WindowMode::Borderless);
    void Run();
    void Shutdown();

    // Simulation time control. PreUpdate systems marked unscaled (input,
    // networking, audio, controller) keep real time while paused.
    void SetTimeScale(float scale);
    float TimeScale() const
    {
        return timeScale_;
    }
    void SetPaused(bool paused)
    {
        paused_ = paused;
    }
    bool IsPaused() const
    {
        return paused_;
    }
    Window& GetWindow()
    {
        return window_;
    }
    IRenderer& Renderer()
    {
        return *renderer_;
    }
    // The frame scheduler. Games can add their own systems to a phase instead of
    // editing Engine::Run().
    Heels& Systems()
    {
        return heels_;
    }
    // Subsystem accessors. These are defined in Engine.cpp: Engine.hpp only
    // forward-declares the subsystem classes, so including the facade no longer
    // pulls fifty headers into every translation unit.
    Profiler& GetProfiler();
    World& GetWorld()
    {
        return world_;
    }
    const World& GetWorld() const
    {
        return world_;
    }
    // Compatibility for systems that still need raw ECS access. New gameplay
    // code should use GetWorld() for lifecycle and views.
    Registry& GetRegistry()
    {
        return registry_;
    }
    Input& GetInput();
    AssetManager& Assets();
    Localization& GetLocalization();
    AudioSystem& Audio();
    VoiceChat& Voice();
    ScriptVM& Script()
    {
        return script_;
    }
    FemboyScript& GetFemboyScript()
    {
        return femboyScript_;
    }
    Sparkles& GetSparkles();
    JobSystem& Jobs();
    SaveSystem& Saves();
    PostFX& GetPostFx();
    AudioMixer& GetAudioMixer();
    HotSwap& GetHotSwap();
    CrowdController& Crowd();
    Whisper& GetWhisper();
    VisualScriptEditor& VisualScript();
    Network& GetNetwork();
    Accessibility& GetAccessibility();
    Controller& GetController();
    TextureOptimizer& GetTextureOptimizer();
    CrashBoy& GetCrashBoy();
    // VideoPlayer is instantiated per-video
    // GameFramework is used via Level/GameMode/GameState subclasses
    PinkUI& Ui();
    Canvas3D& Canvas()
    {
        return canvas_;
    }
    Camera2D& Camera()
    {
        return camera_;
    }
    ModManager& Mods();

    // Renders one frame from the current state without advancing the
    // simulation. Called by the main loop and by the live-resize pump, which is
    // why it must stay side-effect free apart from drawing.
    void RenderFrame();

    // In-engine diagnostics overlay: frame time, FPS, renderer counters and the
    // three most expensive systems. It is drawn through the normal draw list on
    // DrawLayer::Overlay, so it works with every renderer backend and cannot
    // disturb scene depth. Disabled by default.
    void SetProfilerOverlay(bool enabled)
    {
        profilerOverlay_ = enabled;
    }
    bool ProfilerOverlay() const
    {
        return profilerOverlay_;
    }

    // Render-resolution multiplier, clamped to [0.5, 2.0]. 1.0 renders at the
    // window resolution, below 1.0 renders fewer pixels and upscales (faster),
    // above 1.0 renders more and downsamples (supersampling: cleaner edges).
    // Applied by the backend through IRenderer::SetRenderScale().
    void SetRenderScale(float scale);
    // Post AA (FXAA) applies immediately. MSAA is a context property: its sample
    // count is stored and used by the next window Create()/SetMode(), which the
    // game already does when the resolution or window mode changes.
    void SetAntiAliasing(AntiAliasingMode mode);
    AntiAliasingMode AntiAliasing() const
    {
        return antiAliasingMode_;
    }
    float RenderScale() const;

    // Vertical sync and an FPS cap without handing the game a window handle.
    // fps <= 0 with vsync off means uncapped.
    void SetFrameLimit(bool vsync, int fps);

    // Client size of the last presented frame, in pixels.
    sf::Vector2u WindowSize() const;

    // Saves the next finished frame to a PNG (after `afterFrames` frames) and
    // stops the loop. Used by the game's --screenshot flag and by visual tests:
    // capturing the engine's own back buffer works under DPI scaling, on a
    // machine with another app in front, and without a desktop capture tool.
    void RequestScreenshot(const std::string& path, int afterFrames = 1);

    // Boot splash: a black screen with the engine's catboy mark, shown once at
    // the start of run(). It is generated in code, so a packaged game does not
    // need any extra asset. Games that need an instant start (automation,
    // headless capture, a project with its own intro) turn it off before run().
    void SetSplashEnabled(bool enabled)
    {
        splashEnabled_ = enabled;
    }
    bool IsSplashEnabled() const
    {
        return splashEnabled_;
    }
    // Seconds from the first frame to the last; 2 s is the default. Clamped to
    // [0, 10]; zero disables the animation without changing the enable flag.
    void SetSplashDuration(float seconds);
    // Optional studio / publisher cards that play before the engine mark, so a
    // project can sign its work without the engine owning the art. A card with
    // only a caption is text on black; a card with only a path is the image
    // alone; leaving both empty skips the card entirely.
    void SetStudioSplash(const std::string& imagePath, const std::string& caption = "");
    void SetPublisherSplash(const std::string& imagePath, const std::string& caption = "");
    // True while the user is dragging a window border (Windows only).
    bool IsLiveResizing() const;

private:
    void DrawProfilerOverlay(Skirt& FrameDrawList);

    Window window_;
    std::unique_ptr<IRenderer> renderer_;
    AntiAliasingMode antiAliasingMode_ = AntiAliasingMode::Off;
    Heels heels_;
    Registry registry_;
    World world_{registry_};
    ScriptVM script_;
    FemboyScript femboyScript_;
    Canvas3D canvas_;
    Camera2D camera_;
    float accumulator_ = 0.f;
    int renderedWidth_ = 0;
    int renderedHeight_ = 0;
    float timeScale_ = 1.f;
    bool paused_ = false;
    bool initialized_ = false;
    std::string screenshotPath_;
    int screenshotCountdown_ = 0;
    bool profilerOverlay_ = false;
    bool splashEnabled_ = true;
    bool splashShown_ = false;
    float splashDuration_ = 2.f;
    std::string studioSplashPath_;
    std::string studioSplashCaption_;
    std::string publisherSplashPath_;
    std::string publisherSplashCaption_;
};

} // namespace Fem
