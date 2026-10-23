// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include "FemboyEngine/Result.hpp"

namespace Fem
{

// Vanity: the engine's file layer.
//
// Everything that persists user data (scenes, prefabs, settings, saves, mods)
// goes through here so the rules live in exactly one place:
//   * writes are atomic (temp file + rename) - an interrupted write can never
//     truncate the file that is already on disk;
//   * reads are validated (missing file / bad JSON / wrong root type are
//     reported as errors instead of throwing from json::get<>);
//   * file names coming from user data are sanitised so they cannot escape
//     their directory through "..", separators or drive letters.
class Vanity
{
public:
    // Whole-file I/O
    static Result<std::string> ReadText(const std::string& path);
    static Result<void> WriteText(const std::string& path, const std::string& text);
    // Binary-safe atomic write for generated audio/images/manifests. Kept
    // separate from WriteText so callers do not have to pretend bytes are text.
    static Result<void> WriteBytes(const std::string& path, const void* data, std::size_t size);

    static Result<nlohmann::json> ReadJson(const std::string& path);
    static Result<void> WriteJson(const std::string& path, const nlohmann::json& data, int indent = 4);
    // Atomically replaces `path` with a file the caller already wrote (used by
    // binary writers such as resource packs that stream their payload).
    static Result<void> PromoteTemp(const std::string& tempPath, const std::string& path);

    // Filesystem helpers
    static bool Exists(const std::string& path);
    static Result<void> EnsureDirectory(const std::string& dir);
    static Result<void> Remove(const std::string& path);
    static std::string Join(const std::string& directory, const std::string& child);
    static std::string CurrentDirectory();
    static std::string AbsolutePath(const std::string& path);
    static std::string ParentPath(const std::string& path);
    // Walks from `start` (or its containing directory) up to the filesystem
    // root looking for `relativeFile`. `out` receives the complete file path.
    static bool FindUpwards(const std::string& start, const std::string& relativeFile, std::string& out);

    // True when a file can actually be created in `dir`. A game installed under
    // Program Files has a perfectly readable content directory that cannot hold
    // saves, and discovering that by watching writes fail is too late.
    static bool IsDirectoryWritable(const std::string& dir);

    // The per-user data directory for an application: %LOCALAPPDATA% on Windows,
    // ~/Library/Application Support on macOS, $XDG_DATA_HOME or ~/.local/share
    // elsewhere. Returns "" when the environment has nothing to offer, in which
    // case the caller should stay where it is.
    static std::string UserDataDirectory(const std::string& appName);

    // Keeps [A-Za-z0-9._-], replaces everything else with '_', strips leading
    // dots (so "." and ".." cannot survive) and falls back when nothing is
    // left. Path separators can never pass through, which is what makes a
    // name from user data safe to join with a directory.
    static std::string SanitizeFileName(const std::string& name, const std::string& fallback = "unnamed");

    // Opens an http(s) URL in the user's default browser. Returns false when
    // the platform could not launch it. Only web URLs are accepted: this is a
    // UI helper, not a general shell launcher.
    static bool OpenURL(const std::string& url);
};

} // namespace Fem
