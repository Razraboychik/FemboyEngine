// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/Tags.hpp"

namespace Fem
{

// StateTree (UE5-style hierarchical state machine)
// StateTree is a data-driven hierarchical state machine.
// States can have tasks, transitions, and evaluators.
// More scalable than BehaviorTree for complex AI.

enum class STStatus
{
    Succeeded,
    Failed,
    Running
};

// Forward declarations
struct STState;
struct STTransition;

// Task: executed while in a state
struct STTask
{
    std::string name;
    std::function<STStatus(float, void* context)> tick;
    std::function<void(void* context)> enter;
    std::function<void(void* context)> exit;
};

// Evaluator: shared logic evaluated for all states
struct STEvaluator
{
    std::string name;
    std::function<void(float, void* context)> tick;
};

// Transition: condition to move to another state
struct STTransition
{
    std::string targetState;
    std::function<bool(void* context)> condition;
    // When set, the transition only fires on sendEvent(name). A condition may
    // accompany the event, so "spotted" alone is not enough if the target is
    // out of range.
    std::string event;
    int priority = 0; // higher priority wins
};

// State: a node in the state tree
struct STState
{
    std::string name;
    std::vector<STTask> tasks;
    std::vector<STTransition> transitions;
    std::vector<std::shared_ptr<STState>> children;
    std::shared_ptr<STState> parent;
    bool isLeaf = true;

    // lifecycle
    void OnEnter(void* ctx)
    {
        for (auto& t : tasks)
        {
            if (t.enter)
            {
                t.enter(ctx);
            }
        }
    }
    void OnExit(void* ctx)
    {
        // Exit in reverse order, mirroring construction order.
        for (auto it = tasks.rbegin(); it != tasks.rend(); ++it)
        {
            if (it->exit)
            {
                it->exit(ctx);
            }
        }
    }
    STStatus OnTick(float dt, void* ctx)
    {
        STStatus status = STStatus::Succeeded;
        for (auto& t : tasks)
        {
            const STStatus s = t.tick ? t.tick(dt, ctx) : STStatus::Succeeded;
            if (s == STStatus::Running)
            {
                status = STStatus::Running;
            }
            if (s == STStatus::Failed)
            {
                status = STStatus::Failed;
                break;
            }
        }
        return status;
    }
};

class StateTree
{
public:
    StateTree();

    // The context pointer is stored and passed to every lifecycle callback, so
    // tasks do not receive nullptr after a transition triggered by forceState().
    void SetRoot(std::shared_ptr<STState> root, void* context = nullptr);
    void Tick(float dt, void* context = nullptr);
    void ForceState(const std::string& stateName);

    // Data-driven trees
    // Builds a tree from JSON, so an AI can be authored without C++:
    //
    //   {
    //     "root": "Idle",
    //     "states": [
    //       { "name": "Idle",  "actions": ["lookAround"],
    //         "transitions": [{ "to": "Chase", "event": "spotted", "priority": 2 }] },
    //       { "name": "Chase", "actions": ["pursue"],
    //         "transitions": [{ "to": "Idle", "condition": "lostTarget" }] }
    //     ]
    //   }
    //
    // A state list may be flat (with "parent") or nested ("children"). Actions
    // and conditions refer to names registered with registerAction() and
    // registerCondition(); unknown names are reported and ignored, so a tree
    // still loads while its behaviour is being written.
    bool LoadFromJson(const nlohmann::json& j);
    void RegisterAction(const std::string& name, std::function<STStatus(float, void*)> fn);
    void RegisterCondition(const std::string& name, std::function<bool(void*)> fn);
    // Queues an event for the next tick. Events are consumed once, and all
    // unconsumed events are dropped at the end of the tick.
    void SendEvent(const std::string& name);

    std::string CurrentState() const
    {
        return currentState_ ? currentState_->name : "";
    }
    std::shared_ptr<STState> FindState(const std::string& name);
    std::shared_ptr<STState> Root() const
    {
        return root_;
    }
    void* Context() const
    {
        return context_;
    }
    void Reset();

    // Evaluators (shared logic)
    void AddEvaluator(const STEvaluator& e)
    {
        evaluators_.push_back(e);
    }
    void ClearEvaluators()
    {
        evaluators_.clear();
    }
    const std::vector<STEvaluator>& Evaluators() const
    {
        return evaluators_;
    }

    // Debug
    std::vector<std::string> StateHistory() const
    {
        return stateHistory_;
    }

private:
    std::shared_ptr<STState> root_;
    std::shared_ptr<STState> currentState_;
    std::vector<STEvaluator> evaluators_;
    std::vector<std::string> stateHistory_;
    std::map<std::string, std::function<STStatus(float, void*)>> actions_;
    std::map<std::string, std::function<bool(void*)>> conditions_;
    std::map<std::string, std::shared_ptr<STState>> index_; // every state, flat or nested
    std::vector<std::string> events_;
    void* context_ = nullptr;
    void TransitionTo(std::shared_ptr<STState> target, void* ctx, bool allowSelf);
    void EnterHierarchy(std::shared_ptr<STState> state, void* ctx);
    std::shared_ptr<STState> FindStateIn(std::shared_ptr<STState> from, const std::string& name) const;
    bool HasEvent(const std::string& name) const;
    void ConsumeEvent(const std::string& name);
};

// Transition condition backed by a tag. Replaces the old BTHasTag: a state
// tree asks the same question a behaviour tree leaf did, without a second
// node hierarchy. Either bind a container directly or a getter when the actor
// (and its container) can be replaced at runtime.
inline std::function<bool(void*)> STHasTag(TagId tag, const TagContainer* container)
{
    return [tag, container](void*)
    {
        if (container == nullptr || tag == kInvalidTag)
        {
            return false;
        }
        return container->Matches(tag);
    };
}

inline std::function<bool(void*)> STHasTag(TagId tag, std::function<const TagContainer*()> provider)
{
    return [tag, provider = std::move(provider)](void*)
    {
        const TagContainer* container = provider ? provider() : nullptr;
        if (container == nullptr || tag == kInvalidTag)
        {
            return false;
        }
        return container->Matches(tag);
    };
}

} // namespace Fem
