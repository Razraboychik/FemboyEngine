// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/NativeModule.hpp"

#include "FemboyEngine/Boy.hpp"

#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Fem
{

namespace
{
std::string SystemError()
{
#if defined(_WIN32)
    const DWORD code = GetLastError();
    if (code == 0)
    {
        return "unknown error";
    }
    char* buffer = nullptr;
    const DWORD length =
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, 0, reinterpret_cast<char*>(&buffer), 0, nullptr);
    std::string message = length && buffer ? std::string(buffer, length) : std::to_string(code);
    if (buffer)
    {
        LocalFree(buffer);
    }
    return message;
#else
    const char* error = dlerror();
    return error ? error : "unknown error";
#endif
}
} // namespace

NativeModule::~NativeModule()
{
    Unload();
}

std::string NativeModule::PlatformExtension()
{
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

bool NativeModule::Load(const std::string& sourcePath, const std::string& copyTo)
{
    Unload();
    lastError_.clear();
    sourcePath_ = sourcePath;

    std::error_code ec;
    if (!std::filesystem::exists(sourcePath_, ec))
    {
        lastError_ = "module not found: " + sourcePath_;
        return false;
    }

    const std::filesystem::path source(sourcePath_);
    copyDir_ = copyTo.empty() ? (source.parent_path() / (source.stem().string() + ".reload")).string() : copyTo;
    std::filesystem::create_directories(copyDir_, ec);
    if (ec)
    {
        lastError_ = "cannot create reload directory: " + ec.message();
        return false;
    }

    lastWrite_ = std::filesystem::last_write_time(source, ec);
    generation_ = 1;

    const std::filesystem::path copy =
        std::filesystem::path(copyDir_) /
        (source.stem().string() + "_" + std::to_string(generation_) + PlatformExtension());
    std::filesystem::copy_file(source, copy, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        lastError_ = "cannot copy module: " + ec.message();
        return false;
    }

    if (!LoadCopy(copy))
    {
        return false;
    }

    BOY_INFO("HotReload", "loaded " + sourcePath_ + " (generation " + std::to_string(generation_) + ")");
    return true;
}

bool NativeModule::LoadCopy(const std::filesystem::path& path)
{
#if defined(_WIN32)
    // LOAD_WITH_ALTERED_SEARCH_PATH lets the DLL find its siblings (SDL, assets)
    // next to the build output, not next to the temporary copy.
    HMODULE module = LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module)
    {
        lastError_ = "LoadLibrary failed: " + SystemError();
        return false;
    }
    handle_ = module;
#else
    void* module = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module)
    {
        lastError_ = "dlopen failed: " + systemError();
        return false;
    }
    handle_ = module;
#endif
    currentCopy_ = path.string();
    return true;
}

bool NativeModule::Reload()
{
    if (sourcePath_.empty())
    {
        lastError_ = "reload() before load()";
        return false;
    }

    Unload();
    lastError_.clear();

    std::error_code ec;
    const std::filesystem::path source(sourcePath_);
    if (!std::filesystem::exists(source, ec))
    {
        lastError_ = "module not found: " + sourcePath_;
        return false;
    }

    lastWrite_ = std::filesystem::last_write_time(source, ec);
    ++generation_;

    const std::filesystem::path copy =
        std::filesystem::path(copyDir_) /
        (source.stem().string() + "_" + std::to_string(generation_) + PlatformExtension());
    std::filesystem::copy_file(source, copy, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        lastError_ = "cannot copy module: " + ec.message();
        return false;
    }

    if (!LoadCopy(copy))
    {
        return false;
    }

    BOY_INFO("HotReload", "reloaded " + sourcePath_ + " (generation " + std::to_string(generation_) + ")");
    return true;
}

void NativeModule::Unload()
{
    if (!handle_)
    {
        return;
    }

#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
}

void* NativeModule::Symbol(const char* name) const
{
    if (!handle_ || !name)
    {
        return nullptr;
    }
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
    return dlsym(handle_, name);
#endif
}

bool NativeModule::PollForChanges()
{
    if (sourcePath_.empty())
    {
        return false;
    }

    std::error_code ec;
    const auto writeTime = std::filesystem::last_write_time(sourcePath_, ec);
    if (ec)
    {
        return false;
    }
    if (writeTime <= lastWrite_)
    {
        return false;
    }

    lastWrite_ = writeTime;
    return Reload();
}

} // namespace Fem
