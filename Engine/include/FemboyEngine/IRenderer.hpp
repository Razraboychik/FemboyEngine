// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Graphics.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/Skirt.hpp"
#include "FemboyEngine/AntiAliasing.hpp"
#include "FemboyEngine/Display.hpp"

namespace Fem
{

// CPU-side telemetry for one frame. It lets a game or the editor prove that a
// batching change actually reduced work instead of guessing from the frame rate.
struct RenderStats
{
    std::size_t commands = 0;
    std::size_t drawCalls = 0;
    std::size_t textureBinds = 0;
    std::size_t vertices = 0;
};

// Abstract renderer backend.
//
// There is exactly one drawing path in the engine: systems push data-only
// commands into FrameDrawList(), and the backend sorts them by (layer, z), batches them
// and presents. Swapping SFML for another backend therefore means translating
// DrawCommand, not re-implementing depth sorting or text interleaving.
class IRenderer : public IInterface
{
public:
    virtual ~IRenderer() = default;
    static const char* InterfaceName()
    {
        return "IRenderer";
    }

    virtual bool Initialize(int w, int h, const std::string& title) = 0;
    virtual void Shutdown() = 0;

    // Clears the target and resets the draw list.
    virtual void BeginFrame() = 0;
    // Sorts the draw list by (layer, z), flushes it and resets the list.
    virtual void EndFrame() = 0;
    virtual void Clear(const sf::Color& c) = 0;
    virtual void Present() = 0;
    virtual bool IsOpen() const = 0;
    // Backends that cannot measure return the default (all zeroes).
    virtual RenderStats GetRenderStats() const
    {
        return {};
    }

    // The submission surface every visual system writes to.
    virtual DrawList& GetDrawList() = 0;
    // Compatibility accessor for code written before DrawList got its name.
    DrawList& GetSkirt()
    {
        return GetDrawList();
    }

    // camera
    virtual void SetCamera(const sf::View& view) = 0;
    virtual sf::View Camera() const = 0;
    // Affine world projection (isometric, oblique, ...). Applied only to
    // world-space layers, never to UI/overlay pixels. Identity by default.
    virtual void SetWorldTransform(const sf::Transform& transform)
    {
        (void)transform;
    }
    virtual sf::Transform WorldTransform() const
    {
        return sf::Transform::Identity;
    }

    // Render-resolution multiplier. 1.0 renders at the window resolution; below
    // 1 renders fewer pixels and upscales (cheaper, softer); above 1 renders
    // more pixels and downsamples (supersampling: cleaner edges and text, more
    // GPU cost). Backends without an offscreen target may ignore it.
    virtual void SetRenderScale(float scale)
    {
        (void)scale;
    }
    virtual float RenderScale() const
    {
        return 1.f;
    }

    // Post-process anti-aliasing (currently FXAA). MSAA modes are applied when
    // the window is created, because a multisampled context cannot be changed in
    // place; the backend still records the mode so it can report it.
    virtual void SetAntiAliasing(AntiAliasingMode mode)
    {
        (void)mode;
    }
    virtual AntiAliasingMode AntiAliasing() const
    {
        return AntiAliasingMode::Off;
    }

    // Final colour grading / post effects. Backends that cannot implement the
    // full set may ignore this; the game still works.
    virtual void SetPostProcess(const PostProcessParams& params)
    {
        (void)params;
    }
    // OLED protection for screen-space UI. The renderer shifts DrawLayer::UI
    // and DrawLayer::Overlay by an integer pixel offset; world geometry stays
    // put. Set {0,0} to disable.
    virtual void SetUiPixelShift(sf::Vector2i shift)
    {
        (void)shift;
    }
    // OLED UI dissolve. `amount` is 0 (fully visible) .. 1 (fully hidden);
    // `seed` makes the per-draw disappearance order stable for one idle cycle.
    // Backends that cannot fade individual UI commands may ignore it.
    virtual void SetUiDissolve(float amount, std::uint32_t seed)
    {
        (void)amount;
        (void)seed;
    }

    // window
    virtual int Width() const = 0;
    virtual int Height() const = 0;
    virtual sf::RenderWindow* GetWindow() = 0;

    // Lets the engine share its single OS window with the backend instead of the
    // backend opening a second one. Must be called before initialize().
    virtual void SetExternalWindow(sf::RenderWindow* window) = 0;

    // info
    virtual const char* BackendName() const = 0;
    const char* GetInterfaceName() const override
    {
        return "IRenderer";
    }
    bool ImplementsInterface(const std::string& name) const override
    {
        return name == "IRenderer" || IInterface::ImplementsInterface(name);
    }
};

// SFML implementation of IRenderer: the only backend shipped today.
class SFMLRenderer : public IRenderer
{
public:
    FEMCLASS_CREATABLE(SFMLRenderer, IRenderer, false)
    SFMLRenderer();
    ~SFMLRenderer() override;

    bool Initialize(int w, int h, const std::string& title) override;
    void Shutdown() override;

    void BeginFrame() override;
    void EndFrame() override;
    void Clear(const sf::Color& c) override;
    void Present() override;
    bool IsOpen() const override;
    RenderStats GetRenderStats() const override
    {
        return stats_;
    }

    DrawList& GetDrawList() override
    {
        return FrameDrawList_;
    }

    void SetCamera(const sf::View& view) override;
    sf::View Camera() const override;
    void SetWorldTransform(const sf::Transform& transform) override
    {
        worldTransform_ = transform;
    }
    sf::Transform WorldTransform() const override
    {
        return worldTransform_;
    }
    void SetRenderScale(float scale) override;
    float RenderScale() const override;
    void SetAntiAliasing(AntiAliasingMode mode) override;
    AntiAliasingMode AntiAliasing() const override
    {
        return antiAliasingMode_;
    }
    void SetPostProcess(const PostProcessParams& params) override;
    void SetUiPixelShift(sf::Vector2i shift) override;
    void SetUiDissolve(float amount, std::uint32_t seed) override;
    int Width() const override;
    int Height() const override;
    sf::RenderWindow* GetWindow() override;
    void SetExternalWindow(sf::RenderWindow* window) override;

    const char* BackendName() const override
    {
        return "SFML";
    }

private:
    sf::RenderWindow* ActiveWindow();
    const sf::RenderWindow* ActiveWindow() const;
    bool EnsureFxaa();
    bool EnsurePostShader();

    std::unique_ptr<sf::RenderWindow> ownedWindow_;
    sf::RenderWindow* externalWindow_ = nullptr;
    sf::View camera_;
    sf::Transform worldTransform_ = sf::Transform::Identity;
    int width_ = 0;
    int height_ = 0;
    DrawList FrameDrawList_;
    // Offscreen target used when the render scale is not 1.0. Kept alive across
    // frames: recreating it every frame would reallocate a GPU texture.
    sf::RenderTexture renderTexture_;
    float renderScale_ = 1.f;
    bool scaled_ = false;
    AntiAliasingMode antiAliasingMode_ = AntiAliasingMode::Off;
    sf::Shader fxaaShader_;
    bool fxaaReady_ = false;
    bool fxaaTried_ = false;
    PostProcessParams postParams_{};
    bool postActive_ = false;
    sf::Shader postShader_;
    bool postReady_ = false;
    bool postTried_ = false;
    // Intermediate target used only when FXAA and post effects are both on.
    sf::RenderTexture postTexture_;
    sf::Vector2i uiPixelShift_{0, 0};
    float uiDissolve_ = 0.f;
    std::uint32_t uiDissolveSeed_ = 1u;
    // Monotonic shader time for animated noise. A clock avoids tying the effect
    // to simulation time (which stops when the game is paused).
    sf::Clock postClock_;
    float postTime_ = 0.f;
    RenderStats stats_{};
};

} // namespace Fem
