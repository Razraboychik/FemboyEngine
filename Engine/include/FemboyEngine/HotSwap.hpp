// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <filesystem>

namespace Fem
{

// File watcher for hot-reloading assets. Files are never silently dropped:
// a transient stat error or a temporary deletion keeps the watch alive so the
// file is picked up again once it is accessible.
class HotSwap
{
public:
    static HotSwap& Instance();
    void Watch(const std::string& path);
    void Unwatch(const std::string& path);
    void Poll(); // call each frame
    void SetCallback(std::function<void(const std::string&)> cb);
    std::vector<std::string> ChangedFiles() const;
    void ClearChanged();

private:
    HotSwap() = default;
    struct WatchedFile
    {
        std::string path;
        std::filesystem::file_time_type lastWrite{};
        bool missing = false;
    };
    mutable std::mutex mtx_;
    std::vector<WatchedFile> watched_;
    std::vector<std::string> changed_;
    std::function<void(const std::string&)> callback_;
};

} // namespace Fem
