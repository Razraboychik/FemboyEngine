// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <vector>

namespace Fem
{

class FemObject;

// Lightweight class descriptor. It provides the UE-style class object without
// reflection, garbage collection or RTTI. A class is a value that can be stored,
// compared and used to create instances at runtime.
class FemClass
{
public:
    using Factory = FemObject* (*)();

    FemClass(const char* name, const FemClass* parent, bool abstract, std::size_t objectSize, Factory factory)
        : name_(name), parent_(parent), abstract_(abstract), objectSize_(objectSize), factory_(factory)
    {
    }

    const char* GetName() const
    {
        return name_;
    }

    const FemClass* GetSuperClass() const
    {
        return parent_;
    }

    bool IsAbstract() const
    {
        return abstract_;
    }

    std::size_t GetObjectSize() const
    {
        return objectSize_;
    }

    bool IsChildOf(const FemClass* other) const
    {
        if (!other)
        {
            return false;
        }
        const FemClass* current = this;
        while (current)
        {
            if (current == other)
            {
                return true;
            }
            current = current->parent_;
        }
        return false;
    }

    bool IsA(const FemClass* other) const
    {
        return IsChildOf(other);
    }

    FemObject* CreateObject() const
    {
        return factory_ ? factory_() : nullptr;
    }

    // Lazily creates and caches the default object for this class. Non-creatable
    // classes return nullptr. The object is created on first request and lives
    // until the class descriptor is destroyed.
    FemObject* GetDefaultObject() const
    {
        std::call_once(defaultObjectOnce_, [this]()
                       { defaultObject_ = CreateObject(); });
        return defaultObject_;
    }

    template <typename Class, bool Abstract> static const FemClass* Register(const char* name, const FemClass* parent)
    {
        static const FemClass info(name, parent, Abstract, sizeof(Class), &Class::CreateObject);
        return &info;
    }

private:
    const char* name_ = nullptr;
    const FemClass* parent_ = nullptr;
    bool abstract_ = false;
    std::size_t objectSize_ = 0;
    Factory factory_ = nullptr;

    mutable std::once_flag defaultObjectOnce_;
    mutable FemObject* defaultObject_ = nullptr;
};

// Root of the engine object model. It is intentionally small: reflection,
// serialization and lifecycle services can grow around it without forcing every
// subsystem to inherit a heavy framework. The class mirrors the role of UObject
// in Unreal Engine while keeping the engine's existing composition-first design.
class FemObject
{
public:
    virtual ~FemObject()
    {
        UnregisterForTick(this);
    }

    static const FemClass* StaticClass()
    {
        static const FemClass info("FemObject", nullptr, true, sizeof(FemObject), nullptr);
        return &info;
    }

    virtual const FemClass* GetClass() const
    {
        return StaticClass();
    }

    // Centralized object ticking. Only objects with ticking enabled are visited,
    // so a disabled object costs one branch instead of a virtual call.
    static void TickAll(float dt);
    static void RegisterForTick(FemObject* object);
    static void UnregisterForTick(FemObject* object);

    // Lifecycle hooks. They are virtual and empty by default, so a class pays
    // for them only when it overrides them. BeginPlay/EndPlay mirror UE-style
    // activation, while Tick is the per-frame update entry point.
    virtual void BeginPlay()
    {
    }

    virtual void EndPlay()
    {
    }

    virtual void Tick(float dt)
    {
        (void)dt;
    }

    // Ticks can be disabled per object. TickIfEnabled avoids the virtual Tick
    // call entirely for disabled objects, which keeps large object sets cheap.
    void SetTickEnabled(bool enabled)
    {
        tickEnabled_ = enabled;
    }

    bool IsTickEnabled() const
    {
        return tickEnabled_;
    }

    void TickIfEnabled(float dt)
    {
        if (tickEnabled_)
        {
            Tick(dt);
        }
    }

    // Definitions live in FemObject.cpp so the header stays free of duplicate
    // inline definitions and the methods can be called from the engine loop.

    bool IsChildOf(const FemClass* other) const
    {
        return GetClass()->IsChildOf(other);
    }

    bool IsA(const FemClass* other) const
    {
        return GetClass()->IsA(other);
    }

    template <typename T> bool IsA() const
    {
        return GetClass()->IsA(T::StaticClass());
    }

    template <typename T> T* As()
    {
        return IsA<T>() ? static_cast<T*>(this) : nullptr;
    }

    template <typename T> const T* As() const
    {
        return IsA<T>() ? static_cast<const T*>(this) : nullptr;
    }

    // Cheap object flags for editor/transient/dirty/replication state. A raw
    // bitmask keeps the root object small; games can define their own bit names.
    void AddFlags(std::uint32_t flags)
    {
        flags_ |= flags;
    }

    void RemoveFlags(std::uint32_t flags)
    {
        flags_ &= ~flags;
    }

    void SetFlags(std::uint32_t flags)
    {
        flags_ = flags;
    }

    std::uint32_t GetFlags() const
    {
        return flags_;
    }

    bool HasFlags(std::uint32_t flags) const
    {
        return (flags_ & flags) == flags;
    }

protected:
    FemObject()
    {
        RegisterForTick(this);
    }
    FemObject(const FemObject&)
    {
        RegisterForTick(this);
    }
    FemObject& operator=(const FemObject&) = default;

private:
    std::uint32_t flags_ = 0;
    bool tickEnabled_ = true;

    static inline std::vector<FemObject*> tickObjects_;
    static inline std::mutex tickMutex_;
};

// Declares the UE-style class descriptor for a FemObject subclass.
//
//   class Engine : public FemObject
//   {
//   public:
//       FEMCLASS_NO_CREATE(Engine, FemObject, false)
//       ...
//   };
//
// Abstract classes pass true for Abstract. Classes without a default constructor
// are reported as non-creatable by std::is_default_constructible, so CreateObject
// returns nullptr instead of trying to instantiate them.
#define FEMCLASS(Class, Parent, Abstract)                                                                              \
    static const Fem::FemClass* StaticClass()                                                                       \
    {                                                                                                                  \
        return Fem::FemClass::Register<Class, Abstract>(#Class, Parent::StaticClass());                             \
    }                                                                                                                  \
    const Fem::FemClass* GetClass() const override                                                                  \
    {                                                                                                                  \
        return StaticClass();                                                                                          \
    }                                                                                                                  \
    static Fem::FemObject* CreateObject()                                                                           \
    {                                                                                                                  \
        return nullptr;                                                                                                \
    }

#define FEMCLASS_CREATABLE(Class, Parent, Abstract)                                                                    \
    static const Fem::FemClass* StaticClass()                                                                       \
    {                                                                                                                  \
        return Fem::FemClass::Register<Class, Abstract>(#Class, Parent::StaticClass());                             \
    }                                                                                                                  \
    const Fem::FemClass* GetClass() const override                                                                  \
    {                                                                                                                  \
        return StaticClass();                                                                                          \
    }                                                                                                                  \
    static Fem::FemObject* CreateObject()                                                                           \
    {                                                                                                                  \
        if constexpr (Abstract)                                                                                        \
        {                                                                                                              \
            return nullptr;                                                                                            \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            return new Class();                                                                                        \
        }                                                                                                              \
    }

// Same as FEMCLASS, but marks the class as non-creatable. Use this for classes
// that do not have a usable default constructor, such as subsystem singletons.
#define FEMCLASS_NO_CREATE(Class, Parent, Abstract)                                                                    \
    static const Fem::FemClass* StaticClass()                                                                       \
    {                                                                                                                  \
        return Fem::FemClass::Register<Class, Abstract>(#Class, Parent::StaticClass());                             \
    }                                                                                                                  \
    const Fem::FemClass* GetClass() const override                                                                  \
    {                                                                                                                  \
        return StaticClass();                                                                                          \
    }                                                                                                                  \
    static Fem::FemObject* CreateObject()                                                                           \
    {                                                                                                                  \
        return nullptr;                                                                                                \
    }

} // namespace Fem
