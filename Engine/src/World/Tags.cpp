// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Tags.hpp"

#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Vanity.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

namespace Fem
{
namespace
{

// FNV-1a, 32-bit. Stable, trivially portable and plenty for the handful of tags
// a project declares. The point is not cryptographic strength: it is that the id
// for "Enemy.Mine" is the same number on every machine and in every build.
TagId HashName(const std::string& name)
{
    std::uint32_t h = 2166136261u;
    for (unsigned char c : name)
    {
        h ^= c;
        h *= 16777619u;
    }
    // Zero means "no tag", so a hash that lands on it is nudged out of the way.
    return h == kInvalidTag ? 1u : h;
}

bool IsSegmentChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// A tag name is one or more non-empty segments of [A-Za-z0-9_] separated by
// single dots. Spaces, empty segments and leading/trailing dots are rejected
// rather than sanitized: a silently rewritten tag is a bug you find a week later.
bool IsValidName(const std::string& name)
{
    if (name.empty() || name.size() > 256)
    {
        return false;
    }

    bool segmentHasChar = false;
    for (char c : name)
    {
        if (c == '.')
        {
            if (!segmentHasChar)
            {
                return false;
            }
            segmentHasChar = false;
            continue;
        }
        if (!IsSegmentChar(c))
        {
            return false;
        }
        segmentHasChar = true;
    }
    return segmentHasChar;
}

std::string ParentName(const std::string& name)
{
    const std::size_t dot = name.rfind('.');
    return dot == std::string::npos ? std::string() : name.substr(0, dot);
}

std::string Trim(const std::string& value)
{
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

} // namespace

TagRegistry& TagRegistry::Instance()
{
    static TagRegistry registry;
    return registry;
}

TagId TagRegistry::RegisterInternal(const std::string& name)
{
    auto found = byName_.find(name);
    if (found != byName_.end())
    {
        return found->second;
    }

    const TagId id = HashName(name);

    // Two different names hashing to one id would be silent corruption: the
    // second name would start answering queries meant for the first. Refuse it
    // loudly and keep whichever tag arrived first.
    auto clash = entries_.find(id);
    if (clash != entries_.end())
    {
        BOY_ERROR("Tags", "hash collision: \"" + name + "\" and \"" + clash->second.name + "\" both map to id " +
                              std::to_string(id));
        return id;
    }

    Entry entry;
    entry.name = name;
    // The parent falls straight out of the name, so a caller never has to
    // declare the hierarchy twice.
    const std::string parent = ParentName(name);
    entry.parent = parent.empty() ? kInvalidTag : RegisterInternal(parent);

    entries_.emplace(id, std::move(entry));
    byName_.emplace(name, id);
    return id;
}

TagId TagRegistry::RegisterTag(const std::string& name)
{
    if (!IsValidName(name))
    {
        BOY_ERROR("Tags",
                  "invalid tag name: \"" + name + "\" (expected dotted segments of letters, digits and underscores)");
        return kInvalidTag;
    }

    std::lock_guard<std::recursive_mutex> lock(mtx_);
    return RegisterInternal(name);
}

void TagRegistry::RegisterTags(const std::vector<std::string>& names)
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    for (const std::string& name : names)
    {
        if (IsValidName(name))
        {
            RegisterInternal(name);
        }
    }
}

TagId TagRegistry::Find(const std::string& name) const
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = byName_.find(name);
    if (it != byName_.end())
    {
        return it->second;
    }

    // A renamed tag keeps answering to its old name, so a save or a graph
    // written before the rename still resolves to the same tag.
    auto alias = aliases_.find(name);
    if (alias != aliases_.end())
    {
        auto target = byName_.find(alias->second);
        if (target != byName_.end())
        {
            return target->second;
        }
    }
    return kInvalidTag;
}

bool TagRegistry::IsValid(TagId id) const
{
    if (id == kInvalidTag)
    {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    return entries_.find(id) != entries_.end();
}

std::string TagRegistry::Name(TagId id) const
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = entries_.find(id);
    return it == entries_.end() ? std::string() : it->second.name;
}

TagId TagRegistry::Parent(TagId id) const
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = entries_.find(id);
    return it == entries_.end() ? kInvalidTag : it->second.parent;
}

std::vector<TagId> TagRegistry::Children(TagId id) const
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    std::vector<TagId> out;
    for (const auto& [childId, entry] : entries_)
    {
        if (entry.parent == id && id != kInvalidTag)
        {
            out.push_back(childId);
        }
    }
    std::sort(out.begin(), out.end(), [this](TagId a, TagId b) { return entries_.at(a).name < entries_.at(b).name; });
    return out;
}

bool TagRegistry::IsDescendantOf(TagId id, TagId ancestor) const
{
    if (id == kInvalidTag || ancestor == kInvalidTag || id == ancestor)
    {
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = entries_.find(id);
    if (it == entries_.end())
    {
        return false;
    }

    TagId current = it->second.parent;
    // Depth is bounded by the number of dots in the name, so this is a short walk.
    while (current != kInvalidTag)
    {
        if (current == ancestor)
        {
            return true;
        }
        auto up = entries_.find(current);
        if (up == entries_.end())
        {
            break;
        }
        current = up->second.parent;
    }
    return false;
}

std::size_t TagRegistry::Size() const
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    return entries_.size();
}

std::vector<TagId> TagRegistry::All() const
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    std::vector<TagId> out;
    out.reserve(entries_.size());
    for (const auto& [id, entry] : entries_)
    {
        out.push_back(id);
    }
    std::sort(out.begin(), out.end(), [this](TagId a, TagId b) { return entries_.at(a).name < entries_.at(b).name; });
    return out;
}

std::vector<TagId> TagRegistry::Roots() const
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    std::vector<TagId> out;
    for (const auto& [id, entry] : entries_)
    {
        if (entry.parent == kInvalidTag)
        {
            out.push_back(id);
        }
    }
    std::sort(out.begin(), out.end(), [this](TagId a, TagId b) { return entries_.at(a).name < entries_.at(b).name; });
    return out;
}

std::string TagRegistry::Comment(TagId id) const
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = entries_.find(id);
    return it == entries_.end() ? std::string() : it->second.comment;
}

std::string TagRegistry::Color(TagId id) const
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = entries_.find(id);
    return it == entries_.end() ? std::string() : it->second.color;
}

void TagRegistry::SetMeta(TagId id, const std::string& comment, const std::string& color)
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = entries_.find(id);
    if (it == entries_.end())
    {
        return;
    }
    it->second.comment = comment;
    it->second.color = color;
}

bool TagRegistry::LoadFromJson(const nlohmann::json& j, bool replace)
{
    const nlohmann::json* list = nullptr;
    if (j.is_array())
    {
        list = &j;
    }
    else if (j.is_object() && j.contains("tags"))
    {
        list = &j["tags"];
    }

    if (list == nullptr || !list->is_array())
    {
        BOY_ERROR("Tags", "expected an array of tags, or an object with a \"tags\" array");
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(mtx_);
    if (replace)
    {
        entries_.clear();
        byName_.clear();
    }

    int loaded = 0;
    for (const nlohmann::json& item : *list)
    {
        std::string name;
        std::string comment;
        std::string color;

        if (item.is_string())
        {
            name = item.get<std::string>();
        }
        else if (item.is_object() && item.contains("name") && item["name"].is_string())
        {
            name = item["name"].get<std::string>();
            if (item.contains("comment") && item["comment"].is_string())
            {
                comment = item["comment"].get<std::string>();
            }
            if (item.contains("color") && item["color"].is_string())
            {
                color = item["color"].get<std::string>();
            }
        }
        else
        {
            BOY_WARN("Tags", "skipping entry that is neither a name nor an object with a name");
            continue;
        }

        name = Trim(name);
        if (!IsValidName(name))
        {
            BOY_WARN("Tags", "skipping invalid tag name: \"" + name + "\"");
            continue;
        }

        const TagId id = RegisterInternal(name);
        auto it = entries_.find(id);
        // The name check guards the (astronomically unlikely) collision case: the
        // meta must never land on a different tag that happens to share the id.
        if (it != entries_.end() && it->second.name == name)
        {
            if (!comment.empty())
            {
                it->second.comment = comment;
            }
            if (!color.empty())
            {
                it->second.color = color;
            }
        }
        ++loaded;
    }

    BOY_INFO("Tags",
             "loaded " + std::to_string(loaded) + " entries, " + std::to_string(entries_.size()) + " tags registered");
    return true;
}

nlohmann::json TagRegistry::ToJson() const
{
    nlohmann::json out;
    out["version"] = 1;
    out["tags"] = nlohmann::json::array();

    std::lock_guard<std::recursive_mutex> lock(mtx_);
    std::vector<TagId> ids;
    ids.reserve(entries_.size());
    for (const auto& [id, entry] : entries_)
    {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end(), [this](TagId a, TagId b) { return entries_.at(a).name < entries_.at(b).name; });

    for (TagId id : ids)
    {
        const Entry& entry = entries_.at(id);
        nlohmann::json item;
        item["name"] = entry.name;
        if (!entry.comment.empty())
        {
            item["comment"] = entry.comment;
        }
        if (!entry.color.empty())
        {
            item["color"] = entry.color;
        }
        out["tags"].push_back(std::move(item));
    }
    return out;
}

bool TagRegistry::SaveToFile(const std::string& path) const
{
    Fem::Result<void> written = Vanity::WriteJson(path, ToJson());
    if (!written)
    {
        BOY_ERROR("Tags", "could not write " + path + ": " + written.Error());
        return false;
    }
    return true;
}

bool TagRegistry::LoadFromFile(const std::string& path)
{
    Fem::Result<nlohmann::json> parsed = Vanity::ReadJson(path);
    if (!parsed)
    {
        BOY_WARN("Tags", "could not read " + path + ": " + parsed.Error());
        return false;
    }
    return LoadFromJson(parsed.Value());
}

void TagRegistry::Clear()
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    entries_.clear();
    byName_.clear();
    aliases_.clear();
}

bool TagRegistry::RenameTag(const std::string& from, const std::string& to)
{
    if (!IsValidName(from) || !IsValidName(to))
    {
        BOY_ERROR("Tags", "rename refused: \"" + from + "\" -> \"" + to + "\" is not a valid pair of names");
        return false;
    }
    if (from == to)
    {
        return true;
    }

    std::lock_guard<std::recursive_mutex> lock(mtx_);

    auto source = byName_.find(from);
    if (source == byName_.end())
    {
        BOY_WARN("Tags", "rename from unknown tag \"" + from + "\"");
        return false;
    }

    const TagId oldId = source->second;
    const TagId newId = RegisterInternal(to);
    if (newId == kInvalidTag || newId == oldId)
    {
        return false;
    }

    // Move the cosmetics across; the old entry goes away so isValid(oldId) turns
    // false and nothing can keep answering with the retired id.
    auto oldEntry = entries_.find(oldId);
    if (oldEntry != entries_.end())
    {
        auto newEntry = entries_.find(newId);
        if (newEntry != entries_.end())
        {
            if (newEntry->second.comment.empty())
            {
                newEntry->second.comment = oldEntry->second.comment;
            }
            if (newEntry->second.color.empty())
            {
                newEntry->second.color = oldEntry->second.color;
            }
        }
        entries_.erase(oldEntry);
    }
    byName_.erase(from);

    // Chain the alias in case the old name was itself an alias target.
    aliases_[from] = to;
    for (auto& [alias, target] : aliases_)
    {
        if (target == from)
        {
            target = to;
        }
    }

    BOY_INFO("Tags", "renamed \"" + from + "\" -> \"" + to + "\"");
    return true;
}

std::vector<std::pair<std::string, std::string>> TagRegistry::Aliases() const
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(aliases_.size());
    for (const auto& [from, to] : aliases_)
    {
        out.emplace_back(from, to);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::uint64_t TagRegistry::HintBits(TagId id) const
{
    if (id == kInvalidTag)
    {
        return 0;
    }

    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = entries_.find(id);
    if (it == entries_.end())
    {
        return 0;
    }

    std::uint64_t bits = 0;
    TagId current = id;
    // A tag and every ancestor: this is what makes "matches" answerable with a
    // subset test on one machine word.
    while (current != kInvalidTag)
    {
        bits |= (std::uint64_t{1} << (current & 63u));
        auto up = entries_.find(current);
        if (up == entries_.end())
        {
            break;
        }
        current = up->second.parent;
    }
    return bits;
}

bool TagRegistry::WatchFile(const std::string& path)
{
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    if (ec)
    {
        BOY_WARN("Tags", "cannot watch " + path + ": " + ec.message());
        return false;
    }

    bool loaded = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mtx_);
        watchedPath_ = path;
        watchedStamp_ = static_cast<std::int64_t>(stamp.time_since_epoch().count());
        watching_ = true;
    }

    // Read once now, so the caller does not have to remember to do it, and so the
    // stamp recorded above matches content that is already in the table.
    loaded = LoadFromFile(path);
    return loaded;
}

void TagRegistry::UnwatchFile()
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    watchedPath_.clear();
    watchedStamp_ = 0;
    watching_ = false;
}

bool TagRegistry::PollReload()
{
    std::string path;
    std::int64_t known = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(mtx_);
        if (!watching_)
        {
            return false;
        }
        path = watchedPath_;
        known = watchedStamp_;
    }

    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    if (ec)
    {
        return false;
    }

    const std::int64_t value = static_cast<std::int64_t>(stamp.time_since_epoch().count());
    if (value == known)
    {
        return false;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(mtx_);
        watchedStamp_ = value;
    }

    // Adding, never replacing: a tag that a running object already holds keeps
    // its id, and a designer deleting a line does not break the session.
    return LoadFromFile(path);
}

// TagContainer

TagContainer::TagContainer(std::initializer_list<TagId> ids)
{
    for (TagId id : ids)
    {
        Add(id);
    }
}

void TagContainer::Add(TagId id)
{
    if (id == kInvalidTag)
    {
        return;
    }
    // Insert in sorted position: keeps the vector unique and makes every query a
    // binary search or a linear merge.
    auto it = std::lower_bound(ids_.begin(), ids_.end(), id);
    if (it != ids_.end() && *it == id)
    {
        return;
    }
    ids_.insert(it, id);
    hint_ |= TagRegistry::Instance().HintBits(id);
}

bool TagContainer::Add(const std::string& name)
{
    const TagId id = TagRegistry::Instance().Find(name);
    if (id == kInvalidTag)
    {
        // Not invented here: a typo should be visible, not silently absorbed.
        BOY_WARN("Tags", "ignoring unregistered tag \"" + name + "\"");
        return false;
    }
    Add(id);
    return true;
}

bool TagContainer::Remove(TagId id)
{
    auto it = std::lower_bound(ids_.begin(), ids_.end(), id);
    if (it == ids_.end() || *it != id)
    {
        return false;
    }
    ids_.erase(it);

    // A bloom bit cannot be cleared safely, so rebuild. Removals are far rarer
    // than queries and the set is tiny.
    hint_ = 0;
    const TagRegistry& registry = TagRegistry::Instance();
    for (TagId own : ids_)
    {
        hint_ |= registry.HintBits(own);
    }
    return true;
}

void TagContainer::Clear()
{
    ids_.clear();
    hint_ = 0;
}

bool TagContainer::Has(TagId id) const
{
    return std::binary_search(ids_.begin(), ids_.end(), id);
}

bool TagContainer::Matches(TagId id) const
{
    if (id == kInvalidTag)
    {
        return false;
    }

    const TagRegistry& registry = TagRegistry::Instance();
    // One AND rejects the common case: the container holds nothing related.
    if ((hint_ & registry.HintBits(id)) == 0)
    {
        return false;
    }

    for (TagId own : ids_)
    {
        if (own == id || registry.IsDescendantOf(own, id))
        {
            return true;
        }
    }
    return false;
}

bool TagContainer::HasAny(const TagContainer& other) const
{
    for (TagId id : other.ids_)
    {
        if (Has(id))
        {
            return true;
        }
    }
    return false;
}

bool TagContainer::HasAll(const TagContainer& other) const
{
    for (TagId id : other.ids_)
    {
        if (!Has(id))
        {
            return false;
        }
    }
    return true;
}

bool TagContainer::MatchesAny(const TagContainer& other) const
{
    // If not a single required bit is present, no query tag can match.
    if ((hint_ & other.hint_) == 0)
    {
        return false;
    }
    for (TagId id : other.ids_)
    {
        if (Matches(id))
        {
            return true;
        }
    }
    return false;
}

bool TagContainer::MatchesAll(const TagContainer& other) const
{
    if ((hint_ & other.hint_) != other.hint_)
    {
        return false;
    }
    for (TagId id : other.ids_)
    {
        if (!Matches(id))
        {
            return false;
        }
    }
    return true;
}

std::string TagContainer::ToString() const
{
    const TagRegistry& registry = TagRegistry::Instance();
    std::string out;
    for (TagId id : ids_)
    {
        const std::string name = registry.Name(id);
        if (name.empty())
        {
            continue;
        }
        if (!out.empty())
        {
            out += ", ";
        }
        out += name;
    }
    return out;
}

nlohmann::json TagContainer::ToJson() const
{
    const TagRegistry& registry = TagRegistry::Instance();
    std::vector<std::string> names;
    names.reserve(ids_.size());
    for (TagId id : ids_)
    {
        const std::string name = registry.Name(id);
        if (!name.empty())
        {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool TagContainer::FromJson(const nlohmann::json& j)
{
    Clear();

    if (j.is_string())
    {
        return Add(j.get<std::string>());
    }

    if (!j.is_array())
    {
        BOY_WARN("Tags", "expected an array of tag names");
        return false;
    }

    for (const nlohmann::json& item : j)
    {
        if (item.is_string())
        {
            Add(item.get<std::string>());
        }
    }
    return true;
}

TagContainer MakeTags(std::initializer_list<const char*> names)
{
    TagContainer container;
    for (const char* name : names)
    {
        if (name != nullptr)
        {
            container.Add(name);
        }
    }
    return container;
}

// TagQuery

TagQuery::TagQuery(const TagContainer& required, const TagContainer& blocked)
{
    SetRequired(required);
    SetBlocked(blocked);
}

void TagQuery::SetRequired(const TagContainer& required)
{
    required_ = required;
    requiredHint_ = required_.Hint();
}

void TagQuery::SetBlocked(const TagContainer& blocked)
{
    blocked_ = blocked;
    blockedHint_ = blocked_.Hint();
}

bool TagQuery::Matches(const TagContainer& container) const
{
    if ((container.Hint() & requiredHint_) != requiredHint_)
    {
        return false;
    }
    if ((container.Hint() & blockedHint_) != 0 && container.MatchesAny(blocked_))
    {
        return false;
    }
    return container.MatchesAll(required_);
}

// Replication

nlohmann::json TagDelta(const TagContainer& previous, const TagContainer& current)
{
    nlohmann::json delta = nlohmann::json::object();
    nlohmann::json added = nlohmann::json::array();
    nlohmann::json removed = nlohmann::json::array();

    const TagRegistry& registry = TagRegistry::Instance();
    const std::vector<TagId>& before = previous.Ids();
    const std::vector<TagId>& after = current.Ids();

    std::size_t i = 0;
    std::size_t j = 0;
    while (i < before.size() || j < after.size())
    {
        if (j >= after.size() || (i < before.size() && before[i] < after[j]))
        {
            removed.push_back(registry.Name(before[i]));
            ++i;
        }
        else if (i >= before.size() || after[j] < before[i])
        {
            added.push_back(registry.Name(after[j]));
            ++j;
        }
        else
        {
            ++i;
            ++j;
        }
    }

    // An unchanged set costs an empty object, which is what a steady state
    // 20 times a second should cost.
    if (!added.empty())
    {
        delta["add"] = std::move(added);
    }
    if (!removed.empty())
    {
        delta["remove"] = std::move(removed);
    }
    return delta;
}

bool ApplyTagDelta(TagContainer& container, const nlohmann::json& delta, std::vector<std::string>* unknown)
{
    if (!delta.is_object())
    {
        return false;
    }

    const TagRegistry& registry = TagRegistry::Instance();
    bool clean = true;

    const auto walk = [&](const char* key, bool add)
    {
        if (!delta.contains(key) || !delta[key].is_array())
        {
            return;
        }
        for (const nlohmann::json& item : delta[key])
        {
            if (!item.is_string())
            {
                continue;
            }
            const std::string name = item.get<std::string>();
            const TagId id = registry.Find(name);
            if (id == kInvalidTag)
            {
                // The honest failure: the peer named a tag this build does not
                // know. Report it instead of guessing at a nearby id.
                clean = false;
                if (unknown != nullptr)
                {
                    unknown->push_back(name);
                }
                continue;
            }
            if (add)
            {
                container.Add(id);
            }
            else
            {
                container.Remove(id);
            }
        }
    };

    walk("add", true);
    walk("remove", false);
    return clean;
}

// Log filter

void FilterLogByCategory(const TagContainer& allowed)
{
    const TagContainer allowedCopy = allowed;
    Boy::Instance().SetCategoryFilter(
        [allowedCopy](const std::string& category)
        {
            const TagRegistry& registry = TagRegistry::Instance();
            const TagId id = registry.Find(category);
            // A category that is not a tag is engine chatter ("Engine", "Heels");
            // filtering it out silently would hide exactly the messages a broken
            // session needs.
            if (id == kInvalidTag)
            {
                return true;
            }
            for (TagId ancestor : allowedCopy.Ids())
            {
                if (ancestor == id || registry.IsDescendantOf(id, ancestor))
                {
                    return true;
                }
            }
            return false;
        });
}

void ClearLogFilter()
{
    Boy::Instance().ClearCategoryFilter();
}

} // namespace Fem
