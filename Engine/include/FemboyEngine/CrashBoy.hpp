// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include "FemboyEngine/Core.hpp"

namespace Fem
{

// Crash reporter: captures stack traces, logs and system info on crash.
class CrashBoy
{
public:
    static CrashBoy& Instance();
    void Initialize(const std::string& outputDir);
    void SetCallback(std::function<void(const std::string&)> cb);
    void Report(const std::string& message, const std::string& stackTrace = "");
    void WriteCrashFile(const std::string& message, const std::string& stackTrace);
    std::vector<std::string> CrashFiles() const;
    // Install platform crash handlers (Windows SEH / POSIX signals).
    void InstallHandlers();

private:
    CrashBoy() = default;
    mutable std::mutex mtx_;
    // Separate lock for crash-file bookkeeping so the fatal path never contends
    // with the callback mutex.
    mutable std::mutex fileMtx_;
    std::string outputDir_ = "crashes";
    std::vector<std::string> crashFiles_;
    std::function<void(const std::string&)> callback_;
    unsigned counter_ = 0;
};

} // namespace Fem
