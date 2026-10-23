# Design notes

Answers to the design questions that came up during the pre-release pass, plus
the engine work each of them produced. Everything named here is in the tree; the
headers are the source of truth.

For the engine settings registry, display/post-effect pipeline, OLED pixel shift,
system eye-protection probes and autosave rules, see `Docs/SETTINGS.md`.

---

## 1. Multithreading

### What exists

`Fem::JobSystem` (`Engine/include/FemboyEngine/Jobs.hpp`) is a task pool with
a work-stealing deque per worker and a `ParallelFor(count, grain, fn)`. It is
already used for asset decode, pathfinding batches and crowd integration.

### The honest cost model

Parallelism buys wall-clock time, never total work. For a frame with a serial
part `s` and a parallel part `p` (s + p = 1), speedup is
`1 / (s + p/N)`. At 8 threads that means:

| Serial share | Best speedup |
| --- | --- |
| 10% | 4.7x |
| 25% | 2.9x |
| 50% | 1.8x |

For a *2D* game the serial part is large: one draw thread, one simulation order,
one input. Realistic gains on this codebase are 1.5вЂ“3x on CPU-bound frames, not
8x. Anything that needs the answer this frame and feeds the next stage of the
same frame should stay serial.

### What is worth parallelising, in order

1. **Crowd integration.** Independent per entity, no ordering constraints. This
   is where thousands of units live: 5000 units at ~2 Вµs each is 10 ms serial
   and ~1.5 ms on 8 threads. Already the intended home for the split.
2. **Animation and particle updates.** Same shape, trivially independent.
3. **Spatial index rebuild.** Counting sort over a uniform grid; the counts pass
   is cheap and parallel, the prefix sum is not.
4. **Asset streaming and decode.** Latency, not frame time: decode on a worker
   and upload on the main thread (GL contexts are not shareable here).

### What must stay on the main thread

- Anything that touches the SFML window or GL context (upload, draw).
- The order-dependent part of the simulation: spawn/despawn, event dispatch,
  RNG draws that must stay deterministic for replays and networking.
  `Fem::FastRandom` is per-entity seeded for exactly this reason.

### Thousands of units (a large strategy game)

The frame cost of N units is not one line. It decomposes into:

| Stage | Cost | Fix |
| --- | --- | --- |
| Target acquisition | O(N Г— M) naive | spatial hash grid в†’ O(N Г— k) |
| Steering around obstacles | O(N Г— obstacles) | flow field per destination, shared by N units |
| Collision | O(NВІ) | grid + `CrowdController` separation |
| Animation/state machines | O(N) | LOD: 1 Hz tick for units off-screen |
| Draw | 1 draw call per unit dies first | batching/instancing, or a single vertex array |

`CrowdController` already does grid separation. A flow field is the next single
win: one Dijkstra over a coarse grid per destination, sampled by every unit on
that destination, removes the per-unit pathfinding cost that otherwise dominates
at 4-digit unit counts.

Thread layout that this leads to:

```
main thread  : input -> gameplay order -> physics solve -> draw list -> present
worker pool  : crowd steering | animation | spatial rebuild | decode | pathfinding
render thread: (not yet) only worth it with a second GL context and a command queue
```

---

## 2. Soft bodies: rope, chain, cloth, spiral, ribbon

`Engine/include/FemboyEngine/SoftBody.hpp` and `Wind.hpp`:

- `Fem::Rope` вЂ” Verlet particle chain with distance constraints. Pins are
  first-class (`pin`, `movePin`), so a hook, a hand or a projectile can drag the
  chain. New: `Settings::bendStiffness` (0 = limp rope, 0.9 = chain/stiff cable),
  `Settings::breakStretch` (links snap under load, `BrokenLinks()` reports it),
  `Settings::windDrag`, and `RenderLinks()` for a metal-link look.
- `Fem::Cloth` вЂ” Verlet grid with structural, shear and bending links. New:
  a `WindField` binding, quadratic drag per particle, and `tearStretch`
  (`TornLinks()`); a torn link is never solved again, so a banner stays ripped.
- `Fem::Ribbon` вЂ” the 2D answer to twisting: a rope with a width and a twist
  angle per point. The centreline is simulated like a rope; each point carries
  a torsion spring (`torsionStiffness`, `torsionDamping`) and receives torque
  from the cross product of its tangent and the wind. Width follows
  `|cos(twist)|`, so the strip narrows to an edge, darkens on the back side and
  widens again. `ResetHelix()` builds a coil with a rest twist, and
  `SetRestTwist()` distributes a twist along a straight strip.

Why Verlet here and not Box2D: a rope is not a stack of rigid bodies the game
wants to query. It is a curve that has to hang and swing, and Verlet's constraint
pass *is* the solver вЂ” no mass matrix, no impulse bookkeeping, and an extra
constraint (a collision, a pin, a wind gust) is one more pass that moves a point.
It is also unconditionally stable at the step sizes a 60 Hz frame gives.

Costs, measured shape rather than micro-benchmarks:

- A 20-segment rope: 6 iterations Г— 19 links = 114 constraint solves per frame.
  This is noise.
- An 8Г—6 cloth: 5 iterations Г— ~150 links = 750 solves. Still noise.
- A 32Г—32 cloth: ~8k solves per iteration. Now it is a real budget item; that is
  where you drop iterations to 3 and add the bending links only on the border.

### Wind is a field, not a vector

`Fem::WindField::Sample(position, time)` returns velocity, pressure and
turbulence. That distinction is what makes cloth read as cloth: a flag in a
uniform wind finds one pose and freezes, while a flag in a gusting, turning field
keeps folding and snapping.

- `ConstantWind` вЂ” steady base layer and test fixture.
- `GustWind` вЂ” base direction/speed, noise-driven gusts, periodic direction
  sway (`swayDegrees`, `swayPeriod`) for the "side to side" motion, spatial
  noise so two flags a few metres apart do not move in lockstep.
- `VortexWind` вЂ” Rankine vortex: solid-body core, 1/r tail, inward suction,
  updraft, optional drift and clockwise flag. One is a dust devil; several in a
  line are a squall line.
- `HurricaneWind` вЂ” translating eye, calm centre, eyewall maximum, spiral rain
  bands, inflow angle. This is the field that makes a flag snap from one side to
  the other under a real storm.
- `WindZone` вЂ” rectangle with falloff that hosts another field: a cave or a
  courtyard can be calm while the storm rages outside.
- `CompositeWind` вЂ” sums layers, which is how a weather preset is assembled.
- `makeWeather(WeatherKind, seed)` вЂ” Calm / Breeze / Storm / Hurricane / Tornado,
  each with rain, snow, lightning, fog, temperature and a screen tint for the
  renderer and audio mixer.

Force model: `a = drag * pressure * |w - v| * (w - v)` per particle, where `v` is
the particle's own velocity. A stationary flag catches the full wind, a flag
already moving with the air feels almost nothing, and the cloth never
accelerates without bound. The acceleration is clamped so a hurricane cannot
inject a step the constraint pass spends the frame undoing.

Rendering goes through the same draw list as everything else: `Rope::render`
submits a line strip with optional taper, `Cloth::render` submits one quad per
cell, shaded by the cell's slope so a still flag still reads as a surface,
`Ribbon::render` submits a quad per segment with a twist-dependent width.

### What is still on the table

- Cloth-versus-rigid collision (capsule/sphere push-out) is the next physical
  constraint; the solver already has the pass for it.
- Self-collision: a spatial hash over particles, only enabled for hero capes;
  with 32Г—32 and naive pairs it is quadratic and not worth it.
- Plasticity: store rest lengths per link and relax them towards the current
  length above a yield strain, so a bent flag stays creased.
- Anisotropy: separate stiffness along warp and weft, which is what makes silk
  and denim behave differently under the same wind.
- Flag flutter is a vortex-shedding effect, not a force term. The cheap version
  is a small oscillating lateral force at the free edge at a Strouhal-derived
  frequency; the honest version is a 2D fluid grid, which is a different project.
- GPU compute for cloth: the constraint pass is embarrassingly parallel, but a
  readback for `Render()` costs more than the solve until a cloth is ~64Г—64.
- `Jobs` already exists: split links into chunks and solve them in parallel,
  with a barrier between iterations.
- Sleeping: a cloth whose particles have been below a velocity threshold for a
  second can skip the solve and only wake on a pin move or a weather change.

---

## 3. Audio as a material

The request was "sounds should work like materials". The engine already has the
two halves:

- `AudioMixer` вЂ” buses, gains, ducking, effects.
- `AudioSystem` вЂ” voices, playback, positional mixing.
- `VoiceChat` вЂ” microphone capture, Opus packets, jitter buffer, peer mix.

Treating a sound like a material means it is referenced by *id* and resolved by
the asset layer, exactly like a texture: `Content::Id::EnemyHit()` in the game
code is the same pattern as a texture id. What is missing to make it a material
in the full sense is a descriptor, not a new subsystem:

```json
{
  "id": "metal.hit",
  "layer": "impact",
  "files": ["metal_hit_01.wav", "metal_hit_02.wav"],
  "gain": 0.8,
  "pitch": [0.95, 1.05],
  "bus": "sfx.world",
  "attenuation": "inverse",
  "params": { "surface": "metal", "force": ">0.4" }
}
```

Then a hit event names `metal.hit` and the mixer picks a variation, tunes it, and
routes it вЂ” which is what a material does for a surface in a renderer. The
`params` block is the part the visual-script graph then reads and writes, so a
designer can say "on metal, pitch up when the player is dashing" without code.

---

## 4. Visual scripting and the script language

`Fem::VisualScriptGraph` is a source-to-source compiler, not a second
interpreter. It emits FemboyScript, the same runtime parses and caches that
source, and both paths therefore share values, natives, error reporting and
execution limits. The node catalogue in `VisualScript.hpp` is the source of
truth for the editor picker; `VsNodeTypes()` and `VsNodeTypeName()` are tested so
a type cannot be added to the enum and silently omitted from the UI.

The graph covers the language constructs that matter to gameplay:

- literals including `null`, arithmetic, comparison, boolean logic and `Select`
  (the textual ternary);
- `DeclareVar`/`GetVar`/`SetVar`, typed local variables and function parameters;
- `Branch`, `Sequence`, `While`, `DoWhile`, counted `ForLoop`, `Switch`,
  `Break`, `Continue` and `Return`;
- function definition and calls, including arrays, array indexing and length,
  and the built-in math/string conversions (`Min`, `Max`, `Abs`, `Floor`,
  `Ceil`, `Round`, `Sqrt`, `Pow`, `Sin`, `Cos`, `Tan`, `Atan2`, `ToString`,
  `ToNumber`);
- every runtime event: `OnStart`, `OnUpdate`, `OnCollision`, `OnClick`,
  `OnHover`, `OnTagAdded`, `OnTagRemoved` and `OnSignal`;
- tags, widget properties and `Delay`. A `Delay` advances only while the event
  that contains it is dispatched, so it belongs in `OnUpdate`; this is
  documented on the node rather than hidden in the scheduler.

A generic textual `for (init; condition; increment)` can be built from
`DeclareVar`, `While` and `SetVar`. `Switch` takes scalar literal case values,
which is the common form; a case whose pattern is a computed expression can be
expressed with `Compare` + `Branch`. Object construction (`new`) is rejected by
the text parser, so it has no node and no pretend implementation.

The generated script always defines all event functions, including empty ones.
That prevents a handler from a previous graph surviving a reload, and it means
the host can call every event name unconditionally. Data pins use pin `1+` for
new graphs; pin `0` remains accepted as the legacy data slot so old graphs keep
compiling. `Switch` reserves pins `10+index` for case bodies and pin `99` for the
default body.

The editor panel is a form-based graph editor: open/save, add/remove nodes, edit
labels and JSON parameters, connect/disconnect pins, compile, and read the
generated FemboyScript. It deliberately does not pretend to be a canvas. The
graph file format and the compiler are independent of the editor, so a canvas
can be added later without changing either.

Audio and video follow the same rule: an audio node is a `play` statement with
the material descriptor above, a video node is an `Esthete` box with a
descriptor path, and both are expressible through `CallFunction` in the graph
because they are statements/natives in the language.

---

## 5. Esthete (video boxes)

`Fem::Esthete` is the engine side of "a live-video box with replaceable
images, usable offline". One descriptor per box:

```json
{ "video": "intro", "poster": "intro_idle.png", "overlay": "scanlines.png",
  "fps": 24, "loop": true, "fit": "contain", "rect": [0, 0, 1920, 1080],
  "color": [8, 8, 12, 255], "watch": true }
```

Properties that matter for a shipping game:

- **Offline first.** A missing or undecodable clip is not a failure state: the
  poster is drawn, and with no poster the box is filled with its colour. A
  mandatory cutscene can therefore never hang or show the previous frame.
- **Hot swap.** `Reload()` rereads the descriptor; `watch: true` does it
  automatically when the file changes on disk. `SetVideo()` / `SetOverlay()`
  swap a single layer from a settings screen. No recompile, no file editing.
- **No new dependency.** Clips are PNG sequences or stills, decoded by stb/SFML.
  `VideoPlayer::loadFile` documents exactly where FFmpeg would slot in for
  MP4/WebM, and refuses cleanly until it is linked вЂ” which is the property that
  keeps the offline promise.
- **One draw list.** The current frame is uploaded into `AssetManager` under the
  box's own id on the frame it changes, so the box is submitted through `DrawList`
  like every other sprite instead of opening a second rendering path.

---

## 6. Asset memory and quality

The rule that avoids both failure modes: **never trade download size for runtime
memory, and never trade runtime memory for detail that is not visible.**

Cheap wins, in the order they pay back:

1. **Mipmaps** on every minified texture. Costs 33% memory, removes shimmer and
   usually *speeds up* sampling because the cache hit rate goes up. This is the
   rare case where quality and speed both improve.
2. **Atlas** the UI and particle textures. Fewer texture binds, fewer draw calls,
   and padding makes bleeding impossible.
3. **Compress the file, not the GPU copy.** PNG/WebP on disk, RGBA8 in VRAM;
   block compression only for large photographic sets where the artifact budget
   allows it.
4. **Stream what is not on screen.** Music is already streamed (`sf::Music`);
   cutscene frames are the next candidate, since `Esthete` only ever needs the
   current frame resident.
5. **Free what is behind a level boundary.** `AssetManager::Clear()` plus a
   manifest per level keeps peak memory proportional to one level, not to the
   campaign.

`TextureOptimizer` is the place this policy is implemented; it answers "what is
the smallest representation that still looks identical at the sizes this game
draws".

---

## 7. Timers and dates

New in `Engine/include/FemboyEngine/Time.hpp`:

- `DateTime` вЂ” civil date/time, `Now()` (local) and `UtcNow()`, Unix conversion.
- `Format(value, "YYYY-MM-DD hh:mm:ss")` вЂ” explicit tokens, so the output is the
  same on every platform and locale; `parseIso8601` / `toIso8601` for storage.
- `TimeRange` вЂ” closed-open interval, for "from 20:00 until midnight" and
  seasonal windows.
- `Timer` вЂ” frame-driven countdown with `Remaining()`, `Progress()` and a
  `Tick()` that fires exactly once, even across a long frame. Driven by delta,
  not the wall clock, so pausing the game pauses it.
- `Alarm` + `pollAlarm` вЂ” wall-clock alarms ("18:30 on weekdays") with a
  once-per-day latch, down to the second.

The split matters: gameplay timers must not depend on the system clock, and
system-clock events must not be swallowed by a paused game.

---

## 8. UI: Pink widget coverage

`PinkUI` now implements the container set the UE5 vocabulary implies, not only
leaf widgets:

| Widget | Layout | Notes |
| --- | --- | --- |
| `Canvas` / `CanvasPanel` | absolute | x/y per child, size falls back to parent |
| `VBox` / `VerticalBox` / `Panel` | vertical flow | `fill` weight shares leftover space |
| `HBox` / `HorizontalBox` | horizontal flow | same weighting on the x axis |
| `Overlay` | stacked | all children share one rect |
| `Border` | single child | background colour + `padding` |
| `SizeBox` | single child | its own `w`/`h` wins over the parent |
| `GridPanel` | grid | `columns`, optional `rowHeight` |
| `Spacer` | flow | reserves `size`, draws nothing |
| `WidgetSwitcher` | one child | `SetSwitch(id, index)` |
| `ScrollBox` | vertical flow + scroll | clips its content; `scrollBy`/`scrollTo` |
| `Text`, `Button`, `Image`, `Slider`, `ProgressBar`, `CheckBox`, `ComboBox` | leaf | |

`ScrollBox` clipping is per draw command (the draw list is layer-sorted, so a
clip stack would not survive the sort), and nested scroll boxes intersect their
rects so a list inside a panel cannot paint outside either.

---

## 9. Map tools

The map screen already uses the engine primitives a map editor needs: terrain
bands by floor height, a wheel-scrolled viewport, per-command clipping and a
scrollbar. A palette and landscape brush are the editor-side continuation of
`Tilemap` plus `UndoRedo` (both in the tree): a brush stroke is a tile
rectangle, and undo is a command record вЂ” the same two pieces the map screen
already renders.

---

## 10. Visibility is one module, three consumers

Lighting, audio occlusion and gameplay line-of-sight ask the same question, so
they share `VisibilityWorld`:

- occluder segments live in a uniform grid, not a per-frame BVH;
- `Raycast()` returns point, normal, distance, material and absorption;
- `LineOfSight()` is the boolean form used by AI and audio;
- `VisibilityPolygon()` samples nearby endpoints (and the angles just beside
  them) to build the lit area for a light.

`LightingSystem` rasterises those polygons into a low-resolution RGBA image at
0.25 px/world unit by default, adds ambient (and optional SDF ambient
occlusion), then uploads one texture per rebuild. The lightmap is submitted on
`DrawLayer::Lighting` with `sf::BlendMultiply`: above particles, below UI. A
headless process without a GL context still gets the CPU image through
`SampleAt()` and `ProbeColorAt()`.

Multiplied light can only darken, which is why a neon game must also keep its
floor and grid above pure black; Neon Triangle raises the arena base colours and
lets the player light produce the pool of brightness. The same lightmap texture
is reused, so the cost is one low-resolution pass rather than per-pixel work at
native resolution.

`AudioOcclusion` uses the same segments: distance attenuation, one wall test,
an absorption product and a pitch offset as a cheap muffle proxy (`sf::Sound`
has no filter). The API is deliberately separate from `Audio` so miniaudio or a
second backend can replace the proxy without touching gameplay.

---

## 11. Chunk streaming

`ChunkedTilemap` is separate from the dense `Tilemap`. It owns a hash of square
chunks, each with its own vertex array, and supports:

- `SetGenerator()` for procedural or file-backed content;
- `StreamAsync()` schedules generation on `JobSystem` and applies finished
  chunks on the main thread (SFML objects never cross threads);
- `Stream()` adds a synchronous time budget so a frame can make progress
  without stalling;
- an unload radius and a chunk cache directory;
- JSON/file save-load and world tile bounds.

Generation may read any data, but the callback must be thread-safe: several
chunks can be in flight. The result is moved into the map on the main thread,
and cached geometry is invalidated per chunk.

---

## 12. Rollback, and where it stops

`RollbackSession` keeps an input history per player, predicts remote input,
stores snapshots before frames, rewinds on a late input and re-simulates. The
game supplies snapshot/restore/checksum callbacks; the engine does not assume a
world representation.

Determinism is the limit: Box2D v3 is floating point, so identical inputs can
diverge across compilers and platforms. Rollback is therefore suitable for
input-latency hiding on one platform and for games whose physics is fixed-point
or fully custom. Cross-platform competitive rollback would require replacing
the physics solver, not tuning this class.

Box2D's own worker scheduler is exposed through `PhysicsWorld::SetWorkerCount()`.
It is opt-in and stays at one worker by default: the serial path remains the
deterministic choice for rollback, while a normal game can raise the count for
large island graphs. This uses Box2D's scheduler rather than a second engine-side
job system, so contact ordering and solver state are not duplicated.

`NativeModule` has a similar boundary: the ABI between the host and the DLL is
POD structs and C function pointers. C++ objects, STL containers and exceptions
must not cross it. The loader copies the DLL to a generation-stamped name
before loading, because Windows locks a loaded file and cannot overwrite it in
place.

---

## 13. Rendering extras that stay inside SFML

- `RotationAtlas` bakes N rotations of a sprite into one atlas with
  `sf::RenderTexture`, switches to nearest filtering and caches frames by
  texture/rect/angle count. It needs a live GL context; without one it returns
  the original texture.
- `RichText` parses markup into styled runs. The renderer draws each run as its
  own `sf::Text`, so kerning and shaping stay correct; the cost is a batch break
  per style change. `{{`/`}}` are escapes, unknown tags are preserved verbatim.
- Rectangles, circles, lines and sprites share one triangle batch until the
  resolved texture or blend mode changes. `IRenderer::GetRenderStats()` reports
  commands, draw calls, texture binds and vertices for the last frame, so a
  batching change can be measured instead of guessed at.
- `AntiAliasing` is an enum plus helpers: Off, FXAA, MSAA 2x/4x/8x. FXAA runs as
  a full-screen fragment pass with a Shader; MSAA is applied when the window is
  created. Pixel-art mode is a separate switch that turns off texture and font
  smoothing, not a form of AA.
- `Sockets` are named offsets with an optional rotation and scale. `socketWorld`
  resolves them without touching ECS; `SocketSystem` attaches entities and
  writes their transforms after the fixed step, so interpolation never lags a
  muzzle behind its owner. Box2D joints (distance, revolute, weld, prismatic,
  wheel) are wrapped in `PhysicsWorld` with pixel-to-metre anchors.

The common rule: anything that needs a compute shader, an SSBO or a custom
blend state beyond `sf::BlendMode` is out of scope until a second render backend
exists. That is a deliberate boundary of the SFML 3 backend, not a missing
feature of the engine API.


---

## 14. Script runtime, generational handles and the world map

The pre-release review argued for a memory scheme that raw pointers could not
provide, and for a scripting path that does not reparse on every tick. Both are
now in the tree; this section records what was actually built and what is still
staged.

### Generational handles instead of a raw GC

`SlotMap<T>` (`Engine/include/FemboyEngine/Memory.hpp`) stores `unique_ptr<T>`
slots and hands out `Handle{index, generation}`. `Get()` validates the index,
the pending flag and the generation before it touches the object, so a stale
handle returns `nullptr` rather than dereferencing reused memory. `Destroy()` is
idempotent and only marks the slot; `Collect(budget)` performs the destructor
calls, which lets a screen close free hundreds of widgets over a few frames
instead of spiking one. Ownership stays explicit (a screen or quest owns what it
created), so cycles are not a leak the way shared_ptr cycles are.

The rejected alternative (`GCObject` + `GcRef` with `operator->` returning
`nullptr`) is not used anywhere: a null-returning arrow still dereferences, a
collected reference still dangles, and cycles had no collector. `SlotMap` is the
safe primitive; migrating the UI widget tree from `shared_ptr` to it is a staged
change, not a silent one.

### Compiled programs, not per-frame parsing

`FemboyScript::Compile(source)` returns a `Program` and `Run(Program)` executes
its AST. `Run(source)` looks the source up in a bounded cache first, so the
common path for a visual-script `OnUpdate` is a hash lookup and a tree walk, not
a lexer and parser. `Call(name, args)` dispatches a function by name with the
same binding rules as an in-script call, and `setPrintHandler` captures `print`
for an editor console or an in-game log. `Reset()` clears globals and functions
while keeping native functions, which is what stops one mod's state leaking into
the next run.

`VisualScriptRuntime` is the bridge that was missing: `Load(graph)` compiles the
graph to FemboyScript, parses it once through the cache and registers the event
functions; `start()`, `update(dt)`, `tagAdded(tag)`, `tagRemoved(tag)`,
`collision(other)`, `click()`, `hover()` and `signal(name)` dispatch them by
name. `update` injects `time` and `dt`. The compiler now follows execution chains
past the first node, distinguishes execution pins from data pins, emits
structured control flow (including `do`/`while` and `switch`), and writes every
event function on each load so stale handlers cannot survive a reload. Graph
loading drops unknown node types, duplicate ids, negative pins and dangling or
duplicate input connections instead of letting them reach the parser.

### The skill tree is a wide field, not a tier list

The old tree was one row per tier. That was readable while there were four
choices per row, but it could not carry a real build: every column was a stat
slot and the only decision was which row to buy first. The tree is now seven
vertical sections (`Core`, `Ballistics`, `Elements`, `Mobility`, `Sentinels`,
`Grasp`, `Survival`) laid out left to right. Each section stacks its nodes
downward, and a node stores `section` + `row` instead of a tier. The UI is a
compact command deck: column widths are solved against the viewport so all
seven sections are visible at once at 720p and above, while the rows scroll
vertically. Section headers carry progress rails, each column has a right-hand
connector bus with arrowheads, and cards use chamfered silhouettes, section
glyphs, rank pips and distinct colours for owned, affordable and maxed nodes.
The old pan-everything layout is still available through wheel/Shift+wheel
clamping for unusually narrow windows, but it is no longer the normal case.

The redesign keeps the IDs and prerequisites that saves and tests depend on.
`power -> vitality -> swift -> magnet -> grasp` is still the route to the Q
ability, and Grasp still requires the campaign stage that introduces it. New
nodes feed real `PlayerStats` fields: crit chance and crit damage, stamina and
sprint cost, dash cooldown and dash invulnerability, health/XP gain per kill,
explosion radius and elemental multipliers, wingman/orbit-blade damage, and
Grasp radius, damage, cooldown, healing and stun.

### Shields that ask a question, and enemies that read bullets

`ShieldKind::Volatile` adds a Capacitor: kinetic and explosive hits are eaten by
the charge, the first hit arms a 3.2 s fuse, and the shield detonates whether it
is broken or the fuse expires. Electricity detonates it immediately and still
damages the hull, so the yellow build has a clean answer. The blast hurts the
player, not the host, which keeps the enemy alive and disarmed after the pop.

Dodging is intentionally narrow. Orbiter, Sprinter, Elite, Capacitor and
late-stage Aegis scan player bullets for a shot that will cross their hull within
0.36 s, then move perpendicular for 0.22 s on a 1.25-2.25 s cooldown. The step is
finite and the enemy is still hittable while moving, so spread fire, drones and a
second angle remain useful. A short motion streak makes the behaviour visible
instead of feeling like random immunity.

### The world map is paged

The map used to stack every stage into one scrolling box, and it left a clip
rectangle active over the level information panel. It now shows exactly one
stage per page: eight nodes in two serpentine rows, a stage header, `<`/`>` page
arrows and a dashed hint that the route continues. The page is derived from
`game.mapSelection` when the screen opens, so entering the map lands where the
run left off.

### Enemy evolution reaches stage 5

Behaviour at stages 2 and 3 already existed. Stages 4 and 5 add the final layer:
Hardened and Ascended variants with more health, contact damage and a small
damage reduction, an aura ring that forecasts the threat, and (stage 5) a timed
blink toward the player that stays inside the arena and never lands on top of
them. The variants are tagged `Enemy.Evolved.Hardened` / `.Ascended` so queries,
graphs and mods see the same fact the simulation acts on. Bosses keep their
hand-authored forms and are not scaled twice.

### Red ammo and Grasp are earned

Red ammunition is derived from the campaign, not stored as a separate flag:
`Profile::CampaignStage()` maps eight levels to a stage and
`RedAmmoUnlocked()` is true from stage 2. A fresh profile therefore starts with
the HUD chip reading `RED LOCKED`; pressing the swap button explains the gate.
Clearing level 8 (or any later stage boundary) announces the new stage, and the
stage-2 transition specifically adds `RED AMMO UNLOCKED`. Heavy Rounds is a
stage-2 damage upgrade after that point; it is no longer the thing that makes
the right mouse button work.

Grasp is a skill-tree node and deliberately not a starting ability. Q without
the node produces `Grasp locked - buy Grasp in the skill tree`; the first
purchase announces `Q unlocked` and only then does the HUD show the Q cooldown
chip. The HUD reads both facts from the profile, so a loaded save cannot show an
unlocked state the simulation does not have.

### Voice chat is a real signal path

`VoiceChat` now owns the missing half of the audio stack: miniaudio captures
48 kHz mono, Opus encodes 20 ms frames, and one SFML `SoundStream` plays the
decoded mix. Playback deliberately stays on the existing SFML audio device, so
a game never opens a second output endpoint. Capture and playback use lock-free
single-producer/single-consumer rings; the only work on an audio callback is a
memcpy into the capture ring.

Packets are raw binary on ENet channel 1, with a 14-byte magic/version/sequence/
timestamp header. `Network` routes that channel to its binary callback before
JSON parsing, relays server-side voice to every other peer, and keeps voice off
the reliable JSON channel. The receiver has a small per-peer jitter buffer,
drops duplicates and stale sequences, uses Opus PLC for gaps, and mixes every
peer into one 20 ms stereo frame with per-peer gain, mute and optional positional
pan. `VoiceSettings` is the complete persisted configuration, and
`VoiceChat::ApplySettings()`/`GetSettings()` avoid a game mirroring every setter.
`LoadSettings()`/`SaveSettings()` use the same atomic JSON layer as the rest of
the engine. An optional `VoiceTransport` seam decouples the codec from
`Network`; the built-in path remains the zero-configuration one. Device
enumeration is polled at 2 Hz, so a removed headset falls back to the system
default and a changed default is reopened without restarting the game.
`CaptureSelfTest()` is the blocking hardware diagnostic used by the editor
(`FemboyEditor --voice-test [sec]`); it is not part of the game. `FEMBOY_ENABLE_VOICE`
can be switched off and the public API degrades to no-ops.

Microphone controls belong to the engine service and to developer tools, not to
Neon Triangle. The game has no voice tab, no `V` binding, no persisted voice
fields and no microphone code path; the editor keeps a Voice Chat panel for
testing the engine.

### Asset pipeline: authored data, cooked output

`AssetDatabase` reads both the original `AssetManager` manifest and the pipeline
format (`assets` + `dependencies` + per-asset import settings). Existing
texture/font/sound/music maps keep working; `import` can attach atlas, filtering,
premultiply, power-of-two and size limits without rewriting them. Paths are
resolved relative to the manifest, so a cooked folder is relocatable.

The graph is validated before anything is written: missing files, unknown
dependency ids, duplicate ids and cycles are reported together. `topologicalOrder`
puts dependencies before dependants. `AssetCooker` then copies non-texture
assets, applies the import transforms, packs texture groups into deterministic
shelf atlases, writes `manifest.json` plus `cooked.json` (source hashes for
incremental cooks) and optionally creates `assets.fempak` through `ResourcePack`.

`AssetManager` understands the cooked texture form: an object entry with `path`
and `rect` shares one GPU atlas page across every id mapped into it,
`TextureRect(id)` returns the region, and relative manifest paths now resolve
against the manifest directory. `LoadTextureAsync()` decodes the CPU image on a
worker; `PumpAsync()` performs the GPU upload on the main thread and
`Engine::Run()` pumps it once per frame. The editor has an Asset Pipeline panel
for load/validate/cook and a stale-asset table.

The pack is now a complete runtime unit: it contains `manifest.json` and
`cooked.json`, and every entry stores its path relative to the cook output.
`AssetManager::LoadPackedManifest()` extracts the pack into a per-build cache
(using the pack size and timestamp as the key) and loads the cooked manifest
from that tree, so a release does not need a second asset abstraction.
`FemboyEditor --validate-assets` and `--cook-assets` expose the same pipeline to
CI without opening a window.

### Frame telemetry instead of guesswork

`Profiler` is an engine service, not an editor-only stub. It owns a bounded ring
of `FrameProfile` records (frame number, wall time, pre/fixed/post/render
phases, renderer counters, registry counts and named scopes). `Heels` opens a
scope for every system named `Phase/System`, `Engine` opens the four phase scopes
and attaches `IRenderer`/registry statistics, and `ProfileScope` /
`FB_PROFILE_SCOPE` let game code add its own samples. Recording is on by default
and can be disabled at runtime; disabling keeps the history but stops allocating
samples.

`AverageScopes(window)` aggregates by name, `AverageFrameMs()` and
`AverageFps()` summarise the ring, and `ToJson()` exports a report. The editor
Profiler panel draws frame/render/fixed curves with ImPlot and a per-system
table; `Engine::SetProfilerOverlay(true)` draws the same information in a game
through the normal `DrawList` renderer. No GPU timer queries are claimed: the
reported render time is CPU submission plus the existing SFML draw counters.

### Composable scenes and prefabs

Scene files now carry `"version": 1`. Loading migrates legacy `objects` to
`entities` and rejects a future schema instead of guessing. `LoadAdditive()`
applies a scene to an existing registry without clearing it, which is the
building block for streaming chunks and additive gameplay layers.

Prefabs inherit a parent by name, deep-merge `overrides`, and spawn nested
`children` with offsets. `ResolvedData()` detects parent cycles and depth
limits, and `PrefabLibrary::Instantiate()` reports failures instead of throwing
mid-scene. `PrefabLibrary::LoadFromDir()` sorts filenames, so the same directory
always loads in the same order. The editor Hierarchy and Inspector now use this
same `Registry`/`Scene`/`PrefabLibrary` path: scenes can be loaded, saved and
loaded additively, entities and prefabs can be created, and reflected components
are edited through `Mirror` rather than a private editor model.

### Network diagnostics and a usable lobby

`Network` exposes a public `PacketRateLimiter`, byte/packet counters, RTT and
last-error text through `NetworkStats`, and an opt-in receive limit
(`SetReceiveRateLimit()`) so a host can survive a flood without dropping the
JSON parser into an exception path. Counters cover sent/received/dropped
packets, sent/received bytes and connection time.

`GameHost` adds exponential-backoff reconnect (`SetReconnectPolicy()`), with the
policy off by default for single-player builds. It also keeps a lobby that can
actually start a match: `SetReady()`, `IsReady()`, `ReadyCount()` and
`AllPlayersReady()` work for both a listen server and a client, and readiness
changes are broadcast to every peer. `GetNetworkStats()` is available from the same
object, so a game does not have to reach into the `Network` singleton.

### What is still blocked, and why

- **macOS / Linux / Android builds.** Writing the code is not the blocker; the
  toolchains are. A Mac/Xcode host is needed for macOS, and SFML 3 has no Android
  backend, so Android needs an `android_main`/EGL/AAssetManager/Gradle layer
  before the engine can be called portable there. The CMake platform branches and
  CI hooks exist; they cannot be executed or verified on this Windows machine.
- **GPU compute / SSBO / GPU cellular automata / GPU streaming.** These need a
  second render backend (or a raw GL path beside SFML). SFML 3 exposes no compute
  shader or SSBO, so the current renderer cannot host them without weakening the
  one-backend rule the rest of the engine relies on. `Docs/VULKAN_BACKEND.md`
  records the concrete migration path: a `VulkanRenderer` behind the existing
  `IRenderer`, with backend-neutral texture/font handles and the same `DrawList`
  draw list. It is deliberately not started in 1.5.0.
- **Cross-platform rollback determinism.** Box2D v3 is floating point. Rollback
  already rewinds and re-simulates on one platform; cross-platform competitive
  determinism needs fixed-point or integer physics, which is a solver
  replacement, not a flag.
---

## 15. Licensing boundary

The engine is **MPL 2.0** (`LICENSE`). A game may be commercial and may keep its
own proprietary EULA: the engine is linked as a Larger Work, and the MPL applies
to the engine files, not to the game's separate source. `LICENSING.md` spells
out the split; `THIRD-PARTY-NOTICES.md` lists the dependencies that are actually
in the tree.

Every vendored dependency is license-compatible with that arrangement. No
dependency with an incompatible license is linked or required. The animation
system is native engine code (`Skeleton.hpp`/`Skeleton.cpp`), so a game does not
need an extra animation-runtime license or an attribution requirement beyond the
notices that ship with the engine.

The release ZIP carries the same `THIRD-PARTY-NOTICES.md` as the root tree, and
also `LICENSE`, `LICENSING.md` and the game's `EULA.md`. When a dependency is
added, both notices and `LICENSING.md` must be updated in the same change; the
package script copies the legal files so the shipped text cannot drift silently.
