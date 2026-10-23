# Volumes and environmental effects

Volumes are world-space regions with a blend weight. They are the engine's answer
to "the player entered water, so the screen turns blue and the audio changes",
without hard-coding one narrow trigger per effect.

## 1. Why volumes instead of triggers

A trigger is a boolean: inside or outside. That is too narrow for environmental
feedback. A volume can fade in and out, stack with other volumes and blend
several effects at once.

Examples:
- water volume: blue tint, muffled audio, bubbles, `wet` tag;
- fog volume: reduced visibility, muted high frequencies, `smoked` tag;
- gravity volume: pull/push, particle drag, screen distortion;
- fire volume: heat tint, ember particles, `burning` tag.

## 2. Volume model

```cpp
struct Volume
{
    std::string id;
    std::uint32_t priority = 0;
    float blend = 1.f; // 0..1, usually driven by distance to the camera/player
    sf::FloatRect bounds{}; // top-down / side-view AABB
    std::vector<sf::Vector2f> polygon{}; // optional precise shape

    // Effects. A volume may affect any subset.
    PostProcessParams post{};
    std::string audioBus = "master";
    float audioLowpass = 1.f;
    float audioReverb = 0.f;
    std::vector<std::string> tags{};
    std::vector<std::string> particles{};
};
```

The engine should keep a `VolumeSystem` that:
- stores volumes;
- evaluates the dominant volume for a point;
- blends multiple volumes by priority and distance;
- updates post-process, audio and tags.

## 3. Blending rules

- Higher priority wins when volumes overlap.
- Equal priority blends by distance.
- Fade in/out uses a short timer or distance falloff.
- The player, camera and particle emitters can query the dominant volume.
- A volume never forces a hard cut unless `blend` is explicitly 0 or 1.

## 4. Post-process volumes

`DisplaySettings` already owns the final post pass. A volume should be able to
override or blend into it:

- `gamma`, `brightness`, `contrast`, `saturation`;
- `eyeComfort`;
- `vignette`, `scanlines`, `chromaticAberration`;
- `noise`;
- tint colour for water/fog/heat.

The renderer applies the blended result in the final pass. This keeps the effect
cheap: one fullscreen pass, no per-object work.

## 5. Audio volumes

`AudioMixer` should expose group gains and effects. A volume can:
- duck music;
- lower high frequencies;
- add reverb;
- change the listener environment.

This is cheaper and more stable than triggering one-shot sounds on entry.

## 6. Particle contacts

Particle contacts are separate from volumes. Use them when the exact hit point
matters:
- sparks on metal;
- debris collisions;
- bullet impacts.

Use volumes when the region matters:
- underwater tint;
- fog;
- gravity wells;
- ambient sound.

A particle can query the dominant volume once per frame, or only when it changes
cells. Do not test every particle against every volume.

## 7. Events

Volumes can emit events:
- `OnVolumeEnter`
- `OnVolumeExit`
- `OnVolumeChanged`

These events can drive:
- status tags (`wet`, `burning`, `smoked`);
- particle bursts;
- UI hints;
- script callbacks.

## 8. Isometric and side view

Volumes are still 2D. In side view, a volume is an AABB or polygon in world
space. In isometric view, the same volume is projected to screen space; the
simulation does not need a 3D transform.

The only change is the coordinate transform. The volume system stays the same.
