// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Scripting.hpp"
#include "FemboyEngine/Boy.hpp"
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <algorithm>

namespace Fem
{

namespace
{
std::string Trim(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
    {
        return {};
    }
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool IsTruthy(const nlohmann::json& v)
{
    if (v.is_boolean())
    {
        return v.get<bool>();
    }
    if (v.is_number())
    {
        return v.get<double>() != 0.0;
    }
    if (v.is_string())
    {
        return !v.get<std::string>().empty();
    }
    if (v.is_null())
    {
        return false;
    }
    return true;
}

double AsNumber(const nlohmann::json& v)
{
    if (v.is_number())
    {
        return v.get<double>();
    }
    if (v.is_boolean())
    {
        return v.get<bool>() ? 1.0 : 0.0;
    }
    if (v.is_string())
    {
        // std::stod silently accepts a valid prefix ("12abc" -> 12), so validate
        // that the entire string was consumed before trusting the value.
        const std::string s = v.get<std::string>();
        try
        {
            size_t used = 0;
            const double d = std::stod(s, &used);
            if (used == s.size())
            {
                return d;
            }
        }
        catch (...)
        {
        }
        return 0.0;
    }
    return 0.0;
}

// Splits on a top-level operator (not inside parentheses or quotes), scanning
// right-to-left so expressions are left-associative.
bool SplitTopLevel(const std::string& s, const std::string& op, std::string& lhs, std::string& rhs)
{
    int depth = 0;
    bool inString = false;
    char quote = 0;

    for (int i = static_cast<int>(s.size()) - static_cast<int>(op.size()); i >= 0; --i)
    {
        const char c = s[static_cast<size_t>(i)];

        if (inString)
        {
            if (c == quote && (i == 0 || s[static_cast<size_t>(i - 1)] != '\\'))
            {
                inString = false;
            }
            continue;
        }
        if (c == '"' || c == '\'')
        {
            inString = true;
            quote = c;
            continue;
        }

        if (c == ')')
        {
            ++depth;
            continue;
        }
        if (c == '(')
        {
            --depth;
            continue;
        }
        if (depth != 0)
        {
            continue;
        }

        if (s.compare(static_cast<size_t>(i), op.size(), op) == 0)
        {
            lhs = s.substr(0, static_cast<size_t>(i));
            rhs = s.substr(static_cast<size_t>(i) + op.size());
            return true;
        }
    }
    return false;
}

// Like splitTopLevel, but scans left-to-right (for left-associative operators).
bool SplitTopLevelForward(const std::string& s, const std::string& op, std::string& lhs, std::string& rhs)
{
    int depth = 0;
    bool inString = false;
    char quote = 0;

    for (size_t i = 0; i + op.size() <= s.size(); ++i)
    {
        const char c = s[i];

        if (inString)
        {
            if (c == quote && (i == 0 || s[i - 1] != '\\'))
            {
                inString = false;
            }
            continue;
        }
        if (c == '"' || c == '\'')
        {
            inString = true;
            quote = c;
            continue;
        }

        if (c == '(')
        {
            ++depth;
            continue;
        }
        if (c == ')')
        {
            --depth;
            continue;
        }
        if (depth != 0)
        {
            continue;
        }

        if (s.compare(i, op.size(), op) == 0)
        {
            lhs = s.substr(0, i);
            rhs = s.substr(i + op.size());
            return true;
        }
    }
    return false;
}
} // namespace

ScriptVM::ScriptVM()
{
    RegisterFunction("print",
                     [](const std::vector<Value>& args) -> Value
                     {
                         std::string out;
                         for (const auto& a : args)
                         {
                             if (!out.empty())
                             {
                                 out += " ";
                             }
                             out += a.is_string() ? a.get<std::string>() : a.dump();
                         }
                         BOY_INFO("Script", out);
                         return nullptr;
                     });

    RegisterFunction("add",
                     [](const std::vector<Value>& args) -> Value
                     {
                         double s = 0.0;
                         for (const auto& a : args)
                         {
                             s += AsNumber(a);
                         }
                         return s;
                     });

    RegisterFunction("sub",
                     [](const std::vector<Value>& args) -> Value
                     {
                         if (args.empty())
                         {
                             return 0.0;
                         }
                         double s = AsNumber(args[0]);
                         for (size_t i = 1; i < args.size(); ++i)
                         {
                             s -= AsNumber(args[i]);
                         }
                         return s;
                     });

    RegisterFunction("mul",
                     [](const std::vector<Value>& args) -> Value
                     {
                         double s = 1.0;
                         for (const auto& a : args)
                         {
                             s *= AsNumber(a);
                         }
                         return s;
                     });

    RegisterFunction("div",
                     [](const std::vector<Value>& args) -> Value
                     {
                         if (args.size() < 2)
                         {
                             return 0.0;
                         }
                         double s = AsNumber(args[0]);
                         for (size_t i = 1; i < args.size(); ++i)
                         {
                             const double d = AsNumber(args[i]);
                             if (d == 0.0)
                             {
                                 return 0.0; // division by zero yields 0 instead of inf
                             }
                             s /= d;
                         }
                         return s;
                     });

    RegisterFunction("min",
                     [](const std::vector<Value>& args) -> Value
                     {
                         if (args.empty())
                         {
                             return 0.0;
                         }
                         double s = AsNumber(args[0]);
                         for (const auto& a : args)
                         {
                             s = std::min(s, AsNumber(a));
                         }
                         return s;
                     });

    RegisterFunction("max",
                     [](const std::vector<Value>& args) -> Value
                     {
                         if (args.empty())
                         {
                             return 0.0;
                         }
                         double s = AsNumber(args[0]);
                         for (const auto& a : args)
                         {
                             s = std::max(s, AsNumber(a));
                         }
                         return s;
                     });

    RegisterFunction("len",
                     [](const std::vector<Value>& args) -> Value
                     {
                         if (args.empty())
                         {
                             return 0;
                         }
                         const auto& v = args[0];
                         if (v.is_array())
                         {
                             return static_cast<int>(v.size());
                         }
                         if (v.is_string())
                         {
                             return static_cast<int>(v.get<std::string>().size());
                         }
                         return 0;
                     });
}

void ScriptVM::RegisterFunction(const std::string& name, Func f)
{
    if (name.empty() || !f)
    {
        return;
    }
    functions_[name] = std::move(f);
}

ScriptVM::Value ScriptVM::EvalExpr(const std::string& raw, const std::map<std::string, Value>& env)
{
    // Recursion guard: every nested sub-expression consumes one level.
    if (exprDepth_ >= maxDepth_)
    {
        ++errorCount_;
        return nullptr;
    }

    struct DepthGuard
    {
        int& d;
        explicit DepthGuard(int& v) : d(v)
        {
            ++d;
        }
        ~DepthGuard()
        {
            --d;
        }
    } guard(exprDepth_);

    const std::string expr = Trim(raw);
    if (expr.empty())
    {
        return nullptr;
    }

    // Parentheses
    if (expr.front() == '(' && expr.back() == ')')
    {
        // Only strip if they actually enclose the whole expression.
        int depth = 0;
        bool encloses = true;
        for (size_t i = 0; i < expr.size(); ++i)
        {
            if (expr[i] == '(')
            {
                ++depth;
            }
            else if (expr[i] == ')')
            {
                --depth;
                if (depth == 0 && i != expr.size() - 1)
                {
                    encloses = false;
                    break;
                }
            }
        }
        if (encloses)
        {
            return EvalExpr(expr.substr(1, expr.size() - 2), env);
        }
    }

    // Logical OR / AND (lowest precedence)
    {
        std::string l, r;
        if (SplitTopLevelForward(expr, "||", l, r))
        {
            return IsTruthy(EvalExpr(l, env)) || IsTruthy(EvalExpr(r, env));
        }
        if (SplitTopLevelForward(expr, "&&", l, r))
        {
            return IsTruthy(EvalExpr(l, env)) && IsTruthy(EvalExpr(r, env));
        }
    }

    // Comparisons
    {
        std::string l, r;
        if (SplitTopLevelForward(expr, "==", l, r))
        {
            const Value a = EvalExpr(l, env), b = EvalExpr(r, env);
            if (a.is_number() && b.is_number())
            {
                return std::abs(AsNumber(a) - AsNumber(b)) < 1e-9;
            }
            if (a.is_string() && b.is_string())
            {
                return a.get<std::string>() == b.get<std::string>();
            }
            if (a.is_boolean() && b.is_boolean())
            {
                return a.get<bool>() == b.get<bool>();
            }
            if (a.is_null() || b.is_null())
            {
                return a.is_null() && b.is_null();
            }
            return a == b;
        }
        if (SplitTopLevelForward(expr, "!=", l, r))
        {
            const Value a = EvalExpr(l, env), b = EvalExpr(r, env);
            if (a.is_number() && b.is_number())
            {
                return std::abs(AsNumber(a) - AsNumber(b)) >= 1e-9;
            }
            if (a.is_string() && b.is_string())
            {
                return a.get<std::string>() != b.get<std::string>();
            }
            if (a.is_boolean() && b.is_boolean())
            {
                return a.get<bool>() != b.get<bool>();
            }
            if (a.is_null() || b.is_null())
            {
                return !(a.is_null() && b.is_null());
            }
            return a != b;
        }
        if (SplitTopLevelForward(expr, ">=", l, r))
        {
            return AsNumber(EvalExpr(l, env)) >= AsNumber(EvalExpr(r, env));
        }
        if (SplitTopLevelForward(expr, "<=", l, r))
        {
            return AsNumber(EvalExpr(l, env)) <= AsNumber(EvalExpr(r, env));
        }
        if (SplitTopLevelForward(expr, ">", l, r))
        {
            return AsNumber(EvalExpr(l, env)) > AsNumber(EvalExpr(r, env));
        }
        if (SplitTopLevelForward(expr, "<", l, r))
        {
            return AsNumber(EvalExpr(l, env)) < AsNumber(EvalExpr(r, env));
        }
    }

    // Unary
    if (expr.front() == '!' && expr.size() > 1)
    {
        return !IsTruthy(EvalExpr(expr.substr(1), env));
    }
    if (expr.front() == '-' && expr.size() > 1)
    {
        return -AsNumber(EvalExpr(expr.substr(1), env));
    }
    if (expr.front() == '+')
    {
        return EvalExpr(expr.substr(1), env);
    }

    // Arithmetic (+/- then */%)
    {
        std::string l, r;
        if (SplitTopLevel(expr, "+", l, r))
        {
            const Value a = EvalExpr(l, env), b = EvalExpr(r, env);
            if (a.is_string() || b.is_string())
            {
                const std::string as = a.is_string() ? a.get<std::string>() : a.dump();
                const std::string bs = b.is_string() ? b.get<std::string>() : b.dump();
                return as + bs;
            }
            return AsNumber(a) + AsNumber(b);
        }
        if (SplitTopLevel(expr, "-", l, r))
        {
            // Guard against a leading minus being read as a binary operator.
            if (!Trim(l).empty())
            {
                return AsNumber(EvalExpr(l, env)) - AsNumber(EvalExpr(r, env));
            }
        }
        if (SplitTopLevel(expr, "*", l, r))
        {
            return AsNumber(EvalExpr(l, env)) * AsNumber(EvalExpr(r, env));
        }
        if (SplitTopLevel(expr, "/", l, r))
        {
            const double d = AsNumber(EvalExpr(r, env));
            if (d == 0.0)
            {
                return 0.0;
            }
            return AsNumber(EvalExpr(l, env)) / d;
        }
        if (SplitTopLevel(expr, "%", l, r))
        {
            const double d = AsNumber(EvalExpr(r, env));
            if (d == 0.0)
            {
                return 0.0;
            }
            return std::fmod(AsNumber(EvalExpr(l, env)), d);
        }
    }

    // String literal
    if (expr.size() >= 2 &&
        ((expr.front() == '"' && expr.back() == '"') || (expr.front() == '\'' && expr.back() == '\'')))
    {
        return expr.substr(1, expr.size() - 2);
    }

    // Variable lookup: locals first, then globals.
    if (auto it = env.find(expr); it != env.end())
    {
        return it->second;
    }
    if (auto git = globals_.find(expr); git != globals_.end())
    {
        return git->second;
    }

    // Boolean literals
    if (expr == "true")
    {
        return true;
    }
    if (expr == "false")
    {
        return false;
    }
    if (expr == "null")
    {
        return nullptr;
    }

    // Numeric literal
    {
        char* end = nullptr;
        const double d = std::strtod(expr.c_str(), &end);
        if (end && *end == '\0' && end != expr.c_str())
        {
            return d;
        }
    }

    // Unknown identifier: return the name unchanged (visible in output).
    return expr;
}

ScriptVM::Value ScriptVM::Run(const nlohmann::json& program, std::map<std::string, Value>& env)
{
    steps_ = 0;
    errorCount_ = 0;
    exprDepth_ = 0;
    return RunBlock(program, env, 0);
}

ScriptVM::Value ScriptVM::RunBlock(const nlohmann::json& program, std::map<std::string, Value>& env, int depth)
{
    Value last = nullptr;

    if (!program.is_array())
    {
        ++errorCount_;
        BOY_WARN("Script", "program must be an array");
        return last;
    }
    if (depth > maxDepth_)
    {
        ++errorCount_;
        BOY_WARN("Script", "max nesting depth exceeded");
        return last;
    }

    for (const auto& instr : program)
    {
        if (++steps_ > maxSteps_)
        {
            ++errorCount_;
            BOY_WARN("Script", "step limit exceeded");
            break;
        }
        if (!instr.is_object())
        {
            continue;
        }

        // json::value() throws a type_error on a type mismatch, and this read
        // sits outside the try block below, so a malformed instruction aborted
        // the whole script run.
        std::string op;
        if (instr.contains("op") && instr["op"].is_string())
        {
            op = instr["op"].get<std::string>();
        }

        try
        {
            if (op == "set")
            {
                if (!instr.contains("var") || !instr["var"].is_string())
                {
                    ++errorCount_;
                    continue;
                }
                last = instr.contains("val") ? instr["val"] : Value(nullptr);
                env[instr["var"].get<std::string>()] = last;
            }
            else if (op == "let")
            {
                if (!instr.contains("var") || !instr["var"].is_string())
                {
                    ++errorCount_;
                    continue;
                }
                env[instr["var"].get<std::string>()] = instr.contains("val") ? instr["val"] : Value(nullptr);
                last = env[instr["var"].get<std::string>()];
            }
            else if (op == "expr")
            {
                if (!instr.contains("expr") || !instr["expr"].is_string())
                {
                    ++errorCount_;
                    continue;
                }
                last = EvalExpr(instr["expr"].get<std::string>(), env);
                if (instr.contains("var") && instr["var"].is_string())
                {
                    env[instr["var"].get<std::string>()] = last;
                }
            }
            else if (op == "inject")
            {
                if (!instr.contains("key") || !instr["key"].is_string())
                {
                    ++errorCount_;
                    continue;
                }
                globals_[instr["key"].get<std::string>()] = instr.contains("val") ? instr["val"] : Value(nullptr);
            }
            else if (op == "if")
            {
                if (!instr.contains("cond") || !instr["cond"].is_string())
                {
                    ++errorCount_;
                    continue;
                }
                const Value c = EvalExpr(instr["cond"].get<std::string>(), env);
                if (IsTruthy(c))
                {
                    if (instr.contains("then"))
                    {
                        last = RunBlock(instr["then"], env, depth + 1);
                    }
                }
                else if (instr.contains("else"))
                {
                    last = RunBlock(instr["else"], env, depth + 1);
                }
            }
            else if (op == "while")
            {
                if (!instr.contains("cond") || !instr["cond"].is_string() || !instr.contains("body"))
                {
                    ++errorCount_;
                    continue;
                }
                // Each iteration consumes a step, and the outer step limit bounds
                // the loop, so a bad condition cannot hang the engine.
                while (IsTruthy(EvalExpr(instr["cond"].get<std::string>(), env)))
                {
                    if (++steps_ > maxSteps_)
                    {
                        ++errorCount_;
                        BOY_WARN("Script", "while loop exceeded step limit");
                        break;
                    }
                    last = RunBlock(instr["body"], env, depth + 1);
                }
            }
            else if (op == "call")
            {
                if (!instr.contains("fn") || !instr["fn"].is_string())
                {
                    ++errorCount_;
                    continue;
                }
                const std::string fn = instr["fn"].get<std::string>();
                std::vector<Value> args;
                if (instr.contains("args") && instr["args"].is_array())
                {
                    for (const auto& a : instr["args"])
                    {
                        args.push_back(a);
                    }
                }
                if (auto it = functions_.find(fn); it != functions_.end())
                {
                    last = it->second(args);
                }
                else
                {
                    ++errorCount_;
                    BOY_WARN("Script", "unknown function: " + fn);
                }
            }
            else if (op == "loop")
            {
                int n = 0;
                if (instr.contains("n") && instr["n"].is_number())
                {
                    n = static_cast<int>(std::clamp(instr["n"].get<double>(), -1.0e9, 1.0e9));
                }
                n = std::clamp(n, 0, maxSteps_);
                if (!instr.contains("body"))
                {
                    ++errorCount_;
                    continue;
                }
                for (int i = 0; i < n; ++i)
                {
                    if (++steps_ > maxSteps_)
                    {
                        ++errorCount_;
                        break;
                    }
                    env["i"] = i;
                    last = RunBlock(instr["body"], env, depth + 1);
                }
            }
            else if (op == "return")
            {
                return instr.contains("val") ? instr["val"] : last;
            }
            else if (!op.empty())
            {
                ++errorCount_;
                BOY_WARN("Script", "unknown opcode: " + op);
            }
        }
        catch (const std::exception& e)
        {
            ++errorCount_;
            BOY_WARN("Script", std::string("runtime error: ") + e.what());
        }
    }

    return last;
}

void ScriptVM::Inject(const std::string& key, const Value& v)
{
    if (key.empty())
    {
        return;
    }
    globals_[key] = v;
}

} // namespace Fem
