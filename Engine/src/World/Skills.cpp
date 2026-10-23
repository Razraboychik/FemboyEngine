// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Skills.hpp"

#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Engine.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace Fem
{

FEMBOY_DEFINE_TAG(TagAttributeHealth, "Attribute.Health");
FEMBOY_DEFINE_TAG(TagAttributeHealthMax, "Attribute.HealthMax");
FEMBOY_DEFINE_TAG(TagAttributeStamina, "Attribute.Stamina");
FEMBOY_DEFINE_TAG(TagAttributeStaminaMax, "Attribute.StaminaMax");
FEMBOY_DEFINE_TAG(TagAttributeMoveSpeed, "Attribute.MoveSpeed");
FEMBOY_DEFINE_TAG(TagAttributeDamage, "Attribute.Damage");
FEMBOY_DEFINE_TAG(TagAttributeArmor, "Attribute.Armor");

FEMBOY_DEFINE_TAG(TagDamagePhysical, "Damage.Physical");
FEMBOY_DEFINE_TAG(TagDamageElectric, "Damage.Electric");
FEMBOY_DEFINE_TAG(TagDamageExplosive, "Damage.Explosive");
FEMBOY_DEFINE_TAG(TagDamageBurn, "Damage.Burn");

namespace
{

float ClampValue(float value, float minimum, float maximum)
{
    // Written as two compares rather than std::clamp: a caller may define an
    // inverted range by mistake and clamp would be undefined behaviour there.
    if (value < minimum)
    {
        return minimum;
    }
    if (value > maximum)
    {
        return maximum;
    }
    return value;
}

TagId TagByName(const nlohmann::json& value)
{
    if (!value.is_string())
    {
        return kInvalidTag;
    }
    // Data-driven tags are registered on load: a balance file may introduce
    // "Damage.Poison" before any line of code mentions it.
    return TagRegistry::Instance().RegisterTag(value.get<std::string>());
}

// A name that arrives as a plain string (a save key, a graph literal, a script
// argument). Registered on demand so an unknown literal still resolves, and an
// alias left by a rename still wins over creating a fresh id.
TagId ResolveTag(const std::string& name)
{
    if (name.empty())
    {
        return kInvalidTag;
    }
    const TagId found = TagRegistry::Instance().Find(name);
    if (found != kInvalidTag)
    {
        return found;
    }
    return TagRegistry::Instance().RegisterTag(name);
}

TagContainer TagsByName(const nlohmann::json& value)
{
    TagContainer out;
    if (value.is_string())
    {
        out.Add(TagByName(value));
        return out;
    }
    if (!value.is_array())
    {
        return out;
    }
    for (const nlohmann::json& item : value)
    {
        const TagId id = TagByName(item);
        if (id != kInvalidTag)
        {
            out.Add(id);
        }
    }
    return out;
}

nlohmann::json TagsToNames(const TagContainer& tags)
{
    nlohmann::json out = nlohmann::json::array();
    const TagRegistry& registry = TagRegistry::Instance();
    for (TagId id : tags.Ids())
    {
        // A tag whose name is gone (a retired rename, a registry reload) has no
        // key to write. Writing "" would produce a file that cannot be read back.
        const std::string name = registry.Name(id);
        if (!name.empty())
        {
            out.push_back(name);
        }
    }
    return out;
}

// The name is the only key that survives between runs, so an id the registry can
// no longer name is not serialisable at all.
bool IsSerializable(TagId id)
{
    return id != kInvalidTag && !TagRegistry::Instance().Name(id).empty();
}

ModifierOp OpFromString(const std::string& text)
{
    if (text == "multiply" || text == "mul")
    {
        return ModifierOp::Multiply;
    }
    if (text == "override" || text == "set")
    {
        return ModifierOp::Override;
    }
    return ModifierOp::Add;
}

const char* OpToString(ModifierOp op)
{
    switch (op)
    {
        case ModifierOp::Multiply:
            return "multiply";
        case ModifierOp::Override:
            return "override";
        case ModifierOp::Add:
        default:
            return "add";
    }
}

} // namespace

// AttributeSet

void AttributeSet::Define(TagId id, float base, float min, float max)
{
    if (id == kInvalidTag)
    {
        return;
    }

    auto existing = slots_.find(id);
    if (existing != slots_.end())
    {
        // Re-defining changes the legal range, never the value: the caller that
        // owns the attribute decides when it moves.
        Entry& entry = entries_[static_cast<std::size_t>(existing->second)];
        entry.min = min;
        entry.max = max;
        ClampSlot(existing->second);
        return;
    }

    Entry entry;
    entry.id = id;
    entry.base = ClampValue(base, min, max);
    entry.current = entry.base;
    entry.min = min;
    entry.max = max;

    slots_[id] = static_cast<int>(entries_.size());
    entries_.push_back(entry);
}

bool AttributeSet::Contains(TagId id) const
{
    return slots_.find(id) != slots_.end();
}

int AttributeSet::Slot(TagId id) const
{
    auto it = slots_.find(id);
    return it == slots_.end() ? -1 : it->second;
}

float AttributeSet::BaseAt(int slot) const
{
    return entries_[static_cast<std::size_t>(slot)].base;
}

float AttributeSet::CurrentAt(int slot) const
{
    return entries_[static_cast<std::size_t>(slot)].current;
}

float AttributeSet::Base(TagId id) const
{
    const int index = Slot(id);
    return index < 0 ? 0.f : BaseAt(index);
}

float AttributeSet::Current(TagId id) const
{
    const int index = Slot(id);
    return index < 0 ? 0.f : CurrentAt(index);
}

float AttributeSet::Minimum(TagId id) const
{
    const int index = Slot(id);
    return index < 0 ? 0.f : entries_[static_cast<std::size_t>(index)].min;
}

float AttributeSet::Maximum(TagId id) const
{
    const int index = Slot(id);
    return index < 0 ? 0.f : entries_[static_cast<std::size_t>(index)].max;
}

void AttributeSet::SetBase(TagId id, float value)
{
    const int index = Slot(id);
    if (index < 0)
    {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(index)];
    entry.base = value;
    ClampSlot(index);
}

void AttributeSet::AddBase(TagId id, float delta)
{
    const int index = Slot(id);
    if (index < 0)
    {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(index)];
    entry.base += delta;
    ClampSlot(index);
}

void AttributeSet::SetCurrent(TagId id, float value)
{
    const int index = Slot(id);
    if (index < 0)
    {
        return;
    }
    SetCurrentAt(index, value);
    ClampSlot(index);
}

void AttributeSet::AddCurrent(TagId id, float delta)
{
    const int index = Slot(id);
    if (index < 0)
    {
        return;
    }
    AddCurrentAt(index, delta);
    ClampSlot(index);
}

void AttributeSet::SetCurrentAt(int slot, float value)
{
    entries_[static_cast<std::size_t>(slot)].current = value;
}

void AttributeSet::AddCurrentAt(int slot, float delta)
{
    entries_[static_cast<std::size_t>(slot)].current += delta;
}

void AttributeSet::ClampSlot(int slot)
{
    Entry& entry = entries_[static_cast<std::size_t>(slot)];
    entry.current = ClampValue(entry.current, entry.min, entry.max);
    entry.base = ClampValue(entry.base, entry.min, entry.max);
}

void AttributeSet::ClampAll()
{
    for (int i = 0; i < Count(); ++i)
    {
        ClampSlot(i);
    }
}

void AttributeSet::ResetCurrentToBase()
{
    for (int i = 0; i < Count(); ++i)
    {
        entries_[static_cast<std::size_t>(i)].current = entries_[static_cast<std::size_t>(i)].base;
    }
}

void AttributeSet::Clear()
{
    entries_.clear();
    slots_.clear();
}

// SkillSet: attributes and tags

void SkillSet::Reset()
{
    attributes_.Clear();
    persistent_.Clear();
    tags_.Clear();
    grantedRefs_.clear();
    effects_.clear();
    activeSkills_.clear();
    cooldowns_.clear();
    granted_.clear();
    nextHandle_ = 1;
    dirty_ = true;
}

void SkillSet::DefineAttribute(TagId id, float base, float min, float max)
{
    attributes_.Define(id, base, min, max);
    dirty_ = true;
}

void SkillSet::AddPersistentTag(TagId id)
{
    if (id == kInvalidTag || persistent_.Has(id))
    {
        return;
    }
    persistent_.Add(id);
    tags_.Add(id);
}

bool SkillSet::RemovePersistentTag(TagId id)
{
    if (!persistent_.Remove(id))
    {
        return false;
    }
    // An effect may still be granting it, so rebuild instead of removing blindly.
    RebuildTags();
    return true;
}

void SkillSet::RebuildTags()
{
    tags_ = persistent_;
    for (const auto& [id, count] : grantedRefs_)
    {
        if (count > 0)
        {
            tags_.Add(id);
        }
    }
}

void SkillSet::GrantTags(const TagContainer& tags)
{
    for (TagId id : tags.Ids())
    {
        auto it = std::lower_bound(grantedRefs_.begin(), grantedRefs_.end(), id,
                                   [](const std::pair<TagId, int>& entry, TagId value) { return entry.first < value; });
        if (it != grantedRefs_.end() && it->first == id)
        {
            ++it->second;
        }
        else
        {
            grantedRefs_.insert(it, {id, 1});
            tags_.Add(id);
        }
    }
}

void SkillSet::ReleaseTags(const TagContainer& tags)
{
    for (TagId id : tags.Ids())
    {
        auto it = std::lower_bound(grantedRefs_.begin(), grantedRefs_.end(), id,
                                   [](const std::pair<TagId, int>& entry, TagId value) { return entry.first < value; });
        if (it == grantedRefs_.end() || it->first != id)
        {
            continue;
        }
        if (--it->second <= 0)
        {
            grantedRefs_.erase(it);
            // Only remove the tag from the merged view when nothing else grants
            // it either; persistent tags were never in this list.
            tags_.Remove(id);
        }
    }
}

void SkillSet::Emit(SkillEventType type, TagId id, TagId source)
{
    if (!onEvent_)
    {
        return;
    }
    SkillEvent event;
    event.type = type;
    event.id = id;
    event.source = source;
    onEvent_(event);
}

bool SkillSet::CheckRequirements(const TagContainer& required, const TagContainer& blocked) const
{
    if (!required.Empty() && !tags_.MatchesAll(required))
    {
        return false;
    }
    if (!blocked.Empty() && tags_.MatchesAny(blocked))
    {
        return false;
    }
    return true;
}

// SkillSet: skills

bool SkillSet::Grant(TagId skillId)
{
    if (SkillLibrary::Instance().Skill(skillId) == nullptr)
    {
        BOY_WARN("Skills", "cannot grant unknown skill \"" + TagRegistry::Instance().Name(skillId) + "\"");
        return false;
    }
    auto it = std::lower_bound(granted_.begin(), granted_.end(), skillId);
    if (it != granted_.end() && *it == skillId)
    {
        return false;
    }
    granted_.insert(it, skillId);
    return true;
}

bool SkillSet::Revoke(TagId skillId)
{
    auto it = std::lower_bound(granted_.begin(), granted_.end(), skillId);
    if (it == granted_.end() || *it != skillId)
    {
        return false;
    }
    Cancel(skillId);
    granted_.erase(it);
    cooldowns_.erase(skillId);
    return true;
}

bool SkillSet::HasSkill(TagId skillId) const
{
    return std::binary_search(granted_.begin(), granted_.end(), skillId);
}

bool SkillSet::IsActive(TagId skillId) const
{
    for (const ActiveSkill& skill : activeSkills_)
    {
        if (skill.id == skillId)
        {
            return true;
        }
    }
    return false;
}

float SkillSet::Cooldown(TagId skillId) const
{
    auto it = cooldowns_.find(skillId);
    return it == cooldowns_.end() ? 0.f : it->second;
}

float SkillSet::CooldownFraction(TagId skillId) const
{
    const SkillDef* def = SkillLibrary::Instance().Skill(skillId);
    if (def == nullptr || def->cooldown <= 0.f)
    {
        return 0.f;
    }
    return ClampValue(Cooldown(skillId) / def->cooldown, 0.f, 1.f);
}

bool SkillSet::CanActivate(TagId skillId) const
{
    // The data-only half of the question. A skill's own canActivate hook runs at
    // activation time, where the real source is known; that is the one that gets
    // the final word.
    const SkillDef* def = SkillLibrary::Instance().Skill(skillId);
    if (def == nullptr || !HasSkill(skillId))
    {
        return false;
    }
    if (IsActive(skillId) || Cooldown(skillId) > 0.f)
    {
        return false;
    }
    if (def->costAttribute != kInvalidTag && attributes_.Current(def->costAttribute) < def->cost)
    {
        return false;
    }
    return CheckRequirements(def->required, def->blocked);
}

bool SkillSet::Activate(TagId skillId, TagId source)
{
    if (!CanActivate(skillId))
    {
        Emit(SkillEventType::SkillFailed, skillId, source);
        return false;
    }

    const SkillDef* def = SkillLibrary::Instance().Skill(skillId);
    if (def == nullptr)
    {
        return false;
    }

    if (def->canActivate && !def->canActivate(*this, source))
    {
        Emit(SkillEventType::SkillFailed, skillId, source);
        return false;
    }

    if (def->costAttribute != kInvalidTag && def->cost != 0.f)
    {
        attributes_.AddCurrent(def->costAttribute, -def->cost);
    }
    if (def->cooldown > 0.f)
    {
        cooldowns_[skillId] = def->cooldown;
    }

    if (def->duration > 0.f)
    {
        ActiveSkill active;
        active.handle = nextHandle_++;
        active.id = skillId;
        active.source = source;
        active.remaining = def->duration;
        activeSkills_.push_back(active);
        GrantTags(def->grantedTags);
    }

    if (def->onActivate)
    {
        def->onActivate(*this, source);
    }

    Emit(SkillEventType::SkillActivated, skillId, source);
    return true;
}

void SkillSet::Cancel(TagId skillId)
{
    for (std::size_t i = 0; i < activeSkills_.size(); ++i)
    {
        if (activeSkills_[i].id != skillId)
        {
            continue;
        }

        const SkillDef* def = SkillLibrary::Instance().Skill(skillId);
        if (def != nullptr)
        {
            ReleaseTags(def->grantedTags);
            if (def->onEnd)
            {
                def->onEnd(*this);
            }
        }
        activeSkills_.erase(activeSkills_.begin() + static_cast<std::ptrdiff_t>(i));
        Emit(SkillEventType::SkillEnded, skillId, kInvalidTag);
        return;
    }
}

// SkillSet: effects

std::uint32_t SkillSet::ApplyEffect(TagId effectId, TagId source, float durationOverride)
{
    const EffectDef* def = SkillLibrary::Instance().Effect(effectId);
    if (def == nullptr)
    {
        BOY_WARN("Skills", "cannot apply unknown effect \"" + TagRegistry::Instance().Name(effectId) + "\"");
        return 0;
    }
    if (!CheckRequirements(def->required, def->blocked))
    {
        return 0;
    }

    // An override changes how long this application lasts, and a non-zero
    // override on an instant definition turns it into a timed one.
    const float duration = durationOverride > 0.f ? durationOverride : def->duration;

    // Instant: fold into the base and store nothing, so an instant heal leaves no
    // residue in the active list and cannot be dispelled afterwards.
    if (duration <= 0.f)
    {
        for (const AttributeModifier& mod : def->modifiers)
        {
            if (!attributes_.Contains(mod.attribute))
            {
                continue;
            }
            switch (mod.op)
            {
                case ModifierOp::Add:
                    attributes_.AddBase(mod.attribute, mod.magnitude);
                    break;
                case ModifierOp::Multiply:
                    attributes_.SetBase(mod.attribute, attributes_.Base(mod.attribute) * mod.magnitude);
                    break;
                case ModifierOp::Override:
                    attributes_.SetBase(mod.attribute, mod.magnitude);
                    break;
            }
        }
        // An instant effect changed the base, so the cached current is stale:
        // fold it through the same path a duration effect uses.
        dirty_ = true;
        Recompute();
        Emit(SkillEventType::EffectApplied, effectId, source);
        return 0;
    }

    for (ActiveEffect& active : effects_)
    {
        if (active.id != effectId)
        {
            continue;
        }

        // Already present: refresh, or stack when the definition allows it.
        if (!def->refreshOnReapply && active.stacks < def->maxStacks)
        {
            ++active.stacks;
            dirty_ = true;
        }
        active.remaining = duration;
        active.accumulator = 0.f;
        Emit(SkillEventType::EffectApplied, effectId, source);
        return active.handle;
    }

    ActiveEffect active;
    active.handle = nextHandle_++;
    active.id = effectId;
    active.source = source;
    active.remaining = duration;
    active.stacks = 1;
    effects_.push_back(active);
    GrantTags(def->grantedTags);
    dirty_ = true;

    Emit(SkillEventType::EffectApplied, effectId, source);
    return active.handle;
}

namespace
{

// Erases the effect with this handle and returns true when it was found.
bool EraseEffect(std::vector<ActiveEffect>& effects, std::uint32_t handle)
{
    for (std::size_t i = 0; i < effects.size(); ++i)
    {
        if (effects[i].handle != handle)
        {
            continue;
        }
        effects.erase(effects.begin() + static_cast<std::ptrdiff_t>(i));
        return true;
    }
    return false;
}

} // namespace

bool SkillSet::RemoveEffect(TagId effectId)
{
    for (std::size_t i = 0; i < effects_.size(); ++i)
    {
        if (effects_[i].id != effectId)
        {
            continue;
        }

        const EffectDef* def = SkillLibrary::Instance().Effect(effectId);
        if (def != nullptr)
        {
            ReleaseTags(def->grantedTags);
        }
        effects_.erase(effects_.begin() + static_cast<std::ptrdiff_t>(i));
        dirty_ = true;
        Emit(SkillEventType::EffectRemoved, effectId, kInvalidTag);
        return true;
    }
    return false;
}

bool SkillSet::RemoveEffectByHandle(std::uint32_t handle)
{
    for (const ActiveEffect& active : effects_)
    {
        if (active.handle != handle)
        {
            continue;
        }
        // Copy before erasing: `active` points into the vector being modified.
        const TagId id = active.id;
        const EffectDef* def = SkillLibrary::Instance().Effect(id);
        if (def != nullptr)
        {
            ReleaseTags(def->grantedTags);
        }
        dirty_ = true;
        EraseEffect(effects_, handle);
        Emit(SkillEventType::EffectRemoved, id, kInvalidTag);
        return true;
    }
    return false;
}

bool SkillSet::HasEffect(TagId effectId) const
{
    for (const ActiveEffect& active : effects_)
    {
        if (active.id == effectId)
        {
            return true;
        }
    }
    return false;
}

int SkillSet::Stacks(TagId effectId) const
{
    int total = 0;
    for (const ActiveEffect& active : effects_)
    {
        if (active.id == effectId)
        {
            total += active.stacks;
        }
    }
    return total;
}

// SkillSet: frame

void SkillSet::Tick(float dt)
{
    if (dt <= 0.f)
    {
        return;
    }

    for (auto it = cooldowns_.begin(); it != cooldowns_.end();)
    {
        it->second -= dt;
        if (it->second <= 0.f)
        {
            it = cooldowns_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Cancellation tags can appear from any effect, so all of them are checked
    // once per tick rather than at every tag grant.
    for (std::size_t i = 0; i < activeSkills_.size();)
    {
        ActiveSkill& active = activeSkills_[i];
        const SkillDef* def = SkillLibrary::Instance().Skill(active.id);
        if (def == nullptr)
        {
            activeSkills_.erase(activeSkills_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        if (!def->cancelTags.Empty() && tags_.MatchesAny(def->cancelTags))
        {
            const TagId id = active.id;
            Cancel(id);
            continue;
        }
        ++i;
    }

    for (std::size_t i = 0; i < activeSkills_.size();)
    {
        const std::uint32_t handle = activeSkills_[i].handle;
        const TagId id = activeSkills_[i].id;
        activeSkills_[i].remaining -= dt;

        const SkillDef* def = SkillLibrary::Instance().Skill(id);
        if (activeSkills_[i].remaining <= 0.f)
        {
            // A tick may have cancelled this skill; look it up again by handle
            // instead of trusting the index.
            if (def != nullptr && def->onEnd)
            {
                def->onEnd(*this);
            }

            bool found = false;
            for (std::size_t k = 0; k < activeSkills_.size(); ++k)
            {
                if (activeSkills_[k].handle != handle)
                {
                    continue;
                }
                ReleaseTags(def != nullptr ? def->grantedTags : TagContainer());
                activeSkills_.erase(activeSkills_.begin() + static_cast<std::ptrdiff_t>(k));
                found = true;
                break;
            }
            if (found)
            {
                Emit(SkillEventType::SkillEnded, id, kInvalidTag);
            }
            continue;
        }

        if (def != nullptr && def->onTick)
        {
            def->onTick(*this, dt);
        }
        ++i;
    }

    for (std::size_t i = 0; i < effects_.size();)
    {
        const std::uint32_t handle = effects_[i].handle;
        const TagId id = effects_[i].id;
        const EffectDef* def = SkillLibrary::Instance().Effect(id);
        if (def == nullptr)
        {
            effects_.erase(effects_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        if (!def->removeOnTags.Empty() && tags_.MatchesAny(def->removeOnTags))
        {
            ReleaseTags(def->grantedTags);
            EraseEffect(effects_, handle);
            dirty_ = true;
            Emit(SkillEventType::EffectExpired, id, kInvalidTag);
            continue;
        }

        const float before = effects_[i].remaining;
        effects_[i].remaining -= dt;

        if (def->period > 0.f && def->periodAttribute != kInvalidTag)
        {
            // Only the part of the frame that fits inside the effect's lifetime
            // counts, so a DoT expiring mid-frame cannot tick an extra time.
            effects_[i].accumulator += std::min(dt, before);
            while (effects_[i].accumulator >= def->period)
            {
                effects_[i].accumulator -= def->period;
                // Periodic ticks move the base, not the cached current: the next
                // recompute starts from base and must not undo the damage.
                attributes_.AddBase(def->periodAttribute,
                                    def->periodMagnitude * static_cast<float>(effects_[i].stacks));
                dirty_ = true;
                Emit(SkillEventType::EffectTicked, id, kInvalidTag);
            }
        }

        if (effects_[i].remaining <= 0.f)
        {
            ReleaseTags(def->grantedTags);
            EraseEffect(effects_, handle);
            dirty_ = true;
            Emit(SkillEventType::EffectExpired, id, kInvalidTag);
            continue;
        }
        ++i;
    }

    if (dirty_)
    {
        Recompute();
    }
}

void SkillSet::Recompute()
{
    // One pass per attribute, folding every active modifier. The attribute count
    // and the active effect count are both tiny, which is why a full recompute is
    // cheaper than an incremental undo log - and impossible to get out of sync.
    for (int i = 0; i < attributes_.Count(); ++i)
    {
        const TagId id = attributes_.GetEntry(i).id;
        float value = attributes_.BaseAt(i);

        for (const ActiveEffect& active : effects_)
        {
            const EffectDef* def = SkillLibrary::Instance().Effect(active.id);
            if (def == nullptr)
            {
                continue;
            }
            for (const AttributeModifier& mod : def->modifiers)
            {
                if (mod.attribute != id)
                {
                    continue;
                }
                switch (mod.op)
                {
                    case ModifierOp::Add:
                        value += mod.magnitude * static_cast<float>(active.stacks);
                        break;
                    case ModifierOp::Multiply:
                        value *= std::pow(mod.magnitude, static_cast<float>(active.stacks));
                        break;
                    case ModifierOp::Override:
                        value = mod.magnitude;
                        break;
                }
            }
        }

        attributes_.SetCurrentAt(i, value);
        attributes_.ClampSlot(i);
    }
    dirty_ = false;
}

// SkillSet: persistence

nlohmann::json SkillSet::ToJson() const
{
    nlohmann::json j = nlohmann::json::object();
    const TagRegistry& registry = TagRegistry::Instance();

    nlohmann::json attrs = nlohmann::json::array();
    for (int i = 0; i < attributes_.Count(); ++i)
    {
        const AttributeSet::Entry& entry = attributes_.GetEntry(i);
        if (!IsSerializable(entry.id))
        {
            continue;
        }
        attrs.push_back({{"id", registry.Name(entry.id)},
                         {"base", entry.base},
                         {"current", entry.current},
                         {"min", entry.min},
                         {"max", entry.max}});
    }
    j["attributes"] = std::move(attrs);
    j["tags"] = TagsToNames(persistent_);

    nlohmann::json effects = nlohmann::json::array();
    for (const ActiveEffect& active : effects_)
    {
        if (!IsSerializable(active.id))
        {
            continue;
        }
        effects.push_back({{"id", registry.Name(active.id)},
                           {"source", registry.Name(active.source)},
                           {"remaining", active.remaining},
                           {"stacks", active.stacks}});
    }
    j["effects"] = std::move(effects);

    nlohmann::json skills = nlohmann::json::array();
    for (TagId id : granted_)
    {
        if (IsSerializable(id))
        {
            skills.push_back(registry.Name(id));
        }
    }
    j["skills"] = std::move(skills);

    nlohmann::json cooldowns = nlohmann::json::array();
    for (const auto& [id, remaining] : cooldowns_)
    {
        if (IsSerializable(id))
        {
            cooldowns.push_back({{"id", registry.Name(id)}, {"remaining", remaining}});
        }
    }
    j["cooldowns"] = std::move(cooldowns);
    return j;
}

bool SkillSet::FromJson(const nlohmann::json& j)
{
    if (!j.is_object())
    {
        return false;
    }

    Reset();
    bool ok = true;

    if (j.contains("attributes") && j["attributes"].is_array())
    {
        for (const nlohmann::json& item : j["attributes"])
        {
            if (!item.is_object())
            {
                ok = false;
                continue;
            }
            const TagId id = ResolveTag(item.value("id", ""));
            if (id == kInvalidTag)
            {
                ok = false;
                continue;
            }
            const float min = item.value("min", std::numeric_limits<float>::lowest());
            const float max = item.value("max", std::numeric_limits<float>::max());
            DefineAttribute(id, item.value("base", 0.f), min, max);
            attributes_.SetCurrent(id, item.value("current", item.value("base", 0.f)));
        }
    }

    const TagContainer savedTags = TagsByName(j.value("tags", nlohmann::json::array()));
    for (TagId id : savedTags.Ids())
    {
        AddPersistentTag(id);
    }

    if (j.contains("skills") && j["skills"].is_array())
    {
        for (const nlohmann::json& item : j["skills"])
        {
            const TagId id = ResolveTag(item.is_string() ? item.get<std::string>() : std::string());
            if (id == kInvalidTag)
            {
                ok = false;
                continue;
            }
            Grant(id);
        }
    }

    if (j.contains("effects") && j["effects"].is_array())
    {
        for (const nlohmann::json& item : j["effects"])
        {
            if (!item.is_object())
            {
                ok = false;
                continue;
            }
            const TagId id = ResolveTag(item.value("id", ""));
            if (id == kInvalidTag)
            {
                ok = false;
                continue;
            }
            const std::uint32_t handle = ApplyEffect(id, ResolveTag(item.value("source", "")));
            for (ActiveEffect& active : effects_)
            {
                if (active.handle != handle)
                {
                    continue;
                }
                active.remaining = item.value("remaining", active.remaining);
                active.stacks = item.value("stacks", 1);
                break;
            }
        }
    }

    if (j.contains("cooldowns") && j["cooldowns"].is_array())
    {
        for (const nlohmann::json& item : j["cooldowns"])
        {
            if (!item.is_object())
            {
                continue;
            }
            const TagId id = ResolveTag(item.value("id", ""));
            if (id != kInvalidTag)
            {
                cooldowns_[id] = item.value("remaining", 0.f);
            }
        }
    }

    // Effects were installed after the attributes, so fold them in once here
    // rather than leaving the loaded set one tick behind.
    if (dirty_)
    {
        Recompute();
    }

    return ok;
}

// SkillLibrary

SkillLibrary& SkillLibrary::Instance()
{
    static SkillLibrary library;
    return library;
}

void SkillLibrary::DefineEffect(EffectDef def)
{
    if (def.id == kInvalidTag)
    {
        BOY_ERROR("Skills", "effect defined without an id");
        return;
    }
    effects_[def.id] = std::move(def);
}

void SkillLibrary::DefineSkill(SkillDef def)
{
    if (def.id == kInvalidTag)
    {
        BOY_ERROR("Skills", "skill defined without an id");
        return;
    }
    skills_[def.id] = std::move(def);
}

const EffectDef* SkillLibrary::Effect(TagId id) const
{
    auto it = effects_.find(id);
    return it == effects_.end() ? nullptr : &it->second;
}

const SkillDef* SkillLibrary::Skill(TagId id) const
{
    auto it = skills_.find(id);
    return it == skills_.end() ? nullptr : &it->second;
}

EffectDef* SkillLibrary::EditEffect(TagId id)
{
    auto it = effects_.find(id);
    return it == effects_.end() ? nullptr : &it->second;
}

SkillDef* SkillLibrary::EditSkill(TagId id)
{
    auto it = skills_.find(id);
    return it == skills_.end() ? nullptr : &it->second;
}

std::vector<TagId> SkillLibrary::Effects() const
{
    std::vector<TagId> out;
    out.reserve(effects_.size());
    for (const auto& [id, def] : effects_)
    {
        out.push_back(id);
    }
    // Named order: a balance file and the editor list read the same on every run.
    const TagRegistry& registry = TagRegistry::Instance();
    std::sort(out.begin(), out.end(), [&registry](TagId a, TagId b) { return registry.Name(a) < registry.Name(b); });
    return out;
}

std::vector<TagId> SkillLibrary::Skills() const
{
    std::vector<TagId> out;
    out.reserve(skills_.size());
    for (const auto& [id, def] : skills_)
    {
        out.push_back(id);
    }
    const TagRegistry& registry = TagRegistry::Instance();
    std::sort(out.begin(), out.end(), [&registry](TagId a, TagId b) { return registry.Name(a) < registry.Name(b); });
    return out;
}

bool SkillLibrary::LoadFromJson(const nlohmann::json& j)
{
    if (!j.is_object())
    {
        return false;
    }

    bool ok = true;

    if (j.contains("effects") && j["effects"].is_array())
    {
        for (const nlohmann::json& item : j["effects"])
        {
            if (!item.is_object())
            {
                ok = false;
                continue;
            }
            EffectDef def;
            def.id = TagByName(item.value("id", nlohmann::json()));
            if (def.id == kInvalidTag)
            {
                ok = false;
                continue;
            }
            def.duration = item.value("duration", 0.f);
            def.period = item.value("period", 0.f);
            def.periodAttribute = TagByName(item.value("attribute", nlohmann::json()));
            def.periodMagnitude = item.value("magnitude", 0.f);
            def.refreshOnReapply = item.value("refresh", true);
            def.maxStacks = item.value("maxStacks", 1);
            def.grantedTags = TagsByName(item.value("granted", nlohmann::json::array()));
            def.required = TagsByName(item.value("required", nlohmann::json::array()));
            def.blocked = TagsByName(item.value("blocked", nlohmann::json::array()));
            def.removeOnTags = TagsByName(item.value("removeOn", nlohmann::json::array()));

            if (item.contains("modifiers") && item["modifiers"].is_array())
            {
                for (const nlohmann::json& modJson : item["modifiers"])
                {
                    if (!modJson.is_object())
                    {
                        continue;
                    }
                    AttributeModifier mod;
                    mod.attribute = TagByName(modJson.value("attribute", nlohmann::json()));
                    if (mod.attribute == kInvalidTag)
                    {
                        continue;
                    }
                    mod.op = OpFromString(modJson.value("op", std::string("add")));
                    mod.magnitude = modJson.value("magnitude", 0.f);
                    def.modifiers.push_back(mod);
                }
            }
            DefineEffect(std::move(def));
        }
    }

    if (j.contains("skills") && j["skills"].is_array())
    {
        for (const nlohmann::json& item : j["skills"])
        {
            if (!item.is_object())
            {
                ok = false;
                continue;
            }
            SkillDef def;
            def.id = TagByName(item.value("id", nlohmann::json()));
            if (def.id == kInvalidTag)
            {
                ok = false;
                continue;
            }
            def.cooldown = item.value("cooldown", 0.f);
            def.costAttribute = TagByName(item.value("cost", nlohmann::json()));
            def.cost = item.value("costAmount", 0.f);
            def.duration = item.value("duration", 0.f);
            def.required = TagsByName(item.value("required", nlohmann::json::array()));
            def.blocked = TagsByName(item.value("blocked", nlohmann::json::array()));
            def.cancelTags = TagsByName(item.value("cancel", nlohmann::json::array()));
            def.grantedTags = TagsByName(item.value("granted", nlohmann::json::array()));

            // A JSON reload must not erase behaviour that code already attached.
            if (const SkillDef* existing = Skill(def.id))
            {
                def.canActivate = existing->canActivate;
                def.onActivate = existing->onActivate;
                def.onTick = existing->onTick;
                def.onEnd = existing->onEnd;
            }
            DefineSkill(std::move(def));
        }
    }

    return ok;
}

nlohmann::json SkillLibrary::ToJson() const
{
    nlohmann::json j = nlohmann::json::object();
    nlohmann::json effectList = nlohmann::json::array();

    for (TagId id : Effects())
    {
        const EffectDef& def = effects_.at(id);
        if (!IsSerializable(id))
        {
            continue;
        }
        nlohmann::json item = {{"id", TagRegistry::Instance().Name(id)},
                               {"duration", def.duration},
                               {"period", def.period},
                               {"refresh", def.refreshOnReapply},
                               {"maxStacks", def.maxStacks},
                               {"granted", TagsToNames(def.grantedTags)},
                               {"required", TagsToNames(def.required)},
                               {"blocked", TagsToNames(def.blocked)},
                               {"removeOn", TagsToNames(def.removeOnTags)}};
        if (def.periodAttribute != kInvalidTag)
        {
            item["attribute"] = TagRegistry::Instance().Name(def.periodAttribute);
            item["magnitude"] = def.periodMagnitude;
        }
        if (!def.modifiers.empty())
        {
            nlohmann::json mods = nlohmann::json::array();
            for (const AttributeModifier& mod : def.modifiers)
            {
                mods.push_back({{"attribute", TagRegistry::Instance().Name(mod.attribute)},
                                {"op", OpToString(mod.op)},
                                {"magnitude", mod.magnitude}});
            }
            item["modifiers"] = std::move(mods);
        }
        effectList.push_back(std::move(item));
    }
    j["effects"] = std::move(effectList);

    nlohmann::json skillList = nlohmann::json::array();
    for (TagId id : Skills())
    {
        const SkillDef& def = skills_.at(id);
        if (!IsSerializable(id))
        {
            continue;
        }
        nlohmann::json item = {{"id", TagRegistry::Instance().Name(id)},
                               {"cooldown", def.cooldown},
                               {"costAmount", def.cost},
                               {"duration", def.duration},
                               {"required", TagsToNames(def.required)},
                               {"blocked", TagsToNames(def.blocked)},
                               {"cancel", TagsToNames(def.cancelTags)},
                               {"granted", TagsToNames(def.grantedTags)}};
        if (def.costAttribute != kInvalidTag)
        {
            item["cost"] = TagRegistry::Instance().Name(def.costAttribute);
        }
        skillList.push_back(std::move(item));
    }
    j["skills"] = std::move(skillList);
    return j;
}

bool SkillLibrary::LoadFromFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        BOY_WARN("Skills", "cannot open skill data " + path);
        return false;
    }

    nlohmann::json j;
    try
    {
        file >> j;
    }
    catch (const std::exception& e)
    {
        BOY_ERROR("Skills", "skill data " + path + " is not valid JSON: " + e.what());
        return false;
    }

    const bool ok = LoadFromJson(j);
    BOY_INFO("Skills", "loaded skill data from " + path);
    return ok;
}

bool SkillLibrary::SaveToFile(const std::string& path) const
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        BOY_ERROR("Skills", "cannot write skill data " + path);
        return false;
    }
    file << ToJson().dump(2) << '\n';
    return true;
}

void SkillLibrary::Clear()
{
    effects_.clear();
    skills_.clear();
}

// DamageRules

DamageRules& DamageRules::Instance()
{
    static DamageRules rules;
    return rules;
}

void DamageRules::Clear()
{
    rules_.clear();
    default_ = 1.f;
}

void DamageRules::AddRule(TagId damageType, const TagContainer& targetRequired, float multiplier)
{
    if (damageType == kInvalidTag)
    {
        return;
    }
    Rule rule;
    rule.required = targetRequired;
    rule.multiplier = multiplier;
    rules_[damageType].push_back(rule);
}

void DamageRules::SetDefault(float multiplier)
{
    default_ = multiplier;
}

float DamageRules::Multiplier(TagId damageType, const TagContainer& targetTags) const
{
    auto it = rules_.find(damageType);
    if (it == rules_.end())
    {
        return default_;
    }

    float result = default_;
    for (const Rule& rule : it->second)
    {
        // Last match wins: a specific rule written later overrides a general one
        // without a priority field that nobody would keep correct.
        if (rule.required.Empty() || targetTags.MatchesAll(rule.required))
        {
            result = rule.multiplier;
        }
    }
    return result;
}

bool DamageRules::LoadFromJson(const nlohmann::json& j)
{
    if (!j.is_object())
    {
        return false;
    }

    default_ = j.value("default", 1.f);
    if (!j.contains("rules") || !j["rules"].is_array())
    {
        return true;
    }

    bool ok = true;
    for (const nlohmann::json& item : j["rules"])
    {
        if (!item.is_object())
        {
            ok = false;
            continue;
        }
        const TagId damage = TagByName(item.value("damage", nlohmann::json()));
        if (damage == kInvalidTag)
        {
            ok = false;
            continue;
        }
        AddRule(damage, TagsByName(item.value("target", nlohmann::json::array())), item.value("multiplier", 1.f));
    }
    return ok;
}

nlohmann::json DamageRules::ToJson() const
{
    nlohmann::json j = nlohmann::json::object();
    j["default"] = default_;

    nlohmann::json rules = nlohmann::json::array();
    for (const auto& [damage, list] : rules_)
    {
        if (!IsSerializable(damage))
        {
            continue;
        }
        for (const Rule& rule : list)
        {
            rules.push_back({{"damage", TagRegistry::Instance().Name(damage)},
                             {"target", TagsToNames(rule.required)},
                             {"multiplier", rule.multiplier}});
        }
    }
    j["rules"] = std::move(rules);
    return j;
}

// ScriptBridge

namespace
{

// The graph compiler passes the tag as the last argument and the object it acts
// on as the first; in a 2D game the object is the bound set, so only the tag is
// read. Taking the last string also makes a one-argument call work.
std::string LastString(const std::vector<FSValue>& args)
{
    for (std::size_t i = args.size(); i > 0; --i)
    {
        if (const std::string* text = std::get_if<std::string>(&args[i - 1]))
        {
            return *text;
        }
    }
    return {};
}

double NumberArg(const std::vector<FSValue>& args, std::size_t index, double fallback = 0.0)
{
    if (index >= args.size())
    {
        return fallback;
    }
    if (const double* number = std::get_if<double>(&args[index]))
    {
        return *number;
    }
    if (const bool* flag = std::get_if<bool>(&args[index]))
    {
        return *flag ? 1.0 : 0.0;
    }
    return fallback;
}

// The value argument of a setter, which the graph compiler appends last.
double LastNumber(const std::vector<FSValue>& args, double fallback = 0.0)
{
    for (std::size_t i = args.size(); i > 0; --i)
    {
        if (const double* number = std::get_if<double>(&args[i - 1]))
        {
            return *number;
        }
        if (const bool* flag = std::get_if<bool>(&args[i - 1]))
        {
            return *flag ? 1.0 : 0.0;
        }
    }
    return fallback;
}

} // namespace

ScriptBridge& ScriptBridge::Instance()
{
    static ScriptBridge bridge;
    return bridge;
}

void ScriptBridge::Bind(SkillSet* skills)
{
    tags_ = nullptr;
    attributes_ = nullptr;
    skills_ = skills;
}

void ScriptBridge::Bind(TagContainer* tags, AttributeSet* attributes, SkillSet* skills)
{
    tags_ = tags;
    attributes_ = attributes;
    skills_ = skills;
}

void ScriptBridge::Unbind()
{
    tags_ = nullptr;
    attributes_ = nullptr;
    skills_ = nullptr;
}

bool ScriptBridge::HasTagImpl(TagId id) const
{
    if (id == kInvalidTag)
    {
        return false;
    }
    if (skills_ != nullptr)
    {
        return skills_->Tags().Has(id);
    }
    return tags_ != nullptr && tags_->Has(id);
}

bool ScriptBridge::MatchesTagImpl(TagId id) const
{
    if (id == kInvalidTag)
    {
        return false;
    }
    if (skills_ != nullptr)
    {
        return skills_->Tags().Matches(id);
    }
    return tags_ != nullptr && tags_->Matches(id);
}

void ScriptBridge::AddTagImpl(TagId id)
{
    if (id == kInvalidTag)
    {
        return;
    }
    if (skills_ != nullptr)
    {
        skills_->AddPersistentTag(id);
    }
    else if (tags_ != nullptr)
    {
        tags_->Add(id);
    }
}

bool ScriptBridge::RemoveTagImpl(TagId id)
{
    if (id == kInvalidTag)
    {
        return false;
    }
    if (skills_ != nullptr)
    {
        return skills_->RemovePersistentTag(id);
    }
    return tags_ != nullptr && tags_->Remove(id);
}

AttributeSet* ScriptBridge::Attributes()
{
    if (skills_ != nullptr)
    {
        return &skills_->Attributes();
    }
    return attributes_;
}

void ScriptBridge::Install(FemboyScript& script)
{
    script.RegisterFunction("hasTag", [this](const std::vector<FSValue>& args) -> FSValue
                            { return FSValue{HasTagImpl(ResolveTag(LastString(args)))}; });

    script.RegisterFunction("matchesTag", [this](const std::vector<FSValue>& args) -> FSValue
                            { return FSValue{MatchesTagImpl(ResolveTag(LastString(args)))}; });

    script.RegisterFunction("addTag",
                            [this](const std::vector<FSValue>& args) -> FSValue
                            {
                                const TagId id = ResolveTag(LastString(args));
                                AddTagImpl(id);
                                return FSValue{id != kInvalidTag};
                            });

    script.RegisterFunction("removeTag", [this](const std::vector<FSValue>& args) -> FSValue
                            { return FSValue{RemoveTagImpl(ResolveTag(LastString(args)))}; });

    script.RegisterFunction("tagQuery",
                            [this](const std::vector<FSValue>& args) -> FSValue
                            {
                                // mode: 0 = any, 1 = all, 2 = exact, 3 = none.
                                const int mode = static_cast<int>(NumberArg(args, 0, 0.0));
                                const TagId id = ResolveTag(LastString(args));
                                if (id == kInvalidTag || !Bound())
                                {
                                    return FSValue{false};
                                }

                                switch (mode)
                                {
                                    case 1:
                                        return FSValue{MatchesTagImpl(id)};
                                    case 3:
                                        return FSValue{!MatchesTagImpl(id)};
                                    case 2:
                                        return FSValue{HasTagImpl(id)};
                                    case 0:
                                    default:
                                        return FSValue{HasTagImpl(id) || MatchesTagImpl(id)};
                                }
                            });

    script.RegisterFunction("activateSkill",
                            [this](const std::vector<FSValue>& args) -> FSValue
                            {
                                const TagId id = ResolveTag(LastString(args));
                                if (skills_ == nullptr || id == kInvalidTag)
                                {
                                    return FSValue{false};
                                }
                                return FSValue{skills_->Activate(id)};
                            });

    script.RegisterFunction("hasEffect",
                            [this](const std::vector<FSValue>& args) -> FSValue
                            {
                                const TagId id = ResolveTag(LastString(args));
                                if (skills_ == nullptr || id == kInvalidTag)
                                {
                                    return FSValue{false};
                                }
                                return FSValue{skills_->HasEffect(id)};
                            });

    script.RegisterFunction("applyEffect",
                            [this](const std::vector<FSValue>& args) -> FSValue
                            {
                                const TagId id = ResolveTag(LastString(args));
                                if (skills_ == nullptr || id == kInvalidTag)
                                {
                                    return FSValue{false};
                                }
                                return FSValue{skills_->ApplyEffect(id) != 0};
                            });

    script.RegisterFunction("attribute",
                            [this](const std::vector<FSValue>& args) -> FSValue
                            {
                                const TagId id = ResolveTag(LastString(args));
                                AttributeSet* set = Attributes();
                                if (set == nullptr || id == kInvalidTag)
                                {
                                    return FSValue{0.0};
                                }
                                return FSValue{static_cast<double>(set->Current(id))};
                            });

    script.RegisterFunction("setAttribute",
                            [this](const std::vector<FSValue>& args) -> FSValue
                            {
                                const TagId id = ResolveTag(LastString(args));
                                AttributeSet* set = Attributes();
                                if (set == nullptr || id == kInvalidTag)
                                {
                                    return FSValue{false};
                                }
                                set->SetCurrent(id, static_cast<float>(LastNumber(args, 0.0)));
                                return FSValue{true};
                            });
}

void InstallSkillScripts()
{
    ScriptBridge::Instance().Install(Engine::Instance().GetFemboyScript());
}

} // namespace Fem
