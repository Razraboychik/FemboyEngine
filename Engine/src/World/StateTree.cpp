// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/StateTree.hpp"
#include "FemboyEngine/Boy.hpp"
#include <algorithm>

namespace Fem
{

namespace
{
constexpr int kMaxTransitionsPerTick = 32;
}

StateTree::StateTree() = default;

void StateTree::SetRoot(std::shared_ptr<STState> root, void* context)
{
    if (currentState_)
    {
        currentState_->OnExit(context_);
    }
    root_ = std::move(root);
    currentState_ = root_;
    context_ = context;
    stateHistory_.clear();
    index_.clear();
    if (currentState_)
    {
        stateHistory_.push_back(currentState_->name);
        // Register the whole hierarchy so findState() works for nested trees.
        std::function<void(const std::shared_ptr<STState>&)> collect = [&](const std::shared_ptr<STState>& s)
        {
            if (!s)
            {
                return;
            }
            index_[s->name] = s;
            for (const auto& c : s->children)
            {
                collect(c);
            }
        };
        collect(currentState_);
        currentState_->OnEnter(context_);
    }
}

void StateTree::Reset()
{
    if (currentState_)
    {
        currentState_->OnExit(context_);
    }
    currentState_ = root_;
    stateHistory_.clear();
    if (currentState_)
    {
        stateHistory_.push_back(currentState_->name);
        currentState_->OnEnter(context_);
    }
}

std::shared_ptr<STState> StateTree::FindStateIn(std::shared_ptr<STState> from, const std::string& name) const
{
    if (!from)
    {
        return nullptr;
    }
    if (from->name == name)
    {
        return from;
    }
    for (const auto& c : from->children)
    {
        if (auto found = FindStateIn(c, name))
        {
            return found;
        }
    }
    return nullptr;
}

std::shared_ptr<STState> StateTree::FindState(const std::string& name)
{
    auto it = index_.find(name);
    if (it != index_.end())
    {
        return it->second;
    }
    return FindStateIn(root_, name);
}

void StateTree::TransitionTo(std::shared_ptr<STState> target, void* ctx, bool allowSelf)
{
    if (!target)
    {
        return;
    }
    if (target == currentState_ && !allowSelf)
    {
        return;
    }

    if (currentState_)
    {
        currentState_->OnExit(ctx);
    }
    currentState_ = std::move(target);
    stateHistory_.push_back(currentState_->name);
    if (stateHistory_.size() > 64)
    {
        stateHistory_.erase(stateHistory_.begin());
    }
    currentState_->OnEnter(ctx);
    BOY_INFO("StateTree", "transitioned to: " + currentState_->name);
}

// Enters a state and, if it has children, descends into its first child. This
// makes hierarchical states behave like composite nodes.
void StateTree::EnterHierarchy(std::shared_ptr<STState> state, void* ctx)
{
    if (!state)
    {
        return;
    }

    int depth = 0;
    while (state && !state->children.empty() && depth < 32)
    {
        auto child = state->children.front();
        if (!child)
        {
            break;
        }
        TransitionTo(child, ctx, false);
        state = child;
        ++depth;
    }
}

void StateTree::Tick(float dt, void* context)
{
    if (context != nullptr)
    {
        context_ = context;
    }
    if (!currentState_)
    {
        return;
    }

    // Run evaluators first so transition conditions see fresh shared data.
    for (auto& e : evaluators_)
    {
        if (e.tick)
        {
            e.tick(dt, context_);
        }
    }

    // Evaluate transitions from the current state. Higher priority wins, and a
    // transition may point at the current state itself (a re-entry).
    int steps = 0;
    bool transitioned = true;
    while (transitioned && steps++ < kMaxTransitionsPerTick)
    {
        transitioned = false;

        std::vector<STTransition> ordered = currentState_->transitions;
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const STTransition& a, const STTransition& b) { return a.priority > b.priority; });

        for (const auto& t : ordered)
        {
            if (t.condition && !t.condition(context_))
            {
                continue;
            }
            if (!t.event.empty() && !HasEvent(t.event))
            {
                continue;
            }

            auto target = FindState(t.targetState);
            if (!target)
            {
                BOY_WARN("StateTree", "transition targets unknown state: " + t.targetState);
                continue;
            }

            if (!t.event.empty())
            {
                ConsumeEvent(t.event);
            }

            // A self-transition re-enters the state (exit + enter) exactly once
            // per tick. Without the break, an always-true condition would burn
            // the whole transition budget re-entering the same state every frame.
            const bool self = (target == currentState_);
            TransitionTo(target, context_, self);
            transitioned = !self;
            break;
        }
    }

    if (!currentState_)
    {
        return;
    }

    // Tick the current state; if it succeeds and is a composite, descend.
    const STStatus s = currentState_->OnTick(dt, context_);
    if (s == STStatus::Succeeded && !currentState_->children.empty())
    {
        EnterHierarchy(currentState_, context_);
    }

    // Events are one-shot: an unconsumed "spotted" must not fire next frame.
    events_.clear();
}

void StateTree::ForceState(const std::string& stateName)
{
    auto target = FindState(stateName);
    if (!target)
    {
        BOY_WARN("StateTree", "forceState: unknown state: " + stateName);
        return;
    }
    TransitionTo(target, context_, false);
}

void StateTree::RegisterAction(const std::string& name, std::function<STStatus(float, void*)> fn)
{
    actions_[name] = std::move(fn);
}

void StateTree::RegisterCondition(const std::string& name, std::function<bool(void*)> fn)
{
    conditions_[name] = std::move(fn);
}

void StateTree::SendEvent(const std::string& name)
{
    if (!name.empty())
    {
        events_.push_back(name);
    }
}

bool StateTree::HasEvent(const std::string& name) const
{
    return std::find(events_.begin(), events_.end(), name) != events_.end();
}

void StateTree::ConsumeEvent(const std::string& name)
{
    auto it = std::find(events_.begin(), events_.end(), name);
    if (it != events_.end())
    {
        events_.erase(it);
    }
}

bool StateTree::LoadFromJson(const nlohmann::json& j)
{
    if (!j.is_object() || !j.contains("states") || !j["states"].is_array())
    {
        BOY_WARN("StateTree", "loadFromJson: expected an object with a 'states' array");
        return false;
    }

    std::map<std::string, std::shared_ptr<STState>> byName;
    bool ok = true;

    std::function<std::shared_ptr<STState>(const nlohmann::json&, const std::shared_ptr<STState>&)> build =
        [&](const nlohmann::json& sj, const std::shared_ptr<STState>& parent) -> std::shared_ptr<STState>
    {
        if (!sj.is_object())
        {
            return nullptr;
        }
        const std::string name = sj.value("name", std::string{});
        if (name.empty())
        {
            BOY_WARN("StateTree", "loadFromJson: state without a name");
            ok = false;
            return nullptr;
        }
        if (byName.count(name) != 0)
        {
            BOY_WARN("StateTree", "loadFromJson: duplicate state: " + name);
            ok = false;
            return nullptr;
        }

        auto state = std::make_shared<STState>();
        state->name = name;
        state->parent = parent;
        byName[name] = state;

        if (sj.contains("actions") && sj["actions"].is_array())
        {
            for (const auto& a : sj["actions"])
            {
                if (!a.is_string())
                {
                    continue;
                }
                const std::string actionName = a.get<std::string>();
                auto it = actions_.find(actionName);
                if (it == actions_.end())
                {
                    BOY_WARN("StateTree", "loadFromJson: unregistered action: " + actionName);
                    continue;
                }
                STTask task;
                task.name = actionName;
                task.tick = it->second;
                state->tasks.push_back(std::move(task));
            }
        }

        if (sj.contains("transitions") && sj["transitions"].is_array())
        {
            for (const auto& t : sj["transitions"])
            {
                if (!t.is_object() || !t.contains("to"))
                {
                    continue;
                }
                STTransition tr;
                tr.targetState = t.value("to", std::string{});
                tr.event = t.value("event", std::string{});
                tr.priority = t.value("priority", 0);
                const std::string cond = t.value("condition", std::string{});
                if (!cond.empty())
                {
                    auto it = conditions_.find(cond);
                    if (it == conditions_.end())
                    {
                        BOY_WARN("StateTree", "loadFromJson: unregistered condition: " + cond);
                    }
                    else
                    {
                        tr.condition = it->second;
                    }
                }
                state->transitions.push_back(std::move(tr));
            }
        }

        if (sj.contains("children") && sj["children"].is_array())
        {
            for (const auto& child : sj["children"])
            {
                auto built = build(child, state);
                if (built)
                {
                    state->children.push_back(std::move(built));
                }
            }
            state->isLeaf = state->children.empty();
        }
        return state;
    };

    std::vector<std::shared_ptr<STState>> top;
    for (const auto& sj : j["states"])
    {
        auto s = build(sj, nullptr);
        if (s)
        {
            top.push_back(std::move(s));
        }
    }
    if (!ok || top.empty())
    {
        return false;
    }

    // Flat lists attach children through "parent"; the link is applied after
    // every state exists, so order in the file does not matter.
    for (const auto& sj : j["states"])
    {
        if (!sj.is_object() || !sj.contains("parent") || !sj["parent"].is_string())
        {
            continue;
        }
        const std::string childName = sj.value("name", std::string{});
        const std::string parentName = sj["parent"].get<std::string>();
        auto childIt = byName.find(childName);
        auto parentIt = byName.find(parentName);
        if (childIt == byName.end() || parentIt == byName.end())
        {
            BOY_WARN("StateTree", "loadFromJson: bad parent link: " + childName + " -> " + parentName);
            continue;
        }
        childIt->second->parent = parentIt->second;
        parentIt->second->children.push_back(childIt->second);
        parentIt->second->isLeaf = false;
    }

    const std::string rootName = j.value("root", std::string{});
    std::shared_ptr<STState> root;
    if (!rootName.empty())
    {
        auto it = byName.find(rootName);
        if (it == byName.end())
        {
            BOY_WARN("StateTree", "loadFromJson: unknown root: " + rootName);
            return false;
        }
        root = it->second;
    }
    else
    {
        // First state without a parent.
        for (const auto& s : top)
        {
            if (!s->parent)
            {
                root = s;
                break;
            }
        }
    }
    if (!root)
    {
        BOY_WARN("StateTree", "loadFromJson: no root state");
        return false;
    }

    // Validate transition targets before the tree goes live.
    for (auto& [name, state] : byName)
    {
        for (const auto& t : state->transitions)
        {
            if (byName.count(t.targetState) == 0)
            {
                BOY_WARN("StateTree",
                         "loadFromJson: unknown transition target: " + t.targetState + " (from " + name + ")");
            }
        }
    }

    SetRoot(root, context_);
    // Flat lists are valid files: states that are not children of the root can
    // still be transition targets, so index every state by name.
    for (const auto& [name, state] : byName)
    {
        index_[name] = state;
    }
    return true;
}

} // namespace Fem
