// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace Fem
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kEpsilon = 1e-6f;

float Radians(float degrees)
{
    return degrees * kPi / 180.f;
}

float Degrees(float radians)
{
    return radians * 180.f / kPi;
}

float Clamp01(float value)
{
    return std::clamp(value, 0.f, 1.f);
}

float WrapAngle(float angle)
{
    while (angle > kPi)
    {
        angle -= 2.f * kPi;
    }
    while (angle < -kPi)
    {
        angle += 2.f * kPi;
    }
    return angle;
}

float EaseIn(float t)
{
    return t * t;
}

float EaseOut(float t)
{
    return 1.f - (1.f - t) * (1.f - t);
}

float EaseInOut(float t)
{
    return t < 0.5f ? 2.f * t * t : 1.f - 2.f * (1.f - t) * (1.f - t);
}

// Cubic Bezier with fixed endpoints (0,0) and (1,1). x is solved by bisection:
// 16 iterations is well below float resolution for animation needs and avoids
// the Newton fallbacks a general solver needs.
float Bezier(float t, float x1, float y1, float x2, float y2)
{
    const auto curve = [](float s, float p1, float p2)
    {
        const float inv = 1.f - s;
        return 3.f * inv * inv * s * p1 + 3.f * inv * s * s * p2 + s * s * s;
    };

    float low = 0.f;
    float high = 1.f;
    float s = t;
    for (int i = 0; i < 16; ++i)
    {
        const float x = curve(s, x1, x2);
        if (x < t)
        {
            low = s;
        }
        else
        {
            high = s;
        }
        s = (low + high) * 0.5f;
    }
    return curve(s, y1, y2);
}

float ApplyCurve(Curve curve, float t, const BoneKey& key)
{
    t = Clamp01(t);
    switch (curve)
    {
        case Curve::Linear:
            return t;
        case Curve::Stepped:
            return 0.f;
        case Curve::EaseIn:
            return EaseIn(t);
        case Curve::EaseOut:
            return EaseOut(t);
        case Curve::EaseInOut:
            return EaseInOut(t);
        case Curve::Bezier:
            return Bezier(t, key.curveData[0], key.curveData[1], key.curveData[2], key.curveData[3]);
    }
    return t;
}

float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

sf::Vector2f Lerp(sf::Vector2f a, sf::Vector2f b, float t)
{
    return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t)};
}

std::uint8_t Channel(float value)
{
    return static_cast<std::uint8_t>(std::clamp(value, 0.f, 1.f) * 255.f + 0.5f);
}

sf::Color ToColor(const float* rgba)
{
    return sf::Color(Channel(rgba[0]), Channel(rgba[1]), Channel(rgba[2]), Channel(rgba[3]));
}

sf::Color Lerp(sf::Color a, sf::Color b, float t)
{
    const auto mix = [t](std::uint8_t x, std::uint8_t y)
    {
        return static_cast<std::uint8_t>(std::clamp(Lerp(static_cast<float>(x), static_cast<float>(y), t), 0.f, 255.f));
    };
    return sf::Color(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a));
}

sf::Color Multiply(sf::Color a, sf::Color b, float alpha)
{
    const auto scale = [alpha](std::uint8_t x, std::uint8_t y)
    {
        return static_cast<std::uint8_t>(
            std::clamp(static_cast<float>(x) * static_cast<float>(y) * alpha / 255.f, 0.f, 255.f));
    };
    return sf::Color(scale(a.r, b.r), scale(a.g, b.g), scale(a.b, b.b), scale(a.a, b.a));
}

const char* CurveName(Curve curve)
{
    switch (curve)
    {
        case Curve::Linear:
            return "linear";
        case Curve::Stepped:
            return "stepped";
        case Curve::EaseIn:
            return "easeIn";
        case Curve::EaseOut:
            return "easeOut";
        case Curve::EaseInOut:
            return "easeInOut";
        case Curve::Bezier:
            return "bezier";
    }
    return "linear";
}

Curve CurveFromName(const std::string& name)
{
    if (name == "stepped")
    {
        return Curve::Stepped;
    }
    if (name == "easeIn")
    {
        return Curve::EaseIn;
    }
    if (name == "easeOut")
    {
        return Curve::EaseOut;
    }
    if (name == "easeInOut")
    {
        return Curve::EaseInOut;
    }
    if (name == "bezier")
    {
        return Curve::Bezier;
    }
    return Curve::Linear;
}

const char* PropertyName(BoneProperty property)
{
    switch (property)
    {
        case BoneProperty::Rotate:
            return "rotate";
        case BoneProperty::Translate:
            return "translate";
        case BoneProperty::Scale:
            return "scale";
        case BoneProperty::Color:
            return "color";
        case BoneProperty::Attachment:
            return "attachment";
    }
    return "rotate";
}

BoneProperty PropertyFromName(const std::string& name)
{
    if (name == "translate")
    {
        return BoneProperty::Translate;
    }
    if (name == "scale")
    {
        return BoneProperty::Scale;
    }
    if (name == "color")
    {
        return BoneProperty::Color;
    }
    if (name == "attachment")
    {
        return BoneProperty::Attachment;
    }
    return BoneProperty::Rotate;
}

const char* ModeName(BoneMode mode)
{
    switch (mode)
    {
        case BoneMode::Normal:
            return "normal";
        case BoneMode::OnlyTranslation:
            return "onlyTranslation";
        case BoneMode::NoRotationOrReflection:
            return "noRotationOrReflection";
        case BoneMode::NoScale:
            return "noScale";
        case BoneMode::NoScaleOrReflection:
            return "noScaleOrReflection";
    }
    return "normal";
}

BoneMode ModeFromName(const std::string& name)
{
    if (name == "onlyTranslation")
    {
        return BoneMode::OnlyTranslation;
    }
    if (name == "noRotationOrReflection")
    {
        return BoneMode::NoRotationOrReflection;
    }
    if (name == "noScale")
    {
        return BoneMode::NoScale;
    }
    if (name == "noScaleOrReflection")
    {
        return BoneMode::NoScaleOrReflection;
    }
    return BoneMode::Normal;
}

float AxisLength(sf::Vector2f axis)
{
    return std::sqrt(axis.x * axis.x + axis.y * axis.y);
}

} // namespace

// Tracks

void SkeletonTrack::Sample(float time, float* out) const
{
    if (keys.empty())
    {
        return;
    }

    if (time <= keys.front().time)
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = keys.front().value[i];
        }
        return;
    }
    const float lastTime = keys.back().time;
    if (time >= lastTime)
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = keys.back().value[i];
        }
        return;
    }

    // Binary search for the segment; clips can have many keys on long timelines.
    size_t low = 0;
    size_t high = keys.size() - 1;
    while (high - low > 1)
    {
        const size_t mid = (low + high) / 2;
        if (keys[mid].time <= time)
        {
            low = mid;
        }
        else
        {
            high = mid;
        }
    }

    const BoneKey& key = keys[low];
    const BoneKey& next = keys[high];
    const float span = std::max(kEpsilon, next.time - key.time);
    const float t = ApplyCurve(key.curve, (time - key.time) / span, key);
    for (int i = 0; i < 4; ++i)
    {
        out[i] = Lerp(key.value[i], next.value[i], t);
    }
}

std::string SkeletonTrack::AttachmentAt(float time) const
{
    if (attachmentKeys.empty())
    {
        return {};
    }
    if (time <= attachmentKeys.front().time)
    {
        return attachmentKeys.front().name;
    }

    std::string result = attachmentKeys.front().name;
    for (const AttachmentKey& key : attachmentKeys)
    {
        if (key.time <= time)
        {
            result = key.name;
        }
        else
        {
            break;
        }
    }
    return result;
}

// Pose

void SkeletonPose::Reset(const Skeleton& skeleton)
{
    const size_t boneTotal = static_cast<size_t>(skeleton.BoneCount());
    translate.resize(boneTotal);
    rotation.resize(boneTotal);
    scale.resize(boneTotal);
    for (size_t i = 0; i < boneTotal; ++i)
    {
        const SkeletonBone& bone = skeleton.Bone(static_cast<int>(i));
        translate[i] = bone.localPosition;
        rotation[i] = bone.localRotation;
        scale[i] = bone.localScale;
    }

    const size_t slotTotal = static_cast<size_t>(skeleton.SlotCount());
    slotColor.resize(slotTotal);
    attachment.resize(slotTotal);
    for (size_t i = 0; i < slotTotal; ++i)
    {
        const SkeletonSlot& slot = skeleton.Slot(static_cast<int>(i));
        slotColor[i] = slot.color;
        attachment[i] = slot.attachment;
    }
}

void SkeletonPose::Blend(const SkeletonPose& other, float weight)
{
    weight = Clamp01(weight);
    const size_t boneTotal = std::min(translate.size(), other.translate.size());
    for (size_t i = 0; i < boneTotal; ++i)
    {
        translate[i] = Lerp(translate[i], other.translate[i], weight);
        scale[i] = Lerp(scale[i], other.scale[i], weight);
        // Rotations take the shortest path; blending 350 deg and 10 deg the
        // naive way would spin the bone almost all the way around.
        const float delta = WrapAngle(other.rotation[i] - rotation[i]);
        rotation[i] = WrapAngle(rotation[i] + delta * weight);
    }

    const size_t slotTotal = std::min(slotColor.size(), other.slotColor.size());
    for (size_t i = 0; i < slotTotal; ++i)
    {
        slotColor[i] = Lerp(slotColor[i], other.slotColor[i], weight);
    }

    if (weight > 0.5f)
    {
        const size_t total = std::min(attachment.size(), other.attachment.size());
        for (size_t i = 0; i < total; ++i)
        {
            attachment[i] = other.attachment[i];
        }
    }
}

// Clip

// Skeleton

void Skeleton::Clear()
{
    bones_.clear();
    slots_.clear();
    iks_.clear();
    clips_.clear();
}

int Skeleton::AddBone(const SkeletonBone& bone)
{
    bones_.push_back(bone);
    return static_cast<int>(bones_.size()) - 1;
}

int Skeleton::AddBone(const std::string& name, int parent, sf::Vector2f localPosition, float localRotation)
{
    SkeletonBone bone;
    bone.name = name;
    bone.parent = parent;
    bone.localPosition = localPosition;
    bone.localRotation = localRotation;
    return AddBone(bone);
}

int Skeleton::FindBone(const std::string& name) const
{
    for (size_t i = 0; i < bones_.size(); ++i)
    {
        if (bones_[i].name == name)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

SkeletonBone& Skeleton::Bone(int index)
{
    static SkeletonBone fallback;
    if (index < 0 || index >= static_cast<int>(bones_.size()))
    {
        return fallback;
    }
    return bones_[static_cast<size_t>(index)];
}

const SkeletonBone& Skeleton::Bone(int index) const
{
    static const SkeletonBone fallback;
    if (index < 0 || index >= static_cast<int>(bones_.size()))
    {
        return fallback;
    }
    return bones_[static_cast<size_t>(index)];
}

int Skeleton::AddSlot(const std::string& name, int boneIndex, int z, DrawLayer layer)
{
    SkeletonSlot slot;
    slot.name = name;
    slot.bone = boneIndex;
    slot.z = z;
    slot.layer = layer;
    slots_.push_back(slot);
    return static_cast<int>(slots_.size()) - 1;
}

int Skeleton::FindSlot(const std::string& name) const
{
    for (size_t i = 0; i < slots_.size(); ++i)
    {
        if (slots_[i].name == name)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

SkeletonSlot& Skeleton::Slot(int index)
{
    static SkeletonSlot fallback;
    if (index < 0 || index >= static_cast<int>(slots_.size()))
    {
        return fallback;
    }
    return slots_[static_cast<size_t>(index)];
}

const SkeletonSlot& Skeleton::Slot(int index) const
{
    static const SkeletonSlot fallback;
    if (index < 0 || index >= static_cast<int>(slots_.size()))
    {
        return fallback;
    }
    return slots_[static_cast<size_t>(index)];
}

const SkeletonAttachment* SkeletonSlot::FindAttachment(const std::string& name) const
{
    for (const SkeletonAttachment& attachment : attachments)
    {
        if (attachment.name == name)
        {
            return &attachment;
        }
    }
    return nullptr;
}

void Skeleton::SetAttachment(int slotIndex, const SkeletonAttachment& attachment)
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(slots_.size()))
    {
        return;
    }
    SkeletonSlot& target = slots_[static_cast<size_t>(slotIndex)];
    for (SkeletonAttachment& existing : target.attachments)
    {
        if (existing.name == attachment.name)
        {
            existing = attachment;
            return;
        }
    }
    target.attachments.push_back(attachment);
}

const SkeletonAttachment* Skeleton::FindAttachment(int slotIndex, const std::string& name) const
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(slots_.size()))
    {
        return nullptr;
    }
    return slots_[static_cast<size_t>(slotIndex)].FindAttachment(name);
}

int Skeleton::AddIk(const SkeletonIk& ik)
{
    iks_.push_back(ik);
    return static_cast<int>(iks_.size()) - 1;
}

int Skeleton::FindIk(const std::string& name) const
{
    for (size_t i = 0; i < iks_.size(); ++i)
    {
        if (iks_[i].name == name)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Skeleton::AddClip(const SkeletonClip& clip)
{
    for (SkeletonClip& existing : clips_)
    {
        if (existing.name == clip.name)
        {
            existing = clip;
            return;
        }
    }
    clips_.push_back(clip);
}

const SkeletonClip* Skeleton::FindClip(const std::string& name) const
{
    for (const SkeletonClip& clip : clips_)
    {
        if (clip.name == name)
        {
            return &clip;
        }
    }
    return nullptr;
}

sf::Transform SkeletonAttachment::LocalTransform() const
{
    sf::Transform transform;
    transform.translate(offset);
    transform.rotate(sf::degrees(Degrees(rotation)));
    return transform;
}

void Skeleton::SetupPose(SkeletonPose& out) const
{
    out.Reset(*this);
}

void Skeleton::UpdateWorldTransforms()
{
    // Bones are expected to be declared parent-first (the JSON loader preserves
    // file order and the builder API documents it). Resolve recursively anyway
    // so a hand-built rig with a forward reference still works; the depth is
    // bounded by the bone count.
    std::vector<int> state(bones_.size(), 0); // 0 = pending, 1 = resolving, 2 = done

    const auto resolve = [&](auto&& self, int index) -> void
    {
        if (index < 0 || index >= static_cast<int>(bones_.size()))
        {
            return;
        }
        if (state[static_cast<size_t>(index)] == 2)
        {
            return;
        }
        if (state[static_cast<size_t>(index)] == 1)
        {
            return; // cycle: leave the bone at its local transform
        }

        state[static_cast<size_t>(index)] = 1;
        SkeletonBone& bone = bones_[static_cast<size_t>(index)];
        const int parent = bone.parent;

        sf::Transform local;
        local.translate(bone.localPosition);
        local.rotate(sf::degrees(Degrees(bone.localRotation)));
        local.scale(bone.localScale);

        if (parent < 0)
        {
            bone.worldTransform = local;
        }
        else
        {
            self(self, parent);

            const SkeletonBone& parentBone = bones_[static_cast<size_t>(parent)];
            sf::Transform parentTransform = parentBone.worldTransform;
            bool stripRotation = !bone.inheritRotation;
            bool stripScale = !bone.inheritScale;

            switch (bone.mode)
            {
                case BoneMode::Normal:
                    break;
                case BoneMode::OnlyTranslation:
                    stripRotation = true;
                    stripScale = true;
                    break;
                case BoneMode::NoRotationOrReflection:
                    stripRotation = true;
                    break;
                case BoneMode::NoScale:
                    stripScale = true;
                    break;
                case BoneMode::NoScaleOrReflection:
                    stripRotation = true;
                    stripScale = true;
                    break;
            }

            if (stripRotation || stripScale)
            {
                parentTransform = sf::Transform();
                parentTransform.translate(parentBone.worldPosition);
                if (!stripRotation)
                {
                    parentTransform.rotate(sf::degrees(Degrees(parentBone.worldRotation)));
                }
                if (!stripScale)
                {
                    parentTransform.scale(parentBone.worldScale);
                }
            }
            bone.worldTransform = parentTransform * local;
        }

        const sf::Vector2f origin = bone.worldTransform.transformPoint({0.f, 0.f});
        const sf::Vector2f xAxis = bone.worldTransform.transformPoint({1.f, 0.f}) - origin;
        const sf::Vector2f yAxis = bone.worldTransform.transformPoint({0.f, 1.f}) - origin;
        bone.worldPosition = origin;
        bone.worldRotation = std::atan2(xAxis.y, xAxis.x);
        bone.worldScale = {AxisLength(xAxis), AxisLength(yAxis)};
        state[static_cast<size_t>(index)] = 2;
    };

    for (int i = 0; i < static_cast<int>(bones_.size()); ++i)
    {
        resolve(resolve, i);
    }
}

void Skeleton::ApplyIk()
{
    for (const SkeletonIk& ik : iks_)
    {
        if (!ik.enabled || ik.mix <= 0.f)
        {
            continue;
        }
        if (ik.boneA < 0 || ik.boneB < 0)
        {
            continue;
        }

        const int targetBone = FindBone(ik.targetBone);
        if (targetBone < 0)
        {
            continue;
        }

        SkeletonBone& upper = bones_[static_cast<size_t>(ik.boneA)];
        SkeletonBone& lower = bones_[static_cast<size_t>(ik.boneB)];
        const sf::Vector2f goal = bones_[static_cast<size_t>(targetBone)].worldPosition + ik.targetOffset;

        const float upperLength = std::max(kEpsilon, AxisLength(lower.worldPosition - upper.worldPosition));
        float lowerLength = lower.length * std::abs(lower.worldScale.x);
        if (lowerLength <= kEpsilon)
        {
            lowerLength = std::max(kEpsilon, AxisLength(goal - lower.worldPosition));
        }

        sf::Vector2f toGoal = goal - upper.worldPosition;
        float distanceToGoal = AxisLength(toGoal);
        if (distanceToGoal <= kEpsilon)
        {
            continue;
        }

        const float reach = (upperLength + lowerLength) * 0.999f;
        const float minReach = std::abs(upperLength - lowerLength) * 1.001f + kEpsilon;
        distanceToGoal = std::clamp(distanceToGoal, minReach, reach);

        const float base = std::atan2(toGoal.y, toGoal.x);
        const float cosine =
            std::clamp((distanceToGoal * distanceToGoal + upperLength * upperLength - lowerLength * lowerLength) /
                           (2.f * distanceToGoal * upperLength),
                       -1.f, 1.f);
        const float offset = std::acos(cosine);
        const float desiredUpper = base + (ik.bendPositive ? offset : -offset);

        upper.localRotation = WrapAngle(upper.localRotation + WrapAngle(desiredUpper - upper.worldRotation) * ik.mix);

        // Recompute the chain so the lower bone knows where it now starts.
        UpdateWorldTransforms();

        const sf::Vector2f lowerStart = lower.worldPosition;
        const float desiredLower = std::atan2(goal.y - lowerStart.y, goal.x - lowerStart.x);
        lower.localRotation = WrapAngle(lower.localRotation + WrapAngle(desiredLower - lower.worldRotation) * ik.mix);

        UpdateWorldTransforms();
    }
}

void Skeleton::ApplyPose(const SkeletonPose& pose)
{
    const size_t boneTotal = std::min(bones_.size(), pose.rotation.size());
    for (size_t i = 0; i < boneTotal; ++i)
    {
        SkeletonBone& bone = bones_[i];
        bone.localPosition = pose.translate[i];
        bone.localRotation = pose.rotation[i];
        bone.localScale = pose.scale[i];
    }

    UpdateWorldTransforms();
    ApplyIk();

    const size_t slotTotal = std::min(slots_.size(), pose.slotColor.size());
    for (size_t i = 0; i < slotTotal; ++i)
    {
        slots_[i].color = pose.slotColor[i];
        if (i < pose.attachment.size())
        {
            slots_[i].attachment = pose.attachment[i];
        }
    }
}

sf::Vector2f Skeleton::WorldPosition(int boneIndex) const
{
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones_.size()))
    {
        return {};
    }
    return bones_[static_cast<size_t>(boneIndex)].worldPosition;
}

float Skeleton::WorldRotation(int boneIndex) const
{
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones_.size()))
    {
        return 0.f;
    }
    return bones_[static_cast<size_t>(boneIndex)].worldRotation;
}

void Skeleton::ComputeVertices(const SkeletonSlot& slot, const SkeletonAttachment& attachment,
                               std::vector<sf::Vector2f>& out) const
{
    out.clear();
    if (slot.bone < 0 || slot.bone >= static_cast<int>(bones_.size()))
    {
        return;
    }

    if (!attachment.mesh)
    {
        const sf::Transform transform =
            bones_[static_cast<size_t>(slot.bone)].worldTransform * attachment.LocalTransform();
        const float left = -attachment.pivot.x * attachment.size.x;
        const float top = -attachment.pivot.y * attachment.size.y;
        out.push_back(transform.transformPoint({left, top}));
        out.push_back(transform.transformPoint({left + attachment.size.x, top}));
        out.push_back(transform.transformPoint({left + attachment.size.x, top + attachment.size.y}));
        out.push_back(transform.transformPoint({left, top + attachment.size.y}));
        return;
    }

    // Linear blend skinning: p = sum(weight * boneWorld * setupPosition).
    out.resize(attachment.vertices.size());
    for (size_t i = 0; i < attachment.vertices.size(); ++i)
    {
        const SkinnedVertex& vertex = attachment.vertices[i];
        sf::Vector2f position{};
        float total = 0.f;
        for (const BoneWeight& weight : vertex.weights)
        {
            if (weight.bone < 0 || weight.bone >= static_cast<int>(bones_.size()) || weight.weight <= 0.f)
            {
                continue;
            }
            position +=
                bones_[static_cast<size_t>(weight.bone)].worldTransform.transformPoint(vertex.position) * weight.weight;
            total += weight.weight;
        }
        if (total > kEpsilon)
        {
            position /= total;
        }
        out[i] = position;
    }
}

void Skeleton::Render(Skirt& FrameDrawList, float alpha) const
{
    if (alpha <= 0.f)
    {
        return;
    }

    std::vector<int> order(slots_.size());
    for (size_t i = 0; i < slots_.size(); ++i)
    {
        order[i] = static_cast<int>(i);
    }
    std::stable_sort(order.begin(), order.end(), [this](int a, int b)
                     { return slots_[static_cast<size_t>(a)].z < slots_[static_cast<size_t>(b)].z; });

    std::vector<sf::Vector2f> vertices;
    for (int slotIndex : order)
    {
        const SkeletonSlot& slot = slots_[static_cast<size_t>(slotIndex)];
        if (!slot.visible || slot.attachment.empty())
        {
            continue;
        }
        const SkeletonAttachment* attachment = slot.FindAttachment(slot.attachment);
        if (attachment == nullptr)
        {
            continue;
        }

        ComputeVertices(slot, *attachment, vertices);
        if (vertices.empty())
        {
            continue;
        }

        const sf::Color color = Multiply(slot.color, attachment->tint, alpha);
        if (!attachment->mesh)
        {
            if (vertices.size() >= 4)
            {
                FrameDrawList.Polygon({vertices[0], vertices[1], vertices[2], vertices[3]}, color, sf::Color::Transparent,
                              DrawCommand::kNoThickness, slot.z, slot.layer);
            }
            continue;
        }

        for (size_t i = 0; i + 2 < attachment->triangles.size(); i += 3)
        {
            const std::uint16_t a = attachment->triangles[i];
            const std::uint16_t b = attachment->triangles[i + 1];
            const std::uint16_t c = attachment->triangles[i + 2];
            if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size())
            {
                continue;
            }
            FrameDrawList.Polygon({vertices[a], vertices[b], vertices[c]}, color, sf::Color::Transparent,
                          DrawCommand::kNoThickness, slot.z, slot.layer);
        }
    }
}

void Skeleton::RenderDebug(Skirt& FrameDrawList, sf::Color boneColor, sf::Color ikColor, float thickness, int z) const
{
    for (size_t i = 0; i < bones_.size(); ++i)
    {
        const SkeletonBone& bone = bones_[i];
        if (bone.parent >= 0)
        {
            const SkeletonBone& parent = bones_[static_cast<size_t>(bone.parent)];
            FrameDrawList.Line(parent.worldPosition, bone.worldPosition, boneColor, thickness, z, DrawLayer::Overlay);
        }
        FrameDrawList.Circle(bone.worldPosition, std::max(1.5f, thickness * 1.2f), boneColor, z, DrawLayer::Overlay);
        if (bone.length > 0.f)
        {
            const sf::Vector2f tip = bone.worldTransform.transformPoint({bone.length, 0.f});
            FrameDrawList.Line(bone.worldPosition, tip, sf::Color(boneColor.r, boneColor.g, boneColor.b, 120),
                       std::max(1.f, thickness * 0.6f), z, DrawLayer::Overlay);
        }
    }

    for (const SkeletonIk& ik : iks_)
    {
        const int target = FindBone(ik.targetBone);
        if (target < 0 || !ik.enabled)
        {
            continue;
        }
        const sf::Vector2f goal = bones_[static_cast<size_t>(target)].worldPosition + ik.targetOffset;
        FrameDrawList.Line(goal + sf::Vector2f(-8.f, 0.f), goal + sf::Vector2f(8.f, 0.f), ikColor, thickness, z,
                   DrawLayer::Overlay);
        FrameDrawList.Line(goal + sf::Vector2f(0.f, -8.f), goal + sf::Vector2f(0.f, 8.f), ikColor, thickness, z,
                   DrawLayer::Overlay);
        FrameDrawList.Circle(goal, 5.f, ikColor, z, DrawLayer::Overlay);
    }
}

sf::Vector2f Skeleton::AttachmentCenter(int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(slots_.size()))
    {
        return {};
    }
    const SkeletonSlot& slot = slots_[static_cast<size_t>(slotIndex)];
    const SkeletonAttachment* attachment = slot.FindAttachment(slot.attachment);
    if (attachment == nullptr || slot.bone < 0)
    {
        return {};
    }
    std::vector<sf::Vector2f> vertices;
    ComputeVertices(slot, *attachment, vertices);
    if (vertices.empty())
    {
        return {};
    }
    sf::Vector2f sum{};
    for (const sf::Vector2f& vertex : vertices)
    {
        sum += vertex;
    }
    return sum / static_cast<float>(vertices.size());
}

bool Skeleton::HitTest(sf::Vector2f point, int* slotOut) const
{
    for (int i = static_cast<int>(slots_.size()) - 1; i >= 0; --i)
    {
        const SkeletonSlot& slot = slots_[static_cast<size_t>(i)];
        if (!slot.visible || slot.attachment.empty() || slot.bone < 0)
        {
            continue;
        }
        const SkeletonAttachment* attachment = slot.FindAttachment(slot.attachment);
        if (attachment == nullptr || attachment->mesh)
        {
            continue;
        }

        const sf::Transform transform =
            bones_[static_cast<size_t>(slot.bone)].worldTransform * attachment->LocalTransform();
        const float left = -attachment->pivot.x * attachment->size.x;
        const float top = -attachment->pivot.y * attachment->size.y;
        const sf::Transform inverse = transform.getInverse();
        const sf::Vector2f local = inverse.transformPoint(point);
        if (local.x >= left && local.x <= left + attachment->size.x && local.y >= top &&
            local.y <= top + attachment->size.y)
        {
            if (slotOut != nullptr)
            {
                *slotOut = i;
            }
            return true;
        }
    }
    return false;
}

// JSON

namespace
{
nlohmann::json Vec2Json(sf::Vector2f value)
{
    return nlohmann::json::array({value.x, value.y});
}

sf::Vector2f Vec2FromJson(const nlohmann::json& json, sf::Vector2f fallback = {})
{
    if (!json.is_array() || json.size() < 2)
    {
        return fallback;
    }
    return {json[0].get<float>(), json[1].get<float>()};
}

nlohmann::json ColorJson(sf::Color color)
{
    return nlohmann::json::array({color.r, color.g, color.b, color.a});
}

sf::Color ColorFromJson(const nlohmann::json& json, sf::Color fallback = sf::Color::White)
{
    if (!json.is_array() || json.size() < 3)
    {
        return fallback;
    }
    const auto value = [&json](size_t index, int fallbackValue)
    { return index < json.size() ? json[index].get<int>() : fallbackValue; };
    return sf::Color(static_cast<std::uint8_t>(std::clamp(value(0, 255), 0, 255)),
                     static_cast<std::uint8_t>(std::clamp(value(1, 255), 0, 255)),
                     static_cast<std::uint8_t>(std::clamp(value(2, 255), 0, 255)),
                     static_cast<std::uint8_t>(std::clamp(value(3, 255), 0, 255)));
}
} // namespace

nlohmann::json Skeleton::ToJson() const
{
    nlohmann::json json;
    json["format"] = "femboy-skeleton";
    json["version"] = 1;

    json["bones"] = nlohmann::json::array();
    for (const SkeletonBone& bone : bones_)
    {
        nlohmann::json entry;
        entry["name"] = bone.name;
        if (bone.parent >= 0)
        {
            entry["parent"] = bones_[static_cast<size_t>(bone.parent)].name;
        }
        entry["position"] = Vec2Json(bone.localPosition);
        entry["rotation"] = Degrees(bone.localRotation);
        entry["scale"] = Vec2Json(bone.localScale);
        entry["length"] = bone.length;
        entry["mode"] = ModeName(bone.mode);
        entry["inheritRotation"] = bone.inheritRotation;
        entry["inheritScale"] = bone.inheritScale;
        json["bones"].push_back(entry);
    }

    json["slots"] = nlohmann::json::array();
    json["attachments"] = nlohmann::json::array();
    for (const SkeletonSlot& slot : slots_)
    {
        nlohmann::json entry;
        entry["name"] = slot.name;
        if (slot.bone >= 0)
        {
            entry["bone"] = bones_[static_cast<size_t>(slot.bone)].name;
        }
        entry["color"] = ColorJson(slot.color);
        entry["z"] = slot.z;
        entry["visible"] = slot.visible;
        if (!slot.attachment.empty())
        {
            entry["attachment"] = slot.attachment;
        }
        json["slots"].push_back(entry);

        for (const SkeletonAttachment& attachment : slot.attachments)
        {
            nlohmann::json item;
            item["slot"] = slot.name;
            item["name"] = attachment.name;
            item["mesh"] = attachment.mesh;
            if (attachment.mesh)
            {
                item["vertices"] = nlohmann::json::array();
                for (const SkinnedVertex& vertex : attachment.vertices)
                {
                    nlohmann::json v;
                    v["position"] = Vec2Json(vertex.position);
                    v["weights"] = nlohmann::json::array();
                    for (const BoneWeight& weight : vertex.weights)
                    {
                        nlohmann::json w;
                        if (weight.bone >= 0)
                        {
                            w["bone"] = bones_[static_cast<size_t>(weight.bone)].name;
                        }
                        w["weight"] = weight.weight;
                        v["weights"].push_back(w);
                    }
                    item["vertices"].push_back(v);
                }
                item["triangles"] = attachment.triangles;
            }
            else
            {
                item["offset"] = Vec2Json(attachment.offset);
                item["rotation"] = Degrees(attachment.rotation);
                item["size"] = Vec2Json(attachment.size);
                item["pivot"] = Vec2Json(attachment.pivot);
            }
            item["tint"] = ColorJson(attachment.tint);
            json["attachments"].push_back(item);
        }
    }

    json["ik"] = nlohmann::json::array();
    for (const SkeletonIk& ik : iks_)
    {
        nlohmann::json entry;
        entry["name"] = ik.name;
        if (ik.boneA >= 0)
        {
            entry["boneA"] = bones_[static_cast<size_t>(ik.boneA)].name;
        }
        if (ik.boneB >= 0)
        {
            entry["boneB"] = bones_[static_cast<size_t>(ik.boneB)].name;
        }
        entry["target"] = ik.targetBone;
        entry["targetOffset"] = Vec2Json(ik.targetOffset);
        entry["mix"] = ik.mix;
        entry["bendPositive"] = ik.bendPositive;
        entry["enabled"] = ik.enabled;
        json["ik"].push_back(entry);
    }

    json["clips"] = nlohmann::json::array();
    for (const SkeletonClip& clip : clips_)
    {
        nlohmann::json entry;
        entry["name"] = clip.name;
        entry["duration"] = clip.duration;
        entry["loop"] = clip.loop;
        entry["tracks"] = nlohmann::json::array();
        for (const SkeletonTrack& track : clip.tracks)
        {
            nlohmann::json item;
            item["target"] = track.target;
            item["property"] = PropertyName(track.property);
            if (track.property == BoneProperty::Attachment)
            {
                item["keys"] = nlohmann::json::array();
                for (const AttachmentKey& key : track.attachmentKeys)
                {
                    item["keys"].push_back({{"time", key.time}, {"name", key.name}});
                }
            }
            else
            {
                item["keys"] = nlohmann::json::array();
                for (const BoneKey& key : track.keys)
                {
                    nlohmann::json k;
                    k["time"] = key.time;
                    const int components =
                        track.property == BoneProperty::Color                                                ? 4
                        : track.property == BoneProperty::Translate || track.property == BoneProperty::Scale ? 2
                                                                                                             : 1;
                    k["value"] = nlohmann::json::array();
                    for (int i = 0; i < components; ++i)
                    {
                        k["value"].push_back(key.value[i]);
                    }
                    k["curve"] = CurveName(key.curve);
                    if (key.curve == Curve::Bezier)
                    {
                        k["curveData"] = nlohmann::json::array(
                            {key.curveData[0], key.curveData[1], key.curveData[2], key.curveData[3]});
                    }
                    item["keys"].push_back(k);
                }
            }
            entry["tracks"].push_back(item);
        }
        entry["events"] = nlohmann::json::array();
        for (const SkeletonEvent& event : clip.events)
        {
            entry["events"].push_back({{"time", event.time}, {"name", event.name}});
        }
        json["clips"].push_back(entry);
    }

    return json;
}

bool Skeleton::LoadFromJson(const nlohmann::json& json)
{
    if (!json.is_object())
    {
        return false;
    }
    Clear();

    std::vector<std::pair<std::string, int>> parents;

    if (json.contains("bones") && json["bones"].is_array())
    {
        for (const nlohmann::json& entry : json["bones"])
        {
            SkeletonBone bone;
            bone.name = entry.value("name", std::string{});
            if (bone.name.empty())
            {
                return false;
            }
            bone.localPosition = Vec2FromJson(entry.value("position", nlohmann::json{}));
            bone.localRotation = Radians(entry.value("rotation", 0.f));
            bone.localScale = Vec2FromJson(entry.value("scale", nlohmann::json{}), {1.f, 1.f});
            bone.length = entry.value("length", 0.f);
            bone.mode = ModeFromName(entry.value("mode", std::string("normal")));
            bone.inheritRotation = entry.value("inheritRotation", true);
            bone.inheritScale = entry.value("inheritScale", true);
            const std::string parent = entry.value("parent", std::string{});
            parents.push_back({parent, static_cast<int>(bones_.size())});
            bones_.push_back(bone);
        }
    }

    for (const auto& [parentName, index] : parents)
    {
        if (parentName.empty())
        {
            continue;
        }
        const int parent = FindBone(parentName);
        if (parent >= 0)
        {
            bones_[static_cast<size_t>(index)].parent = parent;
        }
    }

    if (json.contains("slots") && json["slots"].is_array())
    {
        for (const nlohmann::json& entry : json["slots"])
        {
            SkeletonSlot slot;
            slot.name = entry.value("name", std::string{});
            if (slot.name.empty())
            {
                return false;
            }
            slot.bone = FindBone(entry.value("bone", std::string{}));
            slot.color = ColorFromJson(entry.value("color", nlohmann::json{}));
            slot.z = entry.value("z", 0);
            slot.visible = entry.value("visible", true);
            slot.attachment = entry.value("attachment", std::string{});
            slots_.push_back(slot);
        }
    }

    if (json.contains("attachments") && json["attachments"].is_array())
    {
        for (const nlohmann::json& entry : json["attachments"])
        {
            const int slotIndex = FindSlot(entry.value("slot", std::string{}));
            if (slotIndex < 0)
            {
                continue;
            }
            SkeletonAttachment attachment;
            attachment.name = entry.value("name", std::string{});
            if (attachment.name.empty())
            {
                continue;
            }
            attachment.mesh = entry.value("mesh", false);
            attachment.offset = Vec2FromJson(entry.value("offset", nlohmann::json{}));
            attachment.rotation = Radians(entry.value("rotation", 0.f));
            attachment.size = Vec2FromJson(entry.value("size", nlohmann::json{}), {10.f, 10.f});
            attachment.pivot = Vec2FromJson(entry.value("pivot", nlohmann::json{}), {0.5f, 0.5f});
            attachment.tint = ColorFromJson(entry.value("tint", nlohmann::json{}));

            if (attachment.mesh && entry.contains("vertices") && entry["vertices"].is_array())
            {
                for (const nlohmann::json& v : entry["vertices"])
                {
                    SkinnedVertex vertex;
                    vertex.position = Vec2FromJson(v.value("position", nlohmann::json{}));
                    if (v.contains("weights") && v["weights"].is_array())
                    {
                        for (const nlohmann::json& w : v["weights"])
                        {
                            BoneWeight weight;
                            weight.bone = FindBone(w.value("bone", std::string{}));
                            weight.weight = w.value("weight", 0.f);
                            if (weight.bone >= 0)
                            {
                                vertex.weights.push_back(weight);
                            }
                        }
                    }
                    attachment.vertices.push_back(vertex);
                }
                if (entry.contains("triangles") && entry["triangles"].is_array())
                {
                    for (const nlohmann::json& index : entry["triangles"])
                    {
                        attachment.triangles.push_back(static_cast<std::uint16_t>(index.get<int>()));
                    }
                }
            }

            SetAttachment(slotIndex, attachment);
        }
    }

    if (json.contains("ik") && json["ik"].is_array())
    {
        for (const nlohmann::json& entry : json["ik"])
        {
            SkeletonIk ik;
            ik.name = entry.value("name", std::string{});
            ik.boneA = FindBone(entry.value("boneA", std::string{}));
            ik.boneB = FindBone(entry.value("boneB", std::string{}));
            ik.targetBone = entry.value("target", std::string{});
            ik.targetOffset = Vec2FromJson(entry.value("targetOffset", nlohmann::json{}));
            ik.mix = entry.value("mix", 1.f);
            ik.bendPositive = entry.value("bendPositive", true);
            ik.enabled = entry.value("enabled", true);
            if (ik.boneA >= 0 && ik.boneB >= 0 && FindBone(ik.targetBone) >= 0)
            {
                iks_.push_back(ik);
            }
        }
    }

    if (json.contains("clips") && json["clips"].is_array())
    {
        for (const nlohmann::json& entry : json["clips"])
        {
            SkeletonClip clip;
            clip.name = entry.value("name", std::string{});
            clip.duration = entry.value("duration", 1.f);
            clip.loop = entry.value("loop", true);
            if (entry.contains("tracks") && entry["tracks"].is_array())
            {
                for (const nlohmann::json& item : entry["tracks"])
                {
                    SkeletonTrack track;
                    track.target = item.value("target", std::string{});
                    track.property = PropertyFromName(item.value("property", std::string("rotate")));
                    if (item.contains("keys") && item["keys"].is_array())
                    {
                        for (const nlohmann::json& k : item["keys"])
                        {
                            if (track.property == BoneProperty::Attachment)
                            {
                                AttachmentKey key;
                                key.time = k.value("time", 0.f);
                                key.name = k.value("name", std::string{});
                                track.attachmentKeys.push_back(key);
                                continue;
                            }
                            BoneKey key;
                            key.time = k.value("time", 0.f);
                            key.curve = CurveFromName(k.value("curve", std::string("linear")));
                            if (k.contains("value") && k["value"].is_array())
                            {
                                const size_t count = std::min<size_t>(4, k["value"].size());
                                for (size_t i = 0; i < count; ++i)
                                {
                                    key.value[i] = k["value"][i].get<float>();
                                }
                            }
                            if (k.contains("curveData") && k["curveData"].is_array())
                            {
                                const size_t count = std::min<size_t>(4, k["curveData"].size());
                                for (size_t i = 0; i < count; ++i)
                                {
                                    key.curveData[i] = k["curveData"][i].get<float>();
                                }
                            }
                            track.keys.push_back(key);
                        }
                    }
                    clip.tracks.push_back(track);
                }
            }
            if (entry.contains("events") && entry["events"].is_array())
            {
                for (const nlohmann::json& item : entry["events"])
                {
                    SkeletonEvent event;
                    event.time = item.value("time", 0.f);
                    event.name = item.value("name", std::string{});
                    clip.events.push_back(event);
                }
            }
            AddClip(clip);
        }
    }

    return !bones_.empty();
}

bool Skeleton::LoadFromFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return false;
    }
    nlohmann::json json;
    try
    {
        file >> json;
    }
    catch (const std::exception&)
    {
        return false;
    }
    return LoadFromJson(json);
}

bool Skeleton::SaveToFile(const std::string& path) const
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        return false;
    }
    file << ToJson().dump(2);
    return true;
}

// Animator

void SkeletonAnimator::SetSkeleton(Skeleton* skeleton)
{
    skeleton_ = skeleton;
    current_ = nullptr;
    previous_ = nullptr;
    currentName_.clear();
    time_ = 0.f;
    blend_ = 0.f;
    blendDuration_ = 0.f;
    finished_ = false;
    events_.clear();
    if (skeleton_ != nullptr)
    {
        skeleton_->SetupPose(pose_);
    }
}

bool SkeletonAnimator::Play(const std::string& clipName, bool loop, float mixTime)
{
    if (skeleton_ == nullptr)
    {
        return false;
    }
    const SkeletonClip* clip = skeleton_->FindClip(clipName);
    if (clip == nullptr)
    {
        return false;
    }

    if (current_ != nullptr && mixTime > 0.f)
    {
        fromPose_ = pose_;
        previous_ = current_;
        previousTime_ = time_;
        blend_ = 0.f;
        blendDuration_ = std::max(0.001f, mixTime);
    }
    else
    {
        previous_ = nullptr;
        blendDuration_ = 0.f;
        blend_ = 0.f;
    }

    current_ = clip;
    currentName_ = clipName;
    loop_ = loop;
    time_ = 0.f;
    finished_ = false;
    eventCursor_ = 0.f;
    return true;
}

void SkeletonAnimator::Stop()
{
    current_ = nullptr;
    previous_ = nullptr;
    currentName_.clear();
    finished_ = false;
    if (skeleton_ != nullptr)
    {
        skeleton_->SetupPose(pose_);
    }
}

void SkeletonAnimator::SetTime(float seconds)
{
    time_ = std::max(0.f, seconds);
    eventCursor_ = time_;
}

void SkeletonAnimator::Update(float dt)
{
    if (skeleton_ == nullptr)
    {
        return;
    }
    if (current_ == nullptr)
    {
        skeleton_->SetupPose(pose_);
        return;
    }

    if (paused_)
    {
        dt = 0.f;
    }
    dt *= timeScale_;

    const float duration = std::max(current_->duration, 1e-4f);
    const float previousTime = time_;
    time_ += std::max(0.f, dt);

    bool wrapped = false;
    if (time_ >= duration)
    {
        if (loop_)
        {
            time_ = std::fmod(time_, duration);
            wrapped = true;
        }
        else
        {
            time_ = duration;
            finished_ = true;
        }
    }

    // Events: everything in (previousTime, time], plus the tail of the clip when
    // it wrapped back to zero.
    const auto fireRange = [this](float from, float to)
    {
        for (const SkeletonEvent& event : current_->events)
        {
            if (event.time > from && event.time <= to)
            {
                events_.push_back(event.name);
            }
        }
    };
    if (wrapped)
    {
        fireRange(previousTime, duration);
        fireRange(-1.f, time_);
    }
    else
    {
        fireRange(previousTime, time_);
    }

    SampleInto(pose_, current_, time_);

    if (blendDuration_ > 0.f && previous_ != nullptr)
    {
        blend_ += std::max(0.f, dt);
        const float weight = Clamp01(blend_ / blendDuration_);
        if (weight >= 1.f)
        {
            previous_ = nullptr;
            blendDuration_ = 0.f;
            blend_ = 0.f;
        }
        else
        {
            pose_.Blend(fromPose_, 1.f - weight);
        }
    }
}

void SkeletonAnimator::SampleInto(SkeletonPose& out, const SkeletonClip* clip, float time) const
{
    if (skeleton_ == nullptr || clip == nullptr)
    {
        return;
    }

    skeleton_->SetupPose(out);

    for (const SkeletonTrack& track : clip->tracks)
    {
        if (track.property == BoneProperty::Attachment)
        {
            const int slotIndex = skeleton_->FindSlot(track.target);
            if (slotIndex < 0 || slotIndex >= static_cast<int>(out.attachment.size()))
            {
                continue;
            }
            out.attachment[static_cast<size_t>(slotIndex)] = track.AttachmentAt(time);
            continue;
        }

        if (track.property == BoneProperty::Color)
        {
            const int slotIndex = skeleton_->FindSlot(track.target);
            if (slotIndex < 0 || slotIndex >= static_cast<int>(out.slotColor.size()))
            {
                continue;
            }
            float value[4] = {1.f, 1.f, 1.f, 1.f};
            track.Sample(time, value);
            out.slotColor[static_cast<size_t>(slotIndex)] = ToColor(value);
            continue;
        }

        const int boneIndex = skeleton_->FindBone(track.target);
        if (boneIndex < 0 || boneIndex >= static_cast<int>(out.rotation.size()))
        {
            continue;
        }

        float value[4] = {0.f, 0.f, 0.f, 0.f};
        track.Sample(time, value);
        switch (track.property)
        {
            case BoneProperty::Rotate:
                out.rotation[static_cast<size_t>(boneIndex)] = Radians(value[0]);
                break;
            case BoneProperty::Translate:
                out.translate[static_cast<size_t>(boneIndex)] = {value[0], value[1]};
                break;
            case BoneProperty::Scale:
                out.scale[static_cast<size_t>(boneIndex)] = {value[0], value[1]};
                break;
            default:
                break;
        }
    }
}

std::vector<std::string> SkeletonAnimator::PollEvents()
{
    std::vector<std::string> result;
    result.swap(events_);
    events_.clear();
    return result;
}

} // namespace Fem
