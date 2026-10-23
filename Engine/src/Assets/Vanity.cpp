// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Vanity.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Fem
{

namespace
{
std::atomic<unsigned> g_tempCounter{0};

// A per-process, per-write temp name. A fixed ".tmp" suffix meant two writers
// (autosave thread + a menu save) could interleave into the same file.
std::string TemporaryPath(const std::string& path)
{
#if defined(_WIN32)
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    return path + ".tmp." + std::to_string(pid) + "." + std::to_string(++g_tempCounter);
}

// Forces the file's contents out of the OS cache before the rename. Without
// this a power loss can leave a renamed but empty/truncated file on disk.
bool FlushToDisk(const std::string& path)
{
#if defined(_WIN32)
    const std::wstring wide = std::filesystem::path(path).wstring();
    HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    const BOOL ok = FlushFileBuffers(h);
    CloseHandle(h);
    return ok != FALSE;
#else
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
    {
        return false;
    }
    const int rc = ::fsync(fd);
    ::close(fd);
    return rc == 0;
#endif
}

// Replaces path with tmp. On Windows the old delete-then-rename fallback left a
// window where the target did not exist at all; MoveFileExW replaces it in one
// step (and write-through flushes the directory entry).
bool replaceFile(const std::string& tmp, const std::string& path)
{
    namespace fs = std::filesystem;
#if defined(_WIN32)
    const std::wstring wtmp = fs::path(tmp).wstring();
    const std::wstring wpath = fs::path(path).wstring();
    return MoveFileExW(wtmp.c_str(), wpath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    fs::rename(tmp, path, ec);
    return !ec;
#endif
}
} // namespace

Result<std::string> Vanity::ReadText(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
    {
        return Result<std::string>::Failure("cannot open: " + path);
    }

    std::string out((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (f.bad())
    {
        return Result<std::string>::Failure("read failed: " + path);
    }
    return Result<std::string>::Success(std::move(out));
}

Result<void> Vanity::WriteText(const std::string& path, const std::string& text)
{
    namespace fs = std::filesystem;

    // Parent directories are part of "just works": saves/manifest directories
    // are commonly created lazily on the first write.
    const fs::path target(path);
    if (target.has_parent_path())
    {
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        if (ec)
        {
            return Result<void>::Failure("cannot create directory: " + target.parent_path().string() + " (" +
                                         ec.message() + ")");
        }
    }

    const std::string tmp = TemporaryPath(path);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
        {
            return Result<void>::Failure("cannot write: " + tmp);
        }
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.flush();
        if (!f)
        {
            f.close();
            std::error_code rec;
            fs::remove(tmp, rec);
            return Result<void>::Failure("write failed: " + tmp);
        }
        f.close();
        if (f.fail())
        {
            std::error_code rec;
            fs::remove(tmp, rec);
            return Result<void>::Failure("close failed: " + tmp);
        }
    }

    if (!FlushToDisk(tmp))
    {
        // Keep the temp file: it is the only complete copy of the new data.
        return Result<void>::Failure("cannot flush: " + tmp);
    }
    if (!replaceFile(tmp, path))
    {
        return Result<void>::Failure("cannot replace: " + path + " (new data kept in " + tmp + ")");
    }
    return Result<void>::Success();
}

Result<void> Vanity::WriteBytes(const std::string& path, const void* data, std::size_t size)
{
    if (data == nullptr && size != 0)
    {
        return Result<void>::Failure("null binary data");
    }
    const std::string bytes = (size == 0) ? std::string{} : std::string(static_cast<const char*>(data), size);
    return WriteText(path, bytes);
}

Result<nlohmann::json> Vanity::ReadJson(const std::string& path)
{
    auto text = ReadText(path);
    if (!text)
    {
        return Result<nlohmann::json>::Failure(text.Error());
    }

    nlohmann::json parsed;
    try
    {
        parsed = nlohmann::json::parse(text.Value());
    }
    catch (const std::exception& e)
    {
        return Result<nlohmann::json>::Failure(std::string("parse error: ") + e.what());
    }
    return Result<nlohmann::json>::Success(std::move(parsed));
}

Result<void> Vanity::WriteJson(const std::string& path, const nlohmann::json& data, int indent)
{
    return WriteText(path, data.dump(indent));
}

Result<void> Vanity::PromoteTemp(const std::string& tempPath, const std::string& path)
{
    if (!replaceFile(tempPath, path))
    {
        return Result<void>::Failure("cannot replace: " + path + " (new data kept in " + tempPath + ")");
    }
    return Result<void>::Success();
}

bool Vanity::Exists(const std::string& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

Result<void> Vanity::EnsureDirectory(const std::string& dir)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        return Result<void>::Failure("cannot create directory: " + dir + " (" + ec.message() + ")");
    }
    return Result<void>::Success();
}

Result<void> Vanity::Remove(const std::string& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec)
    {
        return Result<void>::Failure("cannot remove: " + path + " (" + ec.message() + ")");
    }
    return Result<void>::Success();
}

std::string Vanity::Join(const std::string& directory, const std::string& child)
{
    namespace fs = std::filesystem;
    return (fs::path(directory) / fs::path(child)).string();
}

std::string Vanity::CurrentDirectory()
{
    std::error_code ec;
    const std::filesystem::path current = std::filesystem::current_path(ec);
    return ec ? std::string{} : current.string();
}

std::string Vanity::AbsolutePath(const std::string& path)
{
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return ec ? path : absolute.string();
}

std::string Vanity::ParentPath(const std::string& path)
{
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    return parent.empty() ? path : parent.string();
}

bool Vanity::FindUpwards(const std::string& start, const std::string& relativeFile, std::string& out)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path directory(start);
    if (fs::is_regular_file(directory, ec))
    {
        directory = directory.parent_path();
    }
    if (directory.empty())
    {
        directory = CurrentDirectory();
    }

    for (int depth = 0; depth < 64 && !directory.empty(); ++depth)
    {
        const fs::path candidate = directory / fs::path(relativeFile);
        if (fs::exists(candidate, ec) && !ec)
        {
            out = candidate.string();
            return true;
        }
        const fs::path parent = directory.parent_path();
        if (parent.empty() || parent == directory)
        {
            break;
        }
        directory = parent;
    }
    return false;
}

bool Vanity::IsDirectoryWritable(const std::string& dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec) || ec)
    {
        return false;
    }

    // The probe has to be an actual create: on Windows the read-only attribute
    // and the ACL are two different questions, and only the second one matters
    // for a save file.
    const fs::path probe = fs::path(dir) / (".write_probe." + std::to_string(++g_tempCounter));
    {
        std::ofstream out(probe, std::ios::binary);
        if (!out)
        {
            return false;
        }
        out << '1';
        if (!out)
        {
            return false;
        }
    }
    fs::remove(probe, ec);
    return true;
}

std::string Vanity::UserDataDirectory(const std::string& appName)
{
    namespace fs = std::filesystem;
    const char* home = nullptr;
    fs::path base;

#if defined(_WIN32)
    home = std::getenv("LOCALAPPDATA");
    if (home == nullptr || *home == '\0')
    {
        home = std::getenv("APPDATA");
    }
    if (home == nullptr || *home == '\0')
    {
        return {};
    }
    base = fs::path(home);
#elif defined(__APPLE__)
    home = std::getenv("HOME");
    if (home == nullptr || *home == '\0')
    {
        return {};
    }
    base = fs::path(home) / "Library" / "Application Support";
#else
    home = std::getenv("XDG_DATA_HOME");
    if (home != nullptr && *home != '\0')
    {
        base = fs::path(home);
    }
    else
    {
        home = std::getenv("HOME");
        if (home == nullptr || *home == '\0')
        {
            return {};
        }
        base = fs::path(home) / ".local" / "share";
    }
#endif

    return (base / appName).string();
}

std::string Vanity::SanitizeFileName(const std::string& name, const std::string& fallback)
{
    std::string out;
    out.reserve(name.size());
    for (char c : name)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool alnum = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z');
        out.push_back((alnum || c == '_' || c == '-' || c == '.') ? c : '_');
    }
    // Never allow a name made purely of dots (".", ".."): they address parent
    // directories rather than a file inside the target directory.
    size_t first = out.find_first_not_of('.');
    out = (first == std::string::npos) ? std::string{} : out.substr(first);

    // Windows silently strips trailing dots and spaces, so "save." and "save"
    // would refer to the same file. Strip them ourselves for predictability.
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
    {
        out.pop_back();
    }

    // DOS device names are reserved everywhere, even with an extension
    // (CON.txt, NUL.json). Prefixing is enough to make them ordinary files.
    const size_t dot = out.find('.');
    std::string stem = out.substr(0, dot == std::string::npos ? out.size() : dot);
    std::transform(stem.begin(), stem.end(), stem.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    static const char* const kReserved[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                            "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                            "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* reserved : kReserved)
    {
        if (stem == reserved)
        {
            out = "_" + out;
            break;
        }
    }

    // Keep well below the classic 260-character MAX_PATH budget once a
    // directory prefix is added.
    constexpr size_t kMaxNameLength = 180;
    if (out.size() > kMaxNameLength)
    {
        const std::string extension = std::filesystem::path(out).extension().string();
        out.resize(kMaxNameLength);
        if (!extension.empty() && extension.size() < 16)
        {
            out.resize(kMaxNameLength - extension.size());
            out += extension;
        }
    }

    if (out.empty())
    {
        out = fallback;
    }
    return out;
}

bool Vanity::OpenURL(const std::string& url)
{
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)
    {
        return false;
    }

#if defined(_WIN32)
    const int length = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (length <= 0)
    {
        return false;
    }
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wide.data(), length);
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#elif defined(__APPLE__)
    const std::string command = "open \"" + url + "\" >/dev/null 2>&1";
    return std::system(command.c_str()) == 0;
#else
    const std::string command = "xdg-open \"" + url + "\" >/dev/null 2>&1";
    return std::system(command.c_str()) == 0;
#endif
}

} // namespace Fem
