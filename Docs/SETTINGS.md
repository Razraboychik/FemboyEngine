# Settings and display comfort

This document covers the engine-side settings catalogue, the display/post-effect
pipeline, OLED protection, accessibility and the game's persistence/autosave
rules. It is the reference for extending the settings model without adding a
second source of truth.

## 1. One schema, many owners

`Fem::SettingsRegistry` (`Engine/include/FemboyEngine/SettingsRegistry.hpp`)
is the engine's UE-style `DeveloperSettings`/`DataAsset` equivalent. A subsystem
describes a setting once:

```cpp
Fem::SettingDef definition;
definition.key = "engine.display.gamma";
definition.category = "Display";
definition.label = "Gamma";
definition.type = Fem::SettingType::Float;
definition.defaultValue = 1.f;
definition.minValue = 0.5f;
definition.maxValue = 2.f;
definition.flags = Fem::SettingFlag_Engine | Fem::SettingFlag_UserVisible;
definition.onChange = [](const nlohmann::json& value) { /* apply now */ };
Fem::SettingsRegistry::Instance().Add(std::move(definition));
```

The registry gives you:

- stable dotted keys and a category for generated/settings UIs;
- typed validation (`Bool`, `Int`, `Float`, `Enum`, `String`, `Color`);
- clamping to `minValue`/`maxValue` and enum bounds;
- defaults with `Reset`/`ResetAll`;
- optional change callbacks;
- JSON load/save, with unknown keys preserved for forward compatibility.

A game-side typed struct can still exist. NeonTriangle's `Neon::Settings` is the
convenient facade for gameplay/audio/accessibility; it serializes its fields and
also embeds the engine display settings. A new subsystem can register additional
keys without changing the settings screen:

```cpp
Fem::SettingDef definition;
definition.key = "game.audio.voiceVolume";
definition.category = "Audio";
definition.type = Fem::SettingType::Float;
definition.defaultValue = 0.8f;
definition.minValue = 0.f;
definition.maxValue = 1.f;
Fem::SettingsRegistry::Instance().Add(std::move(definition));
```

`SettingsRegistry::Definition()`, `Categories()` and `InCategory()` are enough to
drive a generic editor. `ToJson()` writes `{ "schema": 1, "values": { ... } }`;
`LoadJson()` also accepts a bare `{key: value}` object. `LoadFile()` and
`SaveFile()` use the same document format.

## 2. Engine display settings

`Fem::DisplaySettings` (`Engine/include/FemboyEngine/Display.hpp`) owns the
engine-wide presentation knobs. It registers these keys:

| Key | Range | Meaning |
| --- | --- | --- |
| `engine.display.gamma` | 0.5-2.0 | gamma correction, 1.0 = off |
| `engine.display.brightness` | 0.5-1.5 | multiply after contrast |
| `engine.display.contrast` | 0.5-1.5 | contrast around mid-grey |
| `engine.display.saturation` | 0-1.5 | luma-preserving saturation |
| `engine.display.eyeComfort` | 0-1 | warm/red in-game filter strength |
| `engine.display.eyeComfortMode` | 0/1 | 0 = warm yellow, 1 = red |
| `engine.display.vignette` | 0-1 | radial darkening |
| `engine.display.scanlines` | 0-1 | subtle CRT lines |
| `engine.display.chromaticAberration` | 0-1 | radial R/B split |
| `engine.display.noise` | 0-0.25 | animated grain for OLED care |
| `engine.display.uiPixelShift` | bool | OLED shift for UI/overlay layers |
| `engine.display.uiPixelShiftStrength` | 1-3 px | maximum offset |
| `engine.display.uiPixelShiftInterval` | 15-600 s | time between positions |
| `engine.display.uiDissolve` | bool | dissolve idle UI to protect the panel |
| `engine.display.uiDissolveInterval` | 30-1800 s | idle time before dissolving |
| `engine.display.uiDissolveDuration` | 0.5-15 s | fade duration |
| `engine.display.uiDissolveStrength` | 0-1 | maximum fade amount |
| `engine.display.eyeProtectionNotice` | bool | show the comfort hint |

`PostProcessParams::Active()` is false for the default profile. When it is true,
`SFMLRenderer` forces the offscreen path even at render scale 1.0. The final pass
runs after the world/UI has been drawn:

1. render world + UI into `renderTexture_` (render scale, MSAA-independent);
2. optional FXAA pass into a window-sized intermediate texture;
3. post shader: gamma, brightness, contrast, saturation, eye comfort, vignette,
   scanlines, chromatic aberration and animated noise;
4. present.

The shader inverts the render-target V coordinate. SFML render textures are
stored bottom-up; the normal sprite path compensates for that, a custom fragment
shader must do it explicitly. Both the FXAA and post passes do so.

## 3. OLED protection and hit testing

`DisplaySettings::UiPixelShift()` returns an offset in whole window pixels. The
renderer applies it only to `DrawLayer::UI`, `DrawLayer::Overlay` and
`DrawLayer::Debug` commands; world geometry never moves. Full-screen backdrop
rectangles are deliberately not shifted, which avoids a one-pixel seam at the
window edge.

The shift follows a 3x3 cycle and changes every `uiPixelShiftInterval` seconds.
It is a burn-in mitigation for static HUD/menu elements, not a per-frame wobble.

`engine.display.noise` adds a deterministic animated grain in the post pass. It
is intentionally subtle, defaults to zero and never changes gameplay colours.

`engine.display.uiDissolve` is an opt-in idle behaviour. After
`uiDissolveInterval` seconds without real input, the renderer fades UI commands
in a randomized per-draw order over `uiDissolveDuration` seconds, up to
`uiDissolveStrength`. Any input calls `DisplaySettings::NotifyActivity()` and
restores the UI immediately. The dissolve seed changes for each idle cycle, so
widgets do not disappear in the same order every time. The behaviour is disabled
by default because it changes what the player sees; it is a protection tool, not
a universal default.

Because the offset happens at render time, a game that hit-tests its UI must
subtract `DisplaySettings::Instance().UiPixelShift()` from the pointer. The
engine's `PinkUI` only renders (it does not own a pointer), so it needs no change.
NeonTriangle does the subtraction in `Ui::Update()` through `UiInput::uiShift`.

## 4. Accessibility settings

`Fem::AccessibilitySettings` (`Engine/include/FemboyEngine/Accessibility.hpp`)
owns engine-wide accessibility values and mirrors them into the runtime
`Fem::Accessibility` singleton:

| Key | Range | Meaning |
| --- | --- | --- |
| `engine.accessibility.uiScale` | 0.75-2.0 | interface scale |
| `engine.accessibility.colorblindMode` | 0-4 | off, protanopia, deuteranopia, tritanopia, achromatopsia |
| `engine.accessibility.highContrast` | bool | stronger UI contrast |
| `engine.accessibility.subtitles` | bool | subtitle strip |
| `engine.accessibility.subtitleScale` | 0.75-2.0 | subtitle text size multiplier |
| `engine.accessibility.screenReader` | bool | screen-reader announcements |
| `engine.accessibility.ttsVolume` | 0-1 | speech volume |
| `engine.accessibility.reduceMotion` | bool | disable shake and decorative motion |
| `engine.accessibility.reduceFlash` | bool | reduce hurt flashes and full-screen pulses |
| `engine.accessibility.oneHanded` | bool | one-handed input layout |

The game's Accessibility tab writes the same values and then calls
`AccessibilitySettings::WriteToRegistry()` and `Apply()`, so the renderer and UI
read one runtime state.

## 5. System eye-protection detection

The engine only **reads** a signal. It never changes a Windows Night Light,
macOS Night Shift, Redshift or Android display setting.

| Platform | Probe | Honest limitation |
| --- | --- | --- |
| Windows 10/11 | Night Light CloudStore registry blob (`...bluelightreductionstate`) | The blob is undocumented. Byte 18 is 0x15/0x10 when enabled and 0x13/0x00 when disabled on shipped builds; an unknown layout reports `Unknown`. |
| Linux | `/proc/*/comm` for `redshift`, `gammastep`, `wlsunset`, `clight`, `sct`, `xflux`, `nightlight`, `gnome-night-light`, `kgamma` | A running process is not proof that the filter is currently warm. Compositor-only implementations without a discoverable process report `Inactive`. |
| macOS | `defaults read com.apple.CoreBrightness`, looking for Night Shift + enabled | Apple exposes no stable public API; private key layout can change between releases. |
| Android | No native probe | Vendor/OEM specific. Report `Unsupported`; a game may supply a JNI result. |
| Other | none | `Unknown`. |

The result is cached for around 20 seconds. `DisplaySettings::EyeProtection()`
refreshes on demand (the Display tab has a Refresh button), and the game shows a
hint when neither the OS nor the in-game comfort filter is active. The hint can
be disabled with `eyeProtectionNotice`.

## 6. Persistence and autosave

NeonTriangle writes one `settings.json` next to the executable when that folder
is writable, otherwise in the per-user data directory. The game fields stay flat
for compatibility; engine display values live in a `display` object:

```json
{
  "language": 1,
  "autoSaveRun": true,
  "autoSaveSettings": true,
  "subtitles": true,
  "subtitleScale": 1.0,
  "reduceMotion": false,
  "reduceFlash": false,
  "display": {
    "gamma": 1.0,
    "eyeComfort": 0.0,
    "noise": 0.0,
    "uiPixelShift": true,
    "uiPixelShiftInterval": 90.0,
    "uiDissolve": false,
    "uiDissolveInterval": 180.0,
    "uiDissolveDuration": 3.0,
    "uiDissolveStrength": 0.9
  }
}
```

- Settings are applied immediately.
- With `autoSaveSettings` on, a slider change writes after a 1.2 s debounce, so a
  drag is one file write rather than one per mouse sample.
- Back flushes immediately; closing the window flushes a dirty file in
  `Game::OnDetach()`.
- With `autoSaveRun` on, clearing a level writes a separate `autosave` slot. It
  never overwrites the three manual slots. `Continue` prefers the autosave and
  falls back to the first used manual slot.
- The autosave is written after campaign progress, skill points and vitals have
  been updated, so a crash or quit cannot lose a completed arena.
- Older files without `display`, `subtitleScale` or `reduceFlash` load defaults;
  unknown keys are preserved when the game rewrites the file.

## 8. Volumes and environmental post-process

Volumes are not settings, but they reuse the same post-process and audio mixer
pipeline. A volume can blend `PostProcessParams`, audio group gains and tags when
the camera or player enters a region. See `Docs/VOLUMES.md` for the volume
model and `Docs/RENDERING.md` for view modes.

## 7. Adding a setting to a game

For a value that only the game understands, keep it in the game's typed struct
and add it to `ToJson()`/`FromJson()` plus the settings screen. For a value that
an engine subsystem or a tool should discover, register it in
`SettingsRegistry` and let the subsystem consume it. The two paths can be mixed:
the display settings are registered in the engine and serialized by the game
under the `display` key.
