# FemboyEngine API Reference

This is the canonical API index. There is **one API document**, not one per OS.
Platform differences are inline notes. Build, deployment and unsupported-target
details live in [PLATFORMS.md](PLATFORMS.md).

## Platform tags

| Tag | Meaning |
| --- | --- |
| `Desktop` | Windows, Linux, macOS |
| `Windows` | Implemented only on Windows |
| `Unix` | Linux and macOS |
| `Desktop backend` | Requires the SFML desktop window/audio backend |
| `Mobile: no` | Not available until a separate platform layer exists |
| `Backend` | Behaviour belongs to the renderer/audio backend, not the API shape |

If an entire module is unavailable on mobile, it is marked once here and once in
`PLATFORMS.md`; individual methods are not duplicated per OS.

## Core

Headers: `FemboyEngine/Core.hpp`, `Engine.hpp`, `FemObject.hpp`, `Result.hpp`,
`Time.hpp`, `Config.hpp`.

- `Engine` вЂ” application lifetime, window, fixed-step loop, renderer.
- `Application` вЂ” layer stack and composition root.
- `FemObject` / `FemClass` вЂ” engine object model and reflection registration.
- `ProfileScope`, `Profiler` вЂ” frame and system timing.
- `Result<T>` вЂ” explicit success/error without exceptions.
- `Time`, `DateTime` вЂ” monotonic and wall-clock time.

Platform: `Desktop`. Mobile needs a platform loop and lifecycle bridge.

## Console

Headers: `FemboyEngine/Console.hpp`, `DebugConsole.hpp`.

- `Console` вЂ” in-game developer console, command registry, history and overlay.
  Renders through `DrawList`; available on all desktop platforms.
- `DebugConsole` вЂ” optional OS console for early diagnostics.
  - `Windows`: `AllocConsole` + stdout/stderr/stdin redirect.
  - `Linux/macOS`: no-op; a terminal launch already has a console.
  - `Mobile: no` вЂ” use platform logs.
- Toggle: `F1` or `` ` ``.
- `--console` / `--dev-console` requests the OS console.

## Math

Header: `FemMath/Math.hpp` (separate library, no ECS/render dependency).

- `Vec2`, `Vec2i`, `Color`, `Rect`.
- `Pi`, `Tau`, `DegreesToRadians`, `RadiansToDegrees`.
- `Length`, `Normalized`, `Dot`, `Cross`, `Rotated`, `FromAngle`, `AngleOf`.
- `Distance`, `DistanceToSegment`, `RectContains`.
- `Lerp`, `WithAlpha`, `Clamp01`.

Platform: `Desktop` and future mobile; no OS-specific behaviour.

## World and scenes

Headers: `FemboyEngine/World.hpp`, `ECS.hpp`, `Scene.hpp`, `Prefab.hpp`,
`ClassDefaults.hpp`, `Mirror.hpp`.

- `World` вЂ” entity lifecycle: `Spawn`, `SpawnWith`, `Destroy`, `Clear`, `Valid`,
  `Count`, `View`, `Get`, `TryGet`, `Emplace`, `Has`.
- `Scene` вЂ” JSON scene load/save and additive application.
- `PrefabLibrary` / `Prefab` вЂ” inheritance, overrides and nested children.
- `ClassDefaults` вЂ” project defaults between C++ defaults and prefab overrides.
- `Mirror` вЂ” reflected component fields and editor metadata.

Platform: all. File paths are handled by `Vanity` and differ by OS.

## Rendering

Headers: `FemboyEngine/Skirt.hpp` (`DrawList`), `IRenderer.hpp`,
`Camera2D.hpp`, `ViewProjection.hpp`, `Tilemap.hpp`, `Lighting.hpp`,
`PostFX.hpp`, `Display.hpp`.

- `DrawList` вЂ” layered command list, stable fractional depth sort and batching.
  Public name is `DrawList`; `Skirt` is a compatibility alias.
- `IRenderer` вЂ” backend contract. `SFMLRenderer` is the shipped desktop backend.
- `Camera2D` / `ViewProjection` вЂ” orthographic and affine isometric projection,
  world/screen conversion.
- `Tilemap::RenderSorted` вЂ” diagonal depth ordering for isometric maps.
- `DisplaySettings`, `PostFX` вЂ” frame presentation and screen effects.

Platform:
- `Desktop backend` only today.
- `Vulkan` is a future backend, not a current build option.
- `Mobile: no` until EGL/Metal + platform renderer land.

## Input, UI and accessibility

Headers: `FemboyEngine/Input.hpp`, `UI.hpp`, `Accessibility.hpp`.

- `Input` вЂ” actions, mapping contexts, modifiers, rebinding, save/load.
- `PinkUI` вЂ” JSON widget trees rendered through `DrawList`.
- `Accessibility` вЂ” colorblind/high-contrast adjustment, reduce motion/flash,
  subtitles, TTS.

Platform:
- Desktop: keyboard/mouse/gamepad.
- Touch input is part of the future mobile platform layer.
- TTS: Windows SAPI; Unix logs unless the host supplies a callback.
- Subtitles and visual accessibility flags are platform-neutral.

## Audio

Headers: `FemboyEngine/Audio.hpp`, `AudioMixer.hpp`, `AudioOcclusion.hpp`,
`VoiceChat.hpp`.

Platform:
- `Desktop`: SFML/miniaudio backend.
- Device names and availability differ by OS.
- `Mobile: no` until a platform audio backend is added.

## Assets and files

Headers: `FemboyEngine/Assets.hpp`, `Image.hpp`, `ResourcePack.hpp`,
`AssetPipeline.hpp`, `Vanity.hpp`.

- `AssetManager` вЂ” textures, fonts, sounds, music, async decoding.
- `AssetDatabase` / `AssetCooker` вЂ” manifest validation and cooking.
- `Vanity` вЂ” atomic reads/writes, path joins, upward search, user data dir.

Platform:
- Desktop paths: `%LOCALAPPDATA%`, `~/Library/Application Support`,
  `$XDG_DATA_HOME`.
- Mobile needs `AAssetManager` / bundle resource bridges.

## Networking and physics

Headers: `FemboyEngine/Networking.hpp`, `Hosting.hpp`, `Replication.hpp`,
`Rollback.hpp`, `Physics.hpp`, `PhysicsSystem.hpp`.

Platform: `Desktop` (ENet + Box2D). No API shape change on other platforms, but
mobile packaging and lifecycle handling are separate work.

## Scripting and tools

Headers: `FemboyEngine/FemboyScript.hpp`, `VisualScript.hpp`, `Reflection.hpp`,
`ProjectGen.hpp`.

- `FemboyScript` вЂ” text scripting VM.
- `VisualScriptGraph` вЂ” node graph compiled to FemboyScript.
- `FemStructRegistry` / `FemEnumRegistry` вЂ” reflection metadata.
- `ProjectGenerator` вЂ” Editor/tooling only.

Platform: scripting and reflection are neutral; the editor and project generator
are desktop tooling.

## Rules for API changes

- Do not duplicate an API page per OS.
- Add a `Platform:` block only when behaviour differs.
- Mark a whole module once if it is unavailable on a platform.
- Keep signatures, JSON shapes and serialized enum values stable.
- A new platform backend implements `IRenderer`/input/audio seams; it does not
  fork the public API.
