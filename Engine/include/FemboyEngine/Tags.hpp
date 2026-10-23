// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
//
// Gameplay tags: the engine's answer to "a label that a designer, a mod or a
// graph can invent without recompiling".
//
// A tag is a hierarchical dotted name: `Enemy.Mine`, `Damage.Electric`,
// `Arena.Feature.Mines`. A tag knows its parent by construction, so asking "is
// this Enemy.Mine?" and asking "is this an Enemy?" are the same query at
// different depths. That is the whole point: the code that spawns a mine never
// has to know that anything cares about mines, and the code that reacts to
// "Enemy" never has to be edited when a new enemy kind appears.
//
// Tags are *not* a replacement for an enum. An enum is a closed set that a
// switch statement can exhaust, which is exactly right for window modes, draw
// layers or a state machine's phases. A tag is an open set that anyone can add
// to, which is right for classification and cross-cutting queries. The engine
// keeps its enums and uses tags only where a set genuinely grows.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include "FemboyEngine/Core.hpp"

namespace Fem
{

// A tag id is a 32-bit FNV-1a hash of the full dotted name, not an index into a
// table. That makes it stable: the same name gives the same id on every peer, in
// every build and across a save/load, which is what a replication key needs. The
// registry still refuses to hand the same id to two different names, and every
// serialized form uses the name, so a collision can never silently attach the
// wrong tag to the wrong object.
using TagId = std::uint32_t;
constexpr TagId kInvalidTag = 0u;

class TagRegistry
{
public:
    static TagRegistry& Instance();

    // Registers a name and every missing ancestor. Registering the same name
    // twice is free and returns the same id. Returns kInvalidTag when the name
    // is empty or is not a dotted identifier, and logs why.
    TagId RegisterTag(const std::string& name);
    void RegisterTags(const std::vector<std::string>& names);

    TagId Find(const std::string& name) const;
    bool IsValid(TagId id) const;
    std::string Name(TagId id) const; // "" when unknown
    TagId Parent(TagId id) const;     // kInvalidTag at the root
    // Direct children only, sorted by name.
    std::vector<TagId> Children(TagId id) const;
    // True when `id` is a strict descendant of `ancestor`.
    bool IsDescendantOf(TagId id, TagId ancestor) const;
    std::size_t Size() const;

    // Every registered id, sorted by name. Stable order keeps editor lists and
    // generated code diff-friendly.
    std::vector<TagId> All() const;
    // Roots, sorted by name.
    std::vector<TagId> Roots() const;

    // Optional designer metadata. Purely cosmetic: the editor uses the colour to
    // draw a tag and the comment as a tooltip. Neither affects matching.
    std::string Comment(TagId id) const;
    std::string Color(TagId id) const;
    void SetMeta(TagId id, const std::string& comment, const std::string& color);

    // JSON. A flat list of names, because hierarchy is derived from the dots and
    // a flat list has no nesting to get wrong:
    //
    //   { "version": 1,
    //     "tags": [ "Enemy", "Enemy.Mine",
    //               { "name": "Damage.Electric", "color": "#8cf" } ] }
    //
    // A bare array of strings is also accepted, which is what a hand-written
    // file usually looks like. `replace` drops the current table first, which is
    // only ever safe before anything has queried a tag.
    bool LoadFromJson(const nlohmann::json& j, bool replace = false);
    nlohmann::json ToJson() const;
    bool SaveToFile(const std::string& path) const;
    bool LoadFromFile(const std::string& path);

    // Renaming a tag cannot move its id: the id *is* the old name hashed. So a
    // rename registers the new name, moves the metadata across, and leaves an
    // alias behind. Every name lookup consults the aliases, which means saves,
    // graphs and mods written against the old name keep resolving to the new tag
    // instead of silently turning into "unregistered".
    bool RenameTag(const std::string& from, const std::string& to);
    // from -> to for every rename ever performed, sorted by the old name.
    std::vector<std::pair<std::string, std::string>> Aliases() const;

    // Hot reload without stealing the host's HotSwap callback. pollReload()
    // compares the file's write stamp and re-reads it when it changed; call it
    // once per frame (editor) or once per level (game) - it is a stat call.
    bool WatchFile(const std::string& path);
    void UnwatchFile();
    bool PollReload();

    void Clear();

    // Bloom-style hint over a tag and all of its ancestors, so a container can
    // reject an impossible query in one AND before walking anything.
    std::uint64_t HintBits(TagId id) const;

private:
    TagRegistry() = default;

    struct Entry
    {
        std::string name;
        TagId parent = kInvalidTag;
        std::string comment;
        std::string color;
    };

    // Recursive: registering "A.B.C" registers "A.B" first, which is what makes
    // the parent link fall out of the name instead of a separate declaration.
    TagId RegisterInternal(const std::string& name);

    mutable std::recursive_mutex mtx_;
    std::unordered_map<TagId, Entry> entries_;
    std::unordered_map<std::string, TagId> byName_;
    std::unordered_map<std::string, std::string> aliases_;

    std::string watchedPath_;
    std::int64_t watchedStamp_ = 0;
    bool watching_ = false;
};

// A set of tags on an object. Stored sorted and deduplicated, so equality,
// subset and superset tests are linear scans over a cache-friendly vector and
// iteration order never depends on insertion order.
class TagContainer
{
public:
    TagContainer() = default;
    TagContainer(std::initializer_list<TagId> ids);

    void Add(TagId id);
    // False when the name is unknown: tags are registered, never invented here.
    bool Add(const std::string& name);
    bool Remove(TagId id);
    void Clear();

    bool Empty() const
    {
        return ids_.empty();
    }
    std::size_t Size() const
    {
        return ids_.size();
    }
    const std::vector<TagId>& Ids() const
    {
        return ids_;
    }
    // Bloom hint covering every held tag and all of its ancestors. Two containers
    // can be compared in one AND; a missing bit is a proof of "no such tag".
    std::uint64_t Hint() const
    {
        return hint_;
    }

    // Exact membership.
    bool Has(TagId id) const;
    // Exact or any descendant: matches("Enemy") is true for "Enemy.Mine".
    bool Matches(TagId id) const;

    bool HasAny(const TagContainer& other) const;
    bool HasAll(const TagContainer& other) const;
    bool MatchesAny(const TagContainer& other) const;
    bool MatchesAll(const TagContainer& other) const;

    // "Enemy.Mine, Enemy.Weak" - for logs, tooltips and the inspector.
    std::string ToString() const;

    nlohmann::json ToJson() const;          // array of names, sorted
    bool FromJson(const nlohmann::json& j); // array of names, or one string

private:
    std::vector<TagId> ids_; // sorted, unique
    std::uint64_t hint_ = 0;
};

// A compiled tag query: build it once (at level load, or when a graph is
// instantiated) and evaluate it many times a frame.
//
// The required set is also folded into a bloom hint, so an object that cannot
// possibly match is rejected by a single AND. That is not a micro-optimisation
// for its own sake: an arena with a few hundred tagged objects re-evaluates the
// same handful of queries every frame, and this turns most of them into one
// instruction. The hint can produce false positives but never a false negative,
// so the exact walk still runs whenever the hint passes.
class TagQuery
{
public:
    TagQuery() = default;
    explicit TagQuery(const TagContainer& required, const TagContainer& blocked = TagContainer());

    void SetRequired(const TagContainer& required);
    void SetBlocked(const TagContainer& blocked);

    bool Empty() const
    {
        return required_.Empty() && blocked_.Empty();
    }
    const TagContainer& Required() const
    {
        return required_;
    }
    const TagContainer& Blocked() const
    {
        return blocked_;
    }

    // Every required tag matched (through the hierarchy) and no blocked tag held.
    bool Matches(const TagContainer& container) const;

private:
    TagContainer required_;
    TagContainer blocked_;
    std::uint64_t requiredHint_ = 0;
    std::uint64_t blockedHint_ = 0;
};

// Replicating a tag set carries *names*, never ids.
//
// An id is only meaningful to a build that has the tag registered, so shipping
// ids would make an older client apply the wrong tag to the wrong object the
// moment one side renames anything. Names make the failure honest: the client
// reports the names it does not know instead of guessing.
//
//   tagDelta(previous, current) -> { "add": ["Enemy.Weak"], "remove": ["Effect.Stunned"] }
//
// The delta holds only what changed, so a steady state costs one empty object.
nlohmann::json TagDelta(const TagContainer& previous, const TagContainer& current);

// Applies a delta in place. Unknown names are collected into `unknown` (when
// given) and skipped, and the function returns false so the caller can rate-limit
// a warning instead of spamming one per packet.
bool ApplyTagDelta(TagContainer& container, const nlohmann::json& delta, std::vector<std::string>* unknown = nullptr);

// ECS component: runtime tags on an entity. This sits next to the older
// TagComponent (a single free-form string) rather than replacing it, so existing
// scenes keep loading; new code should use this one.
struct GameplayTags
{
    TagContainer tags;

    bool Has(TagId id) const
    {
        return tags.Has(id);
    }
    bool Matches(TagId id) const
    {
        return tags.Matches(id);
    }
};

// Builds a container from names, skipping any that are unknown. Handy in game
// code where the tags are known literals:
//
//     Enemy e; e.tags = makeTags({"Enemy", "Enemy.Weak"});
TagContainer MakeTags(std::initializer_list<const char*> names);

// Log categories are tags, so this filters Boy by hierarchy: passing "Enemy"
// keeps "Enemy", "Enemy.Mine" and everything else under it, and drops the rest.
// The filter is built from the registry, so it costs one walk of the allowed set
// per message - which is nothing next to formatting the message.
void FilterLogByCategory(const TagContainer& allowed);
void ClearLogFilter();

// Code-declared tags, in the spirit of UE_DEFINE_GAMEPLAY_TAG.
//
//     FEMBOY_DECLARE_TAG(Tag_Enemy_Mine);               // in a header
//     FEMBOY_DEFINE_TAG(Tag_Enemy_Mine, "Enemy.Mine");  // in one .cpp
//
// The tag is registered on first use, so there is no static-initialization order
// problem and no central list to keep in sync. The id is stable for the process.
#define FEMBOY_DECLARE_TAG(Name) ::Fem::TagId Name()

#define FEMBOY_DEFINE_TAG(Name, TagName)                                                                               \
    ::Fem::TagId Name()                                                                                             \
    {                                                                                                                  \
        static const ::Fem::TagId id = ::Fem::TagRegistry::Instance().RegisterTag(TagName);                      \
        return id;                                                                                                     \
    }

} // namespace Fem
