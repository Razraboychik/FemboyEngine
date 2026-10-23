// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <memory>
#include <functional>
#include <vector>
#include <utility>

// UE5-style unified property/function macros
// Single macro FEMGUARD_PROPERTY() with specifiers, like UE5's UPROPERTY().
//
// In C++, the preprocessor cannot do conditional dispatch on string values.
// FemboyEngine follows UE5's philosophy: FEMGUARD_PROPERTY() is a marker (no-op)
// that prefixes declarations. The actual memory management is done through
// C++ language features (RAII, smart pointers, scope guards).
//
// For automatic cleanup, combine FEMGUARD_PROPERTY() with helper functions:
//
//   FEMGUARD_PROPERTY(Guard)
//   auto _g = ::Fem::MakeScopeGuard([&](){ cleanup_code; });
//
//   FEMGUARD_PROPERTY(AutoPtr)
//   std::unique_ptr<Type> name(new Type(args));
//
//   FEMGUARD_PROPERTY(Shared)
//   auto name = std::make_shared<Type>(args);
//
//   FEMGUARD_PROPERTY(Arena)
//   auto _g = ::Fem::MakeScopeGuard([&](){ arena.reset(); });
//
// The macro marks the field for documentation/tooling (like UPROPERTY in UE5).
// The actual cleanup is guaranteed by C++ RAII (no GC, no periodic scans).

#define FEMGUARD_PROPERTY(...) /* FEMGUARD_PROPERTY marker (UE5-style memory guard) */

// UE5-style function macro

#define FEMGUARD_FUNCTION(...) /* FEMGUARD_FUNCTION marker (UE5-style memory guard) */

// Helper functions for memory management

namespace Fem
{

struct ScopeGuard
{
    std::function<void()> fn;
    bool active = true;

    ScopeGuard() = default;
    explicit ScopeGuard(std::function<void()> f) : fn(std::move(f))
    {
    }
    ~ScopeGuard()
    {
        if (active && fn)
        {
            fn();
        }
    }

    // Copying a guard would run its cleanup twice; only one owner may fire.
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    ScopeGuard(ScopeGuard&& other) noexcept : fn(std::move(other.fn)), active(other.active)
    {
        other.active = false;
    }

    ScopeGuard& operator=(ScopeGuard&& other) noexcept
    {
        if (this != &other)
        {
            if (active && fn)
            {
                fn();
            }
            fn = std::move(other.fn);
            active = other.active;
            other.active = false;
        }
        return *this;
    }

    void Dismiss()
    {
        active = false;
    }
};

inline ScopeGuard MakeScopeGuard(std::function<void()> fn)
{
    return ScopeGuard(std::move(fn));
}

// AutoPtr helper
template <typename T> using AutoPtr = std::unique_ptr<T>;

// Shared helper
template <typename T> using Shared = std::shared_ptr<T>;

// Weak helper
template <typename T> using Weak = std::weak_ptr<T>;

} // namespace Fem
