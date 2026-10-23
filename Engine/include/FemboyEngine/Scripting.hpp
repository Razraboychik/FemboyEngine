// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>

namespace Fem
{

// FemboyScript: a tiny data-driven scripting VM.
// Scripts are JSON arrays of instructions (opcodes) to keep them safe and serializable.
// Example: [{"op":"set","var":"hp","val":100},{"op":"if","cond":"hp>0","then":[...]}]
//
// Expressions support numbers, strings, variables, parentheses, unary !/-,
// arithmetic (+ - * / %), comparisons (== != < <= > >=) and && / ||.
class ScriptVM
{
public:
    using Value = nlohmann::json;
    using Func = std::function<Value(const std::vector<Value>&)>;

    ScriptVM();
    void RegisterFunction(const std::string& name, Func f);

    // Executes a program. Returns the value of the last executed instruction.
    Value Run(const nlohmann::json& program, std::map<std::string, Value>& env);

    // injection: allow external modules to inject variables/functions at runtime
    void Inject(const std::string& key, const Value& v);
    const std::map<std::string, Value>& Globals() const
    {
        return globals_;
    }

    // Safety limits (per run call).
    void SetMaxSteps(int steps)
    {
        maxSteps_ = (steps > 0) ? steps : 1;
    }
    void SetMaxDepth(int depth)
    {
        maxDepth_ = (depth > 0) ? depth : 1;
    }
    int LastErrorCount() const
    {
        return errorCount_;
    }

private:
    Value RunBlock(const nlohmann::json& program, std::map<std::string, Value>& env, int depth);
    Value EvalExpr(const std::string& expr, const std::map<std::string, Value>& env);

    std::map<std::string, Func> functions_;
    std::map<std::string, Value> globals_;
    int maxSteps_ = 100000;
    int maxDepth_ = 32;
    int steps_ = 0;
    int errorCount_ = 0;
    // Guards the recursive descent of evalExpr: a deeply nested expression such
    // as "((((((...))))))" must fail cleanly instead of overflowing the stack.
    int exprDepth_ = 0;
};

} // namespace Fem
