// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
//
// FemboyEditor: ImGui shell for the engine.
//
// The launcher, scene hierarchy, metadata-driven inspector, ClassDefaults
// panel, live viewport, FemboyScript console, terminal, Git panel, asset
// pipeline and canvas-based visual-script editor all operate on engine data
// directly. Scene and asset panels use the same Registry/AssetManager the game
// uses, so a scene saved here loads without translation.
#define _CRT_SECURE_NO_WARNINGS
#include <imgui-SFML.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <SFML/Graphics.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "FemboyEngine/Accessibility.hpp"
#include "FemboyEngine/AssetPipeline.hpp"
#include "FemboyEngine/Assets.hpp"
#include "FemboyEngine/Camera2D.hpp"
#include "FemboyEngine/ClassDefaults.hpp"
#include "FemboyEngine/DebugConsole.hpp"
#include "FemboyEngine/FemboyScript.hpp"
#include "FemboyEngine/Mirror.hpp"
#include "FemboyEngine/Prefab.hpp"
#include "FemboyEngine/Profiler.hpp"
#include "FemboyEngine/ProjectGen.hpp"
#include "FemboyEngine/Scene.hpp"
#include "FemboyEngine/UndoRedo.hpp"
#include "FemboyEngine/Vanity.hpp"
#include "FemboyEngine/VisualScript.hpp"
#include "FemboyEngine/VoiceChat.hpp"
#ifdef FEMBOY_HAS_IMPLOT
#include <implot.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using Fem::Entity;
using Fem::TagComponent;
using Fem::TransformComponent;

namespace
{
struct Panel
{
    const char* name;
    bool open = true;
    const char* hint;
    bool defaultOpen = true;
};

// Bump when the default dock arrangement changes. The editor applies the new
// layout once per user, then respects whatever arrangement they make after it.
constexpr int kEditorLayoutVersion = 5;

// Top-level workspace tabs. Documents use their index in
// VisualScriptWorkspace::documents; negative ids are fixed editor pages.
constexpr int kTopTabEditor = -3;
constexpr int kTopTabProjectSettings = -2;
constexpr int kTopTabEditorSettings = -1;

bool g_editorTooltips = true;

void EditorTooltip(const char* format, ...)
{
    if (!g_editorTooltips)
    {
        return;
    }
    va_list args;
    va_start(args, format);
    ImGui::SetItemTooltipV(format, args);
    va_end(args);
}

std::string KeyChordName(int chord)
{
    std::string name;
    if ((chord & ImGuiMod_Ctrl) != 0)
    {
        name += "Ctrl+";
    }
    if ((chord & ImGuiMod_Shift) != 0)
    {
        name += "Shift+";
    }
    if ((chord & ImGuiMod_Alt) != 0)
    {
        name += "Alt+";
    }
    if ((chord & ImGuiMod_Super) != 0)
    {
        name += "Super+";
    }
    const ImGuiKey key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
    name += ImGui::GetKeyName(key);
    return name;
}

bool IsBindingPressed(int chord)
{
    return ImGui::IsKeyChordPressed(chord);
}

// DPI-aware base scale. SFML does not expose the monitor scale, so on Windows
// use the per-window DPI (falling back to the system DPI) and keep 96 DPI as
// the 1.0 reference. Other platforms stay at 1.0 and rely on the user zoom.
float DetectDpiScale(sf::Window& window)
{
#ifdef _WIN32
    const HWND handle = static_cast<HWND>(window.getNativeHandle());
    if (handle)
    {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        static const GetDpiForWindowFn getDpiForWindow = []() -> GetDpiForWindowFn {
            const HMODULE user32 = GetModuleHandleW(L"user32.dll");
            return user32 ? reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"))
                          : nullptr;
        }();
        if (getDpiForWindow)
        {
            const UINT dpi = getDpiForWindow(handle);
            if (dpi >= 96)
            {
                return static_cast<float>(dpi) / 96.f;
            }
        }

        HDC deviceContext = GetDC(handle);
        if (deviceContext)
        {
            const int dpi = GetDeviceCaps(deviceContext, LOGPIXELSX);
            ReleaseDC(handle, deviceContext);
            if (dpi >= 96)
            {
                return static_cast<float>(dpi) / 96.f;
            }
        }
    }
#else
    (void)window;
#endif
    return 1.f;
}

// Re-applies a user/DPI scale to the untouched ImGui style. Scaling from the
// base style (instead of scaling the current style) keeps repeated zoom
// changes from compounding paddings and font sizes.
void ApplyEditorScale(const ImGuiStyle& baseStyle, float scale)
{
    ImGuiStyle& style = ImGui::GetStyle();
    style = baseStyle;
    style.ScaleAllSizes(scale);
    style.FontScaleMain = scale;
}

// The built-in ImGui font is a bitmap font: at a 1.5x DPI factor it becomes a
// chunky pixel grid. The editor prefers a real UI font so text stays smooth at
// every scale. If none of the usual system fonts exist the bitmap font is kept
// as a last resort.
std::string FindEditorFont(const std::filesystem::path& engineRoot)
{
    std::vector<std::filesystem::path> candidates;
    candidates.push_back(engineRoot / "Data" / "Fonts" / "NotoSans-Regular.ttf");
    candidates.push_back(std::filesystem::current_path() / "Data" / "Fonts" / "NotoSans-Regular.ttf");
#ifdef _WIN32
    if (const char* windir = std::getenv("WINDIR"))
    {
        const std::filesystem::path fonts = std::filesystem::path(windir) / "Fonts";
        candidates.push_back(fonts / "segoeui.ttf");
        candidates.push_back(fonts / "tahoma.ttf");
        candidates.push_back(fonts / "arial.ttf");
    }
    if (const char* local = std::getenv("LOCALAPPDATA"))
    {
        candidates.push_back(std::filesystem::path(local) / "Microsoft" / "Windows" / "Fonts" / "segoeui.ttf");
    }
#elif defined(__APPLE__)
    candidates.push_back("/System/Library/Fonts/SFNS.ttf");
    candidates.push_back("/System/Library/Fonts/Helvetica.ttc");
    candidates.push_back("/System/Library/Fonts/Supplemental/Arial.ttf");
#else
    candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    candidates.push_back("/usr/share/fonts/TTF/DejaVuSans.ttf");
    candidates.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    candidates.push_back("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf");
#endif
    for (const std::filesystem::path& candidate : candidates)
    {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec))
        {
            return candidate.string();
        }
    }
    return {};
}

void LoadEditorFont(ImGuiIO& io, float dpiScale, const std::filesystem::path& engineRoot)
{
    io.Fonts->Clear();
    const std::string path = FindEditorFont(engineRoot);
    if (!path.empty())
    {
        ImFontConfig config;
        config.OversampleH = 2;
        config.OversampleV = 1;
        config.GlyphRanges = io.Fonts->GetGlyphRangesCyrillic();
        // Bake at the physical size used by the initial DPI scale. The
        // imgui-sfml backend uploads one atlas texture, so this keeps the
        // first frame crisp instead of upscaling a 13px atlas.
        if (io.Fonts->AddFontFromFileTTF(path.c_str(), 16.f * dpiScale, &config) != nullptr)
        {
            (void)ImGui::SFML::UpdateFontTexture();
            return;
        }
    }
    io.Fonts->AddFontDefault();
    (void)ImGui::SFML::UpdateFontTexture();
}

void ApplyEditorTheme(ImGuiStyle& style)
{
    const ImVec4 accent = ImVec4(0.33f, 0.58f, 0.95f, 1.f);
    const ImVec4 accentHovered = ImVec4(0.43f, 0.66f, 1.f, 1.f);
    const ImVec4 accentActive = ImVec4(0.56f, 0.76f, 1.f, 1.f);
    auto alpha = [](ImVec4 color, float a)
    {
        color.w = a;
        return color;
    };

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.91f, 0.91f, 0.94f, 1.f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.55f, 0.62f, 1.f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.075f, 0.09f, 1.f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.095f, 0.095f, 0.115f, 1.f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.085f, 0.085f, 0.105f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.19f, 0.19f, 0.23f, 1.f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.f, 0.f, 0.f, 0.f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.145f, 0.145f, 0.175f, 1.f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.245f, 1.f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.26f, 0.31f, 1.f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.075f, 0.075f, 0.09f, 1.f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.145f, 1.f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.075f, 0.075f, 0.09f, 1.f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.075f, 0.075f, 0.09f, 1.f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.075f, 0.075f, 0.09f, 1.f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.28f, 0.34f, 1.f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36f, 0.36f, 0.43f, 1.f);
    colors[ImGuiCol_ScrollbarGrabActive] = accentHovered;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_CheckboxSelectedBg] = alpha(accent, 0.35f);
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentActive;
    colors[ImGuiCol_Button] = ImVec4(0.18f, 0.18f, 0.225f, 1.f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.25f, 0.31f, 1.f);
    colors[ImGuiCol_ButtonActive] = alpha(accent, 0.65f);
    colors[ImGuiCol_Header] = alpha(accent, 0.30f);
    colors[ImGuiCol_HeaderHovered] = alpha(accent, 0.42f);
    colors[ImGuiCol_HeaderActive] = alpha(accent, 0.55f);
    colors[ImGuiCol_Separator] = ImVec4(0.19f, 0.19f, 0.23f, 1.f);
    colors[ImGuiCol_SeparatorHovered] = alpha(accent, 0.70f);
    colors[ImGuiCol_SeparatorActive] = accentActive;
    colors[ImGuiCol_ResizeGrip] = alpha(accent, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = alpha(accent, 0.50f);
    colors[ImGuiCol_ResizeGripActive] = alpha(accent, 0.75f);
    colors[ImGuiCol_InputTextCursor] = accentActive;
    colors[ImGuiCol_TabHovered] = alpha(accent, 0.50f);
    colors[ImGuiCol_Tab] = ImVec4(0.11f, 0.11f, 0.135f, 1.f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.18f, 0.18f, 0.225f, 1.f);
    colors[ImGuiCol_TabSelectedOverline] = accent;
    colors[ImGuiCol_TabDimmed] = ImVec4(0.09f, 0.09f, 0.11f, 1.f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.13f, 0.13f, 0.16f, 1.f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = alpha(accent, 0.50f);
    colors[ImGuiCol_DockingPreview] = alpha(accent, 0.35f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.055f, 0.055f, 0.065f, 1.f);
    colors[ImGuiCol_PlotLines] = accent;
    colors[ImGuiCol_PlotLinesHovered] = accentHovered;
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_PlotHistogramHovered] = accentHovered;
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.14f, 0.14f, 0.17f, 1.f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.23f, 0.23f, 0.28f, 1.f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.17f, 0.17f, 0.20f, 1.f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.f, 1.f, 1.f, 0.035f);
    colors[ImGuiCol_TextLink] = accentHovered;
    colors[ImGuiCol_TextSelectedBg] = alpha(accent, 0.35f);
    colors[ImGuiCol_TreeLines] = ImVec4(0.26f, 0.26f, 0.31f, 1.f);
    colors[ImGuiCol_DragDropTarget] = accentActive;
    colors[ImGuiCol_DragDropTargetBg] = alpha(accent, 0.25f);
    colors[ImGuiCol_UnsavedMarker] = ImVec4(0.98f, 0.58f, 0.32f, 1.f);
    colors[ImGuiCol_NavCursor] = accentActive;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.f, 1.f, 1.f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.55f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.60f);

    style.WindowPadding = ImVec2(10.f, 9.f);
    style.FramePadding = ImVec2(8.f, 4.f);
    style.CellPadding = ImVec2(5.f, 4.f);
    style.ItemSpacing = ImVec2(8.f, 6.f);
    style.ItemInnerSpacing = ImVec2(6.f, 4.f);
    style.IndentSpacing = 20.f;
    style.ScrollbarSize = 12.f;
    style.GrabMinSize = 10.f;
    style.WindowBorderSize = 1.f;
    style.FrameBorderSize = 0.f;
    style.PopupBorderSize = 1.f;
    style.WindowRounding = 6.f;
    style.FrameRounding = 4.f;
    style.PopupRounding = 4.f;
    style.ScrollbarRounding = 6.f;
    style.GrabRounding = 4.f;
    style.TabRounding = 5.f;
    style.DockingSeparatorSize = 2.f;
    style.TabBarBorderSize = 1.f;
    style.TabBarOverlineSize = 2.f;
    style.WindowTitleAlign = ImVec2(0.f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.f, 0.5f);
    style.DisabledAlpha = 0.45f;
}

// Creates the editor's initial dock layout. Runs only when no dockspace node
// was restored from imgui.ini, so a user's own arrangement is never replaced.
void BuildDefaultEditorLayout(ImGuiID dockspaceId, ImVec2 size)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, size);

    ImGuiID center = dockspaceId;
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);
    const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.36f, nullptr, &right);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.32f, nullptr, &center);

    auto dock = [](std::initializer_list<const char*> names, ImGuiID node)
    {
        for (const char* name : names)
        {
            ImGui::DockBuilderDockWindow(name, node);
        }
    };

    dock({"Hierarchy"}, left);
    dock({"Inspector"}, right);
    dock({"Class Defaults", "Asset Pipeline", "Mod Manager", "Localization", "Voice Chat"}, rightBottom);
    dock({"Viewport", "Pink UI Builder"}, center);
    dock({"Asset Browser", "FemboyScript Console", "Terminal", "Git", "Boy Logs", "Profiler", "Project Launcher",
          "Setup & Diagnostics"},
         bottom);

    ImGui::DockBuilderFinish(dockspaceId);
}

// Runs a command through the platform shell and returns its combined output.
// The editor is a local developer tool; it does not try to sandbox what the
// developer types into the terminal (that would be a lie), but the Git panel
// sanitises its message and paths so a commit cannot turn into a second
// command by accident.
std::string RunProcess(const std::string& command)
{
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe)
    {
        return "(failed to run command)";
    }

    std::string output;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe))
    {
        output += buffer;
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return output;
}

// Quotes a path for a shell command line.
std::string QuotePath(const std::string& path)
{
    return "\"" + path + "\"";
}

// Keeps a commit subject from containing characters that cmd/PowerShell would
// read as command separators. The result is still a readable message.
std::string SanitizeCommitMessage(const std::string& message)
{
    std::string out;
    out.reserve(message.size());
    for (char ch : message)
    {
        if (ch == '"' || ch == '&' || ch == '|' || ch == '<' || ch == '>' || ch == '^' || ch == '%')
        {
            out.push_back(' ');
        }
        else
        {
            out.push_back(ch);
        }
    }
    return out;
}

struct ProjectState
{
    char path[512] = "";
    std::vector<std::string> recent;
    std::string status = "Create a new project or import an existing project.femboy.";
};

struct ConsoleState
{
    char input[4096] = "int visits = 0;\nvoid OnStart()\n{\n    print(\"hello from FemboyScript\");\n}";
    std::vector<std::string> output;
    Fem::FemboyScript script;
    int budget = 500000;

    ConsoleState()
    {
        script.SetPrintHandler(
            [this](const std::string& line)
            {
                output.push_back(line);
            });
    }
};

struct TerminalState
{
    char command[512] = "";
    std::vector<std::string> output;
};

struct GitState
{
    char path[512] = "";
    char message[256] = "Editor commit";
    std::vector<std::string> output;
};

struct AssetPipelineState
{
    char manifest[512] = "Projects/NeonTriangle/assets/manifest.json";
    char output[512] = "Projects/NeonTriangle/assets/cooked";
    Fem::AssetDatabase database;
    std::vector<std::string> stale;
    std::string status = "Load a manifest to validate and cook it.";
};

struct PlayState
{
    bool running = false;
    bool rebuild = true;
    std::string status = "Play: ready.";
    std::string executable;
#ifdef _WIN32
    PROCESS_INFORMATION process{};
#endif
};

std::string FindCMakeExecutable(const std::filesystem::path& engineRoot)
{
    std::ifstream cache(engineRoot / "build" / "CMakeCache.txt");
    std::string line;
    while (std::getline(cache, line))
    {
        constexpr const char* prefix = "CMAKE_COMMAND:INTERNAL=";
        if (line.rfind(prefix, 0) == 0)
        {
            return line.substr(std::strlen(prefix));
        }
    }
    return "cmake";
}

std::filesystem::path FindGameExecutable(const std::filesystem::path& projectRoot,
                                         const std::filesystem::path& engineRoot, const std::string& name)
{
    std::error_code ec;
    const char* extensions[] = {"", ".exe"};
    std::vector<std::filesystem::path> roots = {projectRoot / "bin", projectRoot / "build" / "bin",
                                                 projectRoot / "build", engineRoot / "build"};
    for (const std::filesystem::path& root : roots)
    {
        for (const char* extension : extensions)
        {
            const std::filesystem::path candidate = root / (name + extension);
            if (std::filesystem::exists(candidate, ec))
            {
                return candidate;
            }
        }
    }
    return {};
}

void StopGame(PlayState& play)
{
#ifdef _WIN32
    if (play.running && play.process.hProcess != nullptr)
    {
        TerminateProcess(play.process.hProcess, 0);
        WaitForSingleObject(play.process.hProcess, 1500);
        CloseHandle(play.process.hThread);
        CloseHandle(play.process.hProcess);
        play.process = {};
    }
#endif
    play.running = false;
    play.status = "Game stopped.";
}

bool BuildAndLaunchGame(PlayState& play, const std::filesystem::path& projectRoot,
                        const std::filesystem::path& engineRoot, const std::string& projectName,
                        bool standalone)
{
    std::error_code ec;
    const std::string cmake = FindCMakeExecutable(engineRoot);
    std::filesystem::path executable = FindGameExecutable(projectRoot, engineRoot, projectName);

    if (play.rebuild)
    {
        std::string buildCommand;
        if (std::filesystem::exists(projectRoot / "build" / "CMakeCache.txt", ec))
        {
            // Preferred: the project's own build tree. This is what makes a
            // project portable between machines.
            buildCommand = QuotePath(cmake) + " --build " + QuotePath((projectRoot / "build").string());
        }
        else if (projectName == "NeonTriangle")
        {
            buildCommand = QuotePath(cmake) + " --build " + QuotePath((engineRoot / "build").string()) +
                           " --target NeonTriangle";
        }
        if (!buildCommand.empty())
        {
            play.status = "Building " + projectName + "...";
            const std::string output = RunProcess(buildCommand);
            executable = FindGameExecutable(projectRoot, engineRoot, projectName);
            if (executable.empty())
            {
                play.status = "Build produced no executable.\n" + output;
                return false;
            }
            play.status = "Build finished. Launching...";
        }
    }

    if (executable.empty())
    {
        play.status = "No game executable found. Build the project first.";
        return false;
    }

    StopGame(play);
#ifdef _WIN32
    std::wstring commandLine = L"\"" + executable.wstring() + L"\"";
    if (!standalone)
    {
        commandLine += L" --pie";
    }
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring workingDirectory = projectRoot.wstring();
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        workingDirectory.c_str(), &startup, &process))
    {
        play.status = "Could not start the game executable.";
        return false;
    }
    play.process = process;
    play.running = true;
    play.executable = executable.string();
    play.status = (standalone ? "Standalone game running." : "PIE game window running.");
    return true;
#else
    const std::string command = QuotePath(executable.string()) + (standalone ? "" : " --pie") + " &";
    if (std::system(command.c_str()) != 0)
    {
        play.status = "Could not start the game executable.";
        return false;
    }
    play.running = true;
    play.executable = executable.string();
    play.status = standalone ? "Standalone game running." : "PIE game window running.";
    return true;
#endif
}

struct AssetEntry
{
    std::filesystem::path path;
    std::string name;
    bool directory = false;
};

struct AssetBrowserState
{
    std::filesystem::path root;
    std::filesystem::path current;
    std::vector<std::filesystem::path> back;
    std::vector<std::filesystem::path> forward;
    std::vector<AssetEntry> entries;
    double lastRefresh = -1000.0;
    char pathBuffer[1024] = "";
    char createName[128] = "NewAsset";
    int createType = 0;
    bool createPopup = false;
    std::vector<std::filesystem::path> deleteQueue;
    std::filesystem::path selectedPath;
    std::filesystem::path previewLoadedPath;
    sf::Texture previewTexture;
    std::string status;
};

void RefreshAssetBrowser(AssetBrowserState& browser)
{
    browser.entries.clear();
    browser.lastRefresh = ImGui::GetTime();
    std::error_code ec;
    if (browser.current.empty() || !std::filesystem::exists(browser.current, ec))
    {
        return;
    }
    for (std::filesystem::directory_iterator it(browser.current, ec), end; it != end && !ec; it.increment(ec))
    {
        const std::filesystem::path& path = it->path();
        AssetEntry entry;
        entry.path = path;
        entry.name = path.filename().string();
        entry.directory = it->is_directory(ec);
        browser.entries.push_back(std::move(entry));
    }
    std::sort(browser.entries.begin(), browser.entries.end(), [](const AssetEntry& a, const AssetEntry& b)
              {
                  if (a.directory != b.directory)
                  {
                      return a.directory > b.directory;
                  }
                  return a.name < b.name;
              });
}

void NavigateAssetBrowser(AssetBrowserState& browser, const std::filesystem::path& target, bool remember = true)
{
    std::error_code ec;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(target, ec);
    if (ec || !std::filesystem::is_directory(normalized, ec))
    {
        return;
    }
    const std::filesystem::path root = std::filesystem::weakly_canonical(browser.root, ec);
    const std::filesystem::path relative = normalized.lexically_relative(root);
    if (!ec && (relative.empty() || (!relative.string().starts_with("..") && relative != ".")))
    {
        if (remember && !browser.current.empty())
        {
            browser.back.push_back(browser.current);
            browser.forward.clear();
        }
        browser.current = normalized;
        std::snprintf(browser.pathBuffer, sizeof(browser.pathBuffer), "%s", normalized.string().c_str());
        RefreshAssetBrowser(browser);
    }
}

const char* AssetIcon(const AssetEntry& entry)
{
    if (entry.directory)
    {
        return "[D]";
    }
    const std::string extension = entry.path.extension().string();
    if (extension == ".scene")
    {
        return "[S]";
    }
    if (entry.name.find(".prefab") != std::string::npos)
    {
        return "[P]";
    }
    if (extension == ".vsgraph" || extension == ".femboygraph" || extension == ".vsjson")
    {
        return "[G]";
    }
    if (extension == ".femboy")
    {
        return "[F]";
    }
    if (extension == ".json" || extension == ".toml")
    {
        return "[J]";
    }
    if (extension == ".hpp" || extension == ".h" || extension == ".cpp")
    {
        return "[C]";
    }
    return "[?]";
}

ImVec4 AssetIconColor(const AssetEntry& entry)
{
    if (entry.directory)
    {
        return ImVec4(0.95f, 0.78f, 0.35f, 1.f);
    }
    const std::string icon = AssetIcon(entry);
    if (icon == "[S]")
    {
        return ImVec4(0.45f, 0.70f, 1.f, 1.f);
    }
    if (icon == "[P]")
    {
        return ImVec4(0.80f, 0.55f, 1.f, 1.f);
    }
    if (icon == "[G]")
    {
        return ImVec4(0.45f, 0.90f, 0.65f, 1.f);
    }
    if (icon == "[C]")
    {
        return ImVec4(0.90f, 0.65f, 0.45f, 1.f);
    }
    return ImVec4(0.65f, 0.65f, 0.72f, 1.f);
}

struct SetupState
{
    std::vector<std::string> lines;
    bool missing = false;
    bool checked = false;
};

std::string SetupCommand()
{
#ifdef _WIN32
    return "powershell -NoProfile -ExecutionPolicy Bypass -File tools\\setup_windows.ps1 -Install";
#elif defined(__APPLE__)
    return "./tools/setup_macos.sh --install";
#else
    return "./tools/setup_linux.sh --install";
#endif
}

std::string OpenToolsCommand()
{
#ifdef _WIN32
    return "explorer tools";
#elif defined(__APPLE__)
    return "open tools";
#else
    return "xdg-open tools";
#endif
}

SetupState CheckSetup()
{
    SetupState state;
    const auto trim = [](std::string value)
    {
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
        {
            value.pop_back();
        }
        return value;
    };

#ifdef _WIN32
    const std::string visualStudio = trim(RunProcess(
        "\"%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe\" -latest -products * "
        "-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath"));
    const std::filesystem::path vsRoot(visualStudio);
    const std::filesystem::path bundledCMake =
        vsRoot / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" / "bin" / "cmake.exe";
    const std::filesystem::path bundledNinja =
        vsRoot / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "Ninja" / "ninja.exe";
#endif

    const auto check = [&state](const std::string& label, const std::string& command,
                                const std::filesystem::path& fallback = {})
    {
        const std::string output = RunProcess(command);
        bool failed = output.empty() || output.find("(failed to run command)") != std::string::npos ||
                      output.find("not recognized") != std::string::npos ||
                      output.find("not found") != std::string::npos;
#ifdef _WIN32
        if (failed && !fallback.empty() && std::filesystem::exists(fallback))
        {
            state.lines.push_back("[ok] " + label);
            state.lines.push_back("    " + fallback.string());
            failed = false;
        }
#else
        (void)fallback;
#endif
        if (failed)
        {
            state.lines.push_back("[missing] " + label);
            state.missing = true;
            return;
        }
        state.lines.push_back("[ok] " + label);
        if (!output.empty())
        {
            const std::size_t newline = output.find('\n');
            state.lines.push_back("    " + output.substr(0, newline));
        }
    };

#ifdef _WIN32
    check("CMake", "cmake --version", bundledCMake);
    check("Ninja", "ninja --version", bundledNinja);
#else
    check("CMake", "cmake --version");
    check("Ninja", "ninja --version");
#endif
    check("Git", "git --version");
#ifdef _WIN32
    check("Visual Studio C++",
          "\"%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe\" -latest -products * "
          "-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath");
#else
    check("C++ compiler", "c++ --version");
#endif
    state.checked = true;
    return state;
}

// The Hierarchy and Inspector are now backed by the same Scene/Prefab/Registry
// path the runtime uses. The editor does not need a private entity model, and a
// scene saved here loads in a game without translation.
struct SceneState
{
    char scenePath[512] = "Projects/NeonTriangle/assets/example.scene";
    char prefabDir[512] = "Projects/NeonTriangle/assets/prefabs";
    Fem::Scene scene;
    Fem::Registry registry;
    Fem::PrefabLibrary* library = &Fem::PrefabLibrary::Instance();
    entt::entity selected = entt::null;
    std::vector<Entity> extraSelection;
    int hierarchyGrouping = 0; // Parent tree, Tag, Category, Group
    char groupName[128] = "Group";
    std::vector<std::string> groupNames;
    int renamingGroup = -1;
    char renameBuffer[128] = "";
    int prefabIndex = 0;
    bool advanced = false;
    char filter[128] = "";
    std::string status = "Load a scene, create an entity, or instance a prefab.";
};

struct EntityTransform
{
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
};

struct ViewportState
{
    sf::RenderTexture target;
    Fem::Camera2D camera;
    Fem::ProjectionMode projection = Fem::ProjectionMode::Orthographic;
    bool ready = false;
    bool showGrid = true;
    bool hexGrid = false;
    bool hexAllowed = true;
    int sortMode = 1; // 0 none, 1 Y-sort, 2 isometric depth
    int pixelWidth = 0;
    int pixelHeight = 0;
    Entity dragEntity = entt::null;
    sf::Vector2f dragOffset{0.f, 0.f};
    sf::Vector2f dragStart{0.f, 0.f};
    sf::Vector2f dragWorldStart{0.f, 0.f};
    enum class GizmoMode
    {
        Move,
        Rotate,
        Scale
    };
    GizmoMode gizmoMode = GizmoMode::Move;
    int gizmoAxis = 0; // 0 none, 1 X, 2 Y, 3 center/uniform/ring
    int axisLock = 0;  // 0 free, 1 X, 2 Y
    bool snapEnabled = false;
    float moveSnap = 8.f;
    float rotationSnap = 15.f;
    float scaleSnap = 0.1f;
    bool gizmoActive = false;
    bool gizmoSpawned = false;
    Entity gizmoEntity = entt::null;
    Entity gizmoOriginal = entt::null;
    std::vector<Entity> gizmoEntities;
    std::vector<EntityTransform> gizmoBeforeGroup;
    std::vector<EntityTransform> gizmoBeforeWorld;
    sf::Vector2f gizmoPivot{0.f, 0.f};
    ImVec2 gizmoMouseStart{0.f, 0.f};
    EntityTransform gizmoBefore;
    std::string status =
        "Middle/right drag: pan · wheel: zoom · W/E/R: move/rotate/scale · Alt-drag: duplicate · arrows: nudge · F: focus";
};

bool IsEntitySelected(const SceneState& scene, Entity entity)
{
    if (scene.selected == entity)
    {
        return true;
    }
    return std::find(scene.extraSelection.begin(), scene.extraSelection.end(), entity) !=
           scene.extraSelection.end();
}

void SelectEntity(SceneState& scene, Entity entity, bool additive)
{
    if (!additive)
    {
        scene.selected = entity;
        scene.extraSelection.clear();
        return;
    }

    if (scene.selected == entt::null)
    {
        scene.selected = entity;
        return;
    }
    if (scene.selected == entity)
    {
        if (!scene.extraSelection.empty())
        {
            scene.selected = scene.extraSelection.back();
            scene.extraSelection.pop_back();
        }
        else
        {
            scene.selected = entt::null;
        }
        return;
    }

    const auto found = std::find(scene.extraSelection.begin(), scene.extraSelection.end(), entity);
    if (found != scene.extraSelection.end())
    {
        scene.extraSelection.erase(found);
    }
    else
    {
        scene.extraSelection.push_back(entity);
    }
}

std::vector<Entity> SelectedEntities(const SceneState& scene)
{
    std::vector<Entity> entities;
    if (scene.selected != entt::null)
    {
        entities.push_back(scene.selected);
    }
    for (Entity entity : scene.extraSelection)
    {
        if (entity != scene.selected &&
            std::find(entities.begin(), entities.end(), entity) == entities.end())
        {
            entities.push_back(entity);
        }
    }
    return entities;
}

void ClearEntitySelection(SceneState& scene)
{
    scene.selected = entt::null;
    scene.extraSelection.clear();
}

std::string EntityCategory(Fem::Registry& registry, Entity entity)
{
    if (registry.all_of<Fem::SpriteComponent>(entity))
    {
        return "Sprites";
    }
#ifdef FEMBOY_HAS_BOX2D
    if (registry.all_of<Fem::PhysicsBodyComponent>(entity))
    {
        return "Physics";
    }
#endif
    for (const Fem::ComponentReflection& component : Fem::Mirror::Instance().Components())
    {
        if (component.jsonKey != "transform" && component.jsonKey != "tag" &&
            component.jsonKey != "hierarchy" && component.jsonKey != "group" && component.has &&
            component.has(registry, entity))
        {
            return component.name;
        }
    }
    return "Other";
}

std::vector<Entity> GroupEntities(Fem::Registry& registry, const std::string& group)
{
    std::vector<Entity> entities;
    auto view = registry.view<Fem::GroupComponent>();
    for (const Entity entity : view)
    {
        if (view.get<Fem::GroupComponent>(entity).group == group)
        {
            entities.push_back(entity);
        }
    }
    return entities;
}

void MergeGroupNames(SceneState& scene)
{
    auto view = scene.registry.view<Fem::GroupComponent>();
    for (const Entity entity : view)
    {
        const std::string& group = view.get<Fem::GroupComponent>(entity).group;
        if (!group.empty() &&
            std::find(scene.groupNames.begin(), scene.groupNames.end(), group) == scene.groupNames.end())
        {
            scene.groupNames.push_back(group);
        }
    }
}

std::filesystem::path GroupSidecarPath(const SceneState& scene)
{
    if (scene.scenePath[0] == '\0')
    {
        return {};
    }
    return std::filesystem::path(scene.scenePath).string() + ".groups.json";
}

void LoadGroupNames(SceneState& scene)
{
    scene.groupNames.clear();
    MergeGroupNames(scene);
    const std::filesystem::path sidecar = GroupSidecarPath(scene);
    if (sidecar.empty() || !std::filesystem::exists(sidecar))
    {
        return;
    }
    std::ifstream file(sidecar);
    try
    {
        nlohmann::json data;
        file >> data;
        if (data.contains("groups") && data["groups"].is_array())
        {
            for (const nlohmann::json& entry : data["groups"])
            {
                if (entry.is_string())
                {
                    const std::string group = entry.get<std::string>();
                    if (std::find(scene.groupNames.begin(), scene.groupNames.end(), group) ==
                        scene.groupNames.end())
                    {
                        scene.groupNames.push_back(group);
                    }
                }
            }
        }
    }
    catch (const std::exception&)
    {
    }
    std::sort(scene.groupNames.begin(), scene.groupNames.end());
}

void SaveGroupNames(SceneState& scene)
{
    MergeGroupNames(scene);
    std::sort(scene.groupNames.begin(), scene.groupNames.end());
    const std::filesystem::path sidecar = GroupSidecarPath(scene);
    if (sidecar.empty())
    {
        return;
    }
    nlohmann::json data;
    data["groups"] = scene.groupNames;
    (void)Fem::Vanity::WriteJson(sidecar.string(), data);
}

struct ClassDefaultsState
{
    char path[512] = "Projects/NeonTriangle/class_defaults.json";
    int componentIndex = 0;
    bool advanced = false;
    char filter[128] = "";
    std::string status = "Project defaults sit between C++ defaults and prefab overrides.";
};

// Per-project settings stored in project.femboy (identity/view) and
// config.json (window). This is the editor equivalent of UE's Project
// Settings: one place for values that describe the project, not one entity.
struct ProjectSettingsState
{
    char root[512] = "";
    char name[128] = "";
    char version[32] = "0.1.0";
    char title[128] = "";
    int view = 0; // Top-Down, Side, Isometric, Hex
    int grid = 0; // Square, Hex
    int width = 1280;
    int height = 720;
    bool fullscreen = false;

    // Build settings exported to build.settings.cmake. These describe the
    // intended target, not the engine's own developer build.
    int buildConfig = 2;    // Debug, Development, Shipping
    int optimization = 2;   // O0, O1, O2, O3, Ofast
    int simd = 1;           // none, SSE2, AVX2, AVX-512
    int warnings = 1;       // default, high, warnings-as-errors
    int sanitizers = 0;     // none, ASan, UBSan, both
    bool lto = true;
    bool pgo = false;
    bool symbols = true;
    bool assertions = false;
    bool fastMath = false;
    bool staticRuntime = false;
    bool unityBuild = false;
    bool steamBuild = false;

    bool loaded = false;
    std::string status = "Open a project to edit its settings.";

    static const char* ViewName(int index)
    {
        static const char* names[] = {"Top-Down (2D)", "Side View (2D)", "Isometric"};
        return names[std::clamp(index, 0, 3)];
    }

    static const char* GridName(int index)
    {
        static const char* names[] = {"Square", "Hex"};
        return names[std::clamp(index, 0, 1)];
    }

    void Load(const std::string& projectRoot)
    {
        namespace fs = std::filesystem;
        std::snprintf(root, sizeof(root), "%s", projectRoot.c_str());
        loaded = true;
        std::error_code ec;

        std::ifstream manifest(fs::path(projectRoot) / "project.femboy");
        if (manifest.is_open())
        {
            try
            {
                nlohmann::json data;
                manifest >> data;
                if (data.contains("name") && data["name"].is_string())
                {
                    std::snprintf(name, sizeof(name), "%s", data["name"].get<std::string>().c_str());
                }
                if (data.contains("version") && data["version"].is_string())
                {
                    std::snprintf(version, sizeof(version), "%s", data["version"].get<std::string>().c_str());
                }
                if (data.contains("view") && data["view"].is_string())
                {
                    const std::string value = data["view"].get<std::string>();
                    // Older projects used "hex" as a view type. It was only an
                    // isometric project with a hex grid; migrate it in place.
                    if (value == "hex")
                    {
                        view = 2;
                        grid = 1;
                    }
                    else
                    {
                        view = value == "side" ? 1 : value == "isometric" ? 2 : 0;
                    }
                }
                if (data.contains("grid") && data["grid"].is_string())
                {
                    grid = data["grid"].get<std::string>() == "hex" ? 1 : 0;
                }
                if (view == 1)
                {
                    grid = 0;
                }
                if (data.contains("build") && data["build"].is_object())
                {
                    const nlohmann::json& build = data["build"];
                    buildConfig = build.value("configuration", buildConfig);
                    optimization = build.value("optimization", optimization);
                    simd = build.value("simd", simd);
                    warnings = build.value("warnings", warnings);
                    sanitizers = build.value("sanitizers", sanitizers);
                    lto = build.value("lto", lto);
                    pgo = build.value("pgo", pgo);
                    symbols = build.value("symbols", symbols);
                    assertions = build.value("assertions", assertions);
                    fastMath = build.value("fastMath", fastMath);
                    staticRuntime = build.value("staticRuntime", staticRuntime);
                    unityBuild = build.value("unity", unityBuild);
                    steamBuild = build.value("steamBuild", steamBuild);
                }
            }
            catch (const std::exception&)
            {
                status = "project.femboy is not valid JSON";
            }
        }

        std::ifstream config(fs::path(projectRoot) / "config.json");
        if (config.is_open())
        {
            try
            {
                nlohmann::json data;
                config >> data;
                if (data.contains("title") && data["title"].is_string())
                {
                    std::snprintf(title, sizeof(title), "%s", data["title"].get<std::string>().c_str());
                }
                if (data.contains("width") && data["width"].is_number_integer())
                {
                    width = data["width"].get<int>();
                }
                if (data.contains("height") && data["height"].is_number_integer())
                {
                    height = data["height"].get<int>();
                }
                if (data.contains("fullscreen") && data["fullscreen"].is_boolean())
                {
                    fullscreen = data["fullscreen"].get<bool>();
                }
            }
            catch (const std::exception&)
            {
                status = "config.json is not valid JSON";
            }
        }

        if (title[0] == '\0')
        {
            std::snprintf(title, sizeof(title), "%s", name);
        }
        status = "Loaded project settings.";
        (void)ec;
    }

    bool Save()
    {
        namespace fs = std::filesystem;
        if (root[0] == '\0')
        {
            status = "No active project.";
            return false;
        }

        nlohmann::json manifest;
        std::ifstream in(fs::path(root) / "project.femboy");
        if (in.is_open())
        {
            try
            {
                in >> manifest;
            }
            catch (const std::exception&)
            {
                manifest = nlohmann::json::object();
            }
        }
        if (!manifest.is_object())
        {
            manifest = nlohmann::json::object();
        }
        manifest["name"] = name;
        manifest["version"] = version;
        manifest["engine"] = "FemboyEngine";
        manifest["view"] = view == 1 ? "side" : view == 2 ? "isometric" : "top-down";
        if (view == 1)
        {
            grid = 0;
        }
        manifest["grid"] = grid == 1 ? "hex" : "square";
        manifest["build"] = {{"configuration", buildConfig}, {"optimization", optimization},
                             {"simd", simd},           {"warnings", warnings},
                             {"sanitizers", sanitizers}, {"lto", lto},
                             {"pgo", pgo},             {"symbols", symbols},
                             {"assertions", assertions}, {"fastMath", fastMath},
                             {"staticRuntime", staticRuntime}, {"unity", unityBuild},
                             {"steamBuild", steamBuild}};

        nlohmann::json config;
        std::ifstream configIn(fs::path(root) / "config.json");
        if (configIn.is_open())
        {
            try
            {
                configIn >> config;
            }
            catch (const std::exception&)
            {
                config = nlohmann::json::object();
            }
        }
        if (!config.is_object())
        {
            config = nlohmann::json::object();
        }
        config["title"] = title;
        config["width"] = std::max(320, width);
        config["height"] = std::max(240, height);
        config["fullscreen"] = fullscreen;

        const bool manifestOk =
            static_cast<bool>(Fem::Vanity::WriteJson((fs::path(root) / "project.femboy").string(), manifest));
        const bool configOk =
            static_cast<bool>(Fem::Vanity::WriteJson((fs::path(root) / "config.json").string(), config));

        // Export build intent as a CMake fragment. The generated project and
        // the built-in game CMake include this file and append the flags to
        // their game target.
        std::vector<std::string> msvcCompile;
        std::vector<std::string> msvcLink;
        std::vector<std::string> gnuCompile;
        std::vector<std::string> gnuLink;
        const char* optimizationMsvc[] = {"/Od", "/O1", "/O2", "/O2", "/O2"};
        const char* optimizationGnu[] = {"-O0", "-O1", "-O2", "-O3", "-Ofast"};
        const int optimizationIndex = std::clamp(optimization, 0, 4);
        msvcCompile.emplace_back(optimizationMsvc[optimizationIndex]);
        if (optimizationIndex == 3)
        {
            msvcCompile.emplace_back("/Ob3");
        }
        gnuCompile.emplace_back(optimizationGnu[optimizationIndex]);
        if (fastMath)
        {
            msvcCompile.emplace_back("/fp:fast");
            gnuCompile.emplace_back("-ffast-math");
            gnuCompile.emplace_back("-fno-finite-math-only");
        }
        if (buildConfig == 0 && assertions)
        {
            msvcCompile.emplace_back("/UNDEBUG");
            gnuCompile.emplace_back("-UNDEBUG");
        }
        if (lto)
        {
            msvcCompile.emplace_back("/GL");
            msvcLink.emplace_back("/LTCG");
            gnuCompile.emplace_back("-flto");
            gnuLink.emplace_back("-flto");
        }
        if (symbols)
        {
            msvcCompile.emplace_back("/Zi");
            msvcLink.emplace_back("/DEBUG");
            gnuCompile.emplace_back("-g");
        }
        const char* simdMsvc[] = {"", "/arch:SSE2", "/arch:AVX2", "/arch:AVX512"};
        const char* simdGnu[] = {"", "-msse2", "-mavx2", "-mavx512f"};
        if (simd > 0)
        {
            msvcCompile.emplace_back(simdMsvc[std::clamp(simd, 0, 3)]);
            gnuCompile.emplace_back(simdGnu[std::clamp(simd, 0, 3)]);
        }
        if (warnings >= 1)
        {
            msvcCompile.emplace_back("/W4");
            gnuCompile.emplace_back("-Wall");
            gnuCompile.emplace_back("-Wextra");
        }
        if (warnings == 2)
        {
            msvcCompile.emplace_back("/WX");
            gnuCompile.emplace_back("-Werror");
        }
        if (sanitizers != 0)
        {
            msvcCompile.emplace_back("/fsanitize=address");
            msvcLink.emplace_back("/fsanitize=address");
            gnuCompile.emplace_back(sanitizers == 1 ? "-fsanitize=address"
                                                    : sanitizers == 2 ? "-fsanitize=undefined"
                                                                      : "-fsanitize=address,undefined");
            gnuLink.emplace_back(gnuCompile.back());
        }
        if (staticRuntime)
        {
            msvcCompile.emplace_back(buildConfig == 0 ? "/MTd" : "/MT");
            gnuCompile.emplace_back("-static");
        }

        std::string buildCmake = "# Generated by FemboyEditor. Safe to delete; the editor rewrites it.\n";
        buildCmake += "set(FEMBOY_PROJECT_BUILD_PROFILE \"" +
                      std::string(buildConfig == 0 ? "Debug" : buildConfig == 1 ? "Development" : "Shipping") +
                      "\")\n";
        buildCmake += "set(FEMBOY_PROJECT_PGO " + std::string(pgo ? "ON" : "OFF") + ")\n";
        buildCmake += "set(FEMBOY_PROJECT_STEAM " + std::string(steamBuild ? "ON" : "OFF") + ")\n";
        buildCmake += std::string("set(CMAKE_UNITY_BUILD ") + (unityBuild ? "ON" : "OFF") + ")\n";
        buildCmake += "set(FEMBOY_PROJECT_COMPILE_FLAGS\n";
        for (const std::string& value : msvcCompile)
        {
            buildCmake += "    \"$<$<CXX_COMPILER_ID:MSVC>:" + value + ">\"\n";
        }
        for (const std::string& value : gnuCompile)
        {
            buildCmake += "    \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:" + value + ">\"\n";
        }
        buildCmake += ")\n";
        buildCmake += "set(FEMBOY_PROJECT_LINK_FLAGS\n";
        for (const std::string& value : msvcLink)
        {
            buildCmake += "    \"$<$<CXX_COMPILER_ID:MSVC>:" + value + ">\"\n";
        }
        for (const std::string& value : gnuLink)
        {
            buildCmake += "    \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:" + value + ">\"\n";
        }
        buildCmake += ")\n";
        const bool buildOk = static_cast<bool>(
            Fem::Vanity::WriteText((fs::path(root) / "build.settings.cmake").string(), buildCmake));

        status = manifestOk && configOk && buildOk ? "Project settings saved."
                                                   : "Could not save project settings.";
        return manifestOk && configOk && buildOk;
    }
};

// The project browser / creation screen shown on startup. It answers the
// "where and what am I making" question before the editor opens a project, in
// the same spirit as UE's project browser.
struct ProjectWizardState
{
    bool visible = true;
    bool focus = true;
    char name[128] = "MyGame";
    char location[512] = "";
    char openPath[512] = "";
    int view = 0; // Top-Down, Side, Isometric, Hex
    int grid = 0; // Square, Hex
    std::string status;

    static const char* ViewHint(int index)
    {
        switch (index)
        {
        case 1:
            return "Side-on orthographic camera for platformers.";
        case 2:
            return "2:1 isometric projection; choose a square or hex grid.";
        default:
            return "Top-down orthographic camera for a pure 2D canvas.";
        }
    }
};

// Editor-wide preferences stored in the user profile, separate from project
// settings. Every value is reset independently from the Editor Settings panel.
struct EditorSettingsState
{
    float uiZoom = 1.f;
    bool showBrowserOnStartup = true;
    bool noFunctionKeys = false;
    bool tooltipsEnabled = true;

    // Object/editor actions are remappable. Values are ImGuiKeyChord ints.
    int hkMove = ImGuiKey_W;
    int hkRotate = ImGuiKey_E;
    int hkScale = ImGuiKey_R;
    int hkDuplicate = ImGuiMod_Ctrl | ImGuiKey_D;
    int hkCopy = ImGuiMod_Ctrl | ImGuiKey_C;
    int hkPaste = ImGuiMod_Ctrl | ImGuiKey_V;
    int hkDelete = ImGuiKey_Delete;
    int hkFocus = ImGuiKey_F;
    int rebindingHotkey = -1;

    char lastProject[512] = "";
    char file[512] = "";
    std::string status = "Editor preferences loaded.";

    void Load(const std::string& userData)
    {
        if (userData.empty())
        {
            return;
        }
        std::snprintf(file, sizeof(file), "%s", (std::filesystem::path(userData) / "editor.json").string().c_str());
        std::ifstream in(file);
        if (!in.is_open())
        {
            return;
        }
        try
        {
            nlohmann::json data;
            in >> data;
            if (data.contains("uiZoom") && data["uiZoom"].is_number())
            {
                uiZoom = std::clamp(data["uiZoom"].get<float>(), 0.75f, 2.f);
            }
            if (data.contains("showBrowserOnStartup") && data["showBrowserOnStartup"].is_boolean())
            {
                showBrowserOnStartup = data["showBrowserOnStartup"].get<bool>();
            }
            if (data.contains("lastProject") && data["lastProject"].is_string())
            {
                std::snprintf(lastProject, sizeof(lastProject), "%s",
                              data["lastProject"].get<std::string>().c_str());
            }
            if (data.contains("noFunctionKeys") && data["noFunctionKeys"].is_boolean())
            {
                noFunctionKeys = data["noFunctionKeys"].get<bool>();
            }
            if (data.contains("tooltipsEnabled") && data["tooltipsEnabled"].is_boolean())
            {
                tooltipsEnabled = data["tooltipsEnabled"].get<bool>();
            }
            if (data.contains("hotkeys") && data["hotkeys"].is_object())
            {
                const nlohmann::json& hotkeys = data["hotkeys"];
                hkMove = hotkeys.value("move", hkMove);
                hkRotate = hotkeys.value("rotate", hkRotate);
                hkScale = hotkeys.value("scale", hkScale);
                hkDuplicate = hotkeys.value("duplicate", hkDuplicate);
                hkCopy = hotkeys.value("copy", hkCopy);
                hkPaste = hotkeys.value("paste", hkPaste);
                hkDelete = hotkeys.value("delete", hkDelete);
                hkFocus = hotkeys.value("focus", hkFocus);
            }
            if (data.contains("accessibility"))
            {
                Fem::AccessibilitySettings::Instance().FromJson(data["accessibility"]);
            }
        }
        catch (const std::exception&)
        {
            status = "editor.json is not valid JSON";
        }
    }

    bool Save()
    {
        if (file[0] == '\0')
        {
            return false;
        }
        nlohmann::json data;
        data["uiZoom"] = std::clamp(uiZoom, 0.75f, 2.f);
        data["showBrowserOnStartup"] = showBrowserOnStartup;
        data["noFunctionKeys"] = noFunctionKeys;
        data["tooltipsEnabled"] = tooltipsEnabled;
        data["hotkeys"] = {{"move", hkMove},           {"rotate", hkRotate},
                           {"scale", hkScale},         {"duplicate", hkDuplicate},
                           {"copy", hkCopy},           {"paste", hkPaste},
                           {"delete", hkDelete},       {"focus", hkFocus}};
        data["lastProject"] = lastProject;
        data["accessibility"] = Fem::AccessibilitySettings::Instance().ToJson();
        const bool ok = static_cast<bool>(Fem::Vanity::WriteJson(file, data));
        status = ok ? "Editor preferences saved." : "Could not save editor preferences.";
        return ok;
    }
};

void RememberProject(EditorSettingsState& settings, const std::filesystem::path& path)
{
    std::snprintf(settings.lastProject, sizeof(settings.lastProject), "%s", path.string().c_str());
    settings.Save();
}

void ResetPanelWindow(const char* name, int index)
{
    ImGui::ClearWindowSettings(name);
    if (ImGuiWindow* window = ImGui::FindWindowByName(name))
    {
        // ClearWindowSettings marks the window as never-saved so the deleted
        // entry is not resurrected. A manual reset should be saved again.
        window->Flags &= ~ImGuiWindowFlags_NoSavedSettings;
    }
    const ImVec2 position(140.f + static_cast<float>(index % 8) * 28.f,
                          120.f + static_cast<float>(index % 8) * 22.f);
    ImGui::SetWindowPos(name, position, ImGuiCond_Always);
}

void DrawProjectSettingsContent(ProjectSettingsState& projectSettings, ViewportState& viewport)
{
    ImGui::BeginDisabled(!projectSettings.loaded);
    ImGui::TextDisabled("Project root: %s", projectSettings.root[0] ? projectSettings.root : "(no project open)");
    ImGui::Separator();

    ImGui::SetNextItemWidth(360.f);
    ImGui::InputText("Project name", projectSettings.name, sizeof(projectSettings.name));
    ImGui::SetNextItemWidth(360.f);
    ImGui::InputText("Version", projectSettings.version, sizeof(projectSettings.version));
    ImGui::SetNextItemWidth(360.f);
    ImGui::InputText("Window title", projectSettings.title, sizeof(projectSettings.title));

    const char* views[] = {"Top-Down (2D)", "Side View (2D)", "Isometric"};
    ImGui::SetNextItemWidth(260.f);
    if (ImGui::Combo("Project type", &projectSettings.view, views, 3) && projectSettings.view == 1)
    {
        projectSettings.grid = 0;
    }
    ImGui::BeginDisabled(projectSettings.view == 1);
    const char* grids[] = {"Square grid", "Hex grid"};
    ImGui::SetNextItemWidth(260.f);
    ImGui::Combo("Grid", &projectSettings.grid, grids, 2);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Hex is a grid option for 2D or isometric projects.");

    ImGui::SetNextItemWidth(160.f);
    ImGui::DragInt("Window width", &projectSettings.width, 8.f, 320, 7680);
    ImGui::SetNextItemWidth(160.f);
    ImGui::DragInt("Window height", &projectSettings.height, 8.f, 240, 4320);
    ImGui::Checkbox("Start fullscreen", &projectSettings.fullscreen);

    ImGui::Separator();
    ImGui::Text("Build");
    ImGui::TextDisabled("Exported to build.settings.cmake for standalone project builds.");
    const char* configurations[] = {"Debug", "Development", "Shipping"};
    ImGui::SetNextItemWidth(240.f);
    ImGui::Combo("Configuration", &projectSettings.buildConfig, configurations, 3);
    EditorTooltip("Debug: no optimization, full symbols and assertions.\n"
                  "Development: fast iteration with symbols and moderate optimization.\n"
                  "Shipping: optimized, LTO-friendly build for players.");

    const char* optimizations[] = {"O0 / Od", "O1", "O2 (recommended)", "O3 (benchmark first)",
                                   "Ofast (unsafe)"};
    ImGui::SetNextItemWidth(240.f);
    ImGui::Combo("Optimization", &projectSettings.optimization, optimizations, 5);
    EditorTooltip("Shipping default: O2. O3 only after profiling. Ofast disables strict "
                  "floating-point rules and can break physics, replays and networking.");

    const char* simdModes[] = {"Portable (no extra SIMD)", "SSE2", "AVX2", "AVX-512"};
    ImGui::SetNextItemWidth(240.f);
    ImGui::Combo("SIMD", &projectSettings.simd, simdModes, 4);
    EditorTooltip("Use AVX2/AVX-512 only when that ISA is your minimum spec, or add runtime "
                  "dispatch. Do not ship -march=native to other machines.");

    const char* warningModes[] = {"Compiler default", "High (/W4, -Wall -Wextra)", "Warnings as errors"};
    ImGui::SetNextItemWidth(240.f);
    ImGui::Combo("Warnings", &projectSettings.warnings, warningModes, 3);
    EditorTooltip("Use high warnings during development. Warnings-as-errors is best kept "
                  "for CI, not for every local build.");

    const char* sanitizerModes[] = {"None", "AddressSanitizer", "UndefinedBehaviorSanitizer", "ASan + UBSan"};
    ImGui::SetNextItemWidth(240.f);
    ImGui::Combo("Sanitizers", &projectSettings.sanitizers, sanitizerModes, 4);
    EditorTooltip("Sanitizers are a Debug/Development tool. They add significant overhead "
                  "and should not be enabled in Shipping.");

    ImGui::Checkbox("Link-time optimization (LTO)", &projectSettings.lto);
    EditorTooltip("Recommended for Shipping. LTO increases link time and memory usage.");
    ImGui::SameLine();
    ImGui::Checkbox("Profile-guided optimization (PGO)", &projectSettings.pgo);
    EditorTooltip("Optional advanced step: collect a representative profile, then rebuild. "
                  "Do not enable it blindly on every build.");
    ImGui::Checkbox("Generate debug symbols", &projectSettings.symbols);
    EditorTooltip("Keep symbols for crash reports, but ship them separately from the game "
                  "binary when possible.");
    ImGui::SameLine();
    ImGui::Checkbox("Keep assertions", &projectSettings.assertions);
    EditorTooltip("Assertions are useful in Debug/Development. Shipping usually disables them.");

    ImGui::Checkbox("Fast math (unsafe)", &projectSettings.fastMath);
    EditorTooltip("Allows reassociation and relaxed NaN/Inf behaviour. This can break "
                  "determinism and is never recommended as a default.");
    ImGui::SameLine();
    ImGui::Checkbox("Static CRT", &projectSettings.staticRuntime);
    EditorTooltip("Static runtime avoids a Visual C++ redistributable, but increases binary "
                  "size and can duplicate runtime state in plugins.");
    ImGui::Checkbox("Unity build", &projectSettings.unityBuild);
    EditorTooltip("Speeds up clean builds by merging translation units. Can expose missing "
                  "includes and changes symbol visibility; keep it off for debug builds.");
    ImGui::Checkbox("Steam build", &projectSettings.steamBuild);
    EditorTooltip("Enables Steamworks integration for this project (NEON_STEAM). "
                  "Keep it off for itch/portable builds so they never try to launch through Steam.");

    if (projectSettings.fastMath || projectSettings.optimization == 4)
    {
        ImGui::TextColored(ImVec4(1.f, 0.72f, 0.35f, 1.f),
                           "Warning: fast math / Ofast can break deterministic physics, replays and networking.");
    }
    ImGui::TextDisabled("Recommended shipping baseline: O2, LTO, symbols stored separately, precise FP math.");

    ImGui::Separator();
    if (ImGui::Button("Save project settings"))
    {
        projectSettings.Save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply view to editor"))
    {
        viewport.projection =
            projectSettings.view == 2 ? Fem::ProjectionMode::Isometric : Fem::ProjectionMode::Orthographic;
        viewport.camera.SetProjection(viewport.projection);
        viewport.hexAllowed = projectSettings.view != 1;
        viewport.hexGrid = viewport.hexAllowed && projectSettings.grid == 1;
        viewport.sortMode = projectSettings.view == 2 ? 2 : projectSettings.view == 1 ? 0 : 1;
        viewport.showGrid = true;
        projectSettings.status = "View applied to the editor viewport.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset camera"))
    {
        viewport.camera.SetPosition(0.f, 0.f);
        viewport.camera.SetZoom(1.f);
        viewport.camera.SetRotation(0.f);
    }
    ImGui::TextDisabled("%s", projectSettings.status.c_str());
    ImGui::EndDisabled();
}

// Locates the engine checkout from the executable or working directory. The
// editor may be launched as build\FemboyEditor.exe, so paths such as
// Projects/... must not be resolved against the build tree.
std::filesystem::path FindEngineRoot(const char* executable)
{
    std::vector<std::filesystem::path> starts;
    std::error_code ec;
    if (executable != nullptr && executable[0] != '\0')
    {
        const std::filesystem::path absolute = std::filesystem::absolute(executable, ec);
        if (!ec)
        {
            starts.push_back(absolute.parent_path());
        }
    }
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!ec)
    {
        starts.push_back(cwd);
    }

    for (const std::filesystem::path& start : starts)
    {
        std::filesystem::path directory = start;
        while (!directory.empty())
        {
            if (std::filesystem::exists(directory / "engine" / "include" / "FemboyEngine" / "Engine.hpp", ec) &&
                !ec)
            {
                return directory;
            }
            const std::filesystem::path parent = directory.parent_path();
            if (parent == directory)
            {
                break;
            }
            directory = parent;
        }
    }
    return {};
}

void SetPathBuffer(char* destination, std::size_t size, const std::string& value)
{
    std::snprintf(destination, size, "%s", value.c_str());
}

// Points every project-scoped editor path at one project folder. Everything the
// editor writes (scenes, prefabs, defaults, cooked assets) then stays inside
// that folder instead of the engine build directory.
void ApplyProjectPaths(const std::string& root, ProjectState& project, SceneState& scene,
                       ClassDefaultsState& classDefaults, AssetPipelineState& assets, GitState& git,
                       ProjectSettingsState& settings, ViewportState& viewport)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::path(root).lexically_normal();
    SetPathBuffer(project.path, sizeof(project.path), base.string());
    SetPathBuffer(scene.scenePath, sizeof(scene.scenePath), (base / "assets" / "example.scene").string());
    SetPathBuffer(scene.prefabDir, sizeof(scene.prefabDir), (base / "assets" / "prefabs").string());
    SetPathBuffer(classDefaults.path, sizeof(classDefaults.path), (base / "class_defaults.json").string());
    SetPathBuffer(assets.manifest, sizeof(assets.manifest), (base / "assets" / "manifest.json").string());
    SetPathBuffer(assets.output, sizeof(assets.output), (base / "assets" / "cooked").string());
    SetPathBuffer(git.path, sizeof(git.path), base.string());

    std::error_code ec;
    fs::create_directories(base / "assets" / "prefabs", ec);
    fs::create_directories(base / "scripts", ec);
    assets.database.Clear();
    assets.stale.clear();
    assets.status = "Project root: " + base.string();
    if (fs::exists(base / "class_defaults.json", ec) && !ec)
    {
        Fem::ClassDefaults::Instance().Load((base / "class_defaults.json").string());
    }

    settings.Load(base.string());
    viewport.projection = settings.view == 2 ? Fem::ProjectionMode::Isometric : Fem::ProjectionMode::Orthographic;
    viewport.camera.SetProjection(viewport.projection);
    viewport.hexAllowed = settings.view != 1;
    viewport.hexGrid = viewport.hexAllowed && settings.grid == 1;
    viewport.sortMode = settings.view == 2 ? 2 : settings.view == 1 ? 0 : 1;
    viewport.showGrid = true;
    viewport.status = std::string("Project view: ") + ProjectSettingsState::ViewName(settings.view);
}

namespace
{
// One reflected property widget. Keeping the widget independent from the
// storage lets the same code edit a live component and a ClassDefaults object.
struct FieldWidgetResult
{
    bool changed = false;
    bool activated = false;
    bool deactivated = false;
    bool reset = false;
    nlohmann::json value = nullptr;
};

FieldWidgetResult DrawFieldWidget(const Fem::Field& field, const nlohmann::json& current, bool editable)
{
    FieldWidgetResult result;
    result.value = current;
    ImGui::PushID(field.name.c_str());
    ImGui::BeginDisabled(!editable);

    const char* label = field.Label().c_str();
    if (field.kind == Fem::FieldKind::Float)
    {
        float v = current.is_number() ? current.get<float>() : 0.f;
        bool changed = false;
        if (field.min && field.max)
        {
            const float step = field.step ? static_cast<float>(*field.step) : 0.05f;
            changed = ImGui::DragFloat(label, &v, step, static_cast<float>(*field.min),
                                       static_cast<float>(*field.max), "%.3f");
        }
        else
        {
            changed = ImGui::DragFloat(label, &v, field.step ? static_cast<float>(*field.step) : 0.05f);
        }
        if (changed)
        {
            result.changed = true;
            result.value = v;
        }
    }
    else if (field.kind == Fem::FieldKind::Int || field.kind == Fem::FieldKind::Enum)
    {
        int v = current.is_number() ? current.get<int>() : 0;
        bool changed = false;
        if (field.kind == Fem::FieldKind::Enum && !field.enumValues.empty())
        {
            int index = 0;
            for (std::size_t i = 0; i < field.enumValues.size(); ++i)
            {
                if (field.enumValues[i].value == v)
                {
                    index = static_cast<int>(i);
                    break;
                }
            }
            std::vector<const char*> names;
            names.reserve(field.enumValues.size());
            for (const Fem::EnumEntry& entry : field.enumValues)
            {
                names.push_back(entry.name.c_str());
            }
            if (ImGui::Combo(label, &index, names.data(), static_cast<int>(names.size())))
            {
                changed = true;
                v = static_cast<int>(field.enumValues[static_cast<std::size_t>(index)].value);
            }
        }
        else if (field.min && field.max)
        {
            changed = ImGui::DragInt(label, &v, field.step ? static_cast<float>(*field.step) : 1.f,
                                     static_cast<int>(*field.min), static_cast<int>(*field.max));
        }
        else
        {
            changed = ImGui::DragInt(label, &v, field.step ? static_cast<float>(*field.step) : 1.f);
        }
        if (changed)
        {
            result.changed = true;
            result.value = v;
        }
    }
    else if (field.kind == Fem::FieldKind::Bool)
    {
        bool v = current.is_boolean() ? current.get<bool>() : false;
        if (ImGui::Checkbox(label, &v))
        {
            result.changed = true;
            result.value = v;
        }
    }
    else if (field.kind == Fem::FieldKind::String || field.kind == Fem::FieldKind::Asset)
    {
        char buffer[1024] = "";
        if (current.is_string())
        {
            std::snprintf(buffer, sizeof(buffer), "%s", current.get<std::string>().c_str());
        }
        const char* hint = field.kind == Fem::FieldKind::Asset
                               ? (field.assetType.empty() ? "asset id" : field.assetType.c_str())
                               : "";
        bool changed = false;
        if (field.multiline)
        {
            changed = ImGui::InputTextMultiline(label, buffer, sizeof(buffer), ImVec2(-1.f, 80.f));
        }
        else if (field.kind == Fem::FieldKind::Asset)
        {
            changed = ImGui::InputTextWithHint(label, hint, buffer, sizeof(buffer));
        }
        else
        {
            changed = ImGui::InputText(label, buffer, sizeof(buffer));
        }
        if (changed)
        {
            result.changed = true;
            result.value = std::string(buffer);
        }
    }
    else if (field.kind == Fem::FieldKind::Color)
    {
        float rgba[4] = {1.f, 1.f, 1.f, 1.f};
        if (current.is_object())
        {
            rgba[0] = current.value("r", 255) / 255.f;
            rgba[1] = current.value("g", 255) / 255.f;
            rgba[2] = current.value("b", 255) / 255.f;
            rgba[3] = current.value("a", 255) / 255.f;
        }
        if (ImGui::ColorEdit4(label, rgba, ImGuiColorEditFlags_AlphaBar))
        {
            result.changed = true;
            result.value = {{"r", static_cast<int>(rgba[0] * 255.f)},
                            {"g", static_cast<int>(rgba[1] * 255.f)},
                            {"b", static_cast<int>(rgba[2] * 255.f)},
                            {"a", static_cast<int>(rgba[3] * 255.f)}};
        }
    }
    else if (field.kind == Fem::FieldKind::Tags)
    {
        std::string joined;
        if (current.is_array())
        {
            for (const auto& tagValue : current)
            {
                if (!tagValue.is_string())
                {
                    continue;
                }
                if (!joined.empty())
                {
                    joined += ", ";
                }
                joined += tagValue.get<std::string>();
            }
        }
        char buffer[512] = "";
        std::snprintf(buffer, sizeof(buffer), "%s", joined.c_str());
        if (ImGui::InputText(label, buffer, sizeof(buffer)))
        {
            nlohmann::json tags = nlohmann::json::array();
            std::string part;
            for (const char* p = buffer; *p; ++p)
            {
                if (*p == ',')
                {
                    if (!part.empty())
                    {
                        tags.push_back(part);
                    }
                    part.clear();
                }
                else if (*p != ' ')
                {
                    part += *p;
                }
            }
            if (!part.empty())
            {
                tags.push_back(part);
            }
            result.changed = true;
            result.value = std::move(tags);
        }
    }
    else
    {
        ImGui::TextDisabled("%s (unsupported field kind)", field.Label().c_str());
    }

    result.activated = ImGui::IsItemActivated();
    result.deactivated = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::EndDisabled();

    if (!field.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        EditorTooltip("%s", field.tooltip.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset"))
    {
        result.reset = true;
    }
    if (!field.tooltip.empty() && ImGui::IsItemHovered())
    {
        EditorTooltip("Reset to the registered C++ default.");
    }
    ImGui::PopID();
    return result;
}

void ApplyFieldValue(Fem::Registry& registry, Entity entity, const Fem::ComponentReflection& component,
                     const std::string& fieldName, const nlohmann::json& value)
{
    if (!registry.valid(entity))
    {
        return;
    }
    void* object = component.getMutable(registry, entity);
    if (!object)
    {
        return;
    }
    for (const Fem::Field& field : *component.fields)
    {
        if (field.name == fieldName)
        {
            field.FromJson(object, value);
            return;
        }
    }
}

// Field edits are coalesced: a drag or a typed word becomes one undo step, not
// one step per frame / keystroke.
class FieldEditCommand final : public Fem::Command
{
public:
    FieldEditCommand(Fem::Registry& registry, Entity entity, const Fem::ComponentReflection& component,
                     std::string fieldName, nlohmann::json before, nlohmann::json after)
        : registry_(&registry), entity_(entity), component_(&component), fieldName_(std::move(fieldName)),
          before_(std::move(before)), after_(std::move(after))
    {
    }

    void Execute() override
    {
        ApplyFieldValue(*registry_, entity_, *component_, fieldName_, after_);
    }
    void Undo() override
    {
        ApplyFieldValue(*registry_, entity_, *component_, fieldName_, before_);
    }
    std::string Name() const override
    {
        return "Edit " + component_->name + "." + fieldName_;
    }

private:
    Fem::Registry* registry_;
    Entity entity_;
    const Fem::ComponentReflection* component_;
    std::string fieldName_;
    nlohmann::json before_;
    nlohmann::json after_;
};

EntityTransform ReadTransform(Fem::Registry& registry, Entity entity)
{
    if (registry.valid(entity))
    {
        if (const TransformComponent* transform = registry.try_get<TransformComponent>(entity))
        {
            return {transform->x, transform->y, transform->rotation, transform->scaleX, transform->scaleY};
        }
    }
    return {};
}

void WriteTransform(Fem::Registry& registry, Entity entity, const EntityTransform& value)
{
    if (registry.valid(entity))
    {
        if (TransformComponent* transform = registry.try_get<TransformComponent>(entity))
        {
            transform->x = value.x;
            transform->y = value.y;
            transform->prevX = value.x;
            transform->prevY = value.y;
            transform->rotation = value.rotation;
            transform->scaleX = value.scaleX;
            transform->scaleY = value.scaleY;
        }
    }
}

Entity ParentEntity(const Fem::Registry& registry, Entity entity)
{
    if (const Fem::HierarchyComponent* hierarchy = registry.try_get<Fem::HierarchyComponent>(entity))
    {
        if (hierarchy->parent != Fem::kNoParent)
        {
            const Entity parent = static_cast<Entity>(hierarchy->parent);
            if (parent != entity && registry.valid(parent))
            {
                return parent;
            }
        }
    }
    return entt::null;
}

bool IsAncestor(const Fem::Registry& registry, Entity ancestor, Entity node)
{
    Entity current = node;
    for (int depth = 0; depth < 128 && current != entt::null; ++depth)
    {
        if (current == ancestor)
        {
            return true;
        }
        current = ParentEntity(registry, current);
    }
    return false;
}

void SetEntityParent(Fem::Registry& registry, Entity child, Entity parent)
{
    if (child == entt::null || !registry.valid(child) || child == parent)
    {
        return;
    }
    if (parent != entt::null && (!registry.valid(parent) || IsAncestor(registry, child, parent)))
    {
        return;
    }
    Fem::HierarchyComponent& hierarchy =
        registry.get_or_emplace<Fem::HierarchyComponent>(child);
    hierarchy.parent = parent == entt::null ? Fem::kNoParent : static_cast<std::uint32_t>(parent);
}

EntityTransform ComposeTransforms(const EntityTransform& parent, const EntityTransform& local)
{
    constexpr float pi = 3.14159265358979323846f;
    const float radians = parent.rotation * pi / 180.f;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    EntityTransform result;
    result.x = parent.x + c * local.x * parent.scaleX - s * local.y * parent.scaleY;
    result.y = parent.y + s * local.x * parent.scaleX + c * local.y * parent.scaleY;
    result.rotation = parent.rotation + local.rotation;
    result.scaleX = parent.scaleX * local.scaleX;
    result.scaleY = parent.scaleY * local.scaleY;
    return result;
}

EntityTransform WorldTransformOf(Fem::Registry& registry, Entity entity, int depth = 0)
{
    const EntityTransform local = ReadTransform(registry, entity);
    if (depth > 64)
    {
        return local;
    }
    const Entity parent = ParentEntity(registry, entity);
    if (parent == entt::null)
    {
        return local;
    }
    return ComposeTransforms(WorldTransformOf(registry, parent, depth + 1), local);
}

EntityTransform LocalTransformFromWorld(Fem::Registry& registry, Entity entity,
                                        const EntityTransform& world)
{
    const Entity parent = ParentEntity(registry, entity);
    if (parent == entt::null)
    {
        return world;
    }
    const EntityTransform parentWorld = WorldTransformOf(registry, parent);
    EntityTransform local;
    local.scaleX = std::abs(parentWorld.scaleX) > 0.0001f ? world.scaleX / parentWorld.scaleX
                                                         : world.scaleX;
    local.scaleY = std::abs(parentWorld.scaleY) > 0.0001f ? world.scaleY / parentWorld.scaleY
                                                         : world.scaleY;
    local.rotation = world.rotation - parentWorld.rotation;
    constexpr float pi = 3.14159265358979323846f;
    const float radians = -parentWorld.rotation * pi / 180.f;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float dx = world.x - parentWorld.x;
    const float dy = world.y - parentWorld.y;
    local.x = (c * dx - s * dy) /
              (std::abs(parentWorld.scaleX) > 0.0001f ? parentWorld.scaleX : 1.f);
    local.y = (s * dx + c * dy) /
              (std::abs(parentWorld.scaleY) > 0.0001f ? parentWorld.scaleY : 1.f);
    return local;
}

sf::Vector2f WorldDeltaToLocal(Fem::Registry& registry, Entity entity, sf::Vector2f worldDelta)
{
    const Entity parent = ParentEntity(registry, entity);
    if (parent == entt::null)
    {
        return worldDelta;
    }
    const EntityTransform parentWorld = WorldTransformOf(registry, parent);
    constexpr float pi = 3.14159265358979323846f;
    const float radians = -parentWorld.rotation * pi / 180.f;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return {(c * worldDelta.x - s * worldDelta.y) / (std::abs(parentWorld.scaleX) > 0.0001f ? parentWorld.scaleX : 1.f),
            (s * worldDelta.x + c * worldDelta.y) / (std::abs(parentWorld.scaleY) > 0.0001f ? parentWorld.scaleY : 1.f)};
}

class TransformStateCommand final : public Fem::Command
{
public:
    TransformStateCommand(Fem::Registry& registry, Entity entity, EntityTransform before, EntityTransform after)
        : registry_(&registry), entity_(entity), before_(before), after_(after)
    {
    }
    void Execute() override
    {
        WriteTransform(*registry_, entity_, after_);
    }
    void Undo() override
    {
        WriteTransform(*registry_, entity_, before_);
    }
    std::string Name() const override
    {
        return "Transform entity";
    }

private:
    Fem::Registry* registry_;
    Entity entity_;
    EntityTransform before_;
    EntityTransform after_;
};

class TransformManyCommand final : public Fem::Command
{
public:
    struct Entry
    {
        Entity entity;
        EntityTransform before;
        EntityTransform after;
    };

    TransformManyCommand(Fem::Registry& registry, std::vector<Entry> entries)
        : registry_(&registry), entries_(std::move(entries))
    {
    }
    void Execute() override
    {
        for (const Entry& entry : entries_)
        {
            WriteTransform(*registry_, entry.entity, entry.after);
        }
    }
    void Undo() override
    {
        for (const Entry& entry : entries_)
        {
            WriteTransform(*registry_, entry.entity, entry.before);
        }
    }
    std::string Name() const override
    {
        return "Transform selection";
    }

private:
    Fem::Registry* registry_;
    std::vector<Entry> entries_;
};

nlohmann::json CaptureEntity(Fem::Registry& registry, Entity entity)
{
    nlohmann::json object = nlohmann::json::object();
    for (const Fem::ComponentReflection& component : Fem::Mirror::Instance().Components())
    {
        if (component.has && component.has(registry, entity))
        {
            component.capture(registry, entity, object);
        }
    }
    return object;
}

Entity ApplyEntity(Fem::Registry& registry, const nlohmann::json& object)
{
    const Entity entity = registry.create();
    for (const Fem::ComponentReflection& component : Fem::Mirror::Instance().Components())
    {
        if (object.contains(component.jsonKey))
        {
            component.apply(registry, entity, object[component.jsonKey]);
        }
    }
    return entity;
}

// Creating an entity is one undo step. Redo rebuilds it from the captured
// component JSON, so an Alt-drag duplicate can be undone as a single action.
class SpawnEntityCommand final : public Fem::Command
{
public:
    SpawnEntityCommand(Fem::Registry& registry, nlohmann::json object, Entity& selection,
                       Entity existing = entt::null)
        : registry_(&registry), object_(std::move(object)), selection_(&selection), entity_(existing)
    {
    }
    void Execute() override
    {
        if (!registry_->valid(entity_))
        {
            entity_ = ApplyEntity(*registry_, object_);
        }
        *selection_ = entity_;
    }
    void Undo() override
    {
        if (registry_->valid(entity_))
        {
            registry_->destroy(entity_);
        }
        if (*selection_ == entity_)
        {
            *selection_ = entt::null;
        }
        entity_ = entt::null;
    }
    std::string Name() const override
    {
        return "Duplicate entity";
    }

private:
    Fem::Registry* registry_;
    Entity* selection_;
    nlohmann::json object_;
    Entity entity_ = entt::null;
};

class SpawnGroupCommand final : public Fem::Command
{
public:
    SpawnGroupCommand(Fem::Registry& registry, std::vector<nlohmann::json> objects, Entity& selection,
                      std::vector<Entity> existing = {})
        : registry_(&registry), objects_(std::move(objects)), selection_(&selection),
          entities_(std::move(existing))
    {
    }
    void Execute() override
    {
        std::vector<Entity> result;
        for (std::size_t i = 0; i < objects_.size(); ++i)
        {
            if (i < entities_.size() && registry_->valid(entities_[i]))
            {
                result.push_back(entities_[i]);
            }
            else
            {
                result.push_back(ApplyEntity(*registry_, objects_[i]));
            }
        }
        entities_ = std::move(result);
        if (!entities_.empty())
        {
            *selection_ = entities_.back();
        }
    }
    void Undo() override
    {
        for (Entity entity : entities_)
        {
            if (registry_->valid(entity))
            {
                registry_->destroy(entity);
            }
        }
        entities_.clear();
        *selection_ = entt::null;
    }
    std::string Name() const override
    {
        return "Paste entities";
    }

private:
    Fem::Registry* registry_;
    std::vector<nlohmann::json> objects_;
    Entity* selection_;
    std::vector<Entity> entities_;
};

class DeleteEntityCommand final : public Fem::Command
{
public:
    DeleteEntityCommand(Fem::Registry& registry, nlohmann::json object, Entity entity, Entity& selection)
        : registry_(&registry), object_(std::move(object)), entity_(entity), selection_(&selection)
    {
    }
    void Execute() override
    {
        if (registry_->valid(entity_))
        {
            registry_->destroy(entity_);
        }
        *selection_ = entt::null;
    }
    void Undo() override
    {
        entity_ = ApplyEntity(*registry_, object_);
        *selection_ = entity_;
    }
    std::string Name() const override
    {
        return "Delete entity";
    }

private:
    Fem::Registry* registry_;
    nlohmann::json object_;
    Entity entity_ = entt::null;
    Entity* selection_;
};

class TransformMoveCommand final : public Fem::Command
{
public:
    TransformMoveCommand(Fem::Registry& registry, Entity entity, sf::Vector2f before, sf::Vector2f after)
        : registry_(&registry), entity_(entity), before_(before), after_(after)
    {
    }

    void Execute() override
    {
        Apply(after_);
    }
    void Undo() override
    {
        Apply(before_);
    }
    std::string Name() const override
    {
        return "Move entity";
    }

private:
    void Apply(sf::Vector2f value)
    {
        if (registry_->valid(entity_))
        {
            if (TransformComponent* transform = registry_->try_get<TransformComponent>(entity_))
            {
                transform->x = value.x;
                transform->y = value.y;
                transform->prevX = value.x;
                transform->prevY = value.y;
            }
        }
    }

    Fem::Registry* registry_;
    Entity entity_;
    sf::Vector2f before_;
    sf::Vector2f after_;
};

struct PendingFieldEdit
{
    Entity entity = entt::null;
    const Fem::ComponentReflection* component = nullptr;
    std::string fieldName;
    nlohmann::json before = nullptr;
};

std::map<std::string, PendingFieldEdit> g_pendingEdits;

// The editor clipboard is plain JSON text on the system clipboard. The engine
// does not need to know about it: scene entities, groups and visual-script
// graphs all use the same versioned envelope and any tool can inspect it.
constexpr const char* kClipboardTypeEntity = "femboy.entity";
constexpr const char* kClipboardTypeGroup = "femboy.group";
constexpr const char* kClipboardTypeVisualScript = "femboy.visualscript";

void SetEditorClipboard(const char* type, const nlohmann::json& data)
{
    nlohmann::json envelope;
    envelope["format"] = "femboy.clipboard";
    envelope["version"] = 1;
    envelope["type"] = type;
    envelope["data"] = data;
    ImGui::SetClipboardText(envelope.dump(2).c_str());
}

bool GetEditorClipboard(std::string& type, nlohmann::json& data)
{
    const char* text = ImGui::GetClipboardText();
    if (text == nullptr || text[0] == '\0')
    {
        return false;
    }
    try
    {
        const nlohmann::json envelope = nlohmann::json::parse(text);
        if (!envelope.is_object() || envelope.value("format", "") != "femboy.clipboard")
        {
            return false;
        }
        type = envelope.value("type", "");
        data = envelope.value("data", nlohmann::json::object());
        return !type.empty();
    }
    catch (const std::exception&)
    {
        return false;
    }
}

std::string EditKey(Entity entity, const Fem::ComponentReflection& component, const std::string& fieldName)
{
    return std::to_string(entt::to_integral(entity)) + "/" + component.jsonKey + "/" + fieldName;
}

// Draws the editable fields of one component. `defaultsObject` is null for a
// live instance and points at the ClassDefaults override otherwise.
void DrawComponentFields(Fem::Registry& registry, Entity entity,
                         const Fem::ComponentReflection& component, nlohmann::json* defaultsObject,
                         bool advanced, const std::string& filter)
{
    if (!component.fields)
    {
        return;
    }
    void* object = defaultsObject ? nullptr : component.getMutable(registry, entity);
    if (!defaultsObject && !object)
    {
        return;
    }

    std::string lastCategory;
    for (const Fem::Field& field : *component.fields)
    {
        if (field.advanced && !advanced)
        {
            continue;
        }
        if (!filter.empty())
        {
            std::string haystack = field.Label() + " " + field.name + " " + field.category + " " + field.tooltip;
            std::string needle = filter;
            std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::transform(needle.begin(), needle.end(), needle.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (haystack.find(needle) == std::string::npos)
            {
                continue;
            }
        }

        if (!field.category.empty() && field.category != lastCategory)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", field.category.c_str());
            lastCategory = field.category;
        }

        nlohmann::json value = field.defaultValue;
        if (defaultsObject)
        {
            if (field.flattened && field.defaultValue.is_object())
            {
                for (const auto& [key, entry] : field.defaultValue.items())
                {
                    if (defaultsObject->contains(key))
                    {
                        value[key] = (*defaultsObject)[key];
                    }
                }
            }
            else if (defaultsObject->contains(field.name))
            {
                value = (*defaultsObject)[field.name];
            }
        }
        else
        {
            value = field.ToJson(object);
        }

        const bool editable = field.edit == Fem::EditFlags::EditAnywhere ||
                              (defaultsObject && field.edit == Fem::EditFlags::EditDefaultsOnly);
        const nlohmann::json before = value;
        const FieldWidgetResult result = DrawFieldWidget(field, value, editable);

        if (result.reset)
        {
            if (defaultsObject)
            {
                if (field.flattened && field.defaultValue.is_object())
                {
                    for (const auto& [key, entry] : field.defaultValue.items())
                    {
                        defaultsObject->erase(key);
                    }
                }
                else
                {
                    defaultsObject->erase(field.name);
                }
            }
            else if (object)
            {
                const nlohmann::json before = field.ToJson(object);
                field.FromJson(object, field.defaultValue);
                const nlohmann::json after = field.ToJson(object);
                if (before != after)
                {
                    Fem::UndoStack::Instance().Push(std::make_shared<FieldEditCommand>(
                        registry, entity, component, field.name, before, after));
                }
            }
            continue;
        }

        if (!result.changed || !editable)
        {
            continue;
        }

        if (defaultsObject)
        {
            if (field.flattened && result.value.is_object())
            {
                for (const auto& [key, entry] : result.value.items())
                {
                    (*defaultsObject)[key] = entry;
                }
            }
            else
            {
                (*defaultsObject)[field.name] = result.value;
            }
            continue;
        }

        if (!object)
        {
            continue;
        }
        field.FromJson(object, result.value);

        const std::string key = EditKey(entity, component, field.name);
        if (result.activated)
        {
            g_pendingEdits[key] = PendingFieldEdit{entity, &component, field.name, before};
        }
        if (result.deactivated)
        {
            const auto it = g_pendingEdits.find(key);
            const nlohmann::json after = field.ToJson(object);
            const nlohmann::json start = it != g_pendingEdits.end() ? it->second.before : before;
            g_pendingEdits.erase(key);
            if (start != after)
            {
                Fem::UndoStack::Instance().Push(std::make_shared<FieldEditCommand>(
                    registry, entity, component, field.name, start, after));
            }
        }
    }
}
} // namespace

// The graph editor draws nodes on a pan/zoom canvas with typed pins, a
// searchable palette and generated widgets for scalar parameters. The same
// graph format and compiler are used; the canvas only changes presentation.
// One graph document. Like a Blueprint asset in Unreal, every file keeps its
// own graph, selection, pan/zoom, palette filter and generated source. The
// document is presented as its own dockable editor tab.
struct VisualScriptDocument
{
    char path[512] = "";
    std::string customTitle;
    char label[128] = "";
    char params[4096] = "{}";
    char source[32768] = "";
    bool open = true;
    bool focus = false;
    int uid = 0;
    std::string status;
    Fem::VisualScriptGraph graph;
    int selected = 0;
    int lastSelected = -1;
    int addType = 0;
    int fromNode = 0;
    int fromPin = 0;
    int toNode = 0;
    int toPin = 0;
    float panX = 80.f, panY = 60.f;
    float zoom = 1.f;
    char paletteFilter[96] = "";
    char debugValue[4096] = "";
    int dragNode = 0;
    sf::Vector2f dragOffset{0.f, 0.f};
    int linkNode = 0;
    int linkPin = -1;
    bool linkIsOutput = false;

    std::string Title() const
    {
        if (!customTitle.empty())
        {
            return customTitle;
        }
        if (path[0] != '\0')
        {
            return std::filesystem::path(path).filename().string();
        }
        return "Untitled " + std::to_string(uid);
    }

    // ImGui needs a stable unique ID even when two files share a filename.
    std::string WindowTitle() const
    {
        return Title() + "###vsdoc_" + std::to_string(uid);
    }

    void RefreshNodeText()
    {
        const Fem::VSNode* node = graph.Node(selected);
        if (!node)
        {
            lastSelected = -1;
            return;
        }
        std::snprintf(label, sizeof(label), "%s", node->label.c_str());
        const std::string text = nlohmann::json(node->params).dump(2);
        std::snprintf(params, sizeof(params), "%s", text.c_str());
        lastSelected = selected;
    }

    bool Compile()
    {
        std::string error;
        const std::string code = graph.Compile(&error);
        std::snprintf(source, sizeof(source), "%s", code.c_str());
        status = error.empty() ? "compiled" : ("error: " + error);
        return error.empty();
    }
};

// Owns the open graph documents. Opening the same file twice focuses the
// existing tab instead of creating a duplicate editor.
struct VisualScriptWorkspace
{
    std::vector<std::unique_ptr<VisualScriptDocument>> documents;
    char openPath[512] = "";
    std::string status;
    int nextUid = 1;

    static std::string NormalizePath(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
        if (ec)
        {
            normalized = std::filesystem::path(path).lexically_normal();
        }
        std::string text = normalized.string();
#ifdef _WIN32
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
        return text;
    }

    VisualScriptDocument* Find(const std::string& path) const
    {
        const std::string normalized = NormalizePath(path);
        for (const std::unique_ptr<VisualScriptDocument>& document : documents)
        {
            if (NormalizePath(document->path) == normalized)
            {
                return document.get();
            }
        }
        return nullptr;
    }

    VisualScriptDocument& NewDocument()
    {
        auto document = std::make_unique<VisualScriptDocument>();
        document->uid = nextUid++;
        document->status = "new graph";
        documents.push_back(std::move(document));
        return *documents.back();
    }

    VisualScriptDocument& PasteDocument(const nlohmann::json& graphData, const std::string& name)
    {
        auto document = std::make_unique<VisualScriptDocument>();
        document->uid = nextUid++;
        document->customTitle = name.empty() ? "Pasted graph " + std::to_string(document->uid)
                                             : name + " (copy)";
        if (document->graph.FromJson(graphData))
        {
            document->Compile();
        }
        else
        {
            document->status = "clipboard graph is invalid";
        }
        document->focus = true;
        documents.push_back(std::move(document));
        return *documents.back();
    }

    VisualScriptDocument& OpenDocument(const std::string& path)
    {
        if (VisualScriptDocument* existing = Find(path))
        {
            existing->open = true;
            existing->focus = true;
            return *existing;
        }

        auto document = std::make_unique<VisualScriptDocument>();
        document->uid = nextUid++;
        std::snprintf(document->path, sizeof(document->path), "%s", path.c_str());
        if (document->graph.LoadFromFile(path))
        {
            document->Compile();
        }
        else
        {
            document->status = "could not open graph";
        }
        document->focus = true;
        documents.push_back(std::move(document));
        return *documents.back();
    }
};

int FindDocumentIndex(const VisualScriptWorkspace& workspace, const VisualScriptDocument* document)
{
    for (std::size_t i = 0; i < workspace.documents.size(); ++i)
    {
        if (workspace.documents[i].get() == document)
        {
            return static_cast<int>(i);
        }
    }
    return kTopTabEditor;
}

void DrawEditorSettingsContent(EditorSettingsState& editorSettings, float& userZoom, Panel* panels, int panelCount,
                               bool& resetLayout, const char*& focusPanel, VisualScriptWorkspace& visualScripts,
                               int& activeTopTab)
{
    ImGui::Text("Interface");
    ImGui::SetNextItemWidth(240.f);
    ImGui::SliderFloat("UI zoom", &userZoom, 0.75f, 2.f, "%.2fx");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##zoom"))
    {
        userZoom = 1.f;
    }
    if (ImGui::Checkbox("Show project browser on startup", &editorSettings.showBrowserOnStartup))
    {
        editorSettings.Save();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##startup"))
    {
        editorSettings.showBrowserOnStartup = true;
        editorSettings.Save();
    }
    if (ImGui::Checkbox("Avoid function keys (F1-F12)", &editorSettings.noFunctionKeys))
    {
        editorSettings.Save();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##fnkeys"))
    {
        editorSettings.noFunctionKeys = false;
        editorSettings.Save();
    }
    ImGui::TextDisabled("Helper shortcut when function keys are unavailable: Ctrl+Alt+S.");
    if (ImGui::Checkbox("Show editor tooltips", &editorSettings.tooltipsEnabled))
    {
        editorSettings.Save();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##tooltips"))
    {
        editorSettings.tooltipsEnabled = true;
        editorSettings.Save();
    }

    if (ImGui::Button("Save editor preferences"))
    {
        editorSettings.uiZoom = userZoom;
        editorSettings.Save();
    }

    ImGui::Separator();
    ImGui::Text("Object hotkeys");
    ImGui::TextDisabled("Click Rebind, then press the new key combination. Esc cancels.");
    struct Binding
    {
        const char* label;
        int* chord;
        int defaultChord;
    };
    Binding bindings[] = {
        {"Move gizmo", &editorSettings.hkMove, ImGuiKey_W},
        {"Rotate gizmo", &editorSettings.hkRotate, ImGuiKey_E},
        {"Scale gizmo", &editorSettings.hkScale, ImGuiKey_R},
        {"Duplicate", &editorSettings.hkDuplicate, ImGuiMod_Ctrl | ImGuiKey_D},
        {"Copy", &editorSettings.hkCopy, ImGuiMod_Ctrl | ImGuiKey_C},
        {"Paste", &editorSettings.hkPaste, ImGuiMod_Ctrl | ImGuiKey_V},
        {"Delete", &editorSettings.hkDelete, ImGuiKey_Delete},
        {"Focus selected", &editorSettings.hkFocus, ImGuiKey_F},
    };
    for (int i = 0; i < static_cast<int>(std::size(bindings)); ++i)
    {
        ImGui::PushID(i);
        ImGui::TextUnformatted(bindings[i].label);
        ImGui::SameLine(190.f);
        ImGui::TextDisabled("%s", KeyChordName(*bindings[i].chord).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Rebind"))
        {
            editorSettings.rebindingHotkey = i;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset"))
        {
            *bindings[i].chord = bindings[i].defaultChord;
            editorSettings.Save();
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Reset all hotkeys"))
    {
        for (Binding& binding : bindings)
        {
            *binding.chord = binding.defaultChord;
        }
        editorSettings.Save();
    }

    if (editorSettings.rebindingHotkey >= 0 &&
        editorSettings.rebindingHotkey < static_cast<int>(std::size(bindings)))
    {
        ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.f, 1.f), "Press a key combination for %s... (Esc cancels)",
                           bindings[editorSettings.rebindingHotkey].label);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            editorSettings.rebindingHotkey = -1;
        }
        else
        {
            const ImGuiIO& input = ImGui::GetIO();
            int modifiers = 0;
            if (input.KeyCtrl)
            {
                modifiers |= ImGuiMod_Ctrl;
            }
            if (input.KeyShift)
            {
                modifiers |= ImGuiMod_Shift;
            }
            if (input.KeyAlt)
            {
                modifiers |= ImGuiMod_Alt;
            }
            if (input.KeySuper)
            {
                modifiers |= ImGuiMod_Super;
            }
            for (int keyValue = ImGuiKey_NamedKey_BEGIN; keyValue < ImGuiKey_NamedKey_END; ++keyValue)
            {
                if (keyValue >= ImGuiKey_LeftCtrl && keyValue <= ImGuiKey_RightSuper)
                {
                    continue;
                }
                const ImGuiKey key = static_cast<ImGuiKey>(keyValue);
                if (ImGui::IsKeyPressed(key, false))
                {
                    *bindings[editorSettings.rebindingHotkey].chord = modifiers | keyValue;
                    editorSettings.rebindingHotkey = -1;
                    editorSettings.Save();
                    break;
                }
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("Windows");
    for (int i = 0; i < panelCount; ++i)
    {
        Panel& panel = panels[i];
        ImGui::PushID(panel.name);
        ImGui::Checkbox(panel.name, &panel.open);
        ImGui::SameLine();
        if (ImGui::SmallButton("Focus"))
        {
            panel.open = true;
            focusPanel = panel.name;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset pos"))
        {
            ResetPanelWindow(panel.name, i);
        }
        if (ImGui::IsItemHovered())
        {
            EditorTooltip("Undock this window and restore its default floating position.");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset vis"))
        {
            panel.open = panel.defaultOpen;
        }
        if (ImGui::IsItemHovered())
        {
            EditorTooltip("Restore this window's default visibility.");
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Reset all positions"))
    {
        for (int i = 0; i < panelCount; ++i)
        {
            ResetPanelWindow(panels[i].name, i);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset all visibility"))
    {
        for (int i = 0; i < panelCount; ++i)
        {
            panels[i].open = panels[i].defaultOpen;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset layout"))
    {
        resetLayout = true;
    }

    if (!visualScripts.documents.empty())
    {
        ImGui::Separator();
        ImGui::Text("Graph document tabs");
        for (int i = 0; i < static_cast<int>(visualScripts.documents.size()); ++i)
        {
            VisualScriptDocument& document = *visualScripts.documents[static_cast<std::size_t>(i)];
            ImGui::PushID(document.uid);
            ImGui::TextUnformatted(document.Title().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Open"))
            {
                document.open = true;
                activeTopTab = i;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Close"))
            {
                document.open = false;
                if (activeTopTab == i)
                {
                    activeTopTab = kTopTabEditor;
                }
            }
            ImGui::PopID();
        }
    }

    ImGui::Separator();
    ImGui::Text("Accessibility (engine)");
    ImGui::TextDisabled("Shared with games that use the engine accessibility registry.");
    Fem::AccessibilitySettings& access = Fem::AccessibilitySettings::Instance();
    const auto commitAccessibility = [&access]()
    {
        access.WriteToRegistry();
        access.Apply();
    };

    const char* colorModes[] = {"None", "Protanopia", "Deuteranopia", "Tritanopia", "Achromatopsia"};
    ImGui::SetNextItemWidth(220.f);
    if (ImGui::Combo("Colour filter", &access.colorblindMode, colorModes, 5))
    {
        commitAccessibility();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##colour"))
    {
        access.colorblindMode = 0;
        commitAccessibility();
    }
    if (ImGui::Checkbox("High contrast", &access.highContrast))
    {
        commitAccessibility();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##contrast"))
    {
        access.highContrast = false;
        commitAccessibility();
    }

    if (ImGui::Checkbox("Subtitles", &access.subtitles))
    {
        commitAccessibility();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##subtitles"))
    {
        access.subtitles = true;
        commitAccessibility();
    }
    ImGui::SetNextItemWidth(240.f);
    if (ImGui::SliderFloat("Subtitle size", &access.subtitleScale, 0.75f, 2.f, "%.2fx"))
    {
        commitAccessibility();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##subtitlesize"))
    {
        access.subtitleScale = 1.f;
        commitAccessibility();
    }

    if (ImGui::Checkbox("Screen reader announcements", &access.screenReader))
    {
        commitAccessibility();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##reader"))
    {
        access.screenReader = false;
        commitAccessibility();
    }
    ImGui::SetNextItemWidth(240.f);
    if (ImGui::SliderFloat("Speech volume", &access.ttsVolume, 0.f, 1.f, "%.2f"))
    {
        commitAccessibility();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##speechvolume"))
    {
        access.ttsVolume = 0.8f;
        commitAccessibility();
    }
    ImGui::SameLine();
    if (ImGui::Button("Speak test"))
    {
        Fem::Accessibility::Instance().Announce("FemboyEngine editor accessibility test.");
    }

    if (ImGui::Checkbox("Reduce motion", &access.reduceMotion))
    {
        commitAccessibility();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##motion"))
    {
        access.reduceMotion = false;
        commitAccessibility();
    }
    if (ImGui::Checkbox("Reduce flashing", &access.reduceFlash))
    {
        commitAccessibility();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##flash"))
    {
        access.reduceFlash = false;
        commitAccessibility();
    }
    if (ImGui::Checkbox("Photosensitivity safety", &access.photosensitivity))
    {
        commitAccessibility();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##photosensitivity"))
    {
        access.photosensitivity = false;
        commitAccessibility();
    }
    if (ImGui::Checkbox("One-handed mode", &access.oneHanded))
    {
        commitAccessibility();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##onehanded"))
    {
        access.oneHanded = false;
        commitAccessibility();
    }

    if (ImGui::Button("Reset all accessibility"))
    {
        access.ResetToDefaults();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save accessibility"))
    {
        editorSettings.Save();
    }
}

namespace
{
enum class VSPinKind
{
    Exec,
    Number,
    Bool,
    String,
    Object,
    Tag,
    Color,
    Array,
    Any
};

struct VSPinSpec
{
    const char* name;
    VSPinKind kind;
    // -1 means "the vector index". Switch uses sparse pins (10+case, 99) to
    // match the compiler's serialized pin model.
    int pin = -1;
};

struct VSNodePins
{
    std::vector<VSPinSpec> inputs;
    std::vector<VSPinSpec> outputs;
};

VSNodePins PinsFor(Fem::VSNodeType type)
{
    using T = Fem::VSNodeType;
    VSNodePins pins;
    auto exec = [](std::vector<VSPinSpec>& list, const char* name = "Then")
    { list.push_back({name, VSPinKind::Exec}); };
    auto val = [](std::vector<VSPinSpec>& list, const char* name, VSPinKind kind = VSPinKind::Any)
    { list.push_back({name, kind}); };

    switch (type)
    {
        case T::IntLiteral:
        case T::FloatLiteral:
            val(pins.outputs, "Value", VSPinKind::Number);
            break;
        case T::StringLiteral:
            val(pins.outputs, "Value", VSPinKind::String);
            break;
        case T::BoolLiteral:
            val(pins.outputs, "Value", VSPinKind::Bool);
            break;
        case T::NullLiteral:
            val(pins.outputs, "Value", VSPinKind::Any);
            break;
        case T::Color:
            val(pins.inputs, "R", VSPinKind::Number);
            val(pins.inputs, "G", VSPinKind::Number);
            val(pins.inputs, "B", VSPinKind::Number);
            val(pins.inputs, "A", VSPinKind::Number);
            val(pins.outputs, "Color", VSPinKind::Color);
            break;
        case T::Time:
            val(pins.outputs, "Seconds", VSPinKind::Number);
            break;
        case T::Add:
        case T::Subtract:
        case T::Multiply:
        case T::Divide:
        case T::Modulo:
        case T::Min:
        case T::Max:
        case T::Pow:
        case T::Atan2:
            val(pins.inputs, "A", VSPinKind::Number);
            val(pins.inputs, "B", VSPinKind::Number);
            val(pins.outputs, "Result", VSPinKind::Number);
            break;
        case T::And:
        case T::Or:
            val(pins.inputs, "A", VSPinKind::Bool);
            val(pins.inputs, "B", VSPinKind::Bool);
            val(pins.outputs, "Result", VSPinKind::Bool);
            break;
        case T::Not:
            val(pins.inputs, "A", VSPinKind::Bool);
            val(pins.outputs, "Result", VSPinKind::Bool);
            break;
        case T::Compare:
            val(pins.inputs, "A");
            val(pins.inputs, "B");
            val(pins.outputs, "Result", VSPinKind::Bool);
            break;
        case T::Branch:
            exec(pins.inputs, "In");
            val(pins.inputs, "Condition", VSPinKind::Bool);
            exec(pins.outputs, "True");
            exec(pins.outputs, "False");
            break;
        case T::Sequence:
            exec(pins.inputs, "In");
            exec(pins.outputs, "Step 0");
            exec(pins.outputs, "Step 1");
            exec(pins.outputs, "Step 2");
            exec(pins.outputs, "Step 3");
            break;
        case T::ForLoop:
            exec(pins.inputs, "In");
            val(pins.inputs, "Start", VSPinKind::Number);
            val(pins.inputs, "Count", VSPinKind::Number);
            val(pins.inputs, "Step", VSPinKind::Number);
            exec(pins.outputs, "Body");
            break;
        case T::While:
        case T::DoWhile:
            exec(pins.inputs, "In");
            val(pins.inputs, "Condition", VSPinKind::Bool);
            exec(pins.outputs, "Body");
            break;
        case T::Delay:
            exec(pins.inputs, "In");
            val(pins.inputs, "Seconds", VSPinKind::Number);
            exec(pins.outputs, "Then");
            break;
        case T::GetVar:
            val(pins.outputs, "Value");
            break;
        case T::SetVar:
        case T::DeclareVar:
            exec(pins.inputs, "In");
            val(pins.inputs, "Value");
            exec(pins.outputs, "Then");
            break;
        case T::CallFunction:
            exec(pins.inputs, "In");
            val(pins.inputs, "Arg 0");
            val(pins.inputs, "Arg 1");
            val(pins.inputs, "Arg 2");
            val(pins.inputs, "Arg 3");
            exec(pins.outputs, "Then");
            break;
        case T::Print:
            exec(pins.inputs, "In");
            val(pins.inputs, "Value");
            exec(pins.outputs, "Then");
            break;
        case T::Output:
            exec(pins.inputs, "In");
            val(pins.inputs, "Value");
            break;
        case T::Return:
            exec(pins.inputs, "In");
            val(pins.inputs, "Value");
            break;
        case T::Break:
        case T::Continue:
            exec(pins.inputs, "In");
            break;
        case T::GetWidgetProp:
            val(pins.outputs, "Value");
            break;
        case T::SetWidgetProp:
            exec(pins.inputs, "In");
            val(pins.inputs, "Value");
            exec(pins.outputs, "Then");
            break;
        case T::OnStart:
        case T::OnUpdate:
        case T::OnClick:
        case T::OnHover:
        case T::OnSignal:
            exec(pins.outputs, "Then");
            break;
        case T::OnCollision:
            exec(pins.outputs, "Then");
            val(pins.outputs, "Other", VSPinKind::Object);
            break;
        case T::OnTagAdded:
        case T::OnTagRemoved:
            exec(pins.outputs, "Then");
            val(pins.outputs, "Tag", VSPinKind::Tag);
            break;
        case T::TagLiteral:
            val(pins.outputs, "Tag", VSPinKind::Tag);
            break;
        case T::HasTag:
        case T::TagQuery:
            val(pins.inputs, "Object", VSPinKind::Object);
            val(pins.inputs, "Tag", VSPinKind::Tag);
            val(pins.outputs, "Result", VSPinKind::Bool);
            break;
        case T::AddTag:
        case T::RemoveTag:
            exec(pins.inputs, "In");
            val(pins.inputs, "Object", VSPinKind::Object);
            val(pins.inputs, "Tag", VSPinKind::Tag);
            exec(pins.outputs, "Then");
            break;
        case T::Lerp:
            val(pins.inputs, "A", VSPinKind::Number);
            val(pins.inputs, "B", VSPinKind::Number);
            val(pins.inputs, "T", VSPinKind::Number);
            val(pins.outputs, "Result");
            break;
        case T::Clamp:
            val(pins.inputs, "Value", VSPinKind::Number);
            val(pins.inputs, "Min", VSPinKind::Number);
            val(pins.inputs, "Max", VSPinKind::Number);
            val(pins.outputs, "Result", VSPinKind::Number);
            break;
        case T::TextureSample:
            val(pins.inputs, "UV");
            val(pins.outputs, "Color", VSPinKind::Color);
            break;
        case T::Select:
            val(pins.inputs, "Condition", VSPinKind::Bool);
            val(pins.inputs, "True");
            val(pins.inputs, "False");
            val(pins.outputs, "Result");
            break;
        case T::ArrayLiteral:
            val(pins.inputs, "0");
            val(pins.inputs, "1");
            val(pins.inputs, "2");
            val(pins.inputs, "3");
            val(pins.outputs, "Array", VSPinKind::Array);
            break;
        case T::ArrayGet:
            val(pins.inputs, "Array", VSPinKind::Array);
            val(pins.inputs, "Index", VSPinKind::Number);
            val(pins.outputs, "Value");
            break;
        case T::ArrayLength:
            val(pins.inputs, "Array", VSPinKind::Array);
            val(pins.outputs, "Length", VSPinKind::Number);
            break;
        case T::Abs:
        case T::Floor:
        case T::Ceil:
        case T::Round:
        case T::Sqrt:
        case T::Sin:
        case T::Cos:
        case T::Tan:
        case T::ToString:
        case T::ToNumber:
            val(pins.inputs, "Value");
            val(pins.outputs, "Result");
            break;
        case T::FunctionDefinition:
            val(pins.outputs, "Function");
            exec(pins.outputs, "Body");
            break;
        case T::Switch:
            exec(pins.inputs, "In");
            val(pins.inputs, "Value");
            exec(pins.outputs, "Case 0");
            exec(pins.outputs, "Case 1");
            exec(pins.outputs, "Case 2");
            exec(pins.outputs, "Default");
            pins.outputs[0].pin = 10;
            pins.outputs[1].pin = 11;
            pins.outputs[2].pin = 12;
            pins.outputs[3].pin = 99;
            break;
        case T::EnumLiteral:
            val(pins.outputs, "Value");
            break;
        case T::StructLiteral:
            val(pins.outputs, "Struct");
            break;
        case T::StructGet:
            val(pins.inputs, "Struct");
            val(pins.outputs, "Value");
            break;
        case T::StructSet:
            exec(pins.inputs, "In");
            val(pins.inputs, "Struct");
            val(pins.inputs, "Value");
            exec(pins.outputs, "Then");
            break;
    }
    return pins;
}

ImU32 PinColor(VSPinKind kind)
{
    switch (kind)
    {
        case VSPinKind::Exec:
            return IM_COL32(245, 245, 245, 255);
        case VSPinKind::Number:
            return IM_COL32(120, 200, 255, 255);
        case VSPinKind::Bool:
            return IM_COL32(255, 120, 160, 255);
        case VSPinKind::String:
            return IM_COL32(255, 210, 120, 255);
        case VSPinKind::Object:
            return IM_COL32(140, 230, 160, 255);
        case VSPinKind::Tag:
            return IM_COL32(200, 150, 255, 255);
        case VSPinKind::Color:
            return IM_COL32(255, 160, 90, 255);
        case VSPinKind::Array:
            return IM_COL32(150, 220, 210, 255);
        case VSPinKind::Any:
            break;
    }
    return IM_COL32(180, 180, 180, 255);
}

void AddNodeFromCatalog(VisualScriptDocument&, Fem::VisualScriptGraph& graph, int index, float x, float y)
{
    const std::vector<Fem::VSNodeType>& types = Fem::VsNodeTypes();
    const std::vector<Fem::VSReflectionNode>& reflection = Fem::VsReflectionCatalog();
    if (index < 0 || index >= static_cast<int>(types.size() + reflection.size()))
    {
        return;
    }
    if (index < static_cast<int>(types.size()))
    {
        graph.AddNode(types[static_cast<std::size_t>(index)], x, y);
        return;
    }
    const Fem::VSReflectionNode& entry = reflection[static_cast<std::size_t>(index) - types.size()];
    const int id = graph.AddNode(entry.type, x, y);
    if (Fem::VSNode* node = graph.Node(id))
    {
        if (entry.type == Fem::VSNodeType::EnumLiteral)
        {
            node->params["enumType"] = entry.typeName;
            node->params["value"] = entry.valueName;
        }
        else if (entry.type == Fem::VSNodeType::StructLiteral)
        {
            node->params["structType"] = entry.typeName;
        }
    }
}

void DrawVisualScriptCanvas(VisualScriptDocument& state)
{
    ImGui::BeginChild("##vs_canvas", ImVec2(0.f, 420.f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##vs_canvas_input", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                               ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvasMin, ImVec2(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y),
                        IM_COL32(24, 24, 30, 255));

    auto screenPos = [&](sf::Vector2f world)
    {
        return ImVec2(canvasMin.x + (world.x + state.panX) * state.zoom,
                      canvasMin.y + (world.y + state.panY) * state.zoom);
    };
    auto worldPos = [&](ImVec2 screen)
    {
        return sf::Vector2f((screen.x - canvasMin.x) / state.zoom - state.panX,
                            (screen.y - canvasMin.y) / state.zoom - state.panY);
    };

    // Dot grid. It gives the canvas depth without a texture.
    const float gridStep = 32.f * state.zoom;
    if (gridStep >= 6.f)
    {
        const float ox = std::fmod(state.panX * state.zoom, gridStep);
        const float oy = std::fmod(state.panY * state.zoom, gridStep);
        for (float x = ox; x < canvasSize.x; x += gridStep)
        {
            for (float y = oy; y < canvasSize.y; y += gridStep)
            {
                draw->AddCircleFilled(ImVec2(canvasMin.x + x, canvasMin.y + y), 1.f,
                                      IM_COL32(54, 54, 66, 255));
            }
        }
    }

    struct NodeLayout
    {
        const Fem::VSNode* node = nullptr;
        ImVec2 min;
        ImVec2 max;
        VSNodePins pins;
    };
    std::vector<NodeLayout> layouts;
    layouts.reserve(state.graph.Nodes().size());
    for (const Fem::VSNode& node : state.graph.Nodes())
    {
        VSNodePins pins = PinsFor(node.type);
        const int rows = std::max({static_cast<int>(pins.inputs.size()), static_cast<int>(pins.outputs.size()),
                                   static_cast<int>(node.params.size()), 1});
        const float width = 190.f * state.zoom;
        const float header = 26.f * state.zoom;
        const float row = 18.f * state.zoom;
        const ImVec2 min = screenPos({node.posX, node.posY});
        const ImVec2 max(min.x + width, min.y + header + row * static_cast<float>(rows) + 8.f * state.zoom);
        layouts.push_back({&node, min, max, std::move(pins)});
    }

    auto pinPos = [&](int nodeId, int pin, bool output) -> std::optional<ImVec2>
    {
        for (const NodeLayout& layout : layouts)
        {
            if (layout.node->id != nodeId)
            {
                continue;
            }
            const std::vector<VSPinSpec>& specs = output ? layout.pins.outputs : layout.pins.inputs;
            for (std::size_t i = 0; i < specs.size(); ++i)
            {
                const int actual = specs[i].pin >= 0 ? specs[i].pin : static_cast<int>(i);
                if (actual != pin)
                {
                    continue;
                }
                const float header = 26.f * state.zoom;
                const float row = 18.f * state.zoom;
                return ImVec2(output ? layout.max.x : layout.min.x,
                              layout.min.y + header + row * (static_cast<float>(i) + 0.5f));
            }
            return std::nullopt;
        }
        return std::nullopt;
    };

    // Connections first, so node cards cover their endpoints.
    for (const Fem::VSConnection& connection : state.graph.Connections())
    {
        const auto from = pinPos(connection.fromNode, connection.fromPin, true);
        const auto to = pinPos(connection.toNode, connection.toPin, false);
        if (!from || !to)
        {
            continue;
        }
        const float dx = std::max(40.f, std::abs(to->x - from->x) * 0.5f) * state.zoom;
        draw->AddBezierCubic(*from, ImVec2(from->x + dx, from->y), ImVec2(to->x - dx, to->y), *to,
                             IM_COL32(190, 190, 215, 210), 2.f * state.zoom);
    }

    if (state.linkPin >= 0 && state.linkNode != 0)
    {
        if (const auto from = pinPos(state.linkNode, state.linkPin, state.linkIsOutput))
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            draw->AddBezierCubic(*from, ImVec2(from->x + 50.f * state.zoom, from->y),
                                 ImVec2(mouse.x - 50.f * state.zoom, mouse.y), mouse,
                                 IM_COL32(255, 120, 200, 230), 2.f * state.zoom);
        }
    }

    for (const NodeLayout& layout : layouts)
    {
        const Fem::VSNode& node = *layout.node;
        const bool selected = state.selected == node.id;
        draw->AddRectFilled(layout.min, layout.max, IM_COL32(42, 42, 54, 245), 6.f * state.zoom);
        draw->AddRectFilled(layout.min, ImVec2(layout.max.x, layout.min.y + 26.f * state.zoom),
                            selected ? IM_COL32(170, 70, 120, 255) : IM_COL32(62, 62, 80, 255),
                            6.f * state.zoom);
        draw->AddRect(layout.min, layout.max, selected ? IM_COL32(255, 140, 210, 255) : IM_COL32(88, 88, 108, 255),
                      6.f * state.zoom, 0, 1.5f);

        const std::string title = (node.label.empty() ? Fem::VsNodeTypeName(node.type) : node.label) +
                                  "  #" + std::to_string(node.id);
        draw->AddText(ImVec2(layout.min.x + 8.f * state.zoom, layout.min.y + 5.f * state.zoom),
                      IM_COL32(255, 255, 255, 255), title.c_str());

        const float header = 26.f * state.zoom;
        const float row = 18.f * state.zoom;
        for (std::size_t i = 0; i < layout.pins.inputs.size(); ++i)
        {
            const ImVec2 p(layout.min.x, layout.min.y + header + row * (static_cast<float>(i) + 0.5f));
            draw->AddCircleFilled(p, 4.5f * state.zoom, PinColor(layout.pins.inputs[i].kind));
            draw->AddText(ImVec2(p.x + 8.f * state.zoom, p.y - 7.f * state.zoom),
                          IM_COL32(210, 210, 220, 255), layout.pins.inputs[i].name);
        }
        for (std::size_t i = 0; i < layout.pins.outputs.size(); ++i)
        {
            const ImVec2 p(layout.max.x, layout.min.y + header + row * (static_cast<float>(i) + 0.5f));
            draw->AddCircleFilled(p, 4.5f * state.zoom, PinColor(layout.pins.outputs[i].kind));
            const ImVec2 textSize = ImGui::CalcTextSize(layout.pins.outputs[i].name);
            draw->AddText(ImVec2(p.x - 8.f * state.zoom - textSize.x, p.y - 7.f * state.zoom),
                          IM_COL32(210, 210, 220, 255), layout.pins.outputs[i].name);
        }
    }

    // Hit testing and editing.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        state.panX += ImGui::GetIO().MouseDelta.x / state.zoom;
        state.panY += ImGui::GetIO().MouseDelta.y / state.zoom;
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
    {
        state.panX += ImGui::GetIO().MouseDelta.x / state.zoom;
        state.panY += ImGui::GetIO().MouseDelta.y / state.zoom;
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        // A pin wins over the node body: connecting is the more precise action.
        bool pinHit = false;
        for (const NodeLayout& layout : layouts)
        {
            const int nodeId = layout.node->id;
            const float header = 26.f * state.zoom;
            const float row = 18.f * state.zoom;
            for (std::size_t i = 0; i < layout.pins.outputs.size(); ++i)
            {
                const ImVec2 p(layout.max.x, layout.min.y + header + row * (static_cast<float>(i) + 0.5f));
                if (std::hypot(mouse.x - p.x, mouse.y - p.y) <= 8.f * state.zoom)
                {
                    state.linkNode = nodeId;
                    state.linkPin = layout.pins.outputs[i].pin >= 0 ? layout.pins.outputs[i].pin
                                                                    : static_cast<int>(i);
                    state.linkIsOutput = true;
                    pinHit = true;
                    break;
                }
            }
            if (pinHit)
            {
                break;
            }
            for (std::size_t i = 0; i < layout.pins.inputs.size(); ++i)
            {
                const ImVec2 p(layout.min.x, layout.min.y + header + row * (static_cast<float>(i) + 0.5f));
                if (std::hypot(mouse.x - p.x, mouse.y - p.y) <= 8.f * state.zoom && state.linkPin >= 0 &&
                    state.linkIsOutput)
                {
                    const int inputPin = layout.pins.inputs[i].pin >= 0 ? layout.pins.inputs[i].pin
                                                                       : static_cast<int>(i);
                    state.graph.Connect(state.linkNode, state.linkPin, nodeId, inputPin);
                    state.linkPin = -1;
                    state.linkNode = 0;
                    pinHit = true;
                    break;
                }
            }
            if (pinHit)
            {
                break;
            }
        }

        if (!pinHit)
        {
            state.linkPin = -1;
            state.linkNode = 0;
            bool selected = false;
            for (const NodeLayout& layout : layouts)
            {
                if (mouse.x >= layout.min.x && mouse.x <= layout.max.x && mouse.y >= layout.min.y &&
                    mouse.y <= layout.max.y)
                {
                    state.selected = layout.node->id;
                    state.lastSelected = -1;
                    state.dragNode = layout.node->id;
                    const sf::Vector2f p = worldPos(mouse);
                    state.dragOffset = {p.x - layout.node->posX, p.y - layout.node->posY};
                    selected = true;
                    break;
                }
            }
            if (!selected)
            {
                state.selected = 0;
            }
        }
    }

    if (state.dragNode != 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (Fem::VSNode* node = state.graph.Node(state.dragNode))
        {
            const sf::Vector2f p = worldPos(mouse);
            node->posX = p.x - state.dragOffset.x;
            node->posY = p.y - state.dragOffset.y;
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (state.linkPin >= 0 && state.linkIsOutput)
        {
            for (const NodeLayout& layout : layouts)
            {
                const float header = 26.f * state.zoom;
                const float row = 18.f * state.zoom;
                for (std::size_t i = 0; i < layout.pins.inputs.size(); ++i)
                {
                    const ImVec2 p(layout.min.x, layout.min.y + header + row * (static_cast<float>(i) + 0.5f));
                    if (std::hypot(mouse.x - p.x, mouse.y - p.y) <= 10.f * state.zoom)
                    {
                        const int inputPin = layout.pins.inputs[i].pin >= 0 ? layout.pins.inputs[i].pin
                                                                           : static_cast<int>(i);
                        if (state.graph.Connect(state.linkNode, state.linkPin, layout.node->id, inputPin))
                        {
                            state.status = "connected";
                        }
                        state.linkPin = -1;
                        state.linkNode = 0;
                        break;
                    }
                }
            }
        }
        state.dragNode = 0;
    }

    if (hovered && state.selected != 0 && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    {
        state.graph.RemoveNode(state.selected);
        state.selected = 0;
        state.lastSelected = -1;
    }

    draw->AddText(ImVec2(canvasMin.x + 8.f, canvasMin.y + canvasSize.y - 20.f),
                  IM_COL32(150, 150, 170, 255),
                  "Wheel: zoom   Middle/right drag: pan   Drag pin: connect   Del: remove");
    ImGui::EndChild();

    // Zoom on wheel while hovering the canvas.
    if (hovered && ImGui::GetIO().MouseWheel != 0.f)
    {
        state.zoom = std::clamp(state.zoom * std::pow(1.15f, ImGui::GetIO().MouseWheel), 0.35f, 2.5f);
    }
}

} // namespace

void DrawOutput(const std::vector<std::string>& lines, const char* childId, float height)
{
    if (ImGui::BeginChild(childId, ImVec2(0, height), true))
    {
        for (const std::string& line : lines)
        {
            ImGui::TextUnformatted(line.c_str());
        }
        if (lines.empty())
        {
            ImGui::TextDisabled("(no output yet)");
        }
    }
    ImGui::EndChild();
}

bool EndsWith(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Developer diagnostic: the game executable no longer contains a microphone
// path. The editor owns the hardware check so voice remains an engine service.
int RunVoiceTest(float seconds)
{
    Fem::VoiceChat::CaptureTestResult result = Fem::VoiceChat::Instance().CaptureSelfTest(seconds);
    std::printf("voice-test: available=%s capturing=%s captured=%llu samples encoded=%llu frames peak=%.4f\n",
                result.available ? "yes" : "no", result.capturing ? "yes" : "no",
                static_cast<unsigned long long>(result.capturedFrames),
                static_cast<unsigned long long>(result.encodedFrames), result.peak);
    if (result.passed)
    {
        std::printf("voice-test: PASS (capture, Opus encode and level meter work)\n");
        return 0;
    }
    std::printf("voice-test: FAILED%s%s\n", result.error.empty() ? "" : ": ", result.error.c_str());
    return 2;
}

// Headless asset pipeline entry point. The editor panel is convenient, but CI
// and packaging need to validate/cook without opening a window.
int RunAssetTool(const std::string& manifest, const std::string& output, bool cook)
{
    if (manifest.empty())
    {
        std::printf("asset-tool: a manifest path is required\n");
        return 2;
    }

    Fem::AssetDatabase database;
    if (!database.Load(manifest))
    {
        std::printf("asset-tool: cannot load %s\n", manifest.c_str());
        for (const std::string& error : database.LoadErrors())
        {
            std::printf("  %s\n", error.c_str());
        }
        return 2;
    }

    std::vector<std::string> errors;
    const bool valid = database.Validate(&errors);
    std::size_t edges = 0;
    if (database.Raw().contains("dependencies") && database.Raw()["dependencies"].is_object())
    {
        for (const auto& [id, dependencies] : database.Raw()["dependencies"].items())
        {
            (void)id;
            if (dependencies.is_array())
            {
                edges += dependencies.size();
            }
        }
    }
    std::printf("asset-tool: %zu asset(s), %zu dependency edge(s), %s\n", database.Assets().size(), edges,
                valid ? "valid" : "invalid");
    for (const std::string& error : errors)
    {
        std::printf("  error: %s\n", error.c_str());
    }
    if (!valid)
    {
        return 1;
    }

    if (!cook)
    {
        return 0;
    }

    const std::string outputDir = output.empty() ? "cooked-assets" : output;
    Fem::CookOptions options;
    options.outputDir = outputDir;
    options.createPack = true;
    options.verbose = true;
    const Fem::CookResult result = Fem::AssetCooker::Cook(database, options);
    if (!result.ok)
    {
        std::printf("asset-tool: cook failed with %zu error(s)\n", result.errors.size());
        for (const std::string& error : result.errors)
        {
            std::printf("  error: %s\n", error.c_str());
        }
        return 1;
    }

    std::printf("asset-tool: cooked %zu asset(s), %zu atlas page(s), %zu file(s), %llu bytes\n", result.assets,
                result.atlases, result.filesPacked, static_cast<unsigned long long>(result.bytes));
    std::printf("asset-tool: manifest %s\n", result.manifestPath.c_str());
    if (!result.packPath.empty())
    {
        std::printf("asset-tool: pack %s\n", result.packPath.c_str());
    }
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    Fem::DebugConsole::AttachIfRequested(argc, argv, "FemboyEditor - Debug Console");
    std::string startupProject;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument == "--project")
        {
            if (i + 1 >= argc)
            {
                std::printf("FemboyEditor: --project needs a project folder\n");
                return 2;
            }
            startupProject = argv[++i];
            continue;
        }
        if (argument == "--voice-test")
        {
            float seconds = 4.f;
            if (i + 1 < argc)
            {
                const float parsed = std::strtof(argv[i + 1], nullptr);
                if (parsed > 0.f)
                {
                    seconds = parsed;
                }
            }
            return RunVoiceTest(seconds);
        }
        if (argument == "--new-project")
        {
            if (i + 1 >= argc)
            {
                std::printf("project-tool: --new-project needs a project folder\n");
                return 2;
            }
            std::error_code ec;
            const std::filesystem::path target = std::filesystem::absolute(argv[++i], ec).lexically_normal();
            if (ec || target.empty())
            {
                std::printf("project-tool: invalid project folder\n");
                return 2;
            }
            const std::string name = Fem::ProjectGenerator::SanitizeName(target.filename().string());
            const std::filesystem::path actual = target.parent_path() / name;
            if (!Fem::ProjectGenerator::Generate(name, target.parent_path().string()))
            {
                std::printf("project-tool: could not create %s\n", actual.string().c_str());
                return 1;
            }
            std::printf("project-tool: created %s\n", actual.string().c_str());
            return 0;
        }
        if (argument == "--validate-assets" || argument == "--cook-assets")
        {
            if (i + 1 >= argc)
            {
                std::printf("asset-tool: %s needs a manifest path\n", argument.c_str());
                return 2;
            }
            const std::string manifest = argv[++i];
            std::string output;
            if (argument == "--cook-assets" && i + 1 < argc && argv[i + 1][0] != '-')
            {
                output = argv[++i];
            }
            return RunAssetTool(manifest, output, argument == "--cook-assets");
        }
        if (argument == "--help" || argument == "-h")
        {
            std::printf("FemboyEditor\n");
            std::printf("  FemboyEditor                       open the project browser\n");
            std::printf("  FemboyEditor --project <folder>    open a project directly\n");
            std::printf("  FemboyEditor --voice-test [sec]    capture + Opus hardware test\n");
            std::printf("  FemboyEditor --new-project <folder> create a self-contained project\n");
            std::printf("  FemboyEditor --validate-assets m.json\n");
            std::printf("  FemboyEditor --cook-assets m.json [out-dir]\n");
            return 0;
        }
    }

    // Tools and projects belong to the source tree, never to the build tree.
    // When the editor is started from build\ it still resolves Projects/...
    // against the checkout root, so new content is not silently written next
    // to the executable.
    const std::filesystem::path engineRoot = FindEngineRoot(argc > 0 ? argv[0] : nullptr);
    if (!engineRoot.empty())
    {
        std::error_code ec;
        std::filesystem::current_path(engineRoot, ec);
    }

    // Open at most of the desktop instead of a fixed 1600x900 stamp. On a
    // HiDPI display the UI scale below still keeps every label readable.
    const sf::VideoMode desktopMode = sf::VideoMode::getDesktopMode();
    const unsigned int desktopWidth = std::max(1u, desktopMode.size.x);
    const unsigned int desktopHeight = std::max(1u, desktopMode.size.y);
    const unsigned int windowWidth = std::min(desktopWidth, std::max(1024u, desktopWidth * 9u / 10u));
    const unsigned int windowHeight = std::min(desktopHeight, std::max(600u, desktopHeight * 9u / 10u));

    sf::RenderWindow window(sf::VideoMode({windowWidth, windowHeight}), "FemboyEditor");
    window.setFramerateLimit(120);
    if (!ImGui::SFML::Init(window))
    {
        return 1;
    }

    ImGuiIO& io = ImGui::GetIO();
    // Editor preferences (dock layout, window sizes) live in the user profile,
    // not in the engine build directory. A copied/moved build then still reads
    // the same layout, and a project folder never accumulates editor state.
    static std::string editorIniPath;
    const std::string userData = Fem::Vanity::UserDataDirectory("FemboyEditor");
    if (!userData.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(userData, ec);
        editorIniPath = (std::filesystem::path(userData) / "imgui.ini").string();
        io.IniFilename = editorIniPath.c_str();
    }
    bool layoutNeedsReset = false;
    if (!userData.empty())
    {
        std::ifstream marker(std::filesystem::path(userData) / "layout.version");
        std::string version;
        std::getline(marker, version);
        layoutNeedsReset = version != std::to_string(kEditorLayoutVersion);
    }
    Fem::AccessibilitySettings::Instance().EnsureRegistered();
    EditorSettingsState editorSettings;
    editorSettings.Load(userData);

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.ConfigDockingWithShift = false;
    // Every dock node keeps a real tab, including nodes that only hold one
    // window. That is what makes a single panel draggable to another dock.
    io.ConfigDockingAlwaysTabBar = true;

    // A neutral, high-contrast tool theme with a single blue accent. This is
    // an engine editor, not a game HUD: no decorative pixel font or colorful
    // chrome that competes with the scene.
    ImGuiStyle& style = ImGui::GetStyle();
    ApplyEditorTheme(style);
    style.FontSizeBase = 16.f;

    float dpiScale = std::clamp(DetectDpiScale(window), 0.75f, 2.5f);
    // Load a scalable UI font at the physical startup size. Without this the
    // built-in bitmap font is upscaled and looks pixelated on HiDPI screens.
    LoadEditorFont(io, dpiScale, engineRoot);
    const ImGuiStyle baseStyle = style;

    float userZoom = editorSettings.uiZoom;
    float uiScale = std::clamp(dpiScale * userZoom, 0.75f, 3.f);
    ApplyEditorScale(baseStyle, uiScale);
    float appliedScale = uiScale;

    Panel panels[] = {
        {"Project Launcher", false, "Create or open a project folder."},
        {"Setup & Diagnostics", false, "Check the compiler, CMake, Ninja, Git and platform setup scripts."},
        {"Hierarchy", true, "Load/save scenes, browse the Registry and instance prefabs."},
        {"Inspector", true, "Edit reflected components of the selected entity."},
        {"Class Defaults", false, "Project-level defaults for reflected components."},
        {"Viewport", true, "Live scene viewport with pan, zoom, selection and isometric projection."},
        {"Pink UI Builder", false, "Edit UIWidget trees, preview with PinkUI::RenderImGui()."},
        {"Localization", false, "Edit the Strings table."},
        {"FemboyScript Console", true, "Compile and run FemboyScript; print() goes here."},
        {"Terminal", true, "Run a shell command and read its output."},
        {"Git", true, "Status, log, diff and commit through the installed git."},
        {"Mod Manager", false, "Enable or disable content packs."},
        {"Boy Logs", false, "Engine log stream."},
        {"Profiler", false, "Frame graph, renderer counters and per-system timings."},
        {"Asset Browser", true, "Files under the active project. Graph files open in their own editor tabs."},
        {"Asset Pipeline", false, "Validate, atlas-pack and cook assets into a .fempak."},
        {"Voice Chat", false, "Microphone capture, Opus and playback settings."},
    };
    for (Panel& panel : panels)
    {
        panel.defaultOpen = panel.open;
    }

    ProjectState project;
    ConsoleState console;
    TerminalState terminal;
    GitState git;
    VisualScriptWorkspace visualScripts;
    AssetPipelineState assets;
    AssetBrowserState assetBrowser;
    PlayState play;
    SceneState sceneState;
    ViewportState viewport;
    ClassDefaultsState classDefaults;
    ProjectSettingsState projectSettings;
    ProjectWizardState wizard;
    SetupState setup = CheckSetup();
    if (setup.missing)
    {
        for (Panel& panel : panels)
        {
            if (std::string(panel.name) == "Setup & Diagnostics")
            {
                panel.open = true;
            }
        }
    }
    const std::filesystem::path projectsRoot =
        engineRoot.empty() ? std::filesystem::path("Projects") : (engineRoot / "Projects");
    const std::filesystem::path defaultProject = projectsRoot / "NeonTriangle";
    std::snprintf(wizard.location, sizeof(wizard.location), "%s", projectsRoot.string().c_str());
    wizard.visible = true;
    std::error_code startupError;

    // --project wins, then the remembered project, then the browser.
    if (!startupProject.empty())
    {
        const std::filesystem::path requested =
            std::filesystem::absolute(startupProject, startupError).lexically_normal();
        if (!startupError && std::filesystem::exists(requested, startupError))
        {
            ApplyProjectPaths(requested.string(), project, sceneState, classDefaults, assets, git, projectSettings,
                              viewport);
            RememberProject(editorSettings, requested);
            project.recent.push_back(requested.string());
            wizard.visible = false;
        }
    }
    else if (!editorSettings.showBrowserOnStartup)
    {
        const std::filesystem::path remembered =
            std::filesystem::absolute(editorSettings.lastProject, startupError).lexically_normal();
        if (!startupError && editorSettings.lastProject[0] != '\0' &&
            std::filesystem::exists(remembered / "project.femboy", startupError))
        {
            ApplyProjectPaths(remembered.string(), project, sceneState, classDefaults, assets, git, projectSettings,
                              viewport);
            project.recent.push_back(remembered.string());
            wizard.visible = false;
        }
    }

    if (wizard.visible && !defaultProject.empty())
    {
        std::error_code ec;
        if (std::filesystem::exists(defaultProject / "project.femboy", ec) || std::filesystem::exists(defaultProject, ec))
        {
            ApplyProjectPaths(defaultProject.string(), project, sceneState, classDefaults, assets, git,
                              projectSettings, viewport);
            project.recent.push_back(defaultProject.string());
        }
    }
    else if (project.recent.empty() && std::filesystem::exists(defaultProject / "project.femboy", startupError))
    {
        project.recent.push_back(defaultProject.string());
    }
    if (std::filesystem::exists(classDefaults.path))
    {
        Fem::ClassDefaults::Instance().Load(classDefaults.path);
    }

    bool showDemo = false;
    bool quit = false;
    bool resetLayout = false;
    bool openVisualScriptDialog = false;
    int activeTopTab = kTopTabEditor;
    const char* focusPanel = nullptr;
    sf::Clock clock;

    while (window.isOpen() && !quit)
    {
        while (const std::optional event = window.pollEvent())
        {
            ImGui::SFML::ProcessEvent(window, *event);
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
        }

        ImGui::SFML::Update(window, clock.restart());
        g_editorTooltips = editorSettings.tooltipsEnabled;

        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Project..."))
                {
                    wizard.visible = true;
                    wizard.focus = true;
                }
                if (ImGui::MenuItem("Open Project..."))
                {
                    wizard.visible = true;
                    wizard.focus = true;
                }
                if (ImGui::MenuItem("Project Settings"))
                {
                    activeTopTab = kTopTabProjectSettings;
                }
                if (ImGui::MenuItem("Editor Settings"))
                {
                    activeTopTab = kTopTabEditorSettings;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("New Visual Script"))
                {
                    visualScripts.NewDocument();
                    activeTopTab = static_cast<int>(visualScripts.documents.size()) - 1;
                }
                if (ImGui::MenuItem("Open Visual Script..."))
                {
                    openVisualScriptDialog = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                {
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit"))
                {
                    quit = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, Fem::UndoStack::Instance().CanUndo()))
                {
                    Fem::UndoStack::Instance().Undo();
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, Fem::UndoStack::Instance().CanRedo()))
                {
                    Fem::UndoStack::Instance().Redo();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Play"))
            {
                ImGui::Checkbox("Rebuild before launch", &play.rebuild);
                ImGui::Separator();
                if (ImGui::MenuItem("Play in New Window (PIE)", "F5"))
                {
                    const std::string gameName = projectSettings.name[0] != '\0'
                                                     ? projectSettings.name
                                                     : std::filesystem::path(project.path).filename().string();
                    BuildAndLaunchGame(play, project.path, engineRoot, gameName, false);
                }
                if (ImGui::MenuItem("Standalone Game", "F6"))
                {
                    const std::string gameName = projectSettings.name[0] != '\0'
                                                     ? projectSettings.name
                                                     : std::filesystem::path(project.path).filename().string();
                    BuildAndLaunchGame(play, project.path, engineRoot, gameName, true);
                }
                if (ImGui::MenuItem("Stop", "Shift+F5", false, play.running))
                {
                    StopGame(play);
                }
                ImGui::Separator();
                ImGui::TextDisabled("%s", play.status.c_str());
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                for (Panel& p : panels)
                {
                    if (ImGui::MenuItem(p.name, nullptr, &p.open) && p.open)
                    {
                        focusPanel = p.name;
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Project Settings"))
                {
                    activeTopTab = kTopTabProjectSettings;
                }
                if (ImGui::MenuItem("Editor Settings"))
                {
                    activeTopTab = kTopTabEditorSettings;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Zoom In", "Ctrl+="))
                {
                    userZoom = std::min(2.f, userZoom + 0.1f);
                }
                if (ImGui::MenuItem("Zoom Out", "Ctrl+-"))
                {
                    userZoom = std::max(0.75f, userZoom - 0.1f);
                }
                if (ImGui::MenuItem("Reset Zoom", "Ctrl+0"))
                {
                    userZoom = 1.f;
                }
                ImGui::TextDisabled("UI scale: %.0f%%", uiScale * 100.f);
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Layout"))
                {
                    resetLayout = true;
                }
                ImGui::MenuItem("ImGui Demo", nullptr, &showDemo);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("Setup & Diagnostics", "F2 / Ctrl+Alt+S"))
                {
                    activeTopTab = kTopTabEditor;
                    for (Panel& panel : panels)
                    {
                        if (std::string(panel.name) == "Setup & Diagnostics")
                        {
                            panel.open = true;
                        }
                    }
                }
                ImGui::Separator();
                ImGui::MenuItem("FemboyEditor", nullptr, false, false);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        if (!editorSettings.noFunctionKeys && !io.WantTextInput)
        {
            const std::string gameName =
                projectSettings.name[0] != '\0' ? projectSettings.name
                                                : std::filesystem::path(project.path).filename().string();
            if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
            {
                if (io.KeyShift)
                {
                    StopGame(play);
                }
                else
                {
                    BuildAndLaunchGame(play, project.path, engineRoot, gameName, false);
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F6, false))
            {
                BuildAndLaunchGame(play, project.path, engineRoot, gameName, true);
            }
        }

        if (openVisualScriptDialog)
        {
            ImGui::OpenPopup("Open Visual Script");
            openVisualScriptDialog = false;
        }
        if (ImGui::BeginPopupModal("Open Visual Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextDisabled("Graphs open in their own dockable editor tab.");
            ImGui::SetNextItemWidth(520.f);
            ImGui::InputTextWithHint("##vs_open_path", "Path to a .vsgraph or .femboygraph file...",
                                     visualScripts.openPath, sizeof(visualScripts.openPath));
            if (ImGui::Button("Open") && visualScripts.openPath[0] != '\0')
            {
                VisualScriptDocument& document = visualScripts.OpenDocument(visualScripts.openPath);
                activeTopTab = FindDocumentIndex(visualScripts, &document);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // F2 is the usual shortcut, but users without a function-key row get a
        // multi-key alternative. The accessibility setting only disables the
        // F-key half; Ctrl+Alt+S always works.
        const bool setupChord = !io.WantTextInput && io.KeyCtrl && io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_S, false);
        if ((!editorSettings.noFunctionKeys && ImGui::IsKeyPressed(ImGuiKey_F2, false)) || setupChord)
        {
            activeTopTab = kTopTabEditor;
            for (Panel& panel : panels)
            {
                if (std::string(panel.name) == "Setup & Diagnostics")
                {
                    panel.open = true;
                    focusPanel = panel.name;
                    break;
                }
            }
        }

        // Ctrl+Z/Y while no text field has focus. Text fields keep their own
        // local undo, which is what a user expects while editing a value.
        // Ctrl+= / Ctrl+- / Ctrl+0 adjust the editor zoom.
        if (!io.WantTextInput && io.KeyCtrl)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
            {
                Fem::UndoStack::Instance().Undo();
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
            {
                Fem::UndoStack::Instance().Redo();
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false))
            {
                userZoom = std::min(2.f, userZoom + 0.1f);
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false))
            {
                userZoom = std::max(0.75f, userZoom - 0.1f);
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false))
            {
                userZoom = 1.f;
            }
        }

        // Follow the window as it moves between monitors with different DPI.
        const float detectedDpi = std::clamp(DetectDpiScale(window), 0.75f, 2.5f);
        if (std::abs(detectedDpi - dpiScale) > 0.01f)
        {
            dpiScale = detectedDpi;
        }
        uiScale = std::clamp(dpiScale * userZoom, 0.75f, 3.f);
        if (std::abs(uiScale - appliedScale) > 0.001f)
        {
            ApplyEditorScale(baseStyle, uiScale);
            appliedScale = uiScale;
        }

        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        const float topBarHeight = ImGui::GetFrameHeight() + 10.f;
        const ImVec2 topBarPos = mainViewport->WorkPos;
        const ImVec2 topBarSize(mainViewport->WorkSize.x, topBarHeight);
        const ImVec2 contentPos(topBarPos.x, topBarPos.y + topBarHeight);
        const ImVec2 contentSize(topBarSize.x, mainViewport->WorkSize.y - topBarHeight);

        if (activeTopTab >= static_cast<int>(visualScripts.documents.size()))
        {
            activeTopTab = kTopTabEditor;
        }
        if (activeTopTab >= 0 && !visualScripts.documents[static_cast<std::size_t>(activeTopTab)]->open)
        {
            activeTopTab = kTopTabEditor;
        }

        auto activeTabTitle = [&]() -> std::string
        {
            if (activeTopTab == kTopTabProjectSettings)
            {
                return "Project Settings";
            }
            if (activeTopTab == kTopTabEditorSettings)
            {
                return "Editor Settings";
            }
            if (activeTopTab >= 0)
            {
                return visualScripts.documents[static_cast<std::size_t>(activeTopTab)]->Title();
            }
            return "Editor";
        };

        // One visible workspace tab, with a dropdown for everything else. This
        // is a document switcher, not a second row of always-visible tabs.
        ImGui::SetNextWindowPos(topBarPos);
        ImGui::SetNextWindowSize(topBarSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 5.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.085f, 0.085f, 0.105f, 1.f));
        const ImGuiWindowFlags topBarFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
        if (ImGui::Begin("##TopTabBar", nullptr, topBarFlags))
        {
            const std::string title = activeTabTitle() + "   \xE2\x96\xBE";
            if (ImGui::Button(title.c_str()))
            {
                ImGui::OpenPopup("##TopTabsPopup");
            }
            if (ImGui::BeginPopup("##TopTabsPopup"))
            {
                if (ImGui::MenuItem("Editor", nullptr, activeTopTab == kTopTabEditor))
                {
                    activeTopTab = kTopTabEditor;
                }
                if (ImGui::MenuItem("Project Settings", nullptr, activeTopTab == kTopTabProjectSettings))
                {
                    activeTopTab = kTopTabProjectSettings;
                }
                if (ImGui::MenuItem("Editor Settings", nullptr, activeTopTab == kTopTabEditorSettings))
                {
                    activeTopTab = kTopTabEditorSettings;
                }
                if (!visualScripts.documents.empty())
                {
                    ImGui::Separator();
                    for (int i = 0; i < static_cast<int>(visualScripts.documents.size()); ++i)
                    {
                        ImGui::PushID(i);
                        if (ImGui::MenuItem(visualScripts.documents[static_cast<std::size_t>(i)]->Title().c_str(),
                                            nullptr, activeTopTab == i))
                        {
                            visualScripts.documents[static_cast<std::size_t>(i)]->open = true;
                            activeTopTab = i;
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndPopup();
            }

            if (activeTopTab >= 0)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("x##close_top_tab"))
                {
                    visualScripts.documents[static_cast<std::size_t>(activeTopTab)]->open = false;
                    activeTopTab = kTopTabEditor;
                }
                if (ImGui::IsItemHovered())
                {
                    EditorTooltip("Close this document tab.");
                }
            }
            else if (activeTopTab == kTopTabProjectSettings || activeTopTab == kTopTabEditorSettings)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("x##close_settings_tab"))
                {
                    activeTopTab = kTopTabEditor;
                }
            }

            const std::string gameName =
                projectSettings.name[0] != '\0' ? projectSettings.name
                                                : std::filesystem::path(project.path).filename().string();
            ImGui::SameLine();
            if (ImGui::SmallButton("Play"))
            {
                BuildAndLaunchGame(play, project.path, engineRoot, gameName, false);
            }
            EditorTooltip("Build (unless disabled) and launch the game in a separate PIE window.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Standalone"))
            {
                BuildAndLaunchGame(play, project.path, engineRoot, gameName, true);
            }
            EditorTooltip("Build and launch the standalone game executable.");
            if (play.running)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Stop"))
                {
                    StopGame(play);
                }
            }

            const std::string projectLabel =
                std::string("Project: ") + (projectSettings.name[0] != '\0' ? projectSettings.name : "none");
            const float projectWidth = ImGui::CalcTextSize(projectLabel.c_str()).x;
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - projectWidth - 14.f));
            ImGui::TextDisabled("%s", projectLabel.c_str());

            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - 1.f),
                ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                       ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - 1.f),
                ImGui::GetColorU32(ImGuiCol_Border));
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        if (activeTopTab == kTopTabProjectSettings)
        {
            ImGui::SetNextWindowPos(contentPos);
            ImGui::SetNextWindowSize(contentSize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.075f, 0.075f, 0.09f, 1.f));
            const ImGuiWindowFlags pageFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                                               ImGuiWindowFlags_NoSavedSettings;
            if (ImGui::Begin("##ProjectSettingsPage", nullptr, pageFlags))
            {
                ImGui::SetWindowFontScale(1.15f);
                ImGui::Text("Project Settings");
                ImGui::SetWindowFontScale(1.f);
                ImGui::Separator();
                DrawProjectSettingsContent(projectSettings, viewport);
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
        else if (activeTopTab == kTopTabEditorSettings)
        {
            ImGui::SetNextWindowPos(contentPos);
            ImGui::SetNextWindowSize(contentSize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.075f, 0.075f, 0.09f, 1.f));
            const ImGuiWindowFlags pageFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                                               ImGuiWindowFlags_NoSavedSettings;
            if (ImGui::Begin("##EditorSettingsPage", nullptr, pageFlags))
            {
                ImGui::SetWindowFontScale(1.15f);
                ImGui::Text("Editor Settings");
                ImGui::SetWindowFontScale(1.f);
                ImGui::Separator();
                DrawEditorSettingsContent(editorSettings, userZoom, panels,
                                          static_cast<int>(std::size(panels)), resetLayout, focusPanel,
                                          visualScripts, activeTopTab);
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        if (activeTopTab == kTopTabEditor)
        {
        // A dockspace restored from imgui.ini keeps the user's arrangement.
        // Only a missing dockspace, a layout-version bump, or an explicit
        // "Reset Layout" replaces it.
        if (layoutNeedsReset)
        {
            resetLayout = true;
            layoutNeedsReset = false;
        }
        const ImGuiID dockspaceId = ImHashStr("FemboyEditorDock");
        if (resetLayout || ImGui::DockBuilderGetNode(dockspaceId) == nullptr)
        {
            BuildDefaultEditorLayout(dockspaceId, contentSize);
            resetLayout = false;
            if (!userData.empty())
            {
                std::ofstream marker(std::filesystem::path(userData) / "layout.version");
                marker << kEditorLayoutVersion << "\n";
            }
        }

        ImGui::SetNextWindowPos(contentPos);
        ImGui::SetNextWindowSize(contentSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        const ImGuiWindowFlags workspaceFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoBackground;
        if (ImGui::Begin("##EditorWorkspace", nullptr, workspaceFlags))
        {
            ImGui::DockSpace(dockspaceId, ImVec2(0.f, 0.f), ImGuiDockNodeFlags_PassthruCentralNode);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);

        for (Panel& p : panels)
        {
            if (!p.open)
            {
                continue;
            }
            if (focusPanel != nullptr && std::strcmp(focusPanel, p.name) == 0)
            {
                ImGui::SetNextWindowFocus();
                focusPanel = nullptr;
            }
            if (ImGui::Begin(p.name, &p.open))
            {
                ImGui::TextDisabled("%s", p.hint);
                ImGui::Separator();

                const std::string name = p.name;
                if (name == "Project Launcher")
                {
                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputText("Project folder", project.path, sizeof(project.path));
                    if (ImGui::Button("Create"))
                    {
                        std::error_code ec;
                        const std::filesystem::path target =
                            std::filesystem::absolute(project.path, ec).lexically_normal();
                        if (!ec && !target.empty())
                        {
                            // A project is a complete, movable folder: source,
                            // CMake, config, scripts and assets all live under
                            // this one path, never under the engine build dir.
                            const std::string name =
                                Fem::ProjectGenerator::SanitizeName(target.filename().string());
                            const std::filesystem::path actual = target.parent_path() / name;
                            if (Fem::ProjectGenerator::Generate(name, target.parent_path().string()))
                            {
                                ApplyProjectPaths(actual.string(), project, sceneState, classDefaults, assets, git,
                                                  projectSettings, viewport);
                                RememberProject(editorSettings, actual);
                                activeTopTab = kTopTabEditor;
                                project.recent.push_back(actual.string());
                                project.status = "Created self-contained project: " + actual.string();
                            }
                            else
                            {
                                project.status = "Project creation failed.";
                            }
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Open"))
                    {
                        std::error_code ec;
                        const std::filesystem::path target =
                            std::filesystem::absolute(project.path, ec).lexically_normal();
                        if (!ec && std::filesystem::exists(target, ec))
                        {
                            ApplyProjectPaths(target.string(), project, sceneState, classDefaults, assets, git,
                                              projectSettings, viewport);
                            RememberProject(editorSettings, target);
                            activeTopTab = kTopTabEditor;
                            if (project.recent.empty() || project.recent.back() != target.string())
                            {
                                project.recent.push_back(target.string());
                            }
                            project.status = "Opened project: " + target.string();
                        }
                        else
                        {
                            project.status = "Project folder does not exist.";
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Import..."))
                    {
                        // Import is the same operation for a folder that was
                        // moved here: read its project.femboy and point every
                        // editor path at it.
                        std::error_code ec;
                        const std::filesystem::path target =
                            std::filesystem::absolute(project.path, ec).lexically_normal();
                        if (!ec && std::filesystem::exists(target / "project.femboy", ec))
                        {
                            ApplyProjectPaths(target.string(), project, sceneState, classDefaults, assets, git,
                                              projectSettings, viewport);
                            RememberProject(editorSettings, target);
                            activeTopTab = kTopTabEditor;
                            project.recent.push_back(target.string());
                            project.status = "Imported project: " + target.string();
                        }
                        else
                        {
                            project.status = "No project.femboy in that folder.";
                        }
                    }

                    ImGui::TextDisabled("%s", project.status.c_str());
                    ImGui::Text("Recent:");
                    for (const std::string& recent : project.recent)
                    {
                        if (ImGui::Selectable(recent.c_str()))
                        {
                            std::snprintf(project.path, sizeof(project.path), "%s", recent.c_str());
                        }
                    }
                }
                else if (name == "FemboyScript Console")
                {
                    ImGui::InputTextMultiline("##script", console.input, sizeof(console.input), ImVec2(-1, 180));
                    if (ImGui::Button("Run"))
                    {
                        console.script.SetExecutionBudget(console.budget);
                        // Always run the active text, so a developer editing the
                        // same source repeatedly still sees each run's output.
                        const Fem::FemboyScript::Result result = console.script.Run(console.input);
                        if (!result.ok)
                        {
                            console.output.push_back("error: " + result.error);
                        }
                        else
                        {
                            console.output.push_back("ok");
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Run OnStart") && console.script.HasFunction("OnStart"))
                    {
                        const Fem::FemboyScript::Result result = console.script.Call("OnStart");
                        if (!result.ok)
                        {
                            console.output.push_back("error: " + result.error);
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear"))
                    {
                        console.output.clear();
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(140.f);
                    ImGui::InputInt("Step budget", &console.budget, 1000, 100000);
                    ImGui::Text("Cached programs: %zu (hits %zu / misses %zu)", console.script.CacheSize(),
                                console.script.CacheHits(), console.script.CacheMisses());
                    DrawOutput(console.output, "##console_output", 220.f);
                }
                else if (name == "Terminal")
                {
                    ImGui::InputText("Command", terminal.command, sizeof(terminal.command));
                    if (ImGui::Button("Run") && terminal.command[0] != '\0')
                    {
                        const std::string command = terminal.command;
                        terminal.output.push_back("> " + command);
                        std::string output = RunProcess(command);
                        if (output.empty())
                        {
                            output = "(no output)";
                        }
                        terminal.output.push_back(output);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear"))
                    {
                        terminal.output.clear();
                    }
                    DrawOutput(terminal.output, "##terminal_output", 260.f);
                }
                else if (name == "Git")
                {
                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputText("Repository", git.path, sizeof(git.path));
                    if (ImGui::Button("Status") && git.path[0] != '\0')
                    {
                        git.output.push_back("> git status --short");
                        git.output.push_back(RunProcess("git -C " + QuotePath(git.path) + " status --short"));
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Log") && git.path[0] != '\0')
                    {
                        git.output.push_back("> git log --oneline -12");
                        git.output.push_back(RunProcess("git -C " + QuotePath(git.path) + " log --oneline -12"));
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Diff") && git.path[0] != '\0')
                    {
                        git.output.push_back("> git diff --stat");
                        git.output.push_back(RunProcess("git -C " + QuotePath(git.path) + " diff --stat"));
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Stage all") && git.path[0] != '\0')
                    {
                        git.output.push_back("> git add -A");
                        git.output.push_back(RunProcess("git -C " + QuotePath(git.path) + " add -A"));
                    }

                    ImGui::InputText("Commit message", git.message, sizeof(git.message));
                    if (ImGui::Button("Commit") && git.path[0] != '\0' && git.message[0] != '\0')
                    {
                        const std::string message = SanitizeCommitMessage(git.message);
                        git.output.push_back("> git commit");
                        git.output.push_back(
                            RunProcess("git -C " + QuotePath(git.path) + " commit -m \"" + message + "\""));
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##git"))
                    {
                        git.output.clear();
                    }
                    DrawOutput(git.output, "##git_output", 240.f);
                }
                else if (name == "Setup & Diagnostics")
                {
                    ImGui::TextWrapped("One command installs the independent tools for this OS. "
                                       "The engine vendors SFML, ImGui, EnTT, Box2D and the other libraries; "
                                       "only the compiler and build tools are installed here.");
                    ImGui::Separator();
                    if (ImGui::Button("Re-check"))
                    {
                        setup = CheckSetup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Copy install command"))
                    {
                        ImGui::SetClipboardText(SetupCommand().c_str());
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Open tools folder"))
                    {
                        RunProcess(OpenToolsCommand());
                    }
                    ImGui::Separator();
                    for (const std::string& line : setup.lines)
                    {
                        if (line.rfind("[missing]", 0) == 0)
                        {
                            ImGui::TextColored(ImVec4(1.f, 0.5f, 0.45f, 1.f), "%s", line.c_str());
                        }
                        else if (line.rfind("[ok]", 0) == 0)
                        {
                            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1.f), "%s", line.c_str());
                        }
                        else
                        {
                            ImGui::TextDisabled("%s", line.c_str());
                        }
                    }
                    ImGui::Separator();
                    ImGui::TextWrapped("Install command:");
                    ImGui::TextWrapped("%s", SetupCommand().c_str());
                    if (setup.missing)
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.5f, 0.45f, 1.f),
                                           "Some tools are missing. Copy the command, run it in a terminal, "
                                           "then press Re-check.");
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1.f),
                                           "Environment looks ready. Use configure.bat/build.bat on Windows "
                                           "or build-linux.sh/build-macos.sh on Unix.");
                    }
                }
                else if (name == "Hierarchy")
                {
                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputText("Scene file", sceneState.scenePath, sizeof(sceneState.scenePath));
                    if (ImGui::Button("Load"))
                    {
                        if (sceneState.scene.Load(sceneState.scenePath))
                        {
                            sceneState.scene.ApplyTo(sceneState.registry);
                            ClearEntitySelection(sceneState);
                            LoadGroupNames(sceneState);
                            sceneState.status = "scene loaded";
                        }
                        else
                        {
                            sceneState.status = "could not load scene";
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Save"))
                    {
                        sceneState.scene.CaptureFrom(sceneState.registry);
                        const bool saved = sceneState.scene.Save(sceneState.scenePath);
                        if (saved)
                        {
                            SaveGroupNames(sceneState);
                        }
                        sceneState.status = saved ? "scene saved" : "save failed";
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Load additive"))
                    {
                        sceneState.status = sceneState.scene.LoadAdditive(sceneState.scenePath, sceneState.registry)
                                                ? "scene added to world"
                                                : "additive load failed";
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("New"))
                    {
                        sceneState.registry.clear();
                        sceneState.scene = Fem::Scene{};
                        ClearEntitySelection(sceneState);
                        sceneState.groupNames.clear();
                        sceneState.status = "new empty scene";
                    }

                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputText("Prefab folder", sceneState.prefabDir, sizeof(sceneState.prefabDir));
                    if (ImGui::Button("Scan prefabs"))
                    {
                        sceneState.library->Clear();
                        sceneState.library->LoadFromDir(sceneState.prefabDir);
                        sceneState.prefabIndex = 0;
                        sceneState.status =
                            "loaded " + std::to_string(sceneState.library->Names().size()) + " prefab(s)";
                    }
                    ImGui::SameLine();
                    const std::vector<std::string>& prefabNames = sceneState.library->Names();
                    if (!prefabNames.empty())
                    {
                        std::vector<const char*> prefabCStr;
                        prefabCStr.reserve(prefabNames.size());
                        for (const std::string& prefab : prefabNames)
                        {
                            prefabCStr.push_back(prefab.c_str());
                        }
                        sceneState.prefabIndex =
                            std::clamp(sceneState.prefabIndex, 0, static_cast<int>(prefabCStr.size()) - 1);
                        ImGui::SetNextItemWidth(220.f);
                        ImGui::Combo("##prefab_pick", &sceneState.prefabIndex, prefabCStr.data(),
                                     static_cast<int>(prefabCStr.size()));
                        ImGui::SameLine();
                        if (ImGui::Button("Instance"))
                        {
                            const Entity spawned = sceneState.library->Instantiate(
                                prefabNames[static_cast<std::size_t>(sceneState.prefabIndex)], sceneState.registry);
                            if (spawned != entt::null)
                            {
                                sceneState.selected = spawned;
                                sceneState.status = "instanced prefab";
                            }
                            else
                            {
                                sceneState.status = "prefab instantiate failed";
                            }
                        }
                    }

                    if (ImGui::Button("Create entity"))
                    {
                        const Entity created = sceneState.registry.create();
                        if (const Fem::ComponentReflection* transform =
                                Fem::Mirror::Instance().Find("transform"))
                        {
                            Fem::ClassDefaults::Instance().ApplyTo(sceneState.registry, created, *transform);
                        }
                        else
                        {
                            sceneState.registry.emplace<TransformComponent>(created);
                        }
                        sceneState.selected = created;
                        sceneState.extraSelection.clear();
                        sceneState.status = "entity created";
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete selected"))
                    {
                        const std::vector<Entity> selectedEntities = SelectedEntities(sceneState);
                        for (Entity entity : selectedEntities)
                        {
                            if (sceneState.registry.valid(entity))
                            {
                                const nlohmann::json removed = CaptureEntity(sceneState.registry, entity);
                                Fem::UndoStack::Instance().Push(std::make_shared<DeleteEntityCommand>(
                                    sceneState.registry, removed, entity, sceneState.selected));
                            }
                        }
                        ClearEntitySelection(sceneState);
                        sceneState.status = "deleted " + std::to_string(selectedEntities.size()) + " entit(y/ies)";
                    }

                    const char* groupingModes[] = {"Parent tree", "Group by tag", "Group by category",
                                                   "Group by group"};
                    ImGui::SetNextItemWidth(200.f);
                    ImGui::Combo("Group by", &sceneState.hierarchyGrouping, groupingModes, 4);
                    ImGui::SameLine();
                    ImGui::TextDisabled("%zu selected", SelectedEntities(sceneState).size());
                    ImGui::SetNextItemWidth(150.f);
                    ImGui::InputTextWithHint("##group_name", "Group name", sceneState.groupName,
                                             sizeof(sceneState.groupName));
                    ImGui::SameLine();
                    if (ImGui::Button("Assign group"))
                    {
                        for (Entity entity : SelectedEntities(sceneState))
                        {
                            if (sceneState.registry.valid(entity))
                            {
                                sceneState.registry.get_or_emplace<Fem::GroupComponent>(entity).group =
                                    sceneState.groupName;
                            }
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Remove group"))
                    {
                        for (Entity entity : SelectedEntities(sceneState))
                        {
                            if (Fem::GroupComponent* group =
                                    sceneState.registry.try_get<Fem::GroupComponent>(entity))
                            {
                                group->group.clear();
                            }
                        }
                    }
                    if (ImGui::Button("Create empty group"))
                    {
                        const std::string group = sceneState.groupName;
                        if (!group.empty() &&
                            std::find(sceneState.groupNames.begin(), sceneState.groupNames.end(), group) ==
                                sceneState.groupNames.end())
                        {
                            sceneState.groupNames.push_back(group);
                            SaveGroupNames(sceneState);
                        }
                    }

                    MergeGroupNames(sceneState);
                    std::sort(sceneState.groupNames.begin(), sceneState.groupNames.end());
                    if (!sceneState.groupNames.empty())
                    {
                        ImGui::Separator();
                        ImGui::TextDisabled("Groups");
                        int deleteGroup = -1;
                        for (int i = 0; i < static_cast<int>(sceneState.groupNames.size()); ++i)
                        {
                            const std::string& groupName = sceneState.groupNames[i];
                            const std::vector<Entity> entities =
                                GroupEntities(sceneState.registry, groupName);
                            ImGui::PushID(i);
                            const std::string label =
                                groupName + "  (" + std::to_string(entities.size()) + ")";
                            if (ImGui::Selectable(label.c_str()))
                            {
                                if (!entities.empty())
                                {
                                    sceneState.selected = entities.front();
                                    sceneState.extraSelection.assign(entities.begin() + 1, entities.end());
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Select"))
                            {
                                if (!entities.empty())
                                {
                                    sceneState.selected = entities.front();
                                    sceneState.extraSelection.assign(entities.begin() + 1, entities.end());
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Rename"))
                            {
                                sceneState.renamingGroup = i;
                                std::snprintf(sceneState.renameBuffer, sizeof(sceneState.renameBuffer), "%s",
                                              groupName.c_str());
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Delete"))
                            {
                                deleteGroup = i;
                            }
                            if (sceneState.renamingGroup == i)
                            {
                                ImGui::SetNextItemWidth(180.f);
                                ImGui::InputText("##rename_group", sceneState.renameBuffer,
                                                 sizeof(sceneState.renameBuffer));
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Apply##rename"))
                                {
                                    const std::string renamed = sceneState.renameBuffer;
                                    if (!renamed.empty() && renamed != groupName)
                                    {
                                        for (Entity entity : entities)
                                        {
                                            if (Fem::GroupComponent* component =
                                                    sceneState.registry.try_get<Fem::GroupComponent>(entity))
                                            {
                                                component->group = renamed;
                                            }
                                        }
                                        sceneState.groupNames[i] = renamed;
                                        SaveGroupNames(sceneState);
                                    }
                                    sceneState.renamingGroup = -1;
                                }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Cancel##rename"))
                                {
                                    sceneState.renamingGroup = -1;
                                }
                            }
                            ImGui::PopID();
                        }
                        if (deleteGroup >= 0)
                        {
                            const std::string groupName =
                                sceneState.groupNames[static_cast<std::size_t>(deleteGroup)];
                            for (Entity entity : GroupEntities(sceneState.registry, groupName))
                            {
                                if (Fem::GroupComponent* component =
                                        sceneState.registry.try_get<Fem::GroupComponent>(entity))
                                {
                                    component->group.clear();
                                }
                            }
                            sceneState.groupNames.erase(sceneState.groupNames.begin() + deleteGroup);
                            SaveGroupNames(sceneState);
                        }
                    }

                    ImGui::TextWrapped("%s", sceneState.status.c_str());
                    ImGui::Separator();
                    if (ImGui::BeginChild("##hierarchy_entities", ImVec2(0, 0), true))
                    {
                        const auto entityLabel = [&](Entity entity)
                        {
                            std::string label = "#" + std::to_string(entt::to_integral(entity));
                            if (const TagComponent* tag = sceneState.registry.try_get<TagComponent>(entity);
                                tag && !tag->tag.empty())
                            {
                                label += "  " + tag->tag;
                            }
                            return label;
                        };
                        const auto entityDragDrop = [&](Entity entity, const std::string& label)
                        {
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                            {
                                const std::uint32_t raw = static_cast<std::uint32_t>(entity);
                                ImGui::SetDragDropPayload("FEMBOY_ENTITY", &raw, sizeof(raw));
                                ImGui::TextUnformatted(label.c_str());
                                ImGui::EndDragDropSource();
                            }
                            if (ImGui::BeginDragDropTarget())
                            {
                                if (const ImGuiPayload* payload =
                                        ImGui::AcceptDragDropPayload("FEMBOY_ENTITY"))
                                {
                                    const std::uint32_t raw = *static_cast<const std::uint32_t*>(payload->Data);
                                    SetEntityParent(sceneState.registry, static_cast<Entity>(raw), entity);
                                }
                                ImGui::EndDragDropTarget();
                            }
                            if (ImGui::BeginPopupContextItem())
                            {
                                if (ImGui::MenuItem("Unparent"))
                                {
                                    SetEntityParent(sceneState.registry, entity, entt::null);
                                }
                                if (ImGui::MenuItem("Delete"))
                                {
                                    const nlohmann::json removed = CaptureEntity(sceneState.registry, entity);
                                    Fem::UndoStack::Instance().Push(std::make_shared<DeleteEntityCommand>(
                                        sceneState.registry, removed, entity, sceneState.selected));
                                    ClearEntitySelection(sceneState);
                                }
                                ImGui::EndPopup();
                            }
                        };
                        const auto drawEntityRow = [&](Entity entity)
                        {
                            const std::string label = entityLabel(entity);
                            ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
                            if (ImGui::Selectable(label.c_str(), IsEntitySelected(sceneState, entity)))
                            {
                                SelectEntity(sceneState, entity, ImGui::GetIO().KeyCtrl);
                            }
                            entityDragDrop(entity, label);
                            ImGui::PopID();
                        };

                        if (sceneState.hierarchyGrouping == 0)
                        {
                            std::map<std::uint32_t, std::vector<Entity>> children;
                            std::vector<Entity> roots;
                            auto all = sceneState.registry.view<entt::entity>();
                            for (const Entity entity : all)
                            {
                                const Entity parent = ParentEntity(sceneState.registry, entity);
                                if (parent == entt::null)
                                {
                                    roots.push_back(entity);
                                }
                                else
                                {
                                    children[static_cast<std::uint32_t>(parent)].push_back(entity);
                                }
                            }
                            std::vector<Entity> visited;
                            std::function<void(Entity)> drawNode = [&](Entity entity)
                            {
                                if (std::find(visited.begin(), visited.end(), entity) != visited.end())
                                {
                                    return;
                                }
                                visited.push_back(entity);
                                const std::string label = entityLabel(entity);
                                const bool hasChildren = children.count(
                                                             static_cast<std::uint32_t>(entity)) != 0;
                                ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
                                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                                           ImGuiTreeNodeFlags_SpanAvailWidth;
                                if (IsEntitySelected(sceneState, entity))
                                {
                                    flags |= ImGuiTreeNodeFlags_Selected;
                                }
                                if (!hasChildren)
                                {
                                    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                                }
                                const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
                                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                                {
                                    SelectEntity(sceneState, entity, ImGui::GetIO().KeyCtrl);
                                }
                                entityDragDrop(entity, label);
                                if (open && hasChildren)
                                {
                                    for (Entity child : children[static_cast<std::uint32_t>(entity)])
                                    {
                                        drawNode(child);
                                    }
                                    ImGui::TreePop();
                                }
                                ImGui::PopID();
                            };
                            for (Entity entity : roots)
                            {
                                drawNode(entity);
                            }
                        }
                        else
                        {
                            std::map<std::string, std::vector<Entity>> groups;
                            auto all = sceneState.registry.view<entt::entity>();
                            for (const Entity entity : all)
                            {
                                std::string group = "Other";
                                if (sceneState.hierarchyGrouping == 1)
                                {
                                    if (const TagComponent* tag =
                                            sceneState.registry.try_get<TagComponent>(entity);
                                        tag && !tag->tag.empty())
                                    {
                                        group = tag->tag;
                                    }
                                    else
                                    {
                                        group = "(no tag)";
                                    }
                                }
                                else if (sceneState.hierarchyGrouping == 3)
                                {
                                    if (const Fem::GroupComponent* groupComponent =
                                            sceneState.registry.try_get<Fem::GroupComponent>(entity);
                                        groupComponent && !groupComponent->group.empty())
                                    {
                                        group = groupComponent->group;
                                    }
                                    else
                                    {
                                        group = "(no group)";
                                    }
                                }
                                else
                                {
                                    group = EntityCategory(sceneState.registry, entity);
                                }
                                groups[group].push_back(entity);
                            }
                            for (auto& [groupName, entities] : groups)
                            {
                                ImGui::PushID(groupName.c_str());
                                if (ImGui::TreeNodeEx(groupName.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                                {
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Select group"))
                                    {
                                        if (!entities.empty())
                                        {
                                            sceneState.selected = entities.front();
                                            sceneState.extraSelection.assign(entities.begin() + 1, entities.end());
                                        }
                                    }
                                    for (Entity entity : entities)
                                    {
                                        drawEntityRow(entity);
                                    }
                                    ImGui::TreePop();
                                }
                                ImGui::PopID();
                            }
                        }
                        if (sceneState.registry.storage<entt::entity>().size() == 0)
                        {
                            ImGui::TextDisabled("(empty scene)");
                        }
                        if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(),
                                                             ImGui::GetID("##hierarchy_drop")))
                        {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FEMBOY_ASSET"))
                            {
                                const std::string path(static_cast<const char*>(payload->Data));
                                if (EndsWith(path, ".scene"))
                                {
                                    if (sceneState.scene.LoadAdditive(path, sceneState.registry))
                                    {
                                        sceneState.status = "scene added from Asset Browser";
                                    }
                                }
                                else if (path.find(".prefab") != std::string::npos)
                                {
                                    Fem::Prefab prefab;
                                    if (prefab.Load(path))
                                    {
                                        prefab.Instantiate(sceneState.registry);
                                        sceneState.status = "prefab added from Asset Browser";
                                    }
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                    }
                    ImGui::EndChild();
                }
                else if (name == "Inspector")
                {
                    if (!sceneState.registry.valid(sceneState.selected))
                    {
                        ImGui::TextDisabled("Select an entity in the Hierarchy.");
                    }
                    else
                    {
                        const Entity e = sceneState.selected;
                        ImGui::Text("Entity #%u", static_cast<unsigned>(entt::to_integral(e)));

                        char tagBuffer[128] = "";
                        if (const TagComponent* tag = sceneState.registry.try_get<TagComponent>(e))
                        {
                            std::snprintf(tagBuffer, sizeof(tagBuffer), "%s", tag->tag.c_str());
                        }
                        if (ImGui::InputText("Tag", tagBuffer, sizeof(tagBuffer)))
                        {
                            if (TagComponent* tag = sceneState.registry.try_get<TagComponent>(e))
                            {
                                tag->tag = tagBuffer;
                            }
                            else
                            {
                                sceneState.registry.emplace<TagComponent>(e).tag = tagBuffer;
                            }
                        }

                        if (ImGui::Button("Add component"))
                        {
                            ImGui::OpenPopup("add_component");
                        }
                        if (ImGui::BeginPopup("add_component"))
                        {
                            for (const auto& comp : Fem::Mirror::Instance().Components())
                            {
                                if (comp.has(sceneState.registry, e))
                                {
                                    continue;
                                }
                                if (ImGui::Selectable(comp.name.c_str()))
                                {
                                    Fem::ClassDefaults::Instance().ApplyTo(sceneState.registry, e, comp);
                                    sceneState.status = "added " + comp.name;
                                    break;
                                }
                            }
                            ImGui::EndPopup();
                        }

                        ImGui::InputTextWithHint("##field_filter", "Filter properties", sceneState.filter,
                                                 sizeof(sceneState.filter));
                        ImGui::SameLine();
                        ImGui::Checkbox("Advanced", &sceneState.advanced);
                        ImGui::Separator();
                        for (const auto& comp : Fem::Mirror::Instance().Components())
                        {
                            if (!comp.has(sceneState.registry, e))
                            {
                                continue;
                            }
                            if (!ImGui::TreeNodeEx(comp.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                continue;
                            }

                            ImGui::PushID(comp.jsonKey.c_str());
                            bool remove = false;
                            DrawComponentFields(sceneState.registry, e, comp, nullptr, sceneState.advanced,
                                                sceneState.filter);
                            if (!comp.fields->empty())
                            {
                                ImGui::Separator();
                            }
                            if (ImGui::SmallButton("Remove component"))
                            {
                                remove = true;
                            }
                            ImGui::PopID();
                            ImGui::TreePop();
                            if (remove)
                            {
                                comp.remove(sceneState.registry, e);
                                sceneState.status = "removed " + comp.name;
                                break;
                            }
                        }
                    }
                }
                else if (name == "Viewport")
                {
                    const char* modes[] = {"Orthographic", "Isometric (2:1)"};
                    int mode = viewport.projection == Fem::ProjectionMode::Isometric ? 1 : 0;
                    ImGui::SetNextItemWidth(180.f);
                    if (ImGui::Combo("Projection", &mode, modes, 2))
                    {
                        viewport.projection = mode == 0 ? Fem::ProjectionMode::Orthographic
                                                        : Fem::ProjectionMode::Isometric;
                        viewport.camera.SetProjection(viewport.projection);
                    }
                    ImGui::SameLine();
                    ImGui::Checkbox("Grid", &viewport.showGrid);
                    if (viewport.hexAllowed)
                    {
                        ImGui::SameLine();
                        ImGui::Checkbox("Hex", &viewport.hexGrid);
                    }
                    const char* sortModes[] = {"No sorting", "Y-sort", "Isometric depth"};
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(170.f);
                    ImGui::Combo("Sort", &viewport.sortMode, sortModes, 3);
                    EditorTooltip("Y-sort draws lower objects later. Isometric depth sorts by x+y, "
                                  "which is the standard painter's order for 2:1 iso tiles. "
                                  "Sprite Depth (z) remains the explicit layer override.");
                    ImGui::SameLine();
                    if (ImGui::Button("Reset camera"))
                    {
                        viewport.camera.SetPosition(0.f, 0.f);
                        viewport.camera.SetZoom(1.f);
                        viewport.camera.SetRotation(0.f);
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("zoom %.2f", viewport.camera.Zoom());
                    ImGui::TextDisabled("Gizmo:");
                    int gizmoMode = static_cast<int>(viewport.gizmoMode);
                    ImGui::SameLine();
                    ImGui::RadioButton("Move (W)", &gizmoMode, static_cast<int>(ViewportState::GizmoMode::Move));
                    ImGui::SameLine();
                    ImGui::RadioButton("Rotate (E)", &gizmoMode, static_cast<int>(ViewportState::GizmoMode::Rotate));
                    ImGui::SameLine();
                    ImGui::RadioButton("Scale (R)", &gizmoMode, static_cast<int>(ViewportState::GizmoMode::Scale));
                    viewport.gizmoMode = static_cast<ViewportState::GizmoMode>(gizmoMode);
                    ImGui::SameLine();
                    ImGui::TextDisabled("| Axis:");
                    ImGui::SameLine();
                    int axisLock = viewport.axisLock;
                    ImGui::RadioButton("Free", &axisLock, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton("X", &axisLock, 1);
                    ImGui::SameLine();
                    ImGui::RadioButton("Y", &axisLock, 2);
                    viewport.axisLock = axisLock;
                    ImGui::SameLine();
                    ImGui::Checkbox("Snap", &viewport.snapEnabled);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(78.f);
                    ImGui::DragFloat("M", &viewport.moveSnap, 0.01f, 0.001f, 1024.f, "%.3f");
                    EditorTooltip("Move increment. Disable Snap for continuous mouse-driven movement.");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70.f);
                    ImGui::DragFloat("R", &viewport.rotationSnap, 0.5f, 0.1f, 180.f, "%.1f");
                    EditorTooltip("Rotation increment in degrees.");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70.f);
                    ImGui::DragFloat("S", &viewport.scaleSnap, 0.01f, 0.001f, 10.f, "%.3f");
                    EditorTooltip("Scale increment. Values below 0.01 allow very fine resizing.");
                    ImGui::TextDisabled("%s", viewport.status.c_str());

                    const ImVec2 available = ImGui::GetContentRegionAvail();
                    const int pixelWidth = std::max(32, static_cast<int>(available.x));
                    const int pixelHeight = std::max(32, static_cast<int>(available.y));
                    if (!viewport.ready || pixelWidth != viewport.pixelWidth || pixelHeight != viewport.pixelHeight)
                    {
                        viewport.pixelWidth = pixelWidth;
                        viewport.pixelHeight = pixelHeight;
                        viewport.ready = viewport.target.resize(
                            {static_cast<unsigned>(pixelWidth), static_cast<unsigned>(pixelHeight)});
                    }

                    if (viewport.ready)
                    {
                        viewport.camera.SetViewportSize(static_cast<float>(pixelWidth), static_cast<float>(pixelHeight));
                        viewport.camera.SetProjection(viewport.projection);
                        viewport.camera.Update(0.f, sceneState.registry);

                        viewport.target.clear(sf::Color(18, 18, 24));
                        viewport.target.setView(viewport.camera.View());
                        const sf::Transform world = viewport.camera.WorldTransform();
                        sf::RenderStates states;
                        states.transform = world;

                        if (viewport.showGrid)
                        {
                            // Adaptive coordinate grid: the step depends on
                            // zoom, not on a tile size. Far away we draw few
                            // large cells; close up we draw many small ones.
                            const sf::FloatRect screenRect({0.f, 0.f},
                                                           {static_cast<float>(pixelWidth),
                                                            static_cast<float>(pixelHeight)});
                            const sf::Vector2f corners[4] = {
                                viewport.camera.ScreenToWorld({0.f, 0.f}, screenRect),
                                viewport.camera.ScreenToWorld({static_cast<float>(pixelWidth), 0.f}, screenRect),
                                viewport.camera.ScreenToWorld({0.f, static_cast<float>(pixelHeight)}, screenRect),
                                viewport.camera.ScreenToWorld({static_cast<float>(pixelWidth),
                                                               static_cast<float>(pixelHeight)},
                                                              screenRect)};
                            sf::Vector2f worldMin = corners[0];
                            sf::Vector2f worldMax = corners[0];
                            for (int i = 1; i < 4; ++i)
                            {
                                worldMin.x = std::min(worldMin.x, corners[i].x);
                                worldMin.y = std::min(worldMin.y, corners[i].y);
                                worldMax.x = std::max(worldMax.x, corners[i].x);
                                worldMax.y = std::max(worldMax.y, corners[i].y);
                            }
                            const float zoom = std::max(0.001f, viewport.camera.Zoom());
                            const float rawStep = 80.f / zoom;
                            const float magnitude = std::pow(10.f, std::floor(std::log10(std::max(0.00001f, rawStep))));
                            const float normalized = rawStep / magnitude;
                            const float step = (normalized <= 1.f ? 1.f : normalized <= 2.f ? 2.f : normalized <= 5.f ? 5.f : 10.f) * magnitude;
                            const sf::Color minorColor(38, 38, 50);
                            const sf::Color majorColor(55, 55, 70);
                            const sf::Color axisColor(82, 82, 104);
                            sf::VertexArray grid(sf::PrimitiveType::Lines);

                            if (viewport.hexGrid)
                            {
                                // Hexes use the same nice-number step as the
                                // square grid, so they grow/shrink in world
                                // units as the camera zooms instead of staying
                                // visually frozen.
                                const float hexSize = step * 0.5f;
                                const float pi = 3.14159265358979323846f;
                                const float colStep = std::sqrt(3.f) * hexSize;
                                const float rowStep = 1.5f * hexSize;
                                const int firstCol = static_cast<int>(std::floor(worldMin.x / colStep)) - 1;
                                const int lastCol = static_cast<int>(std::ceil(worldMax.x / colStep)) + 1;
                                const int firstRow = static_cast<int>(std::floor(worldMin.y / rowStep)) - 1;
                                const int lastRow = static_cast<int>(std::ceil(worldMax.y / rowStep)) + 1;
                                for (int row = firstRow; row <= lastRow; ++row)
                                {
                                    for (int col = firstCol; col <= lastCol; ++col)
                                    {
                                        const float cx = col * colStep + ((row & 1) ? colStep * 0.5f : 0.f);
                                        const float cy = row * rowStep;
                                        for (int edge = 0; edge < 6; ++edge)
                                        {
                                            const float a0 = (60.f * edge - 30.f) * pi / 180.f;
                                            const float a1 = (60.f * (edge + 1) - 30.f) * pi / 180.f;
                                            grid.append(sf::Vertex{
                                                sf::Vector2f(cx + hexSize * std::cos(a0), cy + hexSize * std::sin(a0)),
                                                minorColor});
                                            grid.append(sf::Vertex{
                                                sf::Vector2f(cx + hexSize * std::cos(a1), cy + hexSize * std::sin(a1)),
                                                minorColor});
                                        }
                                    }
                                }
                            }
                            else
                            {
                                const int firstX = static_cast<int>(std::floor(worldMin.x / step));
                                const int lastX = static_cast<int>(std::ceil(worldMax.x / step));
                                const int firstY = static_cast<int>(std::floor(worldMin.y / step));
                                const int lastY = static_cast<int>(std::ceil(worldMax.y / step));
                                for (int i = firstX; i <= lastX; ++i)
                                {
                                    const float x = static_cast<float>(i) * step;
                                    const sf::Color color = i == 0 ? axisColor : (i % 5 == 0 ? majorColor : minorColor);
                                    grid.append(sf::Vertex{sf::Vector2f(x, worldMin.y), color});
                                    grid.append(sf::Vertex{sf::Vector2f(x, worldMax.y), color});
                                }
                                for (int i = firstY; i <= lastY; ++i)
                                {
                                    const float y = static_cast<float>(i) * step;
                                    const sf::Color color = i == 0 ? axisColor : (i % 5 == 0 ? majorColor : minorColor);
                                    grid.append(sf::Vertex{sf::Vector2f(worldMin.x, y), color});
                                    grid.append(sf::Vertex{sf::Vector2f(worldMax.x, y), color});
                                }
                            }
                            viewport.target.draw(grid, states);
                        }

                        auto sprites = sceneState.registry.view<TransformComponent, Fem::SpriteComponent>();
                        struct SpriteDraw
                        {
                            Entity entity;
                            EntityTransform world;
                            const Fem::SpriteComponent* sprite;
                        };
                        std::vector<SpriteDraw> spriteDraws;
                        spriteDraws.reserve(sprites.size_hint());
                        for (const Entity entity : sprites)
                        {
                            spriteDraws.push_back(
                                {entity, WorldTransformOf(sceneState.registry, entity),
                                 &sprites.get<Fem::SpriteComponent>(entity)});
                        }
                        if (viewport.sortMode == 2)
                        {
                            std::stable_sort(spriteDraws.begin(), spriteDraws.end(),
                                             [](const SpriteDraw& a, const SpriteDraw& b)
                                             {
                                                 if (a.sprite->z != b.sprite->z)
                                                 {
                                                     return a.sprite->z < b.sprite->z;
                                                 }
                                                 const float ka = a.world.x + a.world.y;
                                                 const float kb = b.world.x + b.world.y;
                                                 if (ka != kb)
                                                 {
                                                     return ka < kb;
                                                 }
                                                 if (a.world.y != b.world.y)
                                                 {
                                                     return a.world.y < b.world.y;
                                                 }
                                                 return entt::to_integral(a.entity) <
                                                        entt::to_integral(b.entity);
                                             });
                        }
                        else if (viewport.sortMode == 1)
                        {
                            std::stable_sort(spriteDraws.begin(), spriteDraws.end(),
                                             [](const SpriteDraw& a, const SpriteDraw& b)
                                             {
                                                 if (a.sprite->z != b.sprite->z)
                                                 {
                                                     return a.sprite->z < b.sprite->z;
                                                 }
                                                 if (a.world.y != b.world.y)
                                                 {
                                                     return a.world.y < b.world.y;
                                                 }
                                                 return entt::to_integral(a.entity) <
                                                        entt::to_integral(b.entity);
                                             });
                        }
                        for (const SpriteDraw& drawEntry : spriteDraws)
                        {
                            const EntityTransform& transform = drawEntry.world;
                            const Fem::SpriteComponent& spriteData = *drawEntry.sprite;
                            sf::Sprite sprite(Fem::AssetManager::Instance().Texture(spriteData.textureId));
                            if (spriteData.srcW > 0 && spriteData.srcH > 0)
                            {
                                sprite.setTextureRect(sf::IntRect({spriteData.srcX, spriteData.srcY},
                                                                  {spriteData.srcW, spriteData.srcH}));
                            }
                            const sf::IntRect rect = sprite.getTextureRect();
                            sprite.setOrigin({static_cast<float>(rect.size.x) * spriteData.originX,
                                              static_cast<float>(rect.size.y) * spriteData.originY});
                            sprite.setPosition({transform.x, transform.y});
                            sprite.setScale({transform.scaleX, transform.scaleY});
                            sprite.setRotation(sf::degrees(transform.rotation));
                            sprite.setColor(spriteData.tint);
                            viewport.target.draw(sprite, states);
                        }

                        // Entities without a sprite still need a visible handle;
                        // the hierarchy can create/select them before art exists.
                        auto transforms = sceneState.registry.view<TransformComponent>();
                        for (const Entity entity : transforms)
                        {
                            const EntityTransform transform = WorldTransformOf(sceneState.registry, entity);
                            if (sceneState.registry.all_of<Fem::SpriteComponent>(entity) &&
                                entity != sceneState.selected)
                            {
                                continue;
                            }
                            const float half = 12.f;
                            sf::VertexArray marker(sf::PrimitiveType::LineStrip, 5);
                            const sf::Vector2f corners[4] = {{transform.x - half, transform.y - half},
                                                             {transform.x + half, transform.y - half},
                                                             {transform.x + half, transform.y + half},
                                                             {transform.x - half, transform.y + half}};
                            const sf::Color color =
                                entity == sceneState.selected ? sf::Color(255, 120, 200) : sf::Color(90, 90, 110);
                            for (int i = 0; i < 4; ++i)
                            {
                                marker[i] = sf::Vertex{corners[i], color};
                            }
                            marker[4] = sf::Vertex{corners[0], color};
                            viewport.target.draw(marker, states);
                        }
                        viewport.target.display();

                        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
                        ImGui::Image(viewport.target, sf::Vector2f(static_cast<float>(pixelWidth),
                                                                   static_cast<float>(pixelHeight)));
                        ImGui::SetCursorScreenPos(imageMin);
                        ImGui::InvisibleButton("##viewport_input", ImVec2(static_cast<float>(pixelWidth),
                                                                         static_cast<float>(pixelHeight)),
                                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                                                   ImGuiButtonFlags_MouseButtonRight);
                        const bool hovered = ImGui::IsItemHovered();
                        const sf::FloatRect viewportRect({imageMin.x, imageMin.y},
                                                         {static_cast<float>(pixelWidth),
                                                          static_cast<float>(pixelHeight)});
                        if (ImGui::BeginDragDropTarget())
                        {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FEMBOY_ASSET"))
                            {
                                const std::string path(static_cast<const char*>(payload->Data));
                                if (path.find(".prefab") != std::string::npos)
                                {
                                    Fem::Prefab prefab;
                                    if (prefab.Load(path))
                                    {
                                        const Entity created = prefab.Instantiate(sceneState.registry);
                                        if (TransformComponent* transform =
                                                sceneState.registry.try_get<TransformComponent>(created))
                                        {
                                            const ImVec2 mouse = ImGui::GetIO().MousePos;
                                            const sf::Vector2f world = viewport.camera.ScreenToWorld(
                                                {mouse.x, mouse.y}, viewportRect);
                                            transform->x = world.x;
                                            transform->y = world.y;
                                        }
                                        sceneState.selected = created;
                                        sceneState.extraSelection.clear();
                                        sceneState.status = "prefab dropped into viewport";
                                    }
                                }
                                else if (EndsWith(path, ".scene") &&
                                         sceneState.scene.LoadAdditive(path, sceneState.registry))
                                {
                                    sceneState.status = "scene dropped into viewport";
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        ImGuiIO& viewportIO = ImGui::GetIO();
                        const ImVec2 viewportMouse = viewportIO.MousePos;
                        const bool viewportFocused =
                            hovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

                        // A small screen-space gizmo is enough for a 2D editor
                        // and stays readable through camera zoom/projection.
                        ImVec2 gizmoCenter(imageMin.x, imageMin.y);
                        bool gizmoVisible = false;
                        if (sceneState.registry.valid(sceneState.selected))
                        {
                            if (sceneState.registry.all_of<TransformComponent>(sceneState.selected))
                            {
                                const EntityTransform selectedTransform =
                                    WorldTransformOf(sceneState.registry, sceneState.selected);
                                const sf::Vector2f screen = viewport.camera.WorldToScreen(
                                    {selectedTransform.x, selectedTransform.y}, viewportRect);
                                constexpr float margin = 90.f;
                                if (screen.x >= viewportRect.position.x - margin &&
                                    screen.x <= viewportRect.position.x + viewportRect.size.x + margin &&
                                    screen.y >= viewportRect.position.y - margin &&
                                    screen.y <= viewportRect.position.y + viewportRect.size.y + margin)
                                {
                                    gizmoCenter = ImVec2(screen.x, screen.y);
                                    gizmoVisible = true;
                                }
                            }
                        }
                        const float axisLength = 78.f;
                        const ImVec2 xHandle(gizmoCenter.x + axisLength, gizmoCenter.y);
                        const ImVec2 yHandle(gizmoCenter.x, gizmoCenter.y - axisLength);
                        const ImU32 gizmoRed = IM_COL32(235, 90, 90, 255);
                        const ImU32 gizmoGreen = IM_COL32(95, 205, 125, 255);
                        const ImU32 gizmoYellow = IM_COL32(240, 205, 90, 255);
                        const ImU32 gizmoBlue = IM_COL32(95, 155, 255, 255);
                        ImDrawList* gizmoDraw = ImGui::GetWindowDrawList();
                        if (gizmoVisible)
                        {
                            const int activeAxis =
                                viewport.gizmoActive ? viewport.gizmoAxis : viewport.axisLock;
                            const ImU32 xColor =
                                activeAxis == 2 ? IM_COL32(235, 90, 90, 70) : gizmoRed;
                            const ImU32 yColor =
                                activeAxis == 1 ? IM_COL32(95, 205, 125, 70) : gizmoGreen;
                            if (viewport.gizmoMode == ViewportState::GizmoMode::Move)
                            {
                                gizmoDraw->AddLine(gizmoCenter, xHandle, xColor, 2.f);
                                gizmoDraw->AddTriangleFilled(xHandle, ImVec2(xHandle.x - 10.f, xHandle.y - 6.f),
                                                             ImVec2(xHandle.x - 10.f, xHandle.y + 6.f), xColor);
                                gizmoDraw->AddLine(gizmoCenter, yHandle, yColor, 2.f);
                                gizmoDraw->AddTriangleFilled(yHandle, ImVec2(yHandle.x - 6.f, yHandle.y + 10.f),
                                                             ImVec2(yHandle.x + 6.f, yHandle.y + 10.f), yColor);
                                gizmoDraw->AddRectFilled(ImVec2(gizmoCenter.x - 5.f, gizmoCenter.y - 5.f),
                                                         ImVec2(gizmoCenter.x + 5.f, gizmoCenter.y + 5.f),
                                                         gizmoYellow);
                            }
                            else if (viewport.gizmoMode == ViewportState::GizmoMode::Rotate)
                            {
                                gizmoDraw->AddCircle(gizmoCenter, 60.f, gizmoBlue, 48, 2.f);
                                for (int i = 0; i < 4; ++i)
                                {
                                    const float angle = static_cast<float>(i) * 1.5707963f;
                                    const ImVec2 p(gizmoCenter.x + std::cos(angle) * 60.f,
                                                   gizmoCenter.y + std::sin(angle) * 60.f);
                                    gizmoDraw->AddCircleFilled(p, 4.f, gizmoBlue);
                                }
                            }
                            else
                            {
                                gizmoDraw->AddLine(gizmoCenter, xHandle, xColor, 2.f);
                                gizmoDraw->AddRectFilled(ImVec2(xHandle.x - 6.f, xHandle.y - 6.f),
                                                         ImVec2(xHandle.x + 6.f, xHandle.y + 6.f), xColor);
                                gizmoDraw->AddLine(gizmoCenter, yHandle, yColor, 2.f);
                                gizmoDraw->AddRectFilled(ImVec2(yHandle.x - 6.f, yHandle.y - 6.f),
                                                         ImVec2(yHandle.x + 6.f, yHandle.y + 6.f), yColor);
                                gizmoDraw->AddRectFilled(ImVec2(gizmoCenter.x - 5.f, gizmoCenter.y - 5.f),
                                                         ImVec2(gizmoCenter.x + 5.f, gizmoCenter.y + 5.f),
                                                         gizmoYellow);
                            }
                        }

                        const auto nearPoint = [&](ImVec2 point, float radius)
                        {
                            return std::hypot(viewportMouse.x - point.x, viewportMouse.y - point.y) <= radius;
                        };
                        bool gizmoStarted = false;
                        if (hovered && !viewport.gizmoActive && gizmoVisible &&
                            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            int axis = 0;
                            if (viewport.gizmoMode == ViewportState::GizmoMode::Rotate)
                            {
                                const float distance = std::hypot(viewportMouse.x - gizmoCenter.x,
                                                                  viewportMouse.y - gizmoCenter.y);
                                if (std::abs(distance - 60.f) <= 12.f)
                                {
                                    axis = 3;
                                }
                            }
                            else if (nearPoint(gizmoCenter, 10.f))
                            {
                                axis = 3;
                            }
                            else if (nearPoint(xHandle, 12.f))
                            {
                                axis = 1;
                            }
                            else if (nearPoint(yHandle, 12.f))
                            {
                                axis = 2;
                            }

                            if (axis != 0)
                            {
                                viewport.gizmoActive = true;
                                viewport.gizmoAxis =
                                    (axis == 3 && viewport.gizmoMode != ViewportState::GizmoMode::Rotate &&
                                     viewport.axisLock != 0)
                                        ? viewport.axisLock
                                        : axis;
                                viewport.gizmoOriginal = sceneState.selected;
                                viewport.gizmoMouseStart = viewportMouse;
                                viewport.dragEntity = entt::null;
                                viewport.gizmoSpawned = false;
                                viewport.gizmoEntities = SelectedEntities(sceneState);

                                // Alt-drag duplicates the whole selection and
                                // then manipulates the copies, matching DCC/UE
                                // behaviour.
                                if (viewportIO.KeyAlt)
                                {
                                    std::vector<Entity> clones;
                                    for (Entity entity : viewport.gizmoEntities)
                                    {
                                        clones.push_back(ApplyEntity(
                                            sceneState.registry,
                                            CaptureEntity(sceneState.registry, entity)));
                                    }
                                    viewport.gizmoEntities = clones;
                                    sceneState.extraSelection.clear();
                                    sceneState.selected = clones.empty() ? entt::null : clones.front();
                                    for (std::size_t i = 1; i < clones.size(); ++i)
                                    {
                                        sceneState.extraSelection.push_back(clones[i]);
                                    }
                                    viewport.gizmoSpawned = true;
                                }
                                viewport.gizmoEntity =
                                    viewport.gizmoEntities.empty() ? entt::null : viewport.gizmoEntities.front();
                                viewport.gizmoBeforeGroup.clear();
                                viewport.gizmoBeforeWorld.clear();
                                viewport.gizmoBeforeWorld.clear();
                                viewport.gizmoPivot = {0.f, 0.f};
                                for (Entity entity : viewport.gizmoEntities)
                                {
                                    viewport.gizmoBeforeGroup.push_back(
                                        ReadTransform(sceneState.registry, entity));
                                    const EntityTransform world =
                                        WorldTransformOf(sceneState.registry, entity);
                                    viewport.gizmoBeforeWorld.push_back(world);
                                    viewport.gizmoPivot.x += world.x;
                                    viewport.gizmoPivot.y += world.y;
                                }
                                if (!viewport.gizmoEntities.empty())
                                {
                                    const float count = static_cast<float>(viewport.gizmoEntities.size());
                                    viewport.gizmoPivot.x /= count;
                                    viewport.gizmoPivot.y /= count;
                                }
                                viewport.gizmoBefore = viewport.gizmoBeforeGroup.empty()
                                                           ? EntityTransform{}
                                                           : viewport.gizmoBeforeGroup.front();
                                gizmoStarted = true;
                                viewport.status = "gizmo drag - Alt duplicates";
                            }
                        }

                        if (viewport.gizmoActive)
                        {
                            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                            {
                                if (viewport.gizmoSpawned)
                                {
                                    for (Entity entity : viewport.gizmoEntities)
                                    {
                                        if (sceneState.registry.valid(entity))
                                        {
                                            sceneState.registry.destroy(entity);
                                        }
                                    }
                                    sceneState.selected = viewport.gizmoOriginal;
                                    sceneState.extraSelection.clear();
                                }
                                else
                                {
                                    for (std::size_t i = 0; i < viewport.gizmoEntities.size() &&
                                                            i < viewport.gizmoBeforeGroup.size();
                                         ++i)
                                    {
                                        WriteTransform(sceneState.registry, viewport.gizmoEntities[i],
                                                       viewport.gizmoBeforeGroup[i]);
                                    }
                                }
                                viewport.gizmoActive = false;
                                viewport.gizmoSpawned = false;
                                viewport.gizmoEntities.clear();
                                viewport.gizmoBeforeGroup.clear();
                                viewport.gizmoBeforeWorld.clear();
                                viewport.status = "gizmo drag cancelled";
                            }
                            else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                            {
                                float dx = 0.f;
                                float dy = 0.f;
                                float deltaRotation = 0.f;
                                float factorX = 1.f;
                                float factorY = 1.f;
                                if (viewport.gizmoMode == ViewportState::GizmoMode::Move)
                                {
                                    const sf::Vector2f startWorld =
                                        viewport.camera.ScreenToWorld({viewport.gizmoMouseStart.x,
                                                                       viewport.gizmoMouseStart.y},
                                                                      viewportRect);
                                    const sf::Vector2f nowWorld =
                                        viewport.camera.ScreenToWorld({viewportMouse.x, viewportMouse.y},
                                                                      viewportRect);
                                    dx = nowWorld.x - startWorld.x;
                                    dy = nowWorld.y - startWorld.y;
                                }
                                else if (viewport.gizmoMode == ViewportState::GizmoMode::Rotate)
                                {
                                    const float startAngle = std::atan2(viewport.gizmoMouseStart.y - gizmoCenter.y,
                                                                        viewport.gizmoMouseStart.x - gizmoCenter.x);
                                    const float currentAngle =
                                        std::atan2(viewportMouse.y - gizmoCenter.y, viewportMouse.x - gizmoCenter.x);
                                    deltaRotation = (currentAngle - startAngle) * (180.f / 3.1415926535f);
                                }
                                else
                                {
                                    factorX = 1.f + (viewportMouse.x - viewport.gizmoMouseStart.x) / 120.f;
                                    factorY = 1.f + (viewport.gizmoMouseStart.y - viewportMouse.y) / 120.f;
                                    if ((viewport.snapEnabled || viewportIO.KeyShift) && viewport.scaleSnap > 0.f)
                                    {
                                        factorX = std::max(
                                            0.01f,
                                            std::round(factorX / viewport.scaleSnap) * viewport.scaleSnap);
                                        factorY = std::max(
                                            0.01f,
                                            std::round(factorY / viewport.scaleSnap) * viewport.scaleSnap);
                                    }
                                }
                                if ((viewport.snapEnabled || viewportIO.KeyShift) &&
                                    viewport.gizmoMode == ViewportState::GizmoMode::Rotate &&
                                    viewport.rotationSnap > 0.f)
                                {
                                    deltaRotation = std::round(deltaRotation / viewport.rotationSnap) *
                                                    viewport.rotationSnap;
                                }

                                for (std::size_t i = 0;
                                     i < viewport.gizmoEntities.size() &&
                                     i < viewport.gizmoBeforeGroup.size() &&
                                     i < viewport.gizmoBeforeWorld.size();
                                     ++i)
                                {
                                    EntityTransform targetWorld = viewport.gizmoBeforeWorld[i];
                                    if (viewport.gizmoMode == ViewportState::GizmoMode::Move)
                                    {
                                        if (viewport.gizmoAxis == 1 || viewport.gizmoAxis == 3)
                                        {
                                            targetWorld.x += dx;
                                        }
                                        if (viewport.gizmoAxis == 2 || viewport.gizmoAxis == 3)
                                        {
                                            targetWorld.y += dy;
                                        }
                                        if ((viewport.snapEnabled || viewportIO.KeyShift) && viewport.moveSnap > 0.f)
                                        {
                                            targetWorld.x = std::round(targetWorld.x / viewport.moveSnap) *
                                                            viewport.moveSnap;
                                            targetWorld.y = std::round(targetWorld.y / viewport.moveSnap) *
                                                            viewport.moveSnap;
                                        }
                                    }
                                    else if (viewport.gizmoMode == ViewportState::GizmoMode::Rotate)
                                    {
                                        constexpr float pi = 3.14159265358979323846f;
                                        const float radians = deltaRotation * pi / 180.f;
                                        const float offsetX = targetWorld.x - viewport.gizmoPivot.x;
                                        const float offsetY = targetWorld.y - viewport.gizmoPivot.y;
                                        const float c = std::cos(radians);
                                        const float s = std::sin(radians);
                                        targetWorld.x = viewport.gizmoPivot.x + c * offsetX - s * offsetY;
                                        targetWorld.y = viewport.gizmoPivot.y + s * offsetX + c * offsetY;
                                        targetWorld.rotation += deltaRotation;
                                    }
                                    else
                                    {
                                        if (viewport.gizmoAxis == 1 || viewport.gizmoAxis == 3)
                                        {
                                            targetWorld.x = viewport.gizmoPivot.x +
                                                            (targetWorld.x - viewport.gizmoPivot.x) * factorX;
                                            targetWorld.scaleX =
                                                std::clamp(targetWorld.scaleX * factorX, 0.05f, 20.f);
                                        }
                                        if (viewport.gizmoAxis == 2 || viewport.gizmoAxis == 3)
                                        {
                                            targetWorld.y = viewport.gizmoPivot.y +
                                                            (targetWorld.y - viewport.gizmoPivot.y) * factorY;
                                            targetWorld.scaleY =
                                                std::clamp(targetWorld.scaleY * factorY, 0.05f, 20.f);
                                        }
                                    }
                                    const EntityTransform after = LocalTransformFromWorld(
                                        sceneState.registry, viewport.gizmoEntities[i], targetWorld);
                                    WriteTransform(sceneState.registry, viewport.gizmoEntities[i], after);
                                }
                            }
                            else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                            {
                                if (viewport.gizmoSpawned)
                                {
                                    std::vector<nlohmann::json> created;
                                    for (Entity entity : viewport.gizmoEntities)
                                    {
                                        if (sceneState.registry.valid(entity))
                                        {
                                            created.push_back(CaptureEntity(sceneState.registry, entity));
                                        }
                                    }
                                    if (!created.empty())
                                    {
                                        Fem::UndoStack::Instance().Push(std::make_shared<SpawnGroupCommand>(
                                            sceneState.registry, std::move(created), sceneState.selected,
                                            viewport.gizmoEntities));
                                    }
                                    viewport.status = "duplicated selection";
                                }
                                else
                                {
                                    std::vector<TransformManyCommand::Entry> entries;
                                    for (std::size_t i = 0; i < viewport.gizmoEntities.size() &&
                                                            i < viewport.gizmoBeforeGroup.size();
                                         ++i)
                                    {
                                        const EntityTransform after =
                                            ReadTransform(sceneState.registry, viewport.gizmoEntities[i]);
                                        const EntityTransform& before = viewport.gizmoBeforeGroup[i];
                                        if (after.x != before.x || after.y != before.y ||
                                            after.rotation != before.rotation ||
                                            after.scaleX != before.scaleX || after.scaleY != before.scaleY)
                                        {
                                            entries.push_back({viewport.gizmoEntities[i], before, after});
                                        }
                                    }
                                    if (!entries.empty())
                                    {
                                        Fem::UndoStack::Instance().Push(std::make_shared<TransformManyCommand>(
                                            sceneState.registry, std::move(entries)));
                                    }
                                }
                                viewport.gizmoActive = false;
                                viewport.gizmoSpawned = false;
                                viewport.gizmoAxis = 0;
                                viewport.gizmoEntities.clear();
                                viewport.gizmoBeforeGroup.clear();
                                viewport.gizmoBeforeWorld.clear();
                            }
                        }

                        // Object hotkeys: W/E/R tool modes, F focus, arrows nudge,
                        // Delete remove, Ctrl+D duplicate, Ctrl+C/V clipboard.
                        if (!viewportIO.WantTextInput && viewportFocused && !viewport.gizmoActive)
                        {
                            if (IsBindingPressed(editorSettings.hkMove))
                            {
                                viewport.gizmoMode = ViewportState::GizmoMode::Move;
                            }
                            if (IsBindingPressed(editorSettings.hkRotate))
                            {
                                viewport.gizmoMode = ViewportState::GizmoMode::Rotate;
                            }
                            if (IsBindingPressed(editorSettings.hkScale))
                            {
                                viewport.gizmoMode = ViewportState::GizmoMode::Scale;
                            }
                            if (ImGui::IsKeyPressed(ImGuiKey_X, false))
                            {
                                viewport.axisLock = viewport.axisLock == 1 ? 0 : 1;
                            }
                            if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                            {
                                viewport.axisLock = viewport.axisLock == 2 ? 0 : 2;
                            }
                            if (sceneState.registry.valid(sceneState.selected))
                            {
                                if (IsBindingPressed(editorSettings.hkFocus))
                                {
                                    if (const TransformComponent* transform =
                                            sceneState.registry.try_get<TransformComponent>(sceneState.selected))
                                    {
                                        viewport.camera.SetPosition(transform->x, transform->y);
                                    }
                                }
                                if (!viewportIO.KeyCtrl)
                                {
                                    const float step = viewportIO.KeyShift ? 10.f : 1.f;
                                    const EntityTransform before =
                                        ReadTransform(sceneState.registry, sceneState.selected);
                                    EntityTransform after = before;
                                    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
                                    {
                                        after.x -= step;
                                    }
                                    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
                                    {
                                        after.x += step;
                                    }
                                    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
                                    {
                                        after.y -= step;
                                    }
                                    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
                                    {
                                        after.y += step;
                                    }
                                    if (after.x != before.x || after.y != before.y)
                                    {
                                        WriteTransform(sceneState.registry, sceneState.selected, after);
                                        Fem::UndoStack::Instance().Push(std::make_shared<TransformStateCommand>(
                                            sceneState.registry, sceneState.selected, before, after));
                                    }
                                }
                                if (IsBindingPressed(editorSettings.hkDelete))
                                {
                                    const std::vector<Entity> selectedEntities = SelectedEntities(sceneState);
                                    for (Entity entity : selectedEntities)
                                    {
                                        if (sceneState.registry.valid(entity))
                                        {
                                            const nlohmann::json removed =
                                                CaptureEntity(sceneState.registry, entity);
                                            Fem::UndoStack::Instance().Push(std::make_shared<DeleteEntityCommand>(
                                                sceneState.registry, removed, entity, sceneState.selected));
                                        }
                                    }
                                    ClearEntitySelection(sceneState);
                                    viewport.dragEntity = entt::null;
                                }
                                if (IsBindingPressed(editorSettings.hkCopy))
                                {
                                    const std::vector<Entity> selectedEntities = SelectedEntities(sceneState);
                                    if (selectedEntities.size() > 1)
                                    {
                                        nlohmann::json group = {{"entities", nlohmann::json::array()}};
                                        for (Entity entity : selectedEntities)
                                        {
                                            group["entities"].push_back(CaptureEntity(sceneState.registry, entity));
                                        }
                                        SetEditorClipboard(kClipboardTypeGroup, group);
                                        viewport.status = "group copied as JSON text";
                                    }
                                    else if (!selectedEntities.empty())
                                    {
                                        SetEditorClipboard(
                                            kClipboardTypeEntity,
                                            CaptureEntity(sceneState.registry, selectedEntities.front()));
                                        viewport.status = "entity copied as JSON text";
                                    }
                                }
                                if (IsBindingPressed(editorSettings.hkDuplicate))
                                {
                                    const std::vector<Entity> selectedEntities = SelectedEntities(sceneState);
                                    std::vector<nlohmann::json> copies;
                                    for (Entity entity : selectedEntities)
                                    {
                                        nlohmann::json copy = CaptureEntity(sceneState.registry, entity);
                                        if (copy.contains("transform") && copy["transform"].is_object())
                                        {
                                            copy["transform"]["x"] =
                                                copy["transform"].value("x", 0.f) + 16.f;
                                            copy["transform"]["y"] =
                                                copy["transform"].value("y", 0.f) + 16.f;
                                        }
                                        copies.push_back(std::move(copy));
                                    }
                                    if (!copies.empty())
                                    {
                                        Fem::UndoStack::Instance().Push(std::make_shared<SpawnGroupCommand>(
                                            sceneState.registry, std::move(copies), sceneState.selected));
                                        viewport.status =
                                            "duplicated " + std::to_string(selectedEntities.size()) + " entit(y/ies)";
                                    }
                                }
                            }
                            if (IsBindingPressed(editorSettings.hkPaste))
                            {
                                std::string type;
                                nlohmann::json data;
                                if (GetEditorClipboard(type, data))
                                {
                                    std::vector<nlohmann::json> objects;
                                    if (type == kClipboardTypeEntity && data.is_object())
                                    {
                                        objects.push_back(data);
                                    }
                                    else if (type == kClipboardTypeGroup && data.contains("entities") &&
                                             data["entities"].is_array())
                                    {
                                        for (const nlohmann::json& entry : data["entities"])
                                        {
                                            if (entry.is_object())
                                            {
                                                objects.push_back(entry);
                                            }
                                        }
                                    }
                                    for (nlohmann::json& object : objects)
                                    {
                                        if (object.contains("transform") && object["transform"].is_object())
                                        {
                                            object["transform"]["x"] =
                                                object["transform"].value("x", 0.f) + 16.f;
                                            object["transform"]["y"] =
                                                object["transform"].value("y", 0.f) + 16.f;
                                        }
                                    }
                                    if (!objects.empty())
                                    {
                                        Fem::UndoStack::Instance().Push(std::make_shared<SpawnGroupCommand>(
                                            sceneState.registry, std::move(objects), sceneState.selected));
                                        viewport.status = "pasted " + type;
                                    }
                                }
                            }
                        }

                        if (hovered && ImGui::GetIO().MouseWheel != 0.f)
                        {
                            // Zoom around the cursor instead of the screen
                            // center: the point under the mouse stays fixed.
                            const ImVec2 mouse = ImGui::GetIO().MousePos;
                            const sf::Vector2f before =
                                viewport.camera.ScreenToWorld({mouse.x, mouse.y}, viewportRect);
                            viewport.camera.ZoomBy(std::pow(1.15f, ImGui::GetIO().MouseWheel));
                            const sf::Vector2f after =
                                viewport.camera.ScreenToWorld({mouse.x, mouse.y}, viewportRect);
                            viewport.camera.Move(before.x - after.x, before.y - after.y);
                        }
                        if (ImGui::IsItemActive() &&
                            (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right)))
                        {
                            const ImVec2 delta = ImGui::GetIO().MouseDelta;
                            const float zoom = std::max(0.01f, viewport.camera.Zoom());
                            viewport.camera.Move(-delta.x / zoom, -delta.y / zoom);
                        }
                        if (!gizmoStarted && !viewport.gizmoActive && hovered &&
                            ImGui::IsItemClicked(ImGuiMouseButton_Left))
                        {
                            const ImVec2 mouse = ImGui::GetIO().MousePos;
                            Entity best = entt::null;
                            float bestDistance = 32.f;
                            auto candidates = sceneState.registry.view<TransformComponent>();
                            for (const Entity entity : candidates)
                            {
                                const EntityTransform transform =
                                    WorldTransformOf(sceneState.registry, entity);
                                const sf::Vector2f screen =
                                    viewport.camera.WorldToScreen({transform.x, transform.y}, viewportRect);
                                const float distance = std::hypot(screen.x - mouse.x, screen.y - mouse.y);
                                if (distance < bestDistance)
                                {
                                    bestDistance = distance;
                                    best = entity;
                                }
                            }
                            if (best != entt::null)
                            {
                                sceneState.selected = best;
                                viewport.dragEntity = best;
                                const TransformComponent& transform =
                                    sceneState.registry.get<TransformComponent>(best);
                                viewport.dragStart = {transform.x, transform.y};
                                viewport.dragWorldStart =
                                    viewport.camera.ScreenToWorld({mouse.x, mouse.y}, viewportRect);
                                viewport.status = "selected entity #" +
                                                  std::to_string(entt::to_integral(best));
                            }
                        }
                        if (!viewport.gizmoActive && viewport.dragEntity != entt::null)
                        {
                            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                            {
                                if (TransformComponent* transform =
                                        sceneState.registry.try_get<TransformComponent>(viewport.dragEntity))
                                {
                                    const ImVec2 mouse = ImGui::GetIO().MousePos;
                                    const sf::Vector2f world =
                                        viewport.camera.ScreenToWorld({mouse.x, mouse.y}, viewportRect);
                                    const sf::Vector2f localDelta = WorldDeltaToLocal(
                                        sceneState.registry, viewport.dragEntity,
                                        {world.x - viewport.dragWorldStart.x,
                                         world.y - viewport.dragWorldStart.y});
                                    transform->x = viewport.dragStart.x + localDelta.x;
                                    transform->y = viewport.dragStart.y + localDelta.y;
                                }
                            }
                            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                            {
                                if (const TransformComponent* transform =
                                        sceneState.registry.try_get<TransformComponent>(viewport.dragEntity))
                                {
                                    const sf::Vector2f after{transform->x, transform->y};
                                    if (after != viewport.dragStart)
                                    {
                                        Fem::UndoStack::Instance().Push(std::make_shared<TransformMoveCommand>(
                                            sceneState.registry, viewport.dragEntity, viewport.dragStart, after));
                                    }
                                }
                                viewport.dragEntity = entt::null;
                            }
                        }
                    }
                }
                else if (name == "Class Defaults")
                {
                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputText("Defaults file", classDefaults.path, sizeof(classDefaults.path));
                    if (ImGui::Button("Load"))
                    {
                        classDefaults.status = Fem::ClassDefaults::Instance().Load(classDefaults.path)
                                                   ? "defaults loaded"
                                                   : "could not load defaults";
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Save"))
                    {
                        classDefaults.status = Fem::ClassDefaults::Instance().Save(classDefaults.path)
                                                   ? "defaults saved"
                                                   : "could not save defaults";
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear all"))
                    {
                        Fem::ClassDefaults::Instance().Clear();
                        classDefaults.status = "defaults cleared";
                    }

                    const auto& components = Fem::Mirror::Instance().Components();
                    std::vector<const char*> names;
                    names.reserve(components.size());
                    for (const auto& comp : components)
                    {
                        names.push_back(comp.name.c_str());
                    }
                    if (!names.empty())
                    {
                        classDefaults.componentIndex =
                            std::clamp(classDefaults.componentIndex, 0, static_cast<int>(names.size()) - 1);
                        ImGui::SetNextItemWidth(260.f);
                        ImGui::Combo("Component", &classDefaults.componentIndex, names.data(),
                                     static_cast<int>(names.size()));
                        const Fem::ComponentReflection& comp =
                            components[static_cast<std::size_t>(classDefaults.componentIndex)];

                        ImGui::InputTextWithHint("##defaults_filter", "Filter properties", classDefaults.filter,
                                                 sizeof(classDefaults.filter));
                        ImGui::SameLine();
                        ImGui::Checkbox("Advanced##defaults", &classDefaults.advanced);

                        nlohmann::json& override = Fem::ClassDefaults::Instance().For(comp.jsonKey);
                        ImGui::PushID(comp.jsonKey.c_str());
                        DrawComponentFields(sceneState.registry, entt::null, comp, &override,
                                            classDefaults.advanced, classDefaults.filter);
                        ImGui::PopID();

                        ImGui::Separator();
                        if (ImGui::Button("Reset this component"))
                        {
                            Fem::ClassDefaults::Instance().Remove(comp.jsonKey);
                            classDefaults.status = "component defaults reset";
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Apply to selected entity") &&
                            sceneState.registry.valid(sceneState.selected))
                        {
                            Fem::ClassDefaults::Instance().ApplyTo(sceneState.registry, sceneState.selected, comp);
                            classDefaults.status = "applied to selected entity";
                        }
                    }
                    ImGui::TextWrapped("%s", classDefaults.status.c_str());
                }
                else if (name == "Profiler")
                {
                    Fem::Profiler& profiler = Fem::Profiler::Instance();
                    bool enabled = profiler.Enabled();
                    if (ImGui::Checkbox("Record", &enabled))
                    {
                        profiler.SetEnabled(enabled);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reset"))
                    {
                        profiler.Reset();
                    }
                    ImGui::SameLine();
                    static int maxHistory = 300;
                    ImGui::SetNextItemWidth(140.f);
                    if (ImGui::SliderInt("History", &maxHistory, 30, 4096))
                    {
                        profiler.SetHistorySize(static_cast<std::size_t>(maxHistory));
                    }

                    const Fem::FrameProfile last = profiler.LastFrame();
                    ImGui::Text("Frame %llu   %.2f ms   %.1f FPS   %zu draw calls   %zu verts",
                                static_cast<unsigned long long>(last.frame), last.frameMs, profiler.AverageFps(120),
                                last.drawCalls, last.vertices);
                    ImGui::Text("pre %.2f   fixed %.2f   post %.2f   render %.2f ms", last.preUpdateMs,
                                last.fixedUpdateMs, last.postUpdateMs, last.renderMs);
                    ImGui::Text("entities %zu   systems %zu   textures %zu   commands %zu", last.entities, last.systems,
                                last.textureBinds, last.commands);

                    const std::vector<Fem::FrameProfile> history = profiler.History();
                    if (!history.empty())
                    {
                        std::vector<float> x(history.size());
                        std::vector<float> frameMs(history.size());
                        std::vector<float> renderMs(history.size());
                        std::vector<float> fixedMs(history.size());
                        for (std::size_t i = 0; i < history.size(); ++i)
                        {
                            x[i] = static_cast<float>(i);
                            frameMs[i] = static_cast<float>(history[i].frameMs);
                            renderMs[i] = static_cast<float>(history[i].renderMs);
                            fixedMs[i] = static_cast<float>(history[i].fixedUpdateMs);
                        }
#ifdef FEMBOY_HAS_IMPLOT
                        if (ImPlot::BeginPlot("##frame_times", ImVec2(-1, 220)))
                        {
                            ImPlot::SetupAxes("frame", "ms", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                            ImPlot::SetupLegend(ImPlotLocation_NorthWest);
                            ImPlot::PlotLine("frame", x.data(), frameMs.data(), static_cast<int>(x.size()));
                            ImPlot::PlotLine("render", x.data(), renderMs.data(), static_cast<int>(x.size()));
                            ImPlot::PlotLine("fixed", x.data(), fixedMs.data(), static_cast<int>(x.size()));
                            ImPlot::EndPlot();
                        }
#else
                        for (const Fem::FrameProfile& frame : history)
                        {
                            ImGui::Text("%.2f ms", frame.frameMs);
                        }
#endif
                    }

                    ImGui::Separator();
                    const std::vector<Fem::ProfileSample> average = profiler.AverageScopes(120);
                    if (ImGui::BeginTable("##profiler_systems", 2,
                                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                                          ImVec2(0, 220)))
                    {
                        ImGui::TableSetupColumn("System");
                        ImGui::TableSetupColumn("Average ms");
                        ImGui::TableHeadersRow();
                        std::size_t shown = 0;
                        for (const Fem::ProfileSample& sample : average)
                        {
                            if (sample.name == "PreUpdate" || sample.name == "FixedUpdate" ||
                                sample.name == "PostUpdate" || sample.name == "Render")
                            {
                                continue;
                            }
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(sample.name.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%.3f", sample.milliseconds);
                            if (++shown >= 64)
                            {
                                break;
                            }
                        }
                        if (shown == 0)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextDisabled("(no system samples)");
                        }
                        ImGui::EndTable();
                    }
                    if (ImGui::Button("Copy JSON report"))
                    {
                        ImGui::SetClipboardText(profiler.ToJson(300).dump(2).c_str());
                    }
                }
                else if (name == "Voice Chat")
                {
                    Fem::VoiceChat& voice = Fem::VoiceChat::Instance();
                    static std::string codecStatus;
                    static std::string captureStatus;

                    if (!voice.Available())
                    {
                        ImGui::TextDisabled("Voice support is not initialized.");
                        if (ImGui::Button("Initialize voice"))
                        {
                            voice.Initialize();
                        }
                    }
                    else
                    {
                        if (ImGui::Button("Refresh devices"))
                        {
                            voice.RefreshDevices();
                        }
                        const std::vector<std::string> devices = voice.InputDevices();
                        ImGui::SameLine();
                        ImGui::TextDisabled("Opus 48 kHz mono, %zu capture device(s)", devices.size());
                    }

                    bool enabled = voice.Enabled();
                    if (ImGui::Checkbox("Enabled (opens the microphone)", &enabled))
                    {
                        voice.SetEnabled(enabled);
                    }

                    bool muted = voice.Muted();
                    if (ImGui::Checkbox("Mute microphone", &muted))
                    {
                        voice.SetMuted(muted);
                    }

                    bool pushToTalk = voice.PushToTalk();
                    if (ImGui::Checkbox("Push-to-talk", &pushToTalk))
                    {
                        voice.SetPushToTalk(pushToTalk);
                    }

                    bool voiceActivation = voice.VoiceActivation();
                    if (ImGui::Checkbox("Voice activation (gate by input level)", &voiceActivation))
                    {
                        voice.SetVoiceActivation(voiceActivation);
                    }

                    float threshold = voice.VoiceActivationThreshold();
                    if (ImGui::SliderFloat("Activation threshold", &threshold, 0.f, 1.f, "%.3f"))
                    {
                        voice.SetVoiceActivationThreshold(threshold);
                    }

                    float gain = voice.InputGain();
                    if (ImGui::SliderFloat("Input gain", &gain, 0.f, 4.f, "%.2fx"))
                    {
                        voice.SetInputGain(gain);
                    }

                    float output = voice.OutputVolume();
                    if (ImGui::SliderFloat("Output volume", &output, 0.f, 1.f, "%.2f"))
                    {
                        voice.SetOutputVolume(output);
                    }

                    // The persisted setting is a device name, not an index: the
                    // index changes when a headset is plugged in.
                    std::vector<std::string> deviceNames = voice.InputDevices();
                    std::vector<std::string> deviceLabels{"System default"};
                    deviceLabels.insert(deviceLabels.end(), deviceNames.begin(), deviceNames.end());
                    int deviceIndex = 0;
                    const std::string currentDevice = voice.InputDevice();
                    if (!currentDevice.empty())
                    {
                        bool found = false;
                        for (std::size_t i = 0; i < deviceNames.size(); ++i)
                        {
                            if (deviceNames[i] == currentDevice)
                            {
                                deviceIndex = static_cast<int>(i) + 1;
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            deviceNames.push_back(currentDevice);
                            deviceLabels.push_back(currentDevice + "  (unavailable)");
                            deviceIndex = static_cast<int>(deviceLabels.size()) - 1;
                        }
                    }
                    std::vector<const char*> deviceText;
                    deviceText.reserve(deviceLabels.size());
                    for (const std::string& label : deviceLabels)
                    {
                        deviceText.push_back(label.c_str());
                    }
                    if (ImGui::Combo("Input device", &deviceIndex, deviceText.data(),
                                     static_cast<int>(deviceText.size())))
                    {
                        voice.SetInputDevice(deviceIndex == 0 ? std::string{}
                                                              : deviceNames[static_cast<std::size_t>(deviceIndex - 1)]);
                    }

                    if (ImGui::Button("Test Opus codec"))
                    {
                        std::string error;
                        codecStatus =
                            Fem::VoiceChat::CodecSelfTest(&error) ? "encode/decode OK" : "failed: " + error;
                    }
                    if (!codecStatus.empty())
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", codecStatus.c_str());
                    }

                    if (ImGui::Button("Run 2 s capture test"))
                    {
                        const Fem::VoiceChat::CaptureTestResult test = voice.CaptureSelfTest(2.f);
                        captureStatus = test.passed ? "capture + encode OK, peak " +
                                                          std::to_string(static_cast<int>(test.peak * 100.f)) + "%"
                                                    : "capture failed: " + test.error;
                    }
                    if (!captureStatus.empty())
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", captureStatus.c_str());
                    }

                    // The editor does not run Engine::Run(), so it pumps the
                    // capture ring itself. There is no Network here: incoming
                    // packets are a game concern.
                    voice.Update(ImGui::GetIO().DeltaTime, nullptr);

                    const Fem::VoiceChat::Stats stats = voice.GetStats();
                    ImGui::Separator();
                    ImGui::Text("Available: %s   Enabled: %s   Capturing: %s   Transmitting: %s",
                                stats.available ? "yes" : "no", stats.enabled ? "yes" : "no",
                                stats.capturing ? "yes" : "no", stats.transmitting ? "yes" : "no");
                    ImGui::Text("Input level: %.0f%%   Jitter: %.1f ms   Peers: %zu", stats.inputLevel * 100.f,
                                stats.jitterMs, stats.peers);
                    ImGui::Text("Capturing: %s   Device: %s   Devices: %zu", stats.capturing ? "yes" : "no",
                                stats.activeInputDevice.empty() ? "(none)" : stats.activeInputDevice.c_str(),
                                stats.inputDevices);
                    ImGui::Text("Sent: %llu   Received: %llu   Lost: %llu   Dropped: %llu",
                                static_cast<unsigned long long>(stats.packetsSent),
                                static_cast<unsigned long long>(stats.packetsReceived),
                                static_cast<unsigned long long>(stats.packetsLost),
                                static_cast<unsigned long long>(stats.packetsDropped));
                    if (!stats.lastError.empty())
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.65f, 0.35f, 1.f), "%s", stats.lastError.c_str());
                    }
                }
                else if (name == "Asset Pipeline")
                {
                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputText("Manifest", assets.manifest, sizeof(assets.manifest));
                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputText("Output directory", assets.output, sizeof(assets.output));

                    if (ImGui::Button("Load & validate"))
                    {
                        std::vector<std::string> errors;
                        const bool parsed = assets.database.Load(assets.manifest);
                        const bool valid = parsed && assets.database.Validate(&errors);
                        assets.stale = assets.database.Stale(assets.output);
                        assets.status = valid ? "valid: " + std::to_string(assets.database.Assets().size()) +
                                                    " asset(s), " + std::to_string(assets.stale.size()) + " stale"
                                              : "invalid: " + std::to_string(errors.size()) + " error(s)";
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cook"))
                    {
                        Fem::CookOptions options;
                        options.outputDir = assets.output;
                        options.createPack = true;
                        options.verbose = true;
                        const Fem::CookResult cooked = Fem::AssetCooker::Cook(assets.database, options);
                        assets.stale = assets.database.Stale(assets.output);
                        assets.status = cooked.ok
                                            ? "cooked " + std::to_string(cooked.assets) + " asset(s), " +
                                                  std::to_string(cooked.atlases) + " atlas page(s), " +
                                                  std::to_string(cooked.bytes) + " bytes"
                                            : "cook failed: " + std::to_string(cooked.errors.size()) + " error(s)";
                        for (const std::string& error : cooked.errors)
                        {
                            assets.status += "\n" + error;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear"))
                    {
                        assets.database.Clear();
                        assets.stale.clear();
                        assets.status = "cleared";
                    }

                    ImGui::TextWrapped("%s", assets.status.c_str());
                    if (!assets.database.Assets().empty())
                    {
                        ImGui::Separator();
                        if (ImGui::BeginTable("asset_table", 4,
                                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                  ImGuiTableFlags_ScrollY))
                        {
                            ImGui::TableSetupColumn("Id");
                            ImGui::TableSetupColumn("Kind");
                            ImGui::TableSetupColumn("Atlas");
                            ImGui::TableSetupColumn("Stale");
                            ImGui::TableHeadersRow();
                            for (const auto& [id, settings] : assets.database.Assets())
                            {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextUnformatted(id.c_str());
                                ImGui::TableSetColumnIndex(1);
                                ImGui::TextUnformatted(Fem::AssetKindName(settings.kind));
                                ImGui::TableSetColumnIndex(2);
                                ImGui::TextUnformatted(settings.atlas.c_str());
                                ImGui::TableSetColumnIndex(3);
                                const bool stale =
                                    std::find(assets.stale.begin(), assets.stale.end(), id) != assets.stale.end();
                                ImGui::TextUnformatted(stale ? "yes" : "no");
                            }
                            ImGui::EndTable();
                        }
                    }
                }
                else if (name == "Asset Browser")
                {
                    std::error_code rootError;
                    const std::filesystem::path root =
                        project.path[0] != '\0' && std::filesystem::exists(project.path, rootError)
                            ? std::filesystem::path(project.path)
                            : std::filesystem::current_path(rootError);
                    if (assetBrowser.root != root)
                    {
                        assetBrowser.root = root;
                        assetBrowser.current = root / "assets";
                        if (!std::filesystem::exists(assetBrowser.current, rootError))
                        {
                            assetBrowser.current = root;
                        }
                        assetBrowser.back.clear();
                        assetBrowser.forward.clear();
                        std::snprintf(assetBrowser.pathBuffer, sizeof(assetBrowser.pathBuffer), "%s",
                                      assetBrowser.current.string().c_str());
                        RefreshAssetBrowser(assetBrowser);
                    }
                    if (assetBrowser.current.empty() ||
                        !std::filesystem::is_directory(assetBrowser.current, rootError))
                    {
                        assetBrowser.current = root;
                        std::snprintf(assetBrowser.pathBuffer, sizeof(assetBrowser.pathBuffer), "%s",
                                      assetBrowser.current.string().c_str());
                        RefreshAssetBrowser(assetBrowser);
                    }

                    const auto reload = [&]()
                    {
                        std::snprintf(assetBrowser.pathBuffer, sizeof(assetBrowser.pathBuffer), "%s",
                                      assetBrowser.current.string().c_str());
                        RefreshAssetBrowser(assetBrowser);
                    };
                    const auto openEntry = [&](const AssetEntry& entry)
                    {
                        if (entry.directory)
                        {
                            NavigateAssetBrowser(assetBrowser, entry.path);
                            return;
                        }
                        const std::string path = entry.path.string();
                        if (EndsWith(path, ".scene"))
                        {
                            if (sceneState.scene.Load(path))
                            {
                                sceneState.scene.ApplyTo(sceneState.registry);
                                ClearEntitySelection(sceneState);
                                std::snprintf(sceneState.scenePath, sizeof(sceneState.scenePath), "%s",
                                              path.c_str());
                                LoadGroupNames(sceneState);
                                sceneState.status = "scene loaded: " + entry.name;
                            }
                        }
                        else if (entry.name.find(".prefab") != std::string::npos)
                        {
                            Fem::Prefab prefab;
                            if (prefab.Load(path))
                            {
                                prefab.Instantiate(sceneState.registry);
                                sceneState.status = "prefab instanced: " + entry.name;
                            }
                        }
                        else if (EndsWith(path, ".vsgraph") || EndsWith(path, ".femboygraph") ||
                                 EndsWith(path, ".vsjson"))
                        {
                            VisualScriptDocument& document = visualScripts.OpenDocument(path);
                            activeTopTab = FindDocumentIndex(visualScripts, &document);
                        }
                        else if (EndsWith(path, ".femboy") || EndsWith(path, ".json") ||
                                 EndsWith(path, ".toml"))
                        {
                            std::ifstream file(path);
                            std::string source((std::istreambuf_iterator<char>(file)),
                                               std::istreambuf_iterator<char>());
                            std::snprintf(console.input, sizeof(console.input), "%s", source.c_str());
                            for (Panel& panel : panels)
                            {
                                if (std::strcmp(panel.name, "FemboyScript Console") == 0)
                                {
                                    panel.open = true;
                                    focusPanel = panel.name;
                                    break;
                                }
                            }
                        }
                    };

                    if (ImGui::Button("<"))
                    {
                        if (!assetBrowser.back.empty())
                        {
                            assetBrowser.forward.push_back(assetBrowser.current);
                            assetBrowser.current = assetBrowser.back.back();
                            assetBrowser.back.pop_back();
                            reload();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(">"))
                    {
                        if (!assetBrowser.forward.empty())
                        {
                            assetBrowser.back.push_back(assetBrowser.current);
                            assetBrowser.current = assetBrowser.forward.back();
                            assetBrowser.forward.pop_back();
                            reload();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Up"))
                    {
                        const std::filesystem::path parent = assetBrowser.current.parent_path();
                        NavigateAssetBrowser(assetBrowser, parent);
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-260.f);
                    ImGui::InputText("##asset_path", assetBrowser.pathBuffer, sizeof(assetBrowser.pathBuffer));
                    const bool pathEntered = ImGui::IsItemDeactivatedAfterEdit() ||
                                             (ImGui::IsItemFocused() &&
                                              ImGui::IsKeyPressed(ImGuiKey_Enter, false));
                    ImGui::SameLine();
                    if (ImGui::Button("Go") || pathEntered)
                    {
                        NavigateAssetBrowser(assetBrowser, assetBrowser.pathBuffer, false);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Refresh"))
                    {
                        RefreshAssetBrowser(assetBrowser);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("New"))
                    {
                        ImGui::OpenPopup("##asset_create");
                    }

                    ImGui::TextDisabled("Project root: %s", assetBrowser.root.string().c_str());
                    ImGui::Separator();

                    if (ImGui::BeginPopup("##asset_create"))
                    {
                        ImGui::Text("Create asset");
                        ImGui::SetNextItemWidth(320.f);
                        ImGui::InputTextWithHint("##asset_name", "Name", assetBrowser.createName,
                                                 sizeof(assetBrowser.createName));
                        const char* types[] = {"Folder",          "Scene (.scene)",  "Prefab (.prefab.json)",
                                               "Visual Script",  "FemboyScript",    "JSON data",
                                               "C++ class",       "C++ struct"};
                        ImGui::SetNextItemWidth(320.f);
                        ImGui::Combo("Type", &assetBrowser.createType, types, 8);
                        if (ImGui::Button("Create"))
                        {
                            const std::string name = assetBrowser.createName;
                            const std::filesystem::path base = assetBrowser.current / name;
                            std::error_code ec;
                            switch (assetBrowser.createType)
                            {
                            case 0:
                                std::filesystem::create_directories(base, ec);
                                RefreshAssetBrowser(assetBrowser);
                                break;
                            case 1:
                            {
                                Fem::Registry empty;
                                Fem::Scene scene;
                                scene.CaptureFrom(empty);
                                scene.Save(base.string() + ".scene");
                                RefreshAssetBrowser(assetBrowser);
                                break;
                            }
                            case 2:
                            {
                                nlohmann::json prefab = {{"name", name}};
                                (void)Fem::Vanity::WriteJson(base.string() + ".prefab.json", prefab);
                                RefreshAssetBrowser(assetBrowser);
                                break;
                            }
                            case 3:
                            {
                                Fem::VisualScriptGraph graph;
                                graph.SaveToFile(base.string() + ".vsgraph");
                                RefreshAssetBrowser(assetBrowser);
                                break;
                            }
                            case 4:
                                (void)Fem::Vanity::WriteText(base.string() + ".femboy",
                                                             "void OnStart()\n{\n    print(\"hello\");\n}\n");
                                RefreshAssetBrowser(assetBrowser);
                                break;
                            case 5:
                                (void)Fem::Vanity::WriteText(base.string() + ".json", "{}\n");
                                RefreshAssetBrowser(assetBrowser);
                                break;
                            case 6:
                            case 7:
                            {
                                const std::string keyword = assetBrowser.createType == 6 ? "class" : "struct";
                                const std::string header =
                                    "#pragma once\n\n" + std::string(keyword) + " " + name + "\n{\n"
                                    "public:\n    " + name + "() = default;\n};\n";
                                (void)Fem::Vanity::WriteText(base.string() + ".hpp", header);
                                if (assetBrowser.createType == 6)
                                {
                                    (void)Fem::Vanity::WriteText(base.string() + ".cpp",
                                                                 "#include \"" + name + ".hpp\"\n");
                                }
                                RefreshAssetBrowser(assetBrowser);
                                break;
                            }
                            default:
                                break;
                            }
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel"))
                        {
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    if (ImGui::GetTime() - assetBrowser.lastRefresh > 2.0)
                    {
                        RefreshAssetBrowser(assetBrowser);
                    }
                    if (ImGui::BeginChild("##asset_entries", ImVec2(-320.f, 0), true))
                    {
                        for (const AssetEntry& entry : assetBrowser.entries)
                        {
                            ImGui::PushID(entry.path.string().c_str());
                            ImGui::PushStyleColor(ImGuiCol_Text, AssetIconColor(entry));
                            const std::string label = std::string(AssetIcon(entry)) + "  " + entry.name;
                            if (ImGui::Selectable(label.c_str(), assetBrowser.selectedPath == entry.path,
                                                  ImGuiSelectableFlags_AllowDoubleClick))
                            {
                                assetBrowser.selectedPath = entry.path;
                                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                                {
                                    openEntry(entry);
                                }
                            }
                            ImGui::PopStyleColor();
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                            {
                                const std::string path = entry.path.string();
                                ImGui::SetDragDropPayload("FEMBOY_ASSET", path.c_str(), path.size() + 1);
                                ImGui::TextUnformatted(entry.name.c_str());
                                ImGui::EndDragDropSource();
                            }
                            EditorTooltip("%s", entry.path.string().c_str());
                            if (ImGui::BeginPopupContextItem())
                            {
                                if (ImGui::MenuItem(entry.directory ? "Open folder" : "Open"))
                                {
                                    openEntry(entry);
                                }
                                if (ImGui::MenuItem("Copy path"))
                                {
                                    ImGui::SetClipboardText(entry.path.string().c_str());
                                }
                                ImGui::Separator();
                                if (ImGui::MenuItem(entry.directory ? "Delete folder" : "Delete file"))
                                {
                                    assetBrowser.deleteQueue.push_back(entry.path);
                                }
                                ImGui::EndPopup();
                            }
                            ImGui::PopID();
                        }
                        if (assetBrowser.entries.empty())
                        {
                            ImGui::TextDisabled("(empty folder)");
                        }
                    }
                    ImGui::EndChild();

                    ImGui::SameLine();
                    if (ImGui::BeginChild("##asset_preview", ImVec2(0, 0), true))
                    {
                        if (assetBrowser.selectedPath.empty())
                        {
                            ImGui::TextDisabled("Select an asset to preview it.");
                        }
                        else
                        {
                            const std::filesystem::path& path = assetBrowser.selectedPath;
                            AssetEntry previewEntry;
                            previewEntry.path = path;
                            previewEntry.name = path.filename().string();
                            previewEntry.directory = std::filesystem::is_directory(path);
                            ImGui::PushStyleColor(ImGuiCol_Text, AssetIconColor(previewEntry));
                            ImGui::Text("%s  %s", AssetIcon(previewEntry), previewEntry.name.c_str());
                            ImGui::PopStyleColor();
                            ImGui::TextDisabled("%s", path.string().c_str());
                            std::error_code ec;
                            if (!previewEntry.directory && std::filesystem::exists(path, ec))
                            {
                                ImGui::TextDisabled("%llu bytes", static_cast<unsigned long long>(
                                                                      std::filesystem::file_size(path, ec)));
                            }
                            ImGui::Separator();

                            const std::string extension = path.extension().string();
                            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                                extension == ".bmp")
                            {
                                if (assetBrowser.previewLoadedPath != path)
                                {
                                    assetBrowser.previewLoadedPath = path;
                                    (void)assetBrowser.previewTexture.loadFromFile(path.string());
                                }
                                const sf::Vector2u size = assetBrowser.previewTexture.getSize();
                                if (size.x > 0 && size.y > 0)
                                {
                                    const float scale = std::min(1.f, 260.f / static_cast<float>(
                                                                             std::max(size.x, size.y)));
                                    ImGui::Image(assetBrowser.previewTexture,
                                                 sf::Vector2f(static_cast<float>(size.x) * scale,
                                                              static_cast<float>(size.y) * scale));
                                }
                            }
                            else if (!previewEntry.directory)
                            {
                                std::ifstream file(path, std::ios::binary);
                                std::string text((std::istreambuf_iterator<char>(file)),
                                                 std::istreambuf_iterator<char>());
                                if (extension == ".scene" || extension == ".vsgraph" ||
                                    extension == ".femboygraph" || extension == ".vsjson" ||
                                    path.filename().string().find(".prefab") != std::string::npos)
                                {
                                    try
                                    {
                                        const nlohmann::json data = nlohmann::json::parse(text);
                                        if (extension == ".scene")
                                        {
                                            ImGui::Text("Entities: %zu",
                                                        data.contains("entities") && data["entities"].is_array()
                                                            ? data["entities"].size()
                                                            : 0u);
                                        }
                                        if (data.contains("nodes") && data["nodes"].is_array())
                                        {
                                            ImGui::Text("Nodes: %zu", data["nodes"].size());
                                        }
                                        if (data.contains("connections") &&
                                            data["connections"].is_array())
                                        {
                                            ImGui::Text("Connections: %zu",
                                                        data["connections"].size());
                                        }
                                        if (path.filename().string().find(".prefab") != std::string::npos)
                                        {
                                            ImGui::Text("Prefab keys: %zu", data.size());
                                        }
                                    }
                                    catch (const std::exception&)
                                    {
                                        ImGui::TextDisabled("(invalid JSON)");
                                    }
                                }
                                const std::size_t shown = std::min<std::size_t>(text.size(), 1800);
                                ImGui::TextWrapped("%.*s", static_cast<int>(shown), text.c_str());
                            }
                        }
                    }
                    ImGui::EndChild();

                    for (const std::filesystem::path& path : assetBrowser.deleteQueue)
                    {
                        std::error_code ec;
                        std::filesystem::remove_all(path, ec);
                    }
                    if (!assetBrowser.deleteQueue.empty())
                    {
                        assetBrowser.deleteQueue.clear();
                        RefreshAssetBrowser(assetBrowser);
                    }
                }
            }
            ImGui::End();
        }
        } // activeTopTab == kTopTabEditor

        // The selected graph document occupies the whole content area. There is
        // only one document page visible at a time; the top strip is the tab
        // switcher, so no second row of document tabs is needed.
        if (activeTopTab >= 0)
        {
            VisualScriptDocument& visualScript =
                *visualScripts.documents[static_cast<std::size_t>(activeTopTab)];
            ImGui::SetNextWindowPos(contentPos);
            ImGui::SetNextWindowSize(contentSize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
            const ImGuiWindowFlags documentFlags =
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;
            if (ImGui::Begin(visualScript.WindowTitle().c_str(), &visualScript.open, documentFlags))
            {
                ImGui::SetNextItemWidth(-1.f);
                ImGui::InputText("Graph file", visualScript.path, sizeof(visualScript.path));
                if (ImGui::Button("Save"))
                {
                    visualScript.status =
                        visualScript.path[0] != '\0' && visualScript.graph.SaveToFile(visualScript.path)
                            ? "saved"
                            : "save failed";
                }
                ImGui::SameLine();
                if (ImGui::Button("Compile"))
                {
                    visualScript.Compile();
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Graph"))
                {
                    visualScript.graph.Clear();
                    visualScript.selected = 0;
                    visualScript.lastSelected = -1;
                    visualScript.source[0] = '\0';
                    visualScript.status = "graph cleared";
                }
                ImGui::SameLine();
                if (ImGui::Button("Copy Graph Text"))
                {
                    SetEditorClipboard(kClipboardTypeVisualScript,
                                       {{"name", visualScript.Title()}, {"graph", visualScript.graph.ToJson()}});
                    visualScript.status = "graph copied as JSON text";
                }
                ImGui::SameLine();
                if (ImGui::Button("Paste Graph Text"))
                {
                    std::string type;
                    nlohmann::json data;
                    if (GetEditorClipboard(type, data) && type == kClipboardTypeVisualScript &&
                        data.contains("graph"))
                    {
                        VisualScriptDocument& pasted =
                            visualScripts.PasteDocument(data["graph"], data.value("name", ""));
                        activeTopTab = FindDocumentIndex(visualScripts, &pasted);
                    }
                    else
                    {
                        visualScript.status = "clipboard does not contain a visual script";
                    }
                }

                if (visualScript.status.rfind("error", 0) == 0 || visualScript.status == "could not open graph" ||
                    visualScript.status == "save failed")
                {
                    ImGui::TextColored(ImVec4(1.f, 0.45f, 0.45f, 1.f), "%s", visualScript.status.c_str());
                }
                else
                {
                    ImGui::TextDisabled("%s", visualScript.status.c_str());
                }

                ImGui::Text("Nodes: %zu   Connections: %zu", visualScript.graph.Nodes().size(),
                            visualScript.graph.Connections().size());
                DrawVisualScriptCanvas(visualScript);

                if (ImGui::BeginChild("##vs_side", ImVec2(0.f, 250.f), true))
                {
                    if (ImGui::BeginTabBar("##vs_side_tabs"))
                    {
                        if (ImGui::BeginTabItem("Palette"))
                        {
                            ImGui::InputTextWithHint("##vs_palette_filter", "Search nodes...",
                                                     visualScript.paletteFilter, sizeof(visualScript.paletteFilter));
                            const std::vector<Fem::VSNodeType>& types = Fem::VsNodeTypes();
                            const std::vector<Fem::VSReflectionNode>& reflection = Fem::VsReflectionCatalog();
                            std::vector<std::string> labels;
                            labels.reserve(types.size() + reflection.size());
                            for (Fem::VSNodeType type : types)
                            {
                                labels.emplace_back(Fem::VsNodeTypeName(type));
                            }
                            for (const Fem::VSReflectionNode& node : reflection)
                            {
                                labels.push_back(node.label);
                            }
                            std::string filter = visualScript.paletteFilter;
                            std::transform(filter.begin(), filter.end(), filter.begin(),
                                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            for (std::size_t i = 0; i < labels.size(); ++i)
                            {
                                std::string haystack = labels[i];
                                std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                if (!filter.empty() && haystack.find(filter) == std::string::npos)
                                {
                                    continue;
                                }
                                if (ImGui::Selectable((labels[i] + "##palette" + std::to_string(i)).c_str()))
                                {
                                    const int offset = 80 + static_cast<int>(visualScript.graph.Nodes().size()) * 24;
                                    AddNodeFromCatalog(visualScript, visualScript.graph, static_cast<int>(i),
                                                       static_cast<float>(offset), static_cast<float>(offset));
                                    visualScript.lastSelected = -1;
                                }
                            }
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Node"))
                        {
                            if (Fem::VSNode* node = visualScript.graph.Node(visualScript.selected))
                            {
                                ImGui::Text("Node %d  [%s]", node->id, Fem::VsNodeTypeName(node->type));
                                char labelBuffer[128] = "";
                                std::snprintf(labelBuffer, sizeof(labelBuffer), "%s", node->label.c_str());
                                if (ImGui::InputText("Label", labelBuffer, sizeof(labelBuffer)))
                                {
                                    node->label = labelBuffer;
                                }
                                for (auto& [key, value] : node->params)
                                {
                                    ImGui::PushID(key.c_str());
                                    if (value.is_boolean())
                                    {
                                        bool v = value.get<bool>();
                                        if (ImGui::Checkbox(key.c_str(), &v))
                                        {
                                            value = v;
                                        }
                                    }
                                    else if (value.is_number())
                                    {
                                        float v = value.get<float>();
                                        if (ImGui::DragFloat(key.c_str(), &v, 0.05f))
                                        {
                                            value = v;
                                        }
                                    }
                                    else if (value.is_string())
                                    {
                                        char buffer[512] = "";
                                        std::snprintf(buffer, sizeof(buffer), "%s", value.get<std::string>().c_str());
                                        if (ImGui::InputText(key.c_str(), buffer, sizeof(buffer)))
                                        {
                                            value = std::string(buffer);
                                        }
                                    }
                                    else
                                    {
                                        ImGui::TextWrapped("%s = %s", key.c_str(), value.dump().c_str());
                                    }
                                    ImGui::PopID();
                                }
                                if (ImGui::Button("Evaluate selected"))
                                {
                                    const nlohmann::json result = visualScript.graph.Evaluate(node->id);
                                    const std::string text = result.dump(2);
                                    std::snprintf(visualScript.debugValue, sizeof(visualScript.debugValue), "%s",
                                                  text.c_str());
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Remove node"))
                                {
                                    visualScript.graph.RemoveNode(node->id);
                                    visualScript.selected = 0;
                                    visualScript.lastSelected = -1;
                                }
                            }
                            else
                            {
                                ImGui::TextDisabled("Select a node on the canvas.");
                            }
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Debug"))
                        {
                            ImGui::InputTextMultiline("##vs_debug", visualScript.debugValue,
                                                      sizeof(visualScript.debugValue), ImVec2(-1.f, -1.f),
                                                      ImGuiInputTextFlags_ReadOnly);
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                }
                ImGui::EndChild();

                ImGui::InputTextMultiline("Generated FemboyScript", visualScript.source, sizeof(visualScript.source),
                                          ImVec2(-1, 240.f), ImGuiInputTextFlags_ReadOnly);
            }
            ImGui::End();
            ImGui::PopStyleVar();
        }

        // Startup project browser. It is a full-window overlay rather than a
        // docked panel, so it cannot be lost in a custom layout.
        if (wizard.visible)
        {
            ImGui::SetNextWindowPos(mainViewport->WorkPos);
            ImGui::SetNextWindowSize(mainViewport->WorkSize);
            if (wizard.focus)
            {
                ImGui::SetNextWindowFocus();
                wizard.focus = false;
            }
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.075f, 1.f));
            const ImGuiWindowFlags wizardFlags =
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
            if (ImGui::Begin("###ProjectBrowser", nullptr, wizardFlags))
            {
                ImGui::SetCursorPos(ImVec2(28.f, 24.f));
                ImGui::SetWindowFontScale(1.45f);
                ImGui::Text("FemboyEngine");
                ImGui::SetWindowFontScale(1.f);
                ImGui::SameLine();
                ImGui::TextDisabled("v%s", FEMBOY_ENGINE_VERSION);
                ImGui::SameLine();
                ImGui::TextDisabled("Project Browser");
                ImGui::Separator();

                const ImVec2 available = ImGui::GetContentRegionAvail();
                const float leftWidth = std::max(360.f, available.x * 0.55f);
                if (ImGui::BeginChild("##wizard_create", ImVec2(leftWidth, 0.f), ImGuiChildFlags_Borders))
                {
                    ImGui::Text("Create a new project");
                    ImGui::TextDisabled("Everything is written into the chosen folder.");
                    ImGui::Separator();

                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputTextWithHint("##wizard_name", "Project name", wizard.name, sizeof(wizard.name));
                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputTextWithHint("##wizard_location", "Location (parent folder)", wizard.location,
                                             sizeof(wizard.location));

                    const char* views[] = {"Top-Down (2D)", "Side View (2D)", "Isometric"};
                    ImGui::SetNextItemWidth(260.f);
                    if (ImGui::Combo("Project type", &wizard.view, views, 3) && wizard.view == 1)
                    {
                        wizard.grid = 0;
                    }
                    ImGui::TextDisabled("%s", ProjectWizardState::ViewHint(wizard.view));
                    ImGui::BeginDisabled(wizard.view == 1);
                    const char* grids[] = {"Square grid", "Hex grid"};
                    ImGui::SetNextItemWidth(260.f);
                    ImGui::Combo("Grid", &wizard.grid, grids, 2);
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("Hex is available for 2D and isometric projects, not side view.");

                    const std::string folder = Fem::ProjectGenerator::SanitizeName(wizard.name);
                    ImGui::TextDisabled("Project folder: %s%c%s", wizard.location,
                                        wizard.location[0] ? std::filesystem::path::preferred_separator : ' ',
                                        folder.c_str());
                    ImGui::Spacing();
                    if (ImGui::Button("Create and Open", ImVec2(180.f, 34.f)))
                    {
                        std::error_code ec;
                        const std::filesystem::path location =
                            std::filesystem::absolute(wizard.location, ec).lexically_normal();
                        if (ec || location.empty())
                        {
                            wizard.status = "The location is not a valid path.";
                        }
                        else
                        {
                            const std::filesystem::path actual = location / folder;
                            if (!Fem::ProjectGenerator::Generate(folder, location.string()))
                            {
                                wizard.status = "Could not create the project folder.";
                            }
                            else
                            {
                                ProjectSettingsState created;
                                created.Load(actual.string());
                                SetPathBuffer(created.name, sizeof(created.name), folder);
                                SetPathBuffer(created.title, sizeof(created.title), wizard.name);
                                created.view = wizard.view;
                                created.grid = wizard.view == 1 ? 0 : wizard.grid;
                                created.Save();
                                ApplyProjectPaths(actual.string(), project, sceneState, classDefaults, assets, git,
                                                  projectSettings, viewport);
                                RememberProject(editorSettings, actual);
                                activeTopTab = kTopTabEditor;
                                if (project.recent.empty() || project.recent.back() != actual.string())
                                {
                                    project.recent.push_back(actual.string());
                                }
                                project.status = "Created project: " + actual.string();
                                wizard.status = project.status;
                                wizard.visible = false;
                            }
                        }
                    }
                    if (!wizard.status.empty())
                    {
                        ImGui::TextWrapped("%s", wizard.status.c_str());
                    }
                }
                ImGui::EndChild();

                ImGui::SameLine();
                if (ImGui::BeginChild("##wizard_recent", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders))
                {
                    ImGui::Text("Open an existing project");
                    ImGui::TextDisabled("Recent projects and the current engine Projects folder.");
                    ImGui::Separator();

                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputTextWithHint("##wizard_open", "Path to a project folder...", wizard.openPath,
                                             sizeof(wizard.openPath));
                    if (ImGui::Button("Open Project", ImVec2(160.f, 30.f)) && wizard.openPath[0] != '\0')
                    {
                        std::error_code ec;
                        const std::filesystem::path target =
                            std::filesystem::absolute(wizard.openPath, ec).lexically_normal();
                        if (!ec && std::filesystem::exists(target / "project.femboy", ec))
                        {
                            ApplyProjectPaths(target.string(), project, sceneState, classDefaults, assets, git,
                                              projectSettings, viewport);
                            RememberProject(editorSettings, target);
                            activeTopTab = kTopTabEditor;
                            if (project.recent.empty() || project.recent.back() != target.string())
                            {
                                project.recent.push_back(target.string());
                            }
                            project.status = "Opened project: " + target.string();
                            wizard.status = project.status;
                            wizard.visible = false;
                        }
                        else
                        {
                            wizard.status = "No project.femboy found in that folder.";
                        }
                    }
                    if (project.path[0] != '\0')
                    {
                        ImGui::SameLine();
                        if (ImGui::Button("Continue with current", ImVec2(0.f, 30.f)))
                        {
                            wizard.visible = false;
                        }
                    }

                    ImGui::Separator();
                    if (project.recent.empty())
                    {
                        ImGui::TextDisabled("No recent projects yet.");
                    }
                    int openRecent = -1;
                    for (std::size_t i = 0; i < project.recent.size(); ++i)
                    {
                        const std::string& recent = project.recent[i];
                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::Selectable(recent.c_str(), project.path == recent))
                        {
                            std::snprintf(wizard.openPath, sizeof(wizard.openPath), "%s", recent.c_str());
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            {
                                openRecent = static_cast<int>(i);
                            }
                        }
                        if (ImGui::IsItemHovered())
                        {
                            EditorTooltip("Double-click to open.");
                        }
                        ImGui::PopID();
                    }
                    if (openRecent >= 0)
                    {
                        std::error_code ec;
                        const std::filesystem::path target = project.recent[static_cast<std::size_t>(openRecent)];
                        if (std::filesystem::exists(target / "project.femboy", ec))
                        {
                            ApplyProjectPaths(target.string(), project, sceneState, classDefaults, assets, git,
                                              projectSettings, viewport);
                            RememberProject(editorSettings, target);
                            activeTopTab = kTopTabEditor;
                            project.status = "Opened project: " + target.string();
                            wizard.status = project.status;
                            wizard.visible = false;
                        }
                    }

                    if (!wizard.status.empty())
                    {
                        ImGui::Separator();
                        ImGui::TextWrapped("%s", wizard.status.c_str());
                    }
                }
                ImGui::EndChild();
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        if (showDemo)
        {
            ImGui::ShowDemoWindow(&showDemo);
        }

        window.clear(sf::Color(24, 24, 28));
        ImGui::SFML::Render(window);
        window.display();
    }

    editorSettings.uiZoom = userZoom;
    editorSettings.Save();
    ImGui::SFML::Shutdown();
    return 0;
}
