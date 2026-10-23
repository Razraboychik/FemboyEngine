// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace Fem
{

// Native hot reload for Windows (and dlopen platforms).
//
// The source DLL cannot be overwritten while the loader maps it, so the module
// is copied to a generation-stamped file first. Reload() unloads the old image,
// copies the freshly built DLL to a new name and loads it. A game keeps a stable
// function table (or looks symbols up again) and copies its serialisable state
// across the boundary explicitly; the engine deliberately does not try to
// preserve C++ objects whose layout just changed.
//
// The module ABI is the game's business. A practical contract is:
//
//   extern "C" FemboyModuleApi* femboy_module_create();
//   extern "C" void              femboy_module_destroy(FemboyModuleApi*);
//   extern "C" std::uint32_t     femboy_module_abi_version();
//
// and a POD struct of function pointers inside FemboyModuleApi. That survives
// reload; std::function, vtables and STL containers do not.
class NativeModule
{
public:
    static constexpr std::uint32_t kAbiVersion = 1;

    NativeModule() = default;
    ~NativeModule();
    NativeModule(const NativeModule&) = delete;
    NativeModule& operator=(const NativeModule&) = delete;

    // Loads `sourcePath`. If `copyTo` is empty a sibling directory
    // "<source>.reload" is used. Returns false and sets lastError() on failure.
    bool Load(const std::string& sourcePath, const std::string& copyTo = {});
    bool Reload();
    void Unload();

    bool Loaded() const
    {
        return handle_ != nullptr;
    }
    const std::string& LastError() const
    {
        return lastError_;
    }
    const std::string& SourcePath() const
    {
        return sourcePath_;
    }
    int Generation() const
    {
        return generation_;
    }

    void* Symbol(const char* name) const;
    template <typename T> T Function(const char* name) const
    {
        return reinterpret_cast<T>(Symbol(name));
    }

    // Watches the source file's write time and reloads when it changes. Call
    // once per frame (or once a second) from the main thread.
    bool PollForChanges();

    static std::string PlatformExtension();

private:
    bool LoadCopy(const std::filesystem::path& path);

    void* handle_ = nullptr;
    std::string sourcePath_;
    std::string copyDir_;
    std::string currentCopy_;
    std::string lastError_;
    int generation_ = 0;
    std::filesystem::file_time_type lastWrite_{};
};

} // namespace Fem
