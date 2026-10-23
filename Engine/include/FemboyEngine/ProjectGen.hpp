// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <vector>

namespace Fem
{

// Generates a new project skeleton with copyright headers.
class ProjectGenerator
{
public:
    // Creates <outputDir>/<projectName> with a working CMake project.
    // The project name is sanitized: only [A-Za-z0-9_-] survive.
    static bool Generate(const std::string& projectName, const std::string& outputDir);
    // Exposed so callers can predict the folder Generate() will create.
    static std::string SanitizeName(const std::string& projectName);
    static std::vector<std::string> TemplateFiles();

private:
    static bool WriteFile(const std::string& path, const std::string& content);
};

} // namespace Fem
