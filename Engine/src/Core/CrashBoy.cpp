// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/CrashBoy.hpp"
#include "FemboyEngine/Boy.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <cstdio>
#include <csignal>
#include <cstdint>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Fem
{

namespace
{
// Crash-time state. A crash can happen while the logging mutex is held, so the
// handler must never take it. An atomic flag makes the report path lock-free and
// guarantees a single file even if several threads fault at once.
std::atomic_flag g_crashReporting = ATOMIC_FLAG_INIT;
} // namespace

#ifdef _WIN32
namespace
{
LONG WINAPI FemBoyCrashHandler(EXCEPTION_POINTERS* ep)
{
    if (g_crashReporting.test_and_set(std::memory_order_acq_rel))
    {
        // Another thread is already writing the report; do not deadlock.
        return EXCEPTION_EXECUTE_HANDLER;
    }

    char buffer[256];
    const int n = std::snprintf(buffer, sizeof(buffer), "code=0x%08lX addr=0x%p",
                                static_cast<unsigned long>(ep->ExceptionRecord->ExceptionCode),
                                ep->ExceptionRecord->ExceptionAddress);
    const std::string detail = (n > 0) ? std::string(buffer, static_cast<size_t>(std::min<int>(n, sizeof(buffer) - 1)))
                                       : std::string("unknown");

    // report() avoids the log mutex while a crash is in progress.
    CrashBoy::Instance().Report("Unhandled exception", detail);
    return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace
#endif

CrashBoy& CrashBoy::Instance()
{
    static CrashBoy c;
    return c;
}

void CrashBoy::Initialize(const std::string& outputDir)
{
    {
        std::lock_guard<std::mutex> lk(fileMtx_);
        outputDir_ = outputDir;
    }
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    InstallHandlers();
    BOY_INFO("CrashBoy", "initialized: " + outputDir);
}

void CrashBoy::InstallHandlers()
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(FemBoyCrashHandler);
    BOY_INFO("CrashBoy", "handlers installed (Windows): SetUnhandledExceptionFilter active");
#else
    std::signal(SIGSEGV, [](int) { CrashBoy::Instance().Report("SIGSEGV", "signal"); });
    std::signal(SIGABRT, [](int) { CrashBoy::Instance().Report("SIGABRT", "signal"); });
    BOY_INFO("CrashBoy", "handlers installed (Unix): signals active");
#endif
}

void CrashBoy::SetCallback(std::function<void(const std::string&)> cb)
{
    std::lock_guard<std::mutex> lk(mtx_);
    callback_ = std::move(cb);
}

void CrashBoy::Report(const std::string& message, const std::string& stackTrace)
{
    // Never log through Boy while unwinding a crash: the fault may have happened
    // while the log mutex was held, which would deadlock the handler. Console
    // output is also unsafe here, so the crash file is the source of truth.
    WriteCrashFile(message, stackTrace);

    std::function<void(const std::string&)> cb;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cb = callback_;
    }
    if (cb)
    {
        cb(message);
    }
}

void CrashBoy::WriteCrashFile(const std::string& message, const std::string& stackTrace)
{
    namespace fs = std::filesystem;
    std::string dir;
    unsigned seq;
    {
        std::lock_guard<std::mutex> lk(fileMtx_);
        dir = outputDir_;
        seq = counter_++;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);

    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    std::ostringstream name;
    name << dir << "/crash_" << buf << "_" << std::setw(3) << std::setfill('0') << ms << "_" << seq << ".log";
    const std::string path = name.str();

    std::ofstream f(path);
    if (!f.is_open())
    {
        // This path can run from a crash handler, so it must not touch the
        // logging subsystem (Boy's mutex may be the one that was held when the
        // process faulted). Best-effort output to stderr instead.
        std::fputs("[CrashBoy] cannot write crash file\n", stderr);
        return;
    }
    f << "=== FemboyEngine Crash Report ===\n";
    f << "Time: " << buf << "." << ms << "\n";
    f << "Message: " << message << "\n";
    f << "StackTrace:\n" << stackTrace << "\n";
    f << "=== End ===\n";
    f.flush();
    const bool ok = (bool)f;
    f.close();
    if (!ok)
    {
        std::fputs("[CrashBoy] crash file may be incomplete\n", stderr);
    }
    {
        std::lock_guard<std::mutex> lk(fileMtx_);
        crashFiles_.push_back(path);
    }
}

std::vector<std::string> CrashBoy::CrashFiles() const
{
    std::lock_guard<std::mutex> lk(fileMtx_);
    return crashFiles_;
}

} // namespace Fem
