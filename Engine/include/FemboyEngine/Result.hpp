// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace Fem
{

// Value-or-error return type for fallible engine APIs.
//
// Before this existed every loader invented its own convention: some returned
// bool and logged, some silently substituted a default, and nlohmann::json's
// get<> threw on a type mismatch. Callers could not tell "empty result" from
// "malformed input". Result<T> makes the failure explicit and cheap.
//
//   Result<Scene> sc = Scene::loadFrom("level.json");
//   if (!sc) { BOY_ERROR("Game", sc.Error()); return; }
//   use(sc.value());
template <typename T> class Result
{
public:
    static Result Success(T value)
    {
        return Result(OkTag{}, std::move(value));
    }
    static Result Failure(std::string error)
    {
        return Result(ErrTag{}, std::move(error));
    }

    bool HasValue() const
    {
        return value_.has_value();
    }
    explicit operator bool() const
    {
        return HasValue();
    }

    T& Value()
    {
        if (!value_)
        {
            throw std::runtime_error("Result::Value() on an error: " + error_);
        }
        return *value_;
    }
    const T& Value() const
    {
        if (!value_)
        {
            throw std::runtime_error("Result::Value() on an error: " + error_);
        }
        return *value_;
    }
    T ValueOr(T fallback) const
    {
        return value_ ? *value_ : std::move(fallback);
    }

    const std::string& Error() const
    {
        return error_;
    }

private:
    // Tag types keep the two constructors distinct even when T is std::string.
    struct OkTag
    {
    };
    struct ErrTag
    {
    };

    Result(OkTag, T value) : value_(std::move(value))
    {
    }
    Result(ErrTag, std::string error) : error_(std::move(error))
    {
    }

    std::optional<T> value_;
    std::string error_;
};

// Specialisation for operations that only report success or failure.
template <> class Result<void>
{
public:
    static Result Success()
    {
        return Result(true, {});
    }
    static Result Failure(std::string error)
    {
        return Result(false, std::move(error));
    }

    bool HasValue() const
    {
        return ok_;
    }
    explicit operator bool() const
    {
        return ok_;
    }
    const std::string& Error() const
    {
        return error_;
    }

private:
    Result(bool ok, std::string error) : ok_(ok), error_(std::move(error))
    {
    }

    bool ok_ = false;
    std::string error_;
};

} // namespace Fem
