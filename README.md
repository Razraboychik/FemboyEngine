# FemboyEngine

Universal modular 2D game engine in C++20. Supports multiple games (fighter, complex RPG) on one core.

## Components

- **FemboyEngine** - engine library (`Engine/`)
- **FemboyEditor** - ImGui-based editor (`Editor/`)
- **Pink** - UE5-like widget/UI builder (runtime, data-driven)
- **FemboyScript** - C#-like scripting language with strict typing; compiled-program cache, external `call()`, print sink
- **VisualScriptRuntime** - source-to-source graph compiler with locals, functions, arrays, `if`/`while`/`do`/`for`/`switch`, `break`/`continue`/`return`, math/string nodes and every gameplay event; compiles once, caches the parsed program and dispatches by name
- **Boy** - logging tool (speaks log messages)
- **CrashReporter** - crash reports with stack traces
- **GameFramework** - Character, PlayerController, HUD, GameInstance, Level, GameMode, GameState
- **FastRandom** - PCG PRNG (no allocations)
- **RandomSystem / CryptoRandom** - Global/Frame/Call gameplay streams, a serialisable `mt19937_64` network stream for lockstep and replays, and an OS-backed `CryptoRandom` for seeds and rare critical rolls
- **Reflection / FemInstancedStruct** - `FEMENUM`, `FEMSTRUCT`, `FEMPROPERTY` and `FEMFUNCTION` registries with UE-style `meta = (...)` descriptors, plus a type-checked owning struct value that round-trips through JSON
- **Procedural** - noise functions (Perlin, Simplex, FBM, Cellular)
- **Controller** - gamepad/joystick support
- **Video** - video playback (image sequences, FFmpeg)
- **Esthete** - descriptor-driven video box: poster fallback, overlay, hot swap, works offline
- **SoftBody** - Verlet rope/chain/spring, cloth with tearing, twisting ribbon and helix, rendered through the draw list
- **Wind** - wind fields (gust, vortex, hurricane, zone, composite) and weather presets driving soft bodies
- **Skeleton** - native bones, slots, region/weighted-mesh attachments, keyed clips with easing, two-bone IK, crossfade, events and hit testing (no third-party runtime)
- **ChunkedTilemap** - streaming tile chunks with a generator callback, async generation on `JobSystem`, an unload radius, disk cache and cached chunk geometry
- **VisibilityWorld** - occluder segments in a uniform grid, raycasts, line of sight and visibility polygons; shared by lighting, audio occlusion and gameplay
- **Lighting** - CPU lightmap with ambient, coloured lights, a visibility/SDF shadow mask, probes, AO and a multiply-blended `DrawLayer::Lighting`
- **DistanceField** - jump-flooding SDF with distance queries, AO sampling and `buildFromTilemap`
- **AudioOcclusion** - listener/source distance, wall transmission and a pitch-based muffle proxy on top of `VisibilityWorld`
- **VoiceChat** - miniaudio capture, 20 ms Opus frames over raw ENet channel 1, per-peer jitter buffer and one SFML playback stream; optional at build time
- **Wfc** - weighted wave-function-collapse with constraint propagation, backtracking and JSON round trip
- **Rollback** - input frames, local delay, remote prediction, snapshot/rewind and re-simulation callbacks
- **NativeModule** - Windows `LoadLibraryExW` / POSIX `dlopen` hot reload with generation-stamped DLL copies
- **RotationAtlas** - one-atlas baked sprite rotation with nearest filtering
- **RichText** - `{b}/{i}/{u}/{s}/{color=}/{size=}/{font=}` runs, per-run fallback, shadow and alignment
- **AntiAliasing** - Off, FXAA and MSAA 2x/4x/8x, applied by the engine and exposed in game settings
- **SettingsRegistry / Display** - typed, validated settings descriptors with JSON round-trip, plus engine-owned gamma, colour grading, post effects, OLED pixel shift/noise/idle dissolve and best-effort system eye-protection detection
- **Accessibility** - engine-wide UI scale, colorblind modes, contrast, subtitles with a size multiplier, reduced motion, reduced flashing, screen-reader volume and one-handed input state
- **Sockets / Joints** - named attachment points with world transforms, plus Box2D distance, revolute, weld, prismatic and wheel joint wrappers
- **Jobs / SIMD** - work-stealing parallel-for and reduce, calibration, CPU feature detection and SSE2/NEON-friendly helpers with scalar fallbacks
- **Time** - DateTime, explicit-token formatting, TimeRange, frame Timer, wall-clock Alarm
- **TextureOptimizer** - PNG optimization for GPU
- **Camera2D** - follow, shake, zoom, rotation
- **EventBus** - message bus for decoupled communication
- **Scene / Prefab** - versioned scene files, additive loading, parent prefabs with deep overrides and child prefabs, all driven by `Mirror`
- **AssetPipeline** - manifest validation, dependency order, import settings, greedy texture atlases, incremental hashes and cooked `.fempak` archives
- **Profiler** - bounded frame history, per-system scopes, renderer counters and an in-engine overlay
- **MemoryMacros** - RAII macros, no GC
- **SlotMap** - generational handles with stale-handle validation and budgeted deferred destruction; explicit screen/level ownership, no raw GC

## Architecture

- **FemObject** - the root engine object, analogous to UE's `UObject`; engine, window, renderer, input, settings, audio, voice and interface objects derive from it and expose `GetTypeName()` / `IsA<T>()`
- **Skirt** - the single layered draw list; every visual system submits commands, the backend sorts by `(layer, z)` and batches once
- **IRenderer / SFMLRenderer** - one renderer abstraction; a second backend only has to translate `DrawCommand`
- **Heels** - phase-based system scheduler (`PreUpdate` / `FixedUpdate` / `PostUpdate`) used by `Engine::Run`; systems marked *unscaled* keep real time while the game is paused
- **Profiler** - Heels opens a scope per system, `Engine` records each phase and the renderer counters; the editor draws the frame graph and the game can show the same data through `Engine::SetProfilerOverlay(true)`
- **Mirror** - reflection registry driving scene/prefab serialization, the editor inspector and component add/remove
- **Vanity** - durable file layer (unique temp file + fsync + atomic rename) with validated reads and safe file names
- **Result<T>** - value-or-error return type for fallible APIs

## Runtime guarantees

- **One frame, one draw list** - engine sprites, tilemaps, particles, crowd and game layers all end up in `Skirt`; `Layer::OnRender(Skirt&)` is the extension point, so depth is global instead of submission-ordered
- **Stable timing** - fixed-step simulation with accumulator clamping, `Engine::SetTimeScale()` / `Engine::SetPaused()`, and render interpolation between the last two steps (sprite-sheet frames included)
- **Sprite sheets** - `SpriteComponent` carries a source rectangle, origin pivot and flips; `AnimationComponent` drives them from the fixed step
- **Replication that cannot lose updates** - `Replication::PeekDelta()` + `CommitDelta()` clear dirty flags only after a successful send; late joiners get `SnapshotAll()`
- **Stable peer ids** - server-assigned session ids (starting at 1) instead of raw ENet indices; silent clients are timed out and kicked; `Network::ConnectAsync()` / `GameHost::JoinAsync()` never block the frame
- **Reconnect and flood control** - `GameHost::SetReconnectPolicy()` retries a lost client with exponential backoff, while `Network::SetReceiveRateLimit()` is an opt-in token bucket and `GetStats()` reports RTT, bytes and dropped packets
- **Scenes that compose** - scene files carry a version and migrate legacy `objects`; `LoadAdditive()` streams chunks into a live registry; prefabs inherit a parent, apply deep overrides and spawn child prefabs
- **One cooked archive** - `AssetCooker` writes a manifest and `assets.fempak`; `AssetManager::LoadPackedManifest()` extracts a per-build cache and loads the normal SFML paths, so a runtime does not need loose cooked files
- **Parallelism** - `JobSystem::ParallelFor(begin, end, fn)` splits a range across the pool and is safe to call from a worker; `ParallelReduce`, `ParallelInvoke`, dispatch-cost calibration and `SuggestedParallelItems` cover the other shapes. The particle integrator switches to the pool only above a measured threshold (calibrated on the current CPU), so small bursts stay on the calling thread
- **Measured hot spots** - sprite rotation uses `Simd::RotatePoints4` (2.7x on 500k sprites), the job pool reaches 14.8x on 24 threads, and particles gain 1.37x at 100k while 20k is neutral (hence the adaptive threshold). Cloth parallel solve measured 0.51x/0.83x and was deliberately left sequential
- **Lighting without a second backend** - a low-resolution CPU lightmap (default 0.25 px/world unit) is composited on `DrawLayer::Lighting` with multiply blending, so it darkens the world and particles but never the UI. Static AO comes from `DistanceField`; dynamic shadows reuse `VisibilityWorld`
- **Text** - `RichText` parses styled runs in one pass, keeps unknown tags verbatim, and falls back per run (not per glyph) so shaping and kerning survive. `AssetManager::FontForText()` probes installed CJK/emoji fonts lazily
- **Anti-aliasing** - FXAA is a full-screen post pass; MSAA is a context-creation property applied on the next `Window::Create()`/`SetMode()`. Pixel-art mode switches textures and fonts to nearest filtering
- **Boot splash** - `Engine::Run()` opens with the engine mark and wordmark, fading in and out per card; the mark is `Logo.png` embedded at configure time and the wordmark uses a real bold system font. Any key or click skips it, `Engine::SetSplashEnabled()` / `SetSplashDuration()` retime or disable it, and `SetStudioSplash()` / `SetPublisherSplash()` add the cards before it

## Build (Windows, VS2022)

```powershell
.\configure.bat
.\build.bat
.\build\FemboyEditor.exe
.\build\femboy_tests.exe        # engine suite
```

Third-party sources ship in `libs/`. The prebuilt SFML trees are intentionally
not tracked because they are large platform artifacts: the default
`FEMBOY_FETCH_SFML=ON` downloads and builds SFML 3.1 when no local
`FEMBOY_SFML_ROOT` is supplied. The other dependencies (box2d, ENet, ImGui,
EnTT, nlohmann/json, Opus, Catch2) are read from `libs/` and are not fetched
automatically.

## Source layout

Engine implementation files are grouped under `Engine/src/<Module>/`:
`Core`, `Assets`, `Audio`, `Graphics`, `Input`, `Networking`, `Physics`,
`Scripting`, `UI` and `World`. Public headers stay flat under
`Engine/include/FemboyEngine/`, so game includes remain stable and capitalised
(`FemboyEngine/Engine.hpp`, `FemboyEngine/Input.hpp`, ...).

Engine, editor, test and tool directories use PascalCase. Build output
(`build/`) and third-party vendor sources (`libs/`) keep their lowercase
CMake/vendor names. QA screenshots, logs and crash dumps are not kept in the
source or build tree; write them to a temporary directory instead.

Game projects are not stored in this repository. A game lives in its own folder
or separate repository and consumes the engine through
`FEMBOY_ENGINE_ROOT`/`add_subdirectory`, exactly like any external project.

Linux and macOS build the same tree with the scripts in the repository root
(`./build-linux.sh`, `./build-macos.sh`); CI does it for all three desktop
platforms. `Docs/PLATFORMS.md` has the per-platform details and the Android
status.

## Documentation

The canonical documentation source is Markdown; DOCX/PDF sets are generated
distribution artifacts, not checked-in sources.

- `Docs/STANDARD.md` / `Docs/STANDARD.ru.md` - naming, modules, game architecture and memory rules
- `Docs/API.md` / `Docs/API.ru.md` - canonical API index with inline platform notes
- `Docs/PLATFORMS.md` - build, support matrix and deployment
- `Docs/README.md` - documentation index
- `Docs/docx/` and `Docs/pdf/` - generated Word/PDF distribution docs
- `Docs/ASSET_PIPELINE.md` - manifest forms, cooking, `assets.fempak` and runtime mounting
- `Docs/SETTINGS.md` - settings registry, display/post effects, OLED shift/noise/idle dissolve, accessibility, eye-protection probes and autosave
- `Docs/RENDERING.md` - 2D view modes, camera projection, particle collision events and view-independent rendering
- `Docs/VOLUMES.md` - post-process/audio volumes, blending, particle contacts and environmental effects
- `Docs/DESIGN_NOTES.md` - the reasoning behind the subsystems and their measured trade-offs
- `Docs/VULKAN_BACKEND.md` вЂ” how a second `IRenderer` would be added without touching gameplay

UI ships in English, Russian, Chinese, French, Spanish, Arabic and Italian; the
table falls back to English per row, so a language can be added incrementally.
Documentation is bilingual (EN/RU).

## Key Features

- Data-driven (JSON scenes, prefabs, dialogs, balance)
- Modding / injection (data, scripts, widgets)
- Decoupled localization + voice acting
- Low-latency Opus microphone chat (miniaudio capture, SFML playback, ENet channel 1)
- 2.5D canvas (2D gameplay on a 3D quad, Darkest Dungeon style)
- Maximum modularity (IModule registry, plugins)

## Licence

The engine is **MPL 2.0** (`LICENSE`): use it in commercial and closed-source
games, modify it freely, but publish modifications to files under `Engine/`
under the same licence. Games built with the engine, including Neon Triangle,
are separate Larger Works and are not covered by the MPL. `LICENSING.md`
explains the split in plain language. Third-party components and their notices
are listed in `THIRD-PARTY-NOTICES.md`.
