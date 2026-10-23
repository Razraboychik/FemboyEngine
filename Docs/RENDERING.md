# Rendering, views and 2D projections

This document describes how FemboyEngine treats 2D rendering. The engine is a
2D engine, but "2D" does not mean "top-down only". Side view, top-down and
isometric games all use the same `DrawList` draw list and the same `IRenderer`
backend; only the coordinate transform and the gameplay gravity convention
change.

## 1. View modes

The engine does not create a 3D scene for these modes. It changes how world
coordinates are projected to screen coordinates and how gravity/input are
interpreted.

| Mode | World axes | Screen projection | Typical use |
| --- | --- | --- | --- |
| Top-down | `x`, `y` are horizontal world axes | identity: `screen = world` | shooters, strategy, arena games |
| Side view | `x` horizontal, `y` vertical | identity, but `y` is height | platformers, fighting games |
| Isometric | `x`, `y` are grid axes | `screen.x = (x - y) * tileWidth / 2`, `screen.y = (x + y) * tileHeight / 2` | tactics, city builders, RPGs |

Rules:

- Physics and gameplay stay in world coordinates.
- Rendering applies the view transform once, in `Camera2D` or the backend.
- `DrawLayer::World`, `Entities`, `Particles` and `Lighting` use the same
  projection.
- `DrawLayer::UI`, `Overlay` and `Debug` remain screen-space and are never
  transformed by the world view.
- Isometric depth sorting uses a stable key such as `x + y`, not Z. Equal keys
  keep submission order.

Implemented API (`FemboyEngine/ViewProjection.hpp`):

```cpp
enum class ProjectionMode { Orthographic, Isometric };

struct ViewProjection
{
    ProjectionMode mode = ProjectionMode::Orthographic;
    float isoAxisX = 1.f;   // 2:1 tiles use (1, 0.5)
    float isoAxisY = 0.5f;

    sf::Vector2f Project(sf::Vector2f world) const;
    sf::Vector2f Unproject(sf::Vector2f view) const;
    sf::Transform WorldTransform() const;
};
```

`Camera2D` owns the projection and exposes exact helpers:

```cpp
camera.SetProjection(ProjectionMode::Isometric, 1.f, 0.5f);
sf::Vector2f screen = camera.WorldToScreen(world, viewportRect);
sf::Vector2f world = camera.ScreenToWorld(mouse, viewportRect);
```

The renderer applies the affine transform only to world-space layers. UI,
overlay and debug pixels are never transformed. Turn this on from the editor's
Viewport panel; gameplay code does not need to change its coordinates.

`DrawCommand::z` is a `float`, so two actors can interleave without inventing an
integer layer. `Tilemap::RenderSorted()` groups cells by the `x + y` diagonal and
gives each diagonal a fractional depth; use it for isometric maps. The normal
`Tilemap::Render()` stays the cheapest path for top-down/side-view maps.

## 2. Camera

`Camera2D` is the single owner of the world view. It handles:

- position and zoom;
- follow target and dead zone;
- screen shake (automatically suppressed by accessibility reduce-motion);
- view rotation;
- projection (orthographic or affine isometric);
- conversion between world and screen coordinates, including projection, zoom,
  rotation and a sub-viewport rectangle.

For side view, gravity is a gameplay convention, not a renderer feature:

```cpp
// Top-down
const sf::Vector2f gravity{0.f, 0.f};

// Side view
const sf::Vector2f gravity{0.f, 980.f};

// Isometric gameplay often keeps gravity in screen/world hybrid space,
// or disables it and uses tile movement.
```

The renderer only needs the final view matrix.

## 3. Particle collision events

Particles should not test every particle against every object. Use three levels:

1. **Broad phase**: spatial hash grid or uniform grid.
2. **Narrow phase**: circle/AABB/segment tests only for candidates.
3. **Callback**: emit an event once, with a stable id, so the same hit is not
   reported every frame.

Recommended data:

```cpp
struct ParticleHit
{
    std::uint32_t particleId = 0;
    Fem::FemObject* target = nullptr;
    sf::Vector2f point{};
    sf::Vector2f normal{};
    float impulse = 0.f;
    std::string material{};
};
```

Rules:

- particles are simulated on the CPU or in a worker, never in the render path;
- rendering reads the final particle state only;
- collision callbacks are queued and dispatched on the main thread;
- a particle can be destroyed, reflected, split or tagged on hit;
- particle groups share one spatial query when possible.

For very large counts, use:
- a uniform grid with cell size close to particle radius;
- per-cell candidate lists;
- material tags to skip irrelevant pairs;
- a hit cooldown so a fast particle does not fire the same callback every frame.

## 4. Volumes instead of narrow triggers

A water zone, fog bank or gravity field should be a **volume**, not a thin
trigger. A volume is a region with a blend weight. It can affect:

- post-process parameters (tint, blur, vignette, fog);
- audio mixer groups (muffle, reverb, lowpass);
- particle emission and drag;
- gameplay tags (`wet`, `burning`, `smoked`);
- status effects.

See `Docs/VOLUMES.md`.

## 5. Particle contacts vs volumes

Use particle contacts when the exact hit point matters:
- sparks on metal;
- bullet sparks;
- debris collisions.

Use volumes when the region matters:
- underwater screen tint;
- fog;
- gravity wells;
- ambient sound changes.

Do not test every particle against every volume. A particle can ask the volume
system for the dominant volume at its position once per frame, or only when it
changes cells.
