// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
//
// Native 2D skeletal animation.
//
// This implementation owns its data model and its file format, has no third
// party code, and is therefore distributable under the same MPL-2.0 terms as
// the rest of the engine.
//
// The model is deliberately the one 2D cutout animation actually needs:
//
//   bones        a parented transform hierarchy with inherited rotation/scale
//   slots        draw order, per-slot tint and which attachment is visible
//   attachments  region quads (rigid, one bone) and weighted meshes (LBS)
//   clips        keyed rotate/translate/scale, slot colour, attachment swaps
//   easing       linear, stepped, ease in/out, and cubic Bezier
//   IK           two-bone analytic constraint with a pole/bend direction
//   blending     pose crossfade, which is what a state machine needs
//   events       named marks on the timeline (footsteps, hit frames)
//   JSON         the whole rig and its clips, human-editable and diffable
//
// Why not "just use Box2D joints": a character rig is not simulated, it is
// authored. Bones are driven by keys, then constrained, then drawn; there is no
// mass to integrate and no contact to resolve. Solving the two-bone IK
// analytically (law of cosines) is both cheaper and more predictable than a
// joint chain, and it cannot explode when the target moves fast.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <SFML/Graphics.hpp>
#include <nlohmann/json.hpp>

#include "FemboyEngine/Skirt.hpp"

namespace Fem
{

// How a bone composes its parent's transform. These mirror the cases an
// animation tool actually exposes; the default inherits everything.
enum class BoneMode
{
    Normal,
    OnlyTranslation,
    NoRotationOrReflection,
    NoScale,
    NoScaleOrReflection
};

// Interpolation of one key. Bezier uses the four control values stored on the
// key, in the same normalised space as CSS cubic-bezier().
enum class Curve
{
    Linear,
    Stepped,
    EaseIn,
    EaseOut,
    EaseInOut,
    Bezier
};

enum class BoneProperty
{
    Rotate,
    Translate,
    Scale,
    Color,
    Attachment
};

struct BoneKey
{
    float time = 0.f;
    float value[4] = {0.f, 0.f, 0.f, 0.f}; // x/y, angle, sx/sy, or r/g/b/a 0..1
    Curve curve = Curve::Linear;
    float curveData[4] = {0.f, 0.f, 1.f, 1.f}; // bezier x1,y1,x2,y2
};

struct AttachmentKey
{
    float time = 0.f;
    std::string name;
};

struct SkeletonTrack
{
    std::string target; // bone or slot name
    BoneProperty property = BoneProperty::Rotate;
    std::vector<BoneKey> keys;
    std::vector<AttachmentKey> attachmentKeys;

    // Value at `time`; writes 1, 2 or 4 components into out.
    void Sample(float time, float* out) const;
    std::string AttachmentAt(float time) const;
};

struct BoneWeight
{
    int bone = -1;
    float weight = 0.f;
};

struct SkinnedVertex
{
    sf::Vector2f position{}; // setup-pose position, in the mesh's own space
    std::vector<BoneWeight> weights;
};

struct SkeletonAttachment
{
    std::string name;
    bool mesh = false;

    // Region attachment: a quad, offset and rotated relative to its bone.
    sf::Vector2f offset{};
    float rotation = 0.f; // radians
    sf::Vector2f size{10.f, 10.f};
    sf::Vector2f pivot{0.5f, 0.5f}; // 0..1, the bone sits here
    sf::Color tint = sf::Color::White;

    // Mesh attachment: weighted vertices and triangles (3 indices each).
    std::vector<SkinnedVertex> vertices;
    std::vector<std::uint16_t> triangles;

    sf::Transform LocalTransform() const;
};

struct SkeletonBone
{
    std::string name;
    int parent = -1;
    sf::Vector2f localPosition{};
    float localRotation = 0.f; // radians
    sf::Vector2f localScale{1.f, 1.f};
    float length = 0.f;
    BoneMode mode = BoneMode::Normal;
    bool inheritRotation = true;
    bool inheritScale = true;

    // Runtime, filled by Skeleton::ApplyPose().
    sf::Vector2f worldPosition{};
    float worldRotation = 0.f;
    sf::Vector2f worldScale{1.f, 1.f};
    sf::Transform worldTransform;
};

struct SkeletonSlot
{
    std::string name;
    int bone = -1;
    sf::Color color = sf::Color::White;
    std::string attachment; // active attachment name, empty = nothing drawn
    bool visible = true;
    int z = 0;
    DrawLayer layer = DrawLayer::Entities;
    std::vector<SkeletonAttachment> attachments;

    const SkeletonAttachment* FindAttachment(const std::string& name) const;
};

struct SkeletonIk
{
    std::string name;
    int boneA = -1;              // upper
    int boneB = -1;              // lower
    std::string targetBone;      // bone whose world position is the goal
    sf::Vector2f targetOffset{}; // added to the target bone position
    float mix = 1.f;
    bool bendPositive = true;
    bool enabled = true;
};

// One sampled frame of the rig: local transforms plus slot state.
struct SkeletonPose
{
    std::vector<sf::Vector2f> translate;
    std::vector<float> rotation;
    std::vector<sf::Vector2f> scale;
    std::vector<sf::Color> slotColor;
    std::vector<std::string> attachment;

    void Reset(const class Skeleton& skeleton);
    void Blend(const SkeletonPose& other, float weight);
};

struct SkeletonEvent
{
    float time = 0.f;
    std::string name;
};

struct SkeletonClip
{
    std::string name;
    float duration = 0.f;
    bool loop = true;
    std::vector<SkeletonTrack> tracks;
    std::vector<SkeletonEvent> events;
};

// A rig plus its clips. Load from JSON or build in code; that is the whole
// authoring path, so a tool can emit this format later without touching the
// runtime.
class Skeleton
{
public:
    void Clear();

    int AddBone(const SkeletonBone& bone); // parent is bone.parent
    int AddBone(const std::string& name, int parent = -1, sf::Vector2f localPosition = {}, float localRotation = 0.f);
    int FindBone(const std::string& name) const;
    int BoneCount() const
    {
        return static_cast<int>(bones_.size());
    }
    SkeletonBone& Bone(int index);
    const SkeletonBone& Bone(int index) const;
    std::vector<SkeletonBone>& Bones()
    {
        return bones_;
    }
    const std::vector<SkeletonBone>& Bones() const
    {
        return bones_;
    }

    int AddSlot(const std::string& name, int bone, int z = 0, DrawLayer layer = DrawLayer::Entities);
    int FindSlot(const std::string& name) const;
    int SlotCount() const
    {
        return static_cast<int>(slots_.size());
    }
    SkeletonSlot& Slot(int index);
    const SkeletonSlot& Slot(int index) const;
    std::vector<SkeletonSlot>& Slots()
    {
        return slots_;
    }
    const std::vector<SkeletonSlot>& Slots() const
    {
        return slots_;
    }

    // Adds (or replaces, by name) an attachment on a slot.
    void SetAttachment(int slot, const SkeletonAttachment& attachment);
    const SkeletonAttachment* FindAttachment(int slot, const std::string& name) const;

    int AddIk(const SkeletonIk& ik);
    int FindIk(const std::string& name) const;
    std::vector<SkeletonIk>& Iks()
    {
        return iks_;
    }
    const std::vector<SkeletonIk>& Iks() const
    {
        return iks_;
    }

    // Clips are owned by the skeleton so the animator only needs a pointer to it.
    void AddClip(const SkeletonClip& clip);
    const SkeletonClip* FindClip(const std::string& name) const;
    const std::vector<SkeletonClip>& Clips() const
    {
        return clips_;
    }

    // Applies a pose: local transforms -> world transforms (respecting
    // BoneMode), then IK, then slot attachment selection. Cheap enough to run
    // once per animated character per frame.
    void ApplyPose(const SkeletonPose& pose);

    // Setup pose (all clips off).
    void SetupPose(SkeletonPose& out) const;

    // Region/mesh drawing through the engine's batcher. `alpha` multiplies the
    // slot and attachment colour, which is how a fade-out is done.
    void Render(Skirt& FrameDrawList, float alpha = 1.f) const;

    // Debug view: bones as lines, joints as circles, IK targets as crosses.
    void RenderDebug(Skirt& FrameDrawList, sf::Color boneColor = sf::Color(120, 220, 255),
                     sf::Color ikColor = sf::Color(255, 120, 200), float thickness = 2.f, int z = 1000) const;

    // World-space helpers, valid after applyPose().
    sf::Vector2f WorldPosition(int boneIndex) const;
    float WorldRotation(int boneIndex) const;
    sf::Vector2f AttachmentCenter(int slotIndex) const;

    // Hit testing against the currently visible region attachments.
    bool HitTest(sf::Vector2f point, int* slotOut = nullptr) const;

    // JSON: the complete rig plus clips. Unknown fields are ignored so an
    // exporter can add metadata without breaking older runtimes.
    nlohmann::json ToJson() const;
    bool LoadFromJson(const nlohmann::json& json);
    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;

private:
    void UpdateWorldTransforms();
    void ApplyIk();
    void ComputeVertices(const SkeletonSlot& slot, const SkeletonAttachment& attachment,
                         std::vector<sf::Vector2f>& out) const;

    std::vector<SkeletonBone> bones_;
    std::vector<SkeletonSlot> slots_;
    std::vector<SkeletonIk> iks_;
    std::vector<SkeletonClip> clips_;
};

// Plays clips on a skeleton: one current clip, an optional crossfade from the
// previous one, time scale, looping, and event delivery.
class SkeletonAnimator
{
public:
    void SetSkeleton(Skeleton* skeleton);
    Skeleton* GetSkeleton() const
    {
        return skeleton_;
    }

    bool Play(const std::string& clip, bool loop = true, float mixTime = 0.f);
    void Stop();
    bool Playing() const
    {
        return current_ != nullptr;
    }
    const std::string& CurrentClip() const
    {
        return currentName_;
    }

    void SetTimeScale(float scale)
    {
        timeScale_ = scale;
    }
    float TimeScale() const
    {
        return timeScale_;
    }
    void SetTime(float seconds);
    float Time() const
    {
        return time_;
    }
    void SetPaused(bool paused)
    {
        paused_ = paused;
    }
    bool Paused() const
    {
        return paused_;
    }

    void Update(float dt);

    // Pose after update; apply it with skeleton()->applyPose(animator.pose()).
    const SkeletonPose& Pose() const
    {
        return pose_;
    }

    // Events fired since the last call (footstep, hit, ...).
    std::vector<std::string> PollEvents();

    // True when a non-looping clip reached its end.
    bool Finished() const
    {
        return finished_;
    }

    void SetPose(const SkeletonPose& pose)
    {
        pose_ = pose;
    }

private:
    void SampleInto(SkeletonPose& out, const SkeletonClip* clip, float time) const;

    Skeleton* skeleton_ = nullptr;
    const SkeletonClip* current_ = nullptr;
    const SkeletonClip* previous_ = nullptr;
    std::string currentName_;
    SkeletonPose pose_;
    SkeletonPose fromPose_;
    float time_ = 0.f;
    float previousTime_ = 0.f;
    float blend_ = 0.f;
    float blendDuration_ = 0.f;
    float timeScale_ = 1.f;
    bool paused_ = false;
    bool loop_ = true;
    bool finished_ = false;
    std::vector<std::string> events_;
    float eventCursor_ = 0.f;
};

} // namespace Fem
