// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

// Small, prefixed compiler/introspection macros.
//
// The engine deliberately keeps this list short. Unreal-style macro soup makes
// code harder to read, and most "optimisation macros" do not change generated
// code on a modern compiler. What is here is either portable vocabulary for
// intent (inline/likely/restrict) or a debugging aid (ensure/verify).
//
// All macros are prefixed `FB_` so they cannot collide with windows.h's min/max
// or with a game's own macros.
//
// Note on assembly: MSVC x64 rejects inline `__asm`, so the hot numerical paths
// use intrinsics in Simd.hpp instead. FB_ASSUME/FB_RESTRICT are the portable
// levers that let the optimiser keep those sequences tight.

#include <cassert>
#include <cstddef>

#include "FemboyEngine/Boy.hpp"

#if defined(_MSC_VER)
#define FB_FORCEINLINE __forceinline
#define FB_NOINLINE __declspec(noinline)
#define FB_RESTRICT __restrict
#define FB_ASSUME(expr) __assume(expr)
#define FB_UNREACHABLE() __assume(0)
#define FB_DEBUGBREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#define FB_FORCEINLINE inline __attribute__((always_inline))
#define FB_NOINLINE __attribute__((noinline))
#define FB_RESTRICT __restrict__
#define FB_ASSUME(expr) ((void)0)
#define FB_UNREACHABLE() __builtin_unreachable()
#define FB_DEBUGBREAK() __builtin_trap()
#else
#define FB_FORCEINLINE inline
#define FB_NOINLINE
#define FB_RESTRICT
#define FB_ASSUME(expr) ((void)0)
#define FB_UNREACHABLE() ((void)0)
#define FB_DEBUGBREAK() ((void)0)
#endif

// C++20 branch hints. They help layout of cold paths (error handling) and do
// nothing measurable inside a branch-free hot loop, which is exactly why they
// are used sparingly.
#if defined(__cpp_lib_likely) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L) || __cplusplus >= 202002L
#define FB_LIKELY [[likely]]
#define FB_UNLIKELY [[unlikely]]
#else
#define FB_LIKELY
#define FB_UNLIKELY
#endif

#define FB_STRINGIFY_IMPL(x) #x
#define FB_STRINGIFY(x) FB_STRINGIFY_IMPL(x)
#define FB_CONCAT_IMPL(a, b) a##b
#define FB_CONCAT(a, b) FB_CONCAT_IMPL(a, b)
#define FB_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define FB_UNUSED(x) ((void)(x))

// Convenience wrapper around static_cast so game code reads as intent rather than
// C++ cast syntax. It supports exactly what static_cast supports and nothing
// more: no reinterpret_cast, no const_cast, no dynamic_cast.
#define FEMTO(Type) static_cast<Type>
// invariant once and keeps running. In release builds the condition is still
// evaluated by FB_VERIFY (for side effects) but only FB_ENSURE is compiled out.
// Never put a side effect inside FB_ENSURE.
#if !defined(NDEBUG) && !defined(FEMBOY_DISABLE_ENSURE)
#define FB_ENSURE(cond)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
            FB_UNLIKELY                                                                                                \
            {                                                                                                          \
                ::Fem::Boy::Instance().Error("Ensure", #cond " (" __FILE__ ":" FB_STRINGIFY(__LINE__) ")");         \
                FB_DEBUGBREAK();                                                                                       \
            }                                                                                                          \
    } while (0)
#else
#define FB_ENSURE(cond) ((void)0)
#endif

// Always evaluates `cond`; reports in debug, silent in release. Use when the
// expression has a side effect (e.g. an API call whose failure must be checked).
#define FB_VERIFY(cond)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        const bool fb_verify_ok = static_cast<bool>(cond);                                                             \
        if (!fb_verify_ok)                                                                                             \
            FB_UNLIKELY                                                                                                \
            {                                                                                                          \
                ::Fem::Boy::Instance().Error("Verify", #cond " (" __FILE__ ":" FB_STRINGIFY(__LINE__) ")");         \
            }                                                                                                          \
    } while (0)

#define FB_CHECK(cond) assert(cond)

// Marks a parameter/variable that exists only for API symmetry.
#define FB_MAYBE_UNUSED [[maybe_unused]]
