# Vulkan backend plan

Status: **proposal, not implemented**. The shipped renderer is `SFMLRenderer`.
The game does not need Vulkan today: Neon Triangle is a 2D twin-stick shooter
whose frame cost is dominated by CPU simulation, text and a few hundred sprites.
Vulkan becomes worth its maintenance cost when the engine needs one of these:

* GPU compute (particles, cellular fields, pathfinding or physics preprocessing);
* bindless textures or descriptor indexing;
* indirect / multi-draw submission for very large sprite and tile counts;
* custom render passes or pipelines that SFML 3 cannot express.

This document describes how to add it without rewriting the engine or the game.
It is also the answer to "how do we implement Vulkan in *our* engine": add a
second `IRenderer`, not a second engine.

## 1. What is already in the right shape

The renderer boundary already exists:

* `IRenderer` (`Engine/include/FemboyEngine/IRenderer.hpp`) owns
  initialization, frame begin/end, clear, present, camera, render scale,
  anti-aliasing and the one `Skirt& skirt()` submission surface.
* `DrawList` / `DrawCommand` (`Engine/include/FemboyEngine/Skirt.hpp`) are the
  data-only draw list. Every visual system submits sprites, text, rectangles,
  circles, lines, polygons, pre-built vertices and a lightmap into it.
* `Engine` owns `std::unique_ptr<IRenderer> renderer_` and exposes
  `IRenderer& renderer()`. Gameplay, audio, networking, physics and the editor
  do not know which backend is active.
* `SFMLRenderer` already performs sorting, batching, render-scale offscreen
  rendering and FXAA using the same draw list a Vulkan backend would consume.
* `RenderStats` (commands, draw calls, texture binds, vertices) is the existing
  measurement hook, so a new backend can be compared with the SFML one rather
  than judged by feel.

The parts that are still SFML-specific are equally important:

* `DrawCommand` stores `const sf::Texture*`, `sf::VertexArray`, `sf::Font` ids
  and `sf::BlendMode`.
* `IRenderer::window()` returns `sf::RenderWindow*`.
* `IRenderer::setCamera()` / `camera()` use `sf::View`.
* Text layout currently relies on `sf::Text` / `sf::Font` through the SFML
  backend.

Those are the migration surface. They are deliberately small, but they cannot
be ignored: a Vulkan backend that calls `sf::Texture` or `sf::Text` would still
be an OpenGL renderer with extra steps.

## 2. Target shape

```
game / engine systems
        |
        v
      Skirt  (backend-neutral draw list)
        |
        v
   IRenderer
   /        \
SFMLRenderer  VulkanRenderer
 (default)      (opt-in)
```

`VulkanRenderer` should live in `Engine/src/vulkan/` and implement only
`IRenderer`. The game continues to submit the same `DrawCommand`s.

Backend selection belongs in `Engine`:

```cpp
#if defined(FEMBOY_ENABLE_VULKAN)
    if (config.renderer == RenderBackend::Vulkan)
        renderer_ = std::make_unique<VulkanRenderer>();
    else
#endif
        renderer_ = std::make_unique<SFMLRenderer>();
```

`FEMBOY_ENABLE_VULKAN` is a CMake option and defaults to `OFF` until the backend
reaches parity. A build without the Vulkan SDK must still compile and run the
SFML backend. No game source file should need an `#ifdef` for this.

## 3. Window and surface

Do **not** open a second OS window. The engine already owns a window, input,
DPI handling, fullscreen/borderless switching and the editor's ImGui host. The
Vulkan backend should attach to that window:

1. `sf::Window::getNativeHandle()` gives an `HWND` on Windows and the native
   handle on X11/Wayland/macOS.
2. Create `VkInstance` with the platform surface extension:
   `VK_KHR_win32_surface`, `VK_KHR_xcb_surface` / `VK_KHR_wayland_surface`, or
   the MoltenVK/`VK_EXT_metal_surface` path on macOS.
3. Create `VkSurfaceKHR` from that handle.
4. Pick a physical device and queue families that support graphics, present and
   (optionally) transfer/compute.
5. Create the logical device and swapchain.
6. On resize/fullscreen change, destroy and recreate the swapchain, image views
   and framebuffers. The existing `Window`/`Settings` path already defers window
   recreation; the Vulkan backend needs one more deferred `recreateSwapchain()`.

Windows is the first target because it is the only platform verified on this
machine. Linux and macOS should use the same `IRenderer` code with a different
surface extension block; MoltenVK is the pragmatic macOS route.

## 4. CPU/GPU resources

The current `DrawCommand` cannot be uploaded directly. Introduce a small,
backend-neutral resource layer before writing any Vulkan pipeline code:

```cpp
struct TextureHandle { std::uint32_t id = 0; };
struct FontHandle    { std::uint32_t id = 0; };
```

* `AssetManager` owns the CPU-side image/font data and hands out handles.
* `SFMLRenderer` resolves a handle to its existing `sf::Texture`.
* `VulkanRenderer` resolves it to a `VkImage` + `VkImageView` + sampler and
  uploads on first use through a staging buffer.
* Texture ids remain strings at the game-facing API (`Skirt::sprite("player")`,
  `Skirt::text(..., "default")`), so game code does not change. The string lookup
  happens once per command or is cached by the renderer.
* Keep a fallback white texture for untextured shapes and a 1x1 white pixel for
  solid-colour batches.

For text, use FreeType directly (the engine already depends on it transitively)
and a glyph atlas:

* cache glyphs by `(font, codepoint, pixel size)`;
* upload an R8 atlas;
* build a vertex/index buffer from shaped runs;
* keep the existing `RichText` style runs and fallback-font rules;
* if Arabic/CJK shaping must match SFML exactly, add HarfBuzz before the glyph
  cache. That is a separate dependency and should be justified by a real
  shaping test, not added speculatively.

A Vulkan implementation should use one large persistently mapped staging ring
for per-frame vertices and indices, then copy into device-local buffers. Do not
create a buffer per draw command.

## 5. Pipelines and batching

The first backend only needs a handful of pipelines:

* **Shape/sprite pipeline** - position, UV, colour; alpha blending; optional
  multiply blend for the lightmap.
* **Text pipeline** - R8 glyph atlas, alpha blending.
* **Lightmap pipeline** - multiply blend, full-screen or world quad.
* **FXAA pipeline** - full-screen post pass when anti-aliasing is FXAA.

Batching key:

```
pipeline + texture + blend mode + scissor
```

Rectangles, circles, lines and sprites can share the shape pipeline until the
texture or blend changes. Text breaks the batch per atlas/page, which is already
what the SFML backend does per font run. `RenderStats::textureBinds` is the
number to watch: if it grows, add texture arrays or a bindless descriptor set,
not more draw calls.

For Neon Triangle, the likely result is a handful of draws per frame, not
thousands. Vulkan's win is not "more draw calls"; it is predictable submission
and access to compute/indirect later.

## 6. Frame flow

```
beginFrame()
  acquire swapchain image
  reset command pool / staging ring
  clear target
  sort Skirt by (layer, z)
  upload vertices/indices
  record world pass (Background ... Lighting)
  record UI/Overlay/Debug
  optional render-scale resolve + FXAA
  transition image to PRESENT_SRC
endFrame()
  submit
  present
  wait on frame fence (2-3 frames in flight)
```

Render scale works the same way as `SFMLRenderer`: render into an offscreen
colour image at `window * scale`, then blit/resolve to the swapchain. MSAA is a
sample-count on that image; FXAA is a post pass. Both are already modelled by
`AntiAliasingMode`, so the settings UI does not change.

The camera can stay `sf::View` for the CPU side: it is just a matrix and a
viewport. Convert it to a `mat4` upload; do not make gameplay depend on Vulkan
types.

## 7. Migration stages

### Stage 0 - keep the default green

* Add `FEMBOY_ENABLE_VULKAN` (default `OFF`).
* Add `find_package(Vulkan)` and an optional `vulkan` link target.
* Keep `SFMLRenderer` as the only registered backend and keep all tests passing.
* Add a screenshot/regression harness that can render the same scene with either
  backend.

### Stage 1 - resource abstraction

* Add `TextureHandle` / `FontHandle` and resolve them in `SFMLRenderer`.
* Replace direct `sf::Texture*`/`sf::Font` use in `DrawCommand` only where the
  backend actually needs it. Keep source compatibility shims while the game and
  editor are migrated.
* Add a CPU image/font cache owned by `AssetManager`.
* Verify the SFML backend still produces pixel-identical screenshots.

### Stage 2 - Vulkan vertical slice

* Instance, surface, device, swapchain, clear and present.
* Window resize, fullscreen and close.
* One hard-coded full-screen triangle to prove the surface and synchronization.
* Validation layers and a debug messenger in debug builds only.

### Stage 3 - sprites and shapes

* Shape/sprite pipeline, staging ring, white texture.
* Sprite source rect, origin, rotation, flip, tint, scissor, alpha/multiply
  blend.
* `RenderStats` instrumented from the actual Vulkan submission.
* Neon Triangle's menu and one arena rendered without SFML drawing calls.

### Stage 4 - text

* FreeType glyph atlas, rich-text runs, fallback fonts, shadow, alignment.
* Compare screenshots against the SFML backend at 720p and 1080p.
* Keep a per-run batch break; do not regress kerning/shaping for a small draw
  count win.

### Stage 5 - lighting, render scale, FXAA

* Offscreen colour target, resolve and blit.
* Lightmap multiply pass.
* FXAA post pass and MSAA sample counts.
* Recreate all size-dependent resources on resize.

### Stage 6 - the features that justified Vulkan

Only now add compute, descriptor indexing/bindless and indirect draw:

* compute particles or field simulation, with the CPU path retained;
* texture arrays for atlases;
* `vkCmdDrawIndexedIndirectCount` for tile/sprite batches;
* optional async compute queue.

These are additions to the backend, not changes to `DrawList` or gameplay.

## 8. Testing and acceptance

The Vulkan backend is done when:

* `femboy_tests` still pass with `FEMBOY_ENABLE_VULKAN=OFF` and `ON`.
* The game starts, resizes, toggles borderless/fullscreen, minimizes and exits
  cleanly under validation layers.
* A captured frame matches the SFML frame within a small tolerance (text
  rasterization may differ by a pixel) for menu, gameplay, lighting and UI.
* `RenderStats` reports the same command count and fewer or equal texture binds.
* A 60-second gameplay run produces no validation errors and no device-lost
  events.
* The release package still works on a machine without the Vulkan SDK installed;
  the loader DLL is either shipped or the backend falls back to SFML with a clear
  log line.

## 9. Effort and decision

A vertical slice (window, clear, sprites, shapes) is roughly 1-2 weeks of focused
work for someone comfortable with Vulkan synchronization. Production parity
(text, lighting, render scale, FXAA, resize, packaging, tests) is closer to 4-8
weeks, and compute/bindless/MDI is another project on top.

That is why this pass does **not** start it. The recommended sequence is:

1. Ship the current SFML renderer with `RenderStats` telemetry.
2. Measure the actual CPU/SFML frame cost in Neon Triangle and a heavy scene.
3. If the bottleneck is GPU work that SFML cannot express (compute, bindless,
   indirect), implement the stages above and keep SFML as the fallback.
4. If the bottleneck is just 2D draw submission, optimize the existing batcher
   first; it is far cheaper than a second GPU backend.

Vulkan is an engine capability, not a graphics rewrite. The engine's one draw
list and one `IRenderer` boundary are what make it possible without touching the
game.
