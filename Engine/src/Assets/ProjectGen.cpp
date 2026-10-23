// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/ProjectGen.hpp"
#include "FemboyEngine/Boy.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace Fem
{

namespace
{
const char* kCopyright = "// Copyright (c) 2026 Razraboychik. All rights reserved.\n";

// Restricts a project name to characters that are safe in a directory name,
// a CMake target and a C++ identifier context.
std::string SanitizeProjectName(const std::string& name)
{
    std::string out;
    out.reserve(name.size());
    for (char c : name)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '_' || c == '-')
        {
            out.push_back(c);
        }
        else if (c == ' ')
        {
            out.push_back('_');
        }
    }
    if (out.empty())
    {
        out = "FemboyProject";
    }
    if (std::isdigit(static_cast<unsigned char>(out.front())))
    {
        out.insert(out.begin(), '_');
    }
    return out;
}
} // namespace

bool ProjectGenerator::WriteFile(const std::string& path, const std::string& content)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty())
    {
        fs::create_directories(parent, ec);
        if (ec)
        {
            BOY_ERROR("ProjectGen", "cannot create directory: " + parent.string());
            return false;
        }
    }

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open())
    {
        BOY_ERROR("ProjectGen", "failed: " + path);
        return false;
    }
    f << content;
    f.flush();
    if (!f)
    {
        BOY_ERROR("ProjectGen", "write failed: " + path);
        return false;
    }
    BOY_INFO("ProjectGen", "created: " + path);
    return true;
}

std::string ProjectGenerator::SanitizeName(const std::string& projectName)
{
    return SanitizeProjectName(projectName);
}

bool ProjectGenerator::Generate(const std::string& projectName, const std::string& outputDir)
{
    namespace fs = std::filesystem;

    const std::string name = SanitizeProjectName(projectName);
    if (name != projectName)
    {
        BOY_WARN("ProjectGen", "project name sanitized: '" + projectName + "' -> '" + name + "'");
    }

    const fs::path root = fs::path(outputDir) / name;
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec)
    {
        BOY_ERROR("ProjectGen", "cannot create project directory: " + root.string());
        return false;
    }

    bool ok = true;

    // Project config
    {
        nlohmann::json cfg;
        cfg["name"] = name;
        cfg["version"] = "0.1.0";
        cfg["engine"] = "FemboyEngine";
        ok = WriteFile((root / "project.femboy").string(), cfg.dump(4) + "\n") && ok;
    }

    // main.cpp
    const std::string mainCpp =
        std::string(kCopyright) +
        "#include \"FemboyEngine/DebugConsole.hpp\"\n"
        "#include \"FemboyEngine/Engine.hpp\"\n"
        "#include <SFML/Graphics.hpp>\n"
        "#include <optional>\n"
        "using namespace Fem;\n"
        "\n"
        "int main(int argc, char** argv)\n"
        "{\n"
        "    DebugConsole::AttachIfRequested(argc, argv, \"" +
        name +
        " - Debug Console\");\n"
        "    Engine& engine = Engine::Instance();\n"
        "    if (!engine.Initialize(\"" +
        name +
        "\", 1280, 720))\n"
        "    {\n"
        "        return 1;\n"
        "    }\n"
        "\n"
        "    BOY_INFO(\"Game\", \"Hello from " +
        name +
        "\");\n"
        "\n"
        "    while (engine.GetWindow().IsOpen())\n"
        "    {\n"
        "        while (const std::optional<sf::Event> event = engine.GetWindow().Handle().pollEvent())\n"
        "        {\n"
        "            if (event->is<sf::Event::Closed>())\n"
        "            {\n"
        "                engine.GetWindow().Handle().close();\n"
        "            }\n"
        "            Input::Instance().Update(*event);\n"
        "        }\n"
        "\n"
        "        // TODO: your game code here\n"
        "\n"
        "        engine.GetWindow().Clear();\n"
        "        engine.GetWindow().Display();\n"
        "    }\n"
        "\n"
        "    engine.Shutdown();\n"
        "    return 0;\n"
        "}\n";
    ok = WriteFile((root / "src" / "main.cpp").string(), mainCpp) && ok;

    // CMakeLists.txt
    const std::string cmake =
        "cmake_minimum_required(VERSION 3.21)\n"
        "project(" +
        name +
        " LANGUAGES CXX)\n"
        "\n"
        "set(CMAKE_CXX_STANDARD 20)\n"
        "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
        "\n"
        "# Generated by the editor's Project Settings > Build page.\n"
        "include(\"${CMAKE_CURRENT_SOURCE_DIR}/build.settings.cmake\" OPTIONAL)\n"
        "\n"
        "# Everything this project builds stays inside its own folder.\n"
        "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)\n"
        "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)\n"
        "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)\n"
        "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin)\n"
        "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin)\n"
        "\n"
        "add_executable(" +
        name +
        " src/main.cpp)\n"
        "\n"
        "# Resolve the engine checkout in this order:\n"
        "#   1) -DFEMBOY_ENGINE_ROOT=<engine checkout>\n"
        "#   2) the FEMBOY_ENGINE_ROOT environment variable\n"
        "#   3) a sibling ../FemboyEngine folder\n"
        "# That keeps the project movable: point at the engine on the new machine\n"
        "# and nothing else in this folder needs to change.\n"
        "if(NOT FEMBOY_ENGINE_ROOT AND DEFINED ENV{FEMBOY_ENGINE_ROOT})\n"
        "    set(FEMBOY_ENGINE_ROOT \"$ENV{FEMBOY_ENGINE_ROOT}\" CACHE PATH \"FemboyEngine root\")\n"
        "endif()\n"
        "if(NOT FEMBOY_ENGINE_ROOT)\n"
        "    foreach(_candidate \"${CMAKE_CURRENT_SOURCE_DIR}/../FemboyEngine\" \"${CMAKE_CURRENT_SOURCE_DIR}/../../FemboyEngine\")\n"
        "        if(EXISTS \"${_candidate}/CMakeLists.txt\")\n"
        "            set(FEMBOY_ENGINE_ROOT \"${_candidate}\" CACHE PATH \"FemboyEngine root\")\n"
        "            break()\n"
        "        endif()\n"
        "    endforeach()\n"
        "endif()\n"
        "if(NOT EXISTS \"${FEMBOY_ENGINE_ROOT}/CMakeLists.txt\")\n"
        "    message(FATAL_ERROR \"FEMBOY_ENGINE_ROOT must point at the FemboyEngine checkout (pass -DFEMBOY_ENGINE_ROOT=...)\")\n"
        "endif()\n"
        "\n"
        "# A game project builds the engine library and its game, not the editor\n"
        "# or the test suite.\n"
        "set(FEMBOY_BUILD_EDITOR OFF CACHE BOOL \"\" FORCE)\n"
        "set(FEMBOY_BUILD_TESTS OFF CACHE BOOL \"\" FORCE)\n"
        "set(FEMBOY_BUILD_NEONTRIANGLE OFF CACHE BOOL \"\" FORCE)\n"
        "add_subdirectory(\"${FEMBOY_ENGINE_ROOT}\" femboyengine-build)\n"
        "target_link_libraries(" +
        name +
        " PRIVATE femboyengine)\n"
        "if(FEMBOY_PROJECT_COMPILE_FLAGS)\n"
        "    target_compile_options(" +
        name +
        " PRIVATE ${FEMBOY_PROJECT_COMPILE_FLAGS})\n"
        "endif()\n"
        "if(FEMBOY_PROJECT_LINK_FLAGS)\n"
        "    target_link_options(" +
        name +
        " PRIVATE ${FEMBOY_PROJECT_LINK_FLAGS})\n"
        "endif()\n"
        "\n"
        "# Windows GUI subsystem and the SFML runtime DLLs next to the game.\n"
        "if(WIN32)\n"
        "    find_package(SFML 3 COMPONENTS Main QUIET)\n"
        "    if(TARGET SFML::Main)\n"
        "        set_target_properties(" +
        name +
        " PROPERTIES WIN32_EXECUTABLE TRUE)\n"
        "        target_link_libraries(" +
        name +
        " PRIVATE SFML::Main)\n"
        "    endif()\n"
        "    if(NOT FEMBOY_STATIC_SFML AND FEMBOY_SFML_ROOT)\n"
        "        foreach(_dll sfml-audio-3 sfml-graphics-3 sfml-system-3 sfml-window-3)\n"
        "            add_custom_command(TARGET " +
        name +
        " POST_BUILD\n"
        "                COMMAND ${CMAKE_COMMAND} -E copy_if_different\n"
        "                    \"${FEMBOY_SFML_ROOT}/bin/${_dll}.dll\" \"$<TARGET_FILE_DIR:" +
        name +
        ">\")\n"
        "        endforeach()\n"
        "    endif()\n"
        "endif()\n"
        "\n"
        "# Runtime data travels with the executable, so the whole project folder\n"
        "# can be copied to another machine as-is.\n"
        "if(EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/assets\")\n"
        "    add_custom_command(TARGET " +
        name +
        " POST_BUILD\n"
        "        COMMAND ${CMAKE_COMMAND} -E copy_directory\n"
        "            \"${CMAKE_CURRENT_SOURCE_DIR}/assets\" \"$<TARGET_FILE_DIR:" +
        name +
        ">/assets\")\n"
        "endif()\n";
    ok = WriteFile((root / "CMakeLists.txt").string(), cmake) && ok;

    // config.json
    {
        nlohmann::json cfg;
        cfg["title"] = name;
        cfg["width"] = 1280;
        cfg["height"] = 720;
        cfg["fullscreen"] = false;
        ok = WriteFile((root / "config.json").string(), cfg.dump(4) + "\n") && ok;
    }

    // Scripts, assets and the runtime manifest belong to the project. They are
    // created here instead of being discovered relative to the editor build.
    ok = WriteFile((root / "scripts" / "main.femboy").string(),
                   "void OnStart()\n{\n    print(\"" + name + " ready\");\n}\n") &&
         ok;
    {
        nlohmann::json manifest;
        manifest["version"] = 1;
        manifest["assets"] = nlohmann::json::object();
        ok = WriteFile((root / "assets" / "manifest.json").string(), manifest.dump(4) + "\n") && ok;
    }
    ok = WriteFile((root / "assets" / "README.md").string(),
                   "# Assets\n\nEverything the game loads at runtime lives here. The editor's\n"
                   "Asset Browser and Asset Pipeline read this folder relative to\n"
                   "`project.femboy`, so a project can be moved without editing paths.\n") &&
         ok;
    ok = WriteFile((root / ".gitignore").string(), "/build/\n/bin/\n*.user\n") && ok;
    ok = WriteFile((root / "README.md").string(),
                   "# " + name +
                       "\n\nA FemboyEngine project. All source, config, scripts and\n"
                       "assets are inside this folder, so it can be moved or imported as\n"
                       "a unit.\n\n## Build\n\n```bat\n"
                       "cmake -S . -B build -DFEMBOY_ENGINE_ROOT=<path to FemboyEngine>\n"
                       "cmake --build build\nbuild\\bin\\" +
                       name + ".exe\n```\n") &&
         ok;

    if (ok)
    {
        BOY_INFO("ProjectGen", "project created: " + root.string());
    }
    else
    {
        BOY_ERROR("ProjectGen", "project creation incomplete: " + root.string());
    }
    return ok;
}

std::vector<std::string> ProjectGenerator::TemplateFiles()
{
    return {"project.femboy", "src/main.cpp", "CMakeLists.txt", "config.json", "scripts/main.femboy",
            "assets/manifest.json", "assets/README.md", ".gitignore", "README.md"};
}

} // namespace Fem
