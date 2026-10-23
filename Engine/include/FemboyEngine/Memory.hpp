// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <vector>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace Fem
{

// Linear arena allocator for fast, cache-friendly allocations.
//
// Objects created with create<T>() have their destructors recorded and are
// destroyed in reverse order by reset()/the destructor, so the arena is safe
// for non-trivially-destructible types as well.
class Arena
{
public:
    explicit Arena(size_t capacity = 1024 * 1024);
    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&& other) noexcept;
    Arena& operator=(Arena&& other) noexcept;

    void* Allocate(size_t size, size_t align = alignof(std::max_align_t));
    void Reset();

    size_t Used() const
    {
        return used_;
    }
    size_t Capacity() const
    {
        return capacity_;
    }

    template <typename T, typename... Args> T* Create(Args&&... args)
    {
        void* p = Allocate(sizeof(T), alignof(T));
        if (!p)
        {
            return nullptr;
        }
        // Construct first, then register the destructor. If the constructor
        // throws, the arena must not hold a destructor entry for an object that
        // was never built.
        T* obj = new (p) T(std::forward<Args>(args)...);
        dtors_.push_back({reinterpret_cast<char*>(obj), [](void* q) { static_cast<T*>(q)->~T(); }});
        return obj;
    }

private:
    struct DestructorEntry
    {
        char* ptr;
        void (*fn)(void*);
    };

    void DestroyObjects();

    char* base_ = nullptr;
    size_t capacity_ = 0;
    size_t used_ = 0;
    std::vector<DestructorEntry> dtors_;
};

// Generational slot storage for UI widgets, dialogue nodes and quest objects.
//
// The important difference from a raw pointer (or from the "GcRef" pattern) is
// that a Handle is validated against the slot table before the object is
// touched. A stale Handle therefore returns nullptr safely: the memory may have
// been reused, but its generation no longer matches. Destroy() only marks the
// slot; Collect() performs the actual destructor calls, optionally with a
// budget, so a mass close of a screen cannot turn into a multi-millisecond
// frame spike.
//
// This is deliberately *not* a tracing garbage collector. Ownership stays
// explicit: a screen/quest owns the handles it created and destroys them when
// it closes. Cycles are therefore harmless (there is no refcount to leak) while
// the lifetime remains deterministic and rollback-safe.
template <typename T> class SlotMap
{
public:
    struct Handle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        bool Valid() const
        {
            return generation != 0;
        }
        explicit operator bool() const
        {
            return Valid();
        }
        bool operator==(const Handle& other) const
        {
            return index == other.index && generation == other.generation;
        }
        bool operator!=(const Handle& other) const
        {
            return !(*this == other);
        }
    };

    struct HandleHash
    {
        std::size_t operator()(const Handle& h) const
        {
            // Mix both words; index alone would collide across generations.
            const std::uint64_t v = (static_cast<std::uint64_t>(h.index) << 32) | h.generation;
            return static_cast<std::size_t>(v ^ (v >> 33));
        }
    };

    template <typename... Args> Handle Create(Args&&... args)
    {
        std::uint32_t index = 0;
        if (!free_.empty())
        {
            index = free_.back();
            free_.pop_back();
        }
        else
        {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        }

        Slot& slot = slots_[index];
        slot.object = std::make_unique<T>(std::forward<Args>(args)...);
        if (slot.generation == 0)
        {
            slot.generation = 1;
        }
        slot.pending = false;
        ++alive_;
        return Handle{index, slot.generation};
    }

    T* Get(const Handle& handle)
    {
        return const_cast<T*>(static_cast<const SlotMap*>(this)->Get(handle));
    }

    const T* Get(const Handle& handle) const
    {
        if (handle.generation == 0 || handle.index >= slots_.size())
        {
            return nullptr;
        }
        const Slot& slot = slots_[handle.index];
        if (!slot.object || slot.pending || slot.generation != handle.generation)
        {
            return nullptr;
        }
        return slot.object.get();
    }

    bool Alive(const Handle& handle) const
    {
        return Get(handle) != nullptr;
    }

    // Marks the object dead. The memory is not released until Collect(), so
    // every Handle reports null from this point on without a use-after-free.
    bool Destroy(const Handle& handle)
    {
        if (handle.generation == 0 || handle.index >= slots_.size())
        {
            return false;
        }
        Slot& slot = slots_[handle.index];
        if (!slot.object || slot.pending || slot.generation != handle.generation)
        {
            return false;
        }
        slot.pending = true;
        --alive_;
        disposal_.push_back(handle.index);
        return true;
    }

    // Performs at most `budget` destructor calls. Returns how many slots were
    // released. A budget of 0 means "all pending"; callers can use a small
    // per-frame budget to amortise a large teardown.
    std::size_t Collect(std::size_t budget = 0)
    {
        const std::size_t limit = budget == 0 ? (std::numeric_limits<std::size_t>::max)() : budget;
        std::size_t released = 0;
        while (!disposal_.empty() && released < limit)
        {
            const std::uint32_t index = disposal_.front();
            disposal_.pop_front();
            if (index >= slots_.size())
            {
                continue;
            }

            Slot& slot = slots_[index];
            if (!slot.object || !slot.pending)
            {
                continue;
            }

            slot.object.reset();
            slot.pending = false;
            ++slot.generation;
            if (slot.generation == 0)
            {
                slot.generation = 1;
            }
            free_.push_back(index);
            ++released;
        }
        return released;
    }

    std::size_t Size() const
    {
        return alive_;
    }
    std::size_t Pending() const
    {
        return disposal_.size();
    }
    std::size_t Capacity() const
    {
        return slots_.size();
    }
    bool Empty() const
    {
        return alive_ == 0;
    }

    // Releases everything immediately. Used when a screen or level is torn
    // down: there is no reason to keep dead objects across that boundary.
    void Clear()
    {
        slots_.clear();
        free_.clear();
        disposal_.clear();
        alive_ = 0;
    }

private:
    struct Slot
    {
        std::unique_ptr<T> object;
        std::uint32_t generation = 1;
        bool pending = false;
    };

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
    std::deque<std::uint32_t> disposal_;
    std::size_t alive_ = 0;
};

} // namespace Fem
