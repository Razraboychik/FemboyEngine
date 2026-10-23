// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
//
// Skills: the engine's tag-driven attribute, effect and skill system.
//
// The design follows the same ideas as UE5's Gameplay Ability System, but it is
// rebuilt for a 2D engine instead of ported: the mapping is
//
//     AbilitySystemComponent -> SkillSet      (one per actor that can act)
//     GameplayAbility        -> SkillDef      (data + activation/tick/end hooks)
//     GameplayEffect         -> EffectDef     (modifiers + granted tags + duration)
//     AttributeSet           -> AttributeSet  (tag-keyed floats, dense storage)
//     GameplayTag            -> TagId         (the same tags the rest of the engine uses)
//
// Four things are deliberately different from UE, because a 2D game pays for
// them differently:
//
//  * Attributes are addressed by tag but stored in a flat vector with an index
//    map. A frame touches attributes by slot, never by string or map lookup.
//  * Everything is deferred-safe and allocation-free in the steady state: active
//    effects and skills live in vectors that are compacted once per tick, and
//    the attribute cache is recomputed only when something actually changed.
//  * A skill is a tag, so activation requirements, cancellation and blocking are
//    plain tag queries and are shared with the rest of the engine (visual
//    scripting, damage rules, the log filter, replication).
//  * There is no prediction/net role split in the core. A SkillSet is a value
//    that ticks; deciding where it ticks belongs to the game or the host.
//
// Typical use:
//
//     SkillSet set;
//     set.defineAttribute(Tag_Attribute_Health(), 100.f, 0.f, 100.f);
//     set.addPersistentTag(Tag_Player());          // who this actor is
//     set.grant(Tag_Skill_Dash());
//     set.activate(Tag_Skill_Dash());              // refused if stunned
//     set.Tick(dt);                                // cooldowns, durations, DoT
#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/FemboyScript.hpp"
#include "FemboyEngine/Tags.hpp"

namespace Fem
{

// Engine-standard vocabulary. A project adds its own with FEMBOY_DEFINE_TAG.
FEMBOY_DECLARE_TAG(TagAttributeHealth);
FEMBOY_DECLARE_TAG(TagAttributeHealthMax);
FEMBOY_DECLARE_TAG(TagAttributeStamina);
FEMBOY_DECLARE_TAG(TagAttributeStaminaMax);
FEMBOY_DECLARE_TAG(TagAttributeMoveSpeed);
FEMBOY_DECLARE_TAG(TagAttributeDamage);
FEMBOY_DECLARE_TAG(TagAttributeArmor);

FEMBOY_DECLARE_TAG(TagDamagePhysical);
FEMBOY_DECLARE_TAG(TagDamageElectric);
FEMBOY_DECLARE_TAG(TagDamageExplosive);
FEMBOY_DECLARE_TAG(TagDamageBurn);

// Attributes

// A tag-keyed float set.
//
// base is what the owner was born with (or what an instant effect permanently
// changed); current is base with every active modifier folded in. Storing both
// is what makes "remove the buff and the value comes back" a recompute instead
// of a per-effect undo log.
class AttributeSet
{
public:
    struct Entry
    {
        TagId id = kInvalidTag;
        float base = 0.f;
        float current = 0.f;
        float min = 0.f;
        float max = std::numeric_limits<float>::max();
    };

    // Defining twice updates the range and leaves the stored values alone.
    void Define(TagId id, float base, float min = std::numeric_limits<float>::lowest(),
                float max = std::numeric_limits<float>::max());
    bool Contains(TagId id) const;
    // Slot index, or -1. Cached by the set, so per-frame code uses slots.
    int Slot(TagId id) const;

    int Count() const
    {
        return static_cast<int>(entries_.size());
    }
    // Entry references are only valid until the next define(): parsing JSON can
    // grow the vector, so never hold one across a load.
    const Entry& GetEntry(int slot) const
    {
        return entries_[static_cast<std::size_t>(slot)];
    }

    float BaseAt(int slot) const;
    float CurrentAt(int slot) const;
    float Base(TagId id) const;
    float Current(TagId id) const;
    float Minimum(TagId id) const;
    float Maximum(TagId id) const;

    // setBase re-clamps current to the base value; addBase shifts it. Both are
    // what an instant effect uses. A duration effect never calls them: it is
    // recomputed from base every time the modifier set changes.
    void SetBase(TagId id, float value);
    void AddBase(TagId id, float delta);
    void SetCurrent(TagId id, float value);
    void AddCurrent(TagId id, float delta);
    // Slot-based forms for the per-frame path, where the slot was resolved once.
    void SetCurrentAt(int slot, float value);
    void AddCurrentAt(int slot, float delta);
    // current = clamp(current, min, max) for one slot or all of them.
    void ClampSlot(int slot);
    void ClampAll();
    void ResetCurrentToBase();

    void Clear();

private:
    std::vector<Entry> entries_;
    std::unordered_map<TagId, int> slots_;
};

// Effects

enum class ModifierOp
{
    Add,
    Multiply,
    Override
};

struct AttributeModifier
{
    TagId attribute = kInvalidTag;
    ModifierOp op = ModifierOp::Add;
    float magnitude = 0.f;
};

// The definition of an effect: what it changes, how long it lasts, which tags it
// grants and which tags forbid or remove it. One instance of this is shared by
// every actor that applies it.
struct EffectDef
{
    TagId id = kInvalidTag;
    // 0 = instant: modifiers fold into the base once and nothing is stored.
    float duration = 0.f;
    // > 0 = periodic: every `period` seconds, `periodMagnitude` is added to
    // `periodAttribute` (negative for damage, positive for regeneration).
    float period = 0.f;
    TagId periodAttribute = kInvalidTag;
    float periodMagnitude = 0.f;
    // Re-applying refreshes the timer instead of adding a stack.
    bool refreshOnReapply = true;
    int maxStacks = 1;

    std::vector<AttributeModifier> modifiers;
    TagContainer grantedTags;
    TagContainer required;
    TagContainer blocked;
    // The effect removes itself as soon as one of these is present on the set.
    TagContainer removeOnTags;
};

struct ActiveEffect
{
    std::uint32_t handle = 0;
    TagId id = kInvalidTag;
    TagId source = kInvalidTag;
    float remaining = 0.f;
    float accumulator = 0.f;
    int stacks = 1;
};

// Skills

class SkillSet;

struct SkillDef
{
    TagId id = kInvalidTag;
    float cooldown = 0.f;
    // Cost is an attribute; e.g. "Attribute.Stamina" or "Attribute.Mana".
    TagId costAttribute = kInvalidTag;
    float cost = 0.f;
    // 0 = instant (activate, run onActivate, done). > 0 = stays active, ticks.
    float duration = 0.f;

    TagContainer required;
    TagContainer blocked;
    TagContainer cancelTags;
    TagContainer grantedTags;

    // Optional code hooks. Data-only skills loaded from JSON simply have none.
    std::function<bool(SkillSet&, TagId source)> canActivate;
    std::function<void(SkillSet&, TagId source)> onActivate;
    std::function<void(SkillSet&, float dt)> onTick;
    std::function<void(SkillSet&)> onEnd;
};

struct ActiveSkill
{
    std::uint32_t handle = 0;
    TagId id = kInvalidTag;
    TagId source = kInvalidTag;
    float remaining = 0.f;
};

enum class SkillEventType
{
    EffectApplied,
    EffectRemoved,
    EffectExpired,
    // A periodic effect fired (a damage or regeneration tick).
    EffectTicked,
    SkillActivated,
    SkillEnded,
    SkillFailed
};

struct SkillEvent
{
    SkillEventType type = SkillEventType::EffectApplied;
    TagId id = kInvalidTag;
    TagId source = kInvalidTag;
};

// One actor's live state: attributes, tags, effects and skills.
//
// Not thread-safe by design: a set belongs to one actor and is ticked by one
// system. That is what lets the tick run without a single lock.
class SkillSet
{
public:
    SkillSet() = default;

    // Clears the whole live state, including attribute definitions; re-define
    // attributes afterwards. The shared library is untouched.
    void Reset();

    // Attributes
    void DefineAttribute(TagId id, float base, float min = std::numeric_limits<float>::lowest(),
                         float max = std::numeric_limits<float>::max());
    AttributeSet& Attributes()
    {
        return attributes_;
    }
    const AttributeSet& Attributes() const
    {
        return attributes_;
    }
    float Attribute(TagId id) const
    {
        return attributes_.Current(id);
    }

    // Tags
    // Persistent tags are the actor's own classification ("Enemy", "Player").
    // Effect-granted tags are refcounted, so two effects granting the same tag
    // keep it present until both are gone.
    void AddPersistentTag(TagId id);
    bool RemovePersistentTag(TagId id);
    const TagContainer& PersistentTags() const
    {
        return persistent_;
    }
    // Persistent plus every granted tag. This is what requirements test against.
    const TagContainer& Tags() const
    {
        return tags_;
    }

    // Skills
    bool Grant(TagId skillId);
    bool Revoke(TagId skillId);
    bool HasSkill(TagId skillId) const;
    bool CanActivate(TagId skillId) const;
    bool Activate(TagId skillId, TagId source = kInvalidTag);
    void Cancel(TagId skillId);
    bool IsActive(TagId skillId) const;
    float Cooldown(TagId skillId) const;
    // 1 right after activation, 0 when ready. For UI.
    float CooldownFraction(TagId skillId) const;
    const std::vector<ActiveSkill>& ActiveSkills() const
    {
        return activeSkills_;
    }

    // Effects
    // Returns a handle, or 0 when the effect was refused (unknown id, or its
    // requirements were not met). An instant effect applies and returns a handle
    // that is already dead - which is fine, nothing has to release it.
    //
    // `durationOverride` lets a game scale a single application (a stun that
    // lasts 0.22s on one hit and 0.7s on another) without defining one effect per
    // length. Zero means "use the definition's duration".
    std::uint32_t ApplyEffect(TagId effectId, TagId source = kInvalidTag, float durationOverride = 0.f);
    bool RemoveEffect(TagId effectId);
    bool RemoveEffectByHandle(std::uint32_t handle);
    bool HasEffect(TagId effectId) const;
    int Stacks(TagId effectId) const;
    const std::vector<ActiveEffect>& Effects() const
    {
        return effects_;
    }

    // Frame
    void Tick(float dt);

    // Fired on every state change a UI, a sound or a graph may care about.
    void SetEventCallback(std::function<void(const SkillEvent&)> cb)
    {
        onEvent_ = std::move(cb);
    }

    // Required tags must all match, blocked tags must not match.
    bool CheckRequirements(const TagContainer& required, const TagContainer& blocked) const;

    // Persistence
    // Tags are saved as names; attributes, effects and cooldowns by tag name too,
    // so a save survives a recompile.
    nlohmann::json ToJson() const;
    bool FromJson(const nlohmann::json& j);

private:
    void Recompute();
    void GrantTags(const TagContainer& tags);
    void ReleaseTags(const TagContainer& tags);
    void RebuildTags();
    void Emit(SkillEventType type, TagId id, TagId source);

    AttributeSet attributes_;
    TagContainer persistent_;
    TagContainer tags_;
    // Effect-granted tag refcounts, sorted by id. Small (a handful of tags).
    std::vector<std::pair<TagId, int>> grantedRefs_;

    std::vector<ActiveEffect> effects_;
    std::vector<ActiveSkill> activeSkills_;
    std::unordered_map<TagId, float> cooldowns_;
    std::vector<TagId> granted_;

    std::function<void(const SkillEvent&)> onEvent_;
    std::uint32_t nextHandle_ = 1;
    // Set when the modifier set changed; the next tick recomputes once.
    bool dirty_ = true;
};

// Library

// The definition table: effect and skill data, shared by every actor.
//
// Data can come from JSON (balance tweaks without a rebuild) and behaviour can
// then be attached in code with editSkill()/editEffect(), which is the same
// split UE makes between a data asset and a C++ ability class.
class SkillLibrary
{
public:
    static SkillLibrary& Instance();

    void DefineEffect(EffectDef def);
    void DefineSkill(SkillDef def);
    // Returns nullptr when the id is unknown.
    const EffectDef* Effect(TagId id) const;
    const SkillDef* Skill(TagId id) const;
    // Mutable access for attaching callbacks after loading data.
    EffectDef* EditEffect(TagId id);
    SkillDef* EditSkill(TagId id);

    std::vector<TagId> Effects() const;
    std::vector<TagId> Skills() const;

    bool LoadFromJson(const nlohmann::json& j);
    nlohmann::json ToJson() const;
    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;

    // Drops the definitions. Code-declared callbacks go with them, so this is
    // only safe before any actor holds granted skills.
    void Clear();

private:
    SkillLibrary() = default;

    std::unordered_map<TagId, EffectDef> effects_;
    std::unordered_map<TagId, SkillDef> skills_;
};

// Damage rules

// Data-driven damage typing: "electric damage does 1.5x against a target that is
// Wet", "physical does 0.5x against Armor.Heavy".
//
// Rules are keyed by the damage type tag and tested in insertion order; the last
// matching rule wins, which makes a specific rule override a general one without
// a priority number.
class DamageRules
{
public:
    static DamageRules& Instance();

    void Clear();
    void AddRule(TagId damageType, const TagContainer& targetRequired, float multiplier);
    void SetDefault(float multiplier);
    float DefaultMultiplier() const
    {
        return default_;
    }

    // Never returns 0 for an unknown type unless a rule says so.
    float Multiplier(TagId damageType, const TagContainer& targetTags) const;

    bool LoadFromJson(const nlohmann::json& j);
    nlohmann::json ToJson() const;

private:
    DamageRules() = default;

    struct Rule
    {
        TagContainer required;
        float multiplier = 1.f;
    };

    std::unordered_map<TagId, std::vector<Rule>> rules_;
    float default_ = 1.f;
};

// Script bridge

// Binds the tag nodes of the visual scripting graph to a live object.
//
// The graph compiler emits hasTag(a, b), addTag(a, b), removeTag(a, b) and
// tagQuery(mode, a, b); those names had no implementation before this bridge
// existed. A 2D game almost always means "the player", so the bridge points at
// one object and the `a` argument is accepted and ignored.
//
// Binding a SkillSet is the richer form: tags, attributes and skills all come
// out of it. The plain-pointer form is for an object that has tags but no skill
// system yet.
//
// Extra functions, for the same reason: activateSkill, hasEffect, applyEffect,
// attribute, setAttribute.
class ScriptBridge
{
public:
    static ScriptBridge& Instance();

    // The pointers must outlive the script. Binding null unbinds.
    void Bind(SkillSet* skills);
    void Bind(TagContainer* tags, AttributeSet* attributes = nullptr, SkillSet* skills = nullptr);
    void Unbind();
    bool Bound() const
    {
        return tags_ != nullptr || skills_ != nullptr;
    }

    // Registers the functions on a script VM. Safe to call more than once.
    void Install(FemboyScript& script);

private:
    ScriptBridge() = default;

    bool HasTagImpl(TagId id) const;
    bool MatchesTagImpl(TagId id) const;
    void AddTagImpl(TagId id);
    bool RemoveTagImpl(TagId id);
    AttributeSet* Attributes();

    TagContainer* tags_ = nullptr;
    AttributeSet* attributes_ = nullptr;
    SkillSet* skills_ = nullptr;
};

// Convenience: register the bridge on the engine's own script instance.
void InstallSkillScripts();

} // namespace Fem
