// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <variant>
#include <nlohmann/json.hpp>

namespace Fem
{

// FemboyScript: a beautiful, readable scripting language.
// Syntax example (C#-like, strict typing):
//   int health = 100;
//   void Hit(int inDamage)
//   {
//       health = health - inDamage;
//       if (health <= 0)
//       {
//           print("dead");
//           return;
//       }
//       print("hp: " + health);
//   }
//
//   Hit(30);
//
//   for (int i = 0; i < 10; i++)
//   {
//       print(i);
//   }
//   match state { "idle" => play("idle")  _ => print("unknown") }

// Typed values produced by Reflection-aware natives. They are appended to the
// variant so the indices of the original five alternatives remain stable: a
// host that stored tag numbers or serialised variant indices keeps working.
struct FSValueHolder;

struct FEnumValue
{
    std::string type;
    std::int64_t value = 0;

    bool operator==(const FEnumValue&) const = default;
};

struct FStructValue
{
    std::string type;
    std::map<std::string, std::shared_ptr<FSValueHolder>> fields;

    bool operator==(const FStructValue& other) const;
};

using FSValue = std::variant<std::monostate, bool, double, std::string,
                             std::vector<std::shared_ptr<FSValueHolder>>, FEnumValue, FStructValue>;

struct FSValueHolder
{
    FSValue value;
};

class FemboyScript
{
public:
    struct Result
    {
        bool ok;
        std::string error;
        FSValue value;
    };

    // A parsed source file. Compiling once and running the same Program many
    // times skips the lexer and parser on every tick, which is the difference
    // between a visual-script OnUpdate being cheap and being a per-frame
    // allocation storm.
    class Program
    {
    public:
        Program();
        ~Program();
        Program(const Program&) = delete;
        Program& operator=(const Program&) = delete;

        const std::string& Source() const;
        bool Valid() const;

    private:
        friend class FemboyScript;
        struct Impl;
        std::shared_ptr<Impl> impl_;
    };
    using ProgramPtr = std::shared_ptr<const Program>;

    FemboyScript();
    void RegisterFunction(const std::string& name, std::function<FSValue(const std::vector<FSValue>&)> fn);
    void Inject(const std::string& name, const FSValue& v);
    void SetPrintHandler(std::function<void(const std::string&)> sink);

    // Compile source to a reusable program. Returns null and fills `error` when
    // lexing/parsing fails. The engine keeps a small source-keyed cache.
    ProgramPtr Compile(const std::string& source, std::string* error = nullptr);
    Result Run(const std::string& source);
    Result Run(const ProgramPtr& program);
    Result RunFile(const std::string& path);
    // Invokes a function registered by a previous run(). This is how a visual
    // script runtime dispatches OnStart/OnUpdate/OnTagAdded without reparsing.
    Result Call(const std::string& function, const std::vector<FSValue>& args = {});
    bool HasFunction(const std::string& function) const;

    // Execution guards. They are per FemboyScript instance, not global.
    void SetExecutionBudget(long long maxSteps);
    void SetMaxDepth(int maxDepth);
    void SetMaxCallDepth(int maxCallDepth);

    // Cache controls. Capacity 0 disables caching.
    void SetCacheCapacity(std::size_t entries);
    void ClearCache();
    std::size_t CacheSize() const;
    std::size_t CacheHits() const;
    std::size_t CacheMisses() const;

    // Forgets globals/functions but keeps native functions and the cache.
    // Used when a game returns to the main menu and a mod's state must not
    // leak into the next run.
    void Reset();

    // Convert between FSValue and JSON for mod interop
    static nlohmann::json ToJson(const FSValue& v);
    static FSValue FromJson(const nlohmann::json& j);

private:
    // implementation in cpp
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace Fem
