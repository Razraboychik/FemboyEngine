// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

// FemInstancedStruct: a type-erased, owning struct value.
//
// Unlike a plain void* it knows the registered struct type it holds, so it can
// validate Get<T>(), copy itself safely and serialise through Reflection.
// Registration comes from FEMSTRUCT/FEMSTRUCT_FIELD; a type that is not
// registered cannot be stored (Set returns false) because its field layout and
// JSON form would be unknown.
//
// Scripts use the value-shaped FStructValue from FemboyScript. The bridge is
// JSON: FemboyScript::FromJson(instance.ToJson()) and
// FemboyScript::ToJson(structValue) describe the same object.

#include "FemboyEngine/Reflection.hpp"

#include <string>
#include <utility>

namespace Fem
{

class FemInstancedStruct
{
public:
    FemInstancedStruct() = default;

    template <typename T> explicit FemInstancedStruct(const T& value)
    {
        Set(value);
    }

    FemInstancedStruct(const FemInstancedStruct& other)
    {
        CopyFrom(other);
    }

    FemInstancedStruct(FemInstancedStruct&& other) noexcept
        : type_(other.type_), data_(std::exchange(other.data_, nullptr)), owns_(other.owns_)
    {
        other.type_ = nullptr;
        other.owns_ = false;
    }

    FemInstancedStruct& operator=(const FemInstancedStruct& other)
    {
        if (this != &other)
        {
            Reset();
            CopyFrom(other);
        }
        return *this;
    }

    FemInstancedStruct& operator=(FemInstancedStruct&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            type_ = other.type_;
            data_ = std::exchange(other.data_, nullptr);
            owns_ = other.owns_;
            other.type_ = nullptr;
            other.owns_ = false;
        }
        return *this;
    }

    ~FemInstancedStruct()
    {
        Reset();
    }

    // Stores a copy of `value`. Returns false when T has no FEMSTRUCT
    // registration, because then there is no way to copy or serialise it
    // generically.
    template <typename T> bool Set(const T& value)
    {
        const FemStructType* type = FemStructRegistry::Instance().Find(std::type_index(typeid(T)));
        if (!type || !type->clone)
        {
            return false;
        }
        void* copy = type->clone(&value);
        if (!copy)
        {
            return false;
        }
        Reset();
        type_ = type;
        data_ = copy;
        owns_ = true;
        return true;
    }

    // Creates the registered type with its declared field defaults.
    bool SetDefault(const std::string& typeName)
    {
        const FemStructType* type = FemStructRegistry::Instance().Find(typeName);
        if (!type)
        {
            return false;
        }
        void* object = FemStructRegistry::Instance().CreateDefault(typeName);
        if (!object)
        {
            return false;
        }
        Reset();
        type_ = type;
        data_ = object;
        owns_ = true;
        return true;
    }

    template <typename T> T* Get()
    {
        return IsType<T>() ? static_cast<T*>(data_) : nullptr;
    }

    template <typename T> const T* Get() const
    {
        return IsType<T>() ? static_cast<const T*>(data_) : nullptr;
    }

    template <typename T> bool IsType() const
    {
        return type_ != nullptr && type_->index == std::type_index(typeid(T));
    }

    const std::string& GetType() const
    {
        static const std::string empty;
        return type_ ? type_->name : empty;
    }

    bool IsValid() const
    {
        return type_ != nullptr && data_ != nullptr;
    }

    explicit operator bool() const
    {
        return IsValid();
    }

    void* Data()
    {
        return data_;
    }

    const void* Data() const
    {
        return data_;
    }

    nlohmann::json ToJson() const
    {
        if (!type_ || !data_)
        {
            return nullptr;
        }
        return FemStructRegistry::Instance().ToJson(*type_, data_);
    }

    // Accepts either a full struct object (`{"$type":"struct", ...}`) or a bare
    // field map when a type is already selected. A named object may switch the
    // instance to that registered type.
    bool FromJson(const nlohmann::json& json)
    {
        if (!json.is_object())
        {
            return false;
        }

        const FemStructType* type = type_;
        if (json.contains("name") && json["name"].is_string())
        {
            const std::string name = json["name"].get<std::string>();
            const FemStructType* named = FemStructRegistry::Instance().Find(name);
            if (!named)
            {
                return false;
            }
            if (!type || type->name != name)
            {
                void* object = FemStructRegistry::Instance().CreateDefault(name);
                if (!object)
                {
                    return false;
                }
                Reset();
                type_ = named;
                data_ = object;
                owns_ = true;
            }
            type = named;
        }

        if (!type_ || !data_)
        {
            return false;
        }
        return FemStructRegistry::Instance().FromJson(*type_, data_, json);
    }

    void Reset()
    {
        if (owns_ && data_ && type_ && type_->destroy)
        {
            type_->destroy(data_);
        }
        data_ = nullptr;
        type_ = nullptr;
        owns_ = false;
    }

    // Replaces the payload with the struct described by `other`. Used by
    // reflection bridges that hold a type-erased pointer but do not own it.
    bool AssignFrom(const std::string& typeName, const void* object)
    {
        const FemStructType* type = FemStructRegistry::Instance().Find(typeName);
        if (!type || !type->clone || !object)
        {
            return false;
        }
        void* copy = type->clone(object);
        if (!copy)
        {
            return false;
        }
        Reset();
        type_ = type;
        data_ = copy;
        owns_ = true;
        return true;
    }

private:
    void CopyFrom(const FemInstancedStruct& other)
    {
        if (!other.type_ || !other.data_ || !other.type_->clone)
        {
            return;
        }
        data_ = other.type_->clone(other.data_);
        if (data_)
        {
            type_ = other.type_;
            owns_ = true;
        }
    }

    const FemStructType* type_ = nullptr;
    void* data_ = nullptr;
    bool owns_ = false;
};

} // namespace Fem
