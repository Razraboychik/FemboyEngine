cmake_minimum_required(VERSION 3.21)
project(FemboyEngine VERSION 1.6.0 LANGUAGES CXX)

# The Windows resource compiler carries the executable icon and the version
# block. It only exists in the Windows toolchain, so it is enabled where it can
# be and the resource script is added only there.
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform detection
if(ANDROID)
    set(FEMBOY_PLATFORM "Android")
elseif(APPLE)
    if(IOS)
        set(FEMBOY_PLATFORM "iOS")
    else()
        set(FEMBOY_PLATFORM "macOS")
    endif()
elseif(UNIX)
    set(FEMBOY_PLATFORM "Linux")
elseif(WIN32)
    set(FEMBOY_PLATFORM "Windows")
else()
    set(FEMBOY_PLATFORM "Unknown")
endif()
message(STATUS "FemboyEngine target platform: ${FEMBOY_PLATFORM}")

option(FEMBOY_NATIVE_ARCH "Optimize release builds for the build machine CPU" OFF)

# Optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        # /Zi and /DEBUG keep a PDB for the exact shipped binary: crash reports
        # from players are only useful when the symbols match the executable
        # byte-for-byte, and a RelWithDebInfo build is a different binary.
        add_compile_options(/O2 /Ob2 /Oi /Ot /GL /Zi)
        if(FEMBOY_NATIVE_ARCH)
            add_compile_options(/arch:AVX2)
        endif()
        add_link_options(/LTCG /DEBUG /OPT:REF /OPT:ICF)
    else()
        add_compile_options(-O3 -flto)
        if(FEMBOY_NATIVE_ARCH)
            add_compile_options(-march=native)
        endif()
        add_link_options(-flto)
    endif()
endif()

# SFML still uses an OpenGL context on desktop. The symbol name and library
# differ by platform, and mobile targets need a platform renderer rather than
# the desktop SFML backend.
if(ANDROID OR IOS)
    message(FATAL_ERROR
        "Android/iOS are not supported by the desktop SFML backend yet. "
        "They need a platform layer (EGL/Metal + touch input + asset/audio "
        "bridges) before CMake can produce an app. See Docs/PLATFORMS.md.")
endif()
find_package(Threads REQUIRED)

# Cross-platform defines
add_compile_definitions(FEMBOY_PLATFORM="${FEMBOY_PLATFORM}")

option(FEMBOY_BUILD_EDITOR   "Build FemboyEditor"   ON)
# Build type
# A single-config generator with no CMAKE_BUILD_TYPE compiles with MSVC's
# unoptimised default and links the *debug* SFML binaries. That is useful while
# stepping through code, but a typical `cmake -B build` should not produce a
# 6x slower game. Default to optimised-with-symbols; pass
# -DCMAKE_BUILD_TYPE=Debug when a debugger is needed.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    message(STATUS "No build type given, defaulting to RelWithDebInfo")
endif()

option(FEMBOY_BUILD_TESTS    "Build tests"          OFF)
option(FEMBOY_STATIC_SFML    "Link SFML statically (requires FEMBOY_ENABLE_VOICE=OFF; no SFML DLLs)" OFF)
option(FEMBOY_FETCH_SFML     "Download and build SFML 3.1 when no pre-built or system copy is found" ON)
# Voice chat is optional at build time: the public API stays available and
# degrades to a no-op when Opus or miniaudio is missing. The vendored copies are
# used when present so a fresh checkout needs no system packages.
option(FEMBOY_ENABLE_VOICE   "Build microphone voice chat (Opus + miniaudio)" ON)

# SFML's audio module embeds miniaudio, and VoiceChat embeds it too. In a static
# SFML build the two implementations collapse into one set of global symbols;
# SFML then calls a miniaudio compiled against a different header layout and
# audio device creation fails with MA_INVALID_ARGS. The release package therefore
# links the SFML DLLs, where each side keeps its own miniaudio.
if(FEMBOY_STATIC_SFML AND FEMBOY_ENABLE_VOICE)
    message(FATAL_ERROR
        "FEMBOY_STATIC_SFML=ON and FEMBOY_ENABLE_VOICE=ON cannot be combined: "
        "static SFML and VoiceChat both embed miniaudio. Use "
        "-DFEMBOY_STATIC_SFML=OFF (recommended, packages the SFML DLLs) or "
        "-DFEMBOY_ENABLE_VOICE=OFF.")
endif()

include(FetchContent)

# SFML 3.1 (local pre-built)
# The pre-built tree ships both the import libraries and the static ones, so
# the same project can produce a normal build and, with voice disabled, a static
# build that carries no SFML DLLs.
if(FEMBOY_STATIC_SFML)
    set(SFML_STATIC_LIBRARIES ON CACHE BOOL "Use the static SFML libraries" FORCE)
else()
    # Reconfiguring an existing static build must switch back to the import
    # libraries. Without this FORCE the cache keeps SFML_STATIC_LIBRARIES=ON and
    # the "dynamic" package still links the static tree.
    set(SFML_STATIC_LIBRARIES OFF CACHE BOOL "Use the static SFML libraries" FORCE)
endif()
# Two pre-built trees ship in libs/: the MSVC one and the MinGW-w64 one. They
# are ABI-incompatible, so the compiler picks the tree. FEMBOY_SFML_ROOT is a
# cache path so a cross build can point at a tree built for another target.
set(FEMBOY_SFML_ROOT "" CACHE PATH "Pre-built SFML tree; empty = the one matching this compiler")
if(NOT FEMBOY_SFML_ROOT)
    if(MSVC)
        set(FEMBOY_SFML_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libs/SFML-3.1.0")
    elseif(MINGW)
        set(FEMBOY_SFML_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libs/SFML-3.1.0-windows-gcc-14.2.0-mingw-64-bit/SFML-3.1.0")
    else()
        # Linux and macOS build SFML from the tarball or use the system package;
        # an empty root lets find_package search the usual prefixes.
        set(FEMBOY_SFML_ROOT "")
    endif()
endif()
if(FEMBOY_SFML_ROOT)
    set(SFML_DIR "${FEMBOY_SFML_ROOT}/lib/cmake/SFML")
endif()
# Main is SFML's WinMain wrapper. It is only needed by GUI executables on
# Windows; on other platforms the normal main entry point is used.
if(WIN32)
    find_package(SFML 3 COMPONENTS Graphics Window System Audio Main QUIET)
else()
    find_package(SFML 3 COMPONENTS Graphics Window System Audio QUIET)
endif()
if(NOT SFML_FOUND)
    if(FEMBOY_FETCH_SFML)
        # Minimal SFML build for CI and for machines without a system package:
        # only the four modules the engine links, no examples or docs.
        set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
        set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SFML_BUILD_DOC OFF CACHE BOOL "" FORCE)
        set(SFML_BUILD_TEST_SUITE OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SFML
            GIT_REPOSITORY https://github.com/SFML/SFML.git
            GIT_TAG 3.1.0
            GIT_SHALLOW ON)
        FetchContent_MakeAvailable(SFML)
    else()
        message(FATAL_ERROR
            "SFML 3 was not found. Install it, point FEMBOY_SFML_ROOT at a build, "
            "or configure with -DFEMBOY_FETCH_SFML=ON to download and build it.")
    endif()
endif()

# Imported SFML targets are created inside this directory. A standalone game
# project adds the engine with add_subdirectory(), so the imported targets would
# otherwise be invisible to the parent project: the game would silently miss
# SFML::Main and link as /SUBSYSTEM:CONSOLE. Promote them to GLOBAL so every
# consuming project sees the same GUI entry-point target as the editor.
if(WIN32)
    foreach(_sfml_target SFML::Main SFML::Graphics SFML::Window SFML::System SFML::Audio)
        if(TARGET ${_sfml_target})
            get_target_property(_sfml_imported ${_sfml_target} IMPORTED)
            get_target_property(_sfml_alias ${_sfml_target} ALIASED_TARGET)
            if(_sfml_imported AND NOT _sfml_alias)
                set_property(TARGET ${_sfml_target} PROPERTY IMPORTED_GLOBAL TRUE)
            endif()
        endif()
    endforeach()
endif()

# The pre-built SFML config only declares Debug and Release imported configs.
# Without an explicit map, CMake falls back to the first one listed (Debug) for
# RelWithDebInfo, which silently links debug SFML into a release-CRT executable:
# two different heaps, immediate crashes. Map the optimised configs to Release.
foreach(_sfml_target SFML::Graphics SFML::Window SFML::System SFML::Audio)
    if(TARGET ${_sfml_target})
        get_target_property(_sfml_alias ${_sfml_target} ALIASED_TARGET)
        get_target_property(_sfml_imported ${_sfml_target} IMPORTED)
        if(_sfml_alias OR NOT _sfml_imported)
            continue()
        endif()
        set_property(TARGET ${_sfml_target} PROPERTY MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
        set_property(TARGET ${_sfml_target} PROPERTY MAP_IMPORTED_CONFIG_MINSIZEREL Release)
    endif()
endforeach()

# Dear ImGui (local)
set(imgui_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui-master/imgui-master")
# Imgui-sfml (local)
set(imgui_sfml_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui-sfml-master/imgui-sfml-master")
# Entt (local)
set(entt_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/entt-main/entt-main")
if(EXISTS "${entt_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory(${entt_SOURCE_DIR} entt-build EXCLUDE_FROM_ALL)
endif()
# Nlohmann/json (local)
set(json_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/json-develop/json-develop")
if(EXISTS "${json_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory(${json_SOURCE_DIR} json-build EXCLUDE_FROM_ALL)
endif()


# Box2D (optional physics)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/box2d-main/box2d-main/CMakeLists.txt")
    set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
    add_subdirectory(libs/box2d-main/box2d-main box2d-build)
    set(FEMBOY_HAS_BOX2D ON)
    message(STATUS "Box2D enabled")
else()
    set(FEMBOY_HAS_BOX2D OFF)
endif()

# ImPlot (optional profiler graphs)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/ImPlot-master/implot.h")
    set(FEMBOY_HAS_IMPLOT ON)
    message(STATUS "ImPlot enabled")
else()
    set(FEMBOY_HAS_IMPLOT OFF)
endif()

# ENet (networking)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/enet-master/enet-master/CMakeLists.txt")
    set(FEMBOY_HAS_ENET ON)
    add_subdirectory(libs/enet-master/enet-master enet-build-v2)
    message(STATUS "ENet enabled")
else()
    set(FEMBOY_HAS_ENET OFF)
endif()

# Voice chat (Opus + miniaudio)
# Both dependencies are vendored so a checkout can build voice chat offline.
# Opus supplies the 48 kHz mono codec; miniaudio only opens the capture device.
# Playback deliberately stays on the existing SFML/OpenAL mixer, so the engine
# never ends up with two output devices fighting over the default endpoint.
set(FEMBOY_HAS_VOICE OFF)
if(FEMBOY_ENABLE_VOICE)
    set(_opus_dir "${CMAKE_CURRENT_SOURCE_DIR}/libs/opus-master/opus-main")
    set(_miniaudio_dir "${CMAKE_CURRENT_SOURCE_DIR}/libs/miniaudio-master/miniaudio-master")
    if(EXISTS "${_opus_dir}/CMakeLists.txt" AND EXISTS "${_miniaudio_dir}/miniaudio.h")
        # Keep the dependency quiet: no tools, tests, pkg-config files or CMake
        # package exports. The engine links the static library directly.
        set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
        set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
        set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
        add_subdirectory("${_opus_dir}" opus-build EXCLUDE_FROM_ALL)
        set(FEMBOY_HAS_VOICE ON)
        set(FEMBOY_MINIAUDIO_INCLUDE_DIR "${_miniaudio_dir}")
        message(STATUS "Voice chat enabled (Opus + miniaudio capture)")
    else()
        message(STATUS "Voice chat disabled: Opus or miniaudio sources are missing")
    endif()
endif()

# FlatBuffers
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/flatbuffers-master/flatbuffers-master/include/flatbuffers/flatbuffers.h")
    set(FEMBOY_HAS_FLATBUFFERS ON)
    set(FLATBUFFERS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/flatbuffers-master/flatbuffers-master/include")
    message(STATUS "FlatBuffers enabled")
else()
    set(FEMBOY_HAS_FLATBUFFERS OFF)
endif()

# Asio
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/asio-master/asio-master/asio/include/asio.hpp")
    set(FEMBOY_HAS_ASIO ON)
    set(ASIO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/asio-master/asio-master/asio/include")
    message(STATUS "asio enabled")
else()
    set(FEMBOY_HAS_ASIO OFF)
endif()

# Boot logo
# The splash draws Logo.png from the repository root. Turning it into a byte
# array at configure time keeps the engine self-contained: a packaged game has
# no Logo.png next to its executable, and the mark must still appear.
set(FEMBOY_LOGO_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Logo.png")
set(FEMBOY_LOGO_SIZE 0)
set(FEMBOY_LOGO_BYTES "0")
if(EXISTS "${FEMBOY_LOGO_SOURCE}")
    file(READ "${FEMBOY_LOGO_SOURCE}" FEMBOY_LOGO_HEX HEX)
    string(LENGTH "${FEMBOY_LOGO_HEX}" FEMBOY_LOGO_HEX_LENGTH)
    math(EXPR FEMBOY_LOGO_SIZE "${FEMBOY_LOGO_HEX_LENGTH} / 2")
    string(REGEX REPLACE "(..)" "0x\\1," FEMBOY_LOGO_BYTES "${FEMBOY_LOGO_HEX}")
    message(STATUS "Boot logo embedded from Logo.png (${FEMBOY_LOGO_SIZE} bytes)")
else()
    message(STATUS "Boot logo: Logo.png not found; the splash will fall back to text only")
endif()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Engine/src/Core/FemboyLogoData.hpp.in"
               "${CMAKE_BINARY_DIR}/generated/FemboyLogoData.hpp" @ONLY)

# Engine library
file(GLOB_RECURSE FEMBOY_ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/src/*.cpp)
file(GLOB_RECURSE FEMBOY_ENGINE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/include/*.h)

# FemMath is a header-only, engine-independent math vocabulary. Keeping it as
# an interface target makes the module boundary explicit without adding a
# build step or a runtime library.
add_library(FemMath INTERFACE)
add_library(FemMath::FemMath ALIAS FemMath)
target_include_directories(FemMath INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Engine/include)
target_link_libraries(FemMath INTERFACE SFML::Graphics)

add_library(femboyengine STATIC ${FEMBOY_ENGINE_SOURCES} ${FEMBOY_ENGINE_HEADERS})
add_library(FemboyEngine::femboyengine ALIAS femboyengine)

target_include_directories(femboyengine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/include
    ${CMAKE_BINARY_DIR}/generated
    ${imgui_SOURCE_DIR}
    ${imgui_sfml_SOURCE_DIR}
    ${FEMBOY_SFML_ROOT}/include
    ${entt_SOURCE_DIR}/src
    ${json_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/stb)

target_link_libraries(femboyengine PUBLIC
    FemMath
    SFML::Graphics SFML::Window SFML::System SFML::Audio
    EnTT::EnTT
    nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(femboyengine PUBLIC opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(femboyengine PUBLIC OpenGL::GL)
endif()
if(NOT WIN32)
    target_link_libraries(femboyengine PUBLIC Threads::Threads)
endif()
if(FEMBOY_HAS_BOX2D)
    target_link_libraries(femboyengine PUBLIC box2d)
    target_compile_definitions(femboyengine PUBLIC FEMBOY_HAS_BOX2D)
    target_include_directories(femboyengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libs/box2d-main/box2d-main/include)
endif()
if(FEMBOY_HAS_ENET)
    if(WIN32)
        target_link_libraries(femboyengine PUBLIC enet ws2_32 winmm)
    else()
        target_link_libraries(femboyengine PUBLIC enet)
    endif()
    target_compile_definitions(femboyengine PUBLIC FEMBOY_HAS_ENET)
    target_include_directories(femboyengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libs/enet-master/enet-master/include)
endif()
if(FEMBOY_HAS_FLATBUFFERS)
    target_compile_definitions(femboyengine PUBLIC FEMBOY_HAS_FLATBUFFERS)
    target_include_directories(femboyengine PUBLIC ${FLATBUFFERS_INCLUDE_DIR})
endif()
if(FEMBOY_HAS_ASIO)
    target_compile_definitions(femboyengine PUBLIC FEMBOY_HAS_ASIO)
    target_include_directories(femboyengine PUBLIC ${ASIO_INCLUDE_DIR})
endif()
if(FEMBOY_HAS_VOICE)
    target_link_libraries(femboyengine PUBLIC Opus::opus)
    target_compile_definitions(femboyengine PUBLIC FEMBOY_HAS_VOICE)
    target_include_directories(femboyengine PUBLIC ${FEMBOY_MINIAUDIO_INCLUDE_DIR})
endif()

# Text-to-speech uses SAPI on Windows; the accessibility system degrades to
# logging when no voice is installed.
if(WIN32)
    target_link_libraries(femboyengine PUBLIC sapi ole32)
endif()
if(FEMBOY_HAS_VOICE)
    # miniaudio's backends link against the OS audio stack directly. These are
    # no-ops on platforms that do not have them but keep the Windows and Unix
    # link lines self-contained.
    if(WIN32)
        target_link_libraries(femboyengine PUBLIC winmm ole32)
    elseif(APPLE)
        target_link_libraries(femboyengine PUBLIC
            "-framework CoreAudio" "-framework AudioToolbox" "-framework CoreFoundation")
    else()
        target_link_libraries(femboyengine PUBLIC dl pthread m)
    endif()
endif()

target_compile_definitions(femboyengine PUBLIC FEMBOY_ENGINE_VERSION="1.6.0")

# ImGui sources compiled into engine
target_sources(femboyengine PRIVATE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_sfml_SOURCE_DIR}/imgui-SFML.cpp)
if(FEMBOY_HAS_IMPLOT)
    target_sources(femboyengine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/ImPlot-master/implot.cpp ${CMAKE_CURRENT_SOURCE_DIR}/libs/ImPlot-master/implot_items.cpp)
    target_include_directories(femboyengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libs/ImPlot-master)
    target_compile_definitions(femboyengine PUBLIC FEMBOY_HAS_IMPLOT)
endif()
if(WIN32)
    # ShellExecuteW in Vanity::OpenURL (clickable UI links).
    target_link_libraries(femboyengine PUBLIC shell32)
endif()

# Editor
if(FEMBOY_BUILD_EDITOR)
    add_executable(FemboyEditor Editor/src/main.cpp)
    target_link_libraries(FemboyEditor PRIVATE femboyengine)
    if(WIN32 AND TARGET SFML::Main)
        # GUI subsystem: no console window; SFML::Main supplies WinMain and
        # forwards to the normal main().
        set_target_properties(FemboyEditor PROPERTIES WIN32_EXECUTABLE TRUE)
        target_link_libraries(FemboyEditor PRIVATE SFML::Main)
    endif()
    add_custom_command(TARGET FemboyEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Data/Fonts"
            "$<TARGET_FILE_DIR:FemboyEditor>/Data/Fonts")
endif()

# Tests
if(FEMBOY_BUILD_TESTS)
    # Catch2 ships an amalgamated single-header/source pair, which avoids
    # depending on its full CMake project and generated config headers.
    add_executable(femboy_tests
        Tests/test_main.cpp
        Tests/reflection_tests.cpp
        Tests/math_tests.cpp
        Tests/world_tests.cpp
        Tests/console_tests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/Catch2-master/Catch2-devel/extras/catch_amalgamated.cpp)
    target_include_directories(femboy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/Catch2-master/Catch2-devel/extras)
    target_link_libraries(femboy_tests PRIVATE femboyengine)
endif()

# copy SFML dlls from pre-built
if(NOT FEMBOY_STATIC_SFML AND FEMBOY_SFML_ROOT)
    add_custom_target(copy_sfml_dlls ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${FEMBOY_SFML_ROOT}/bin
            ${CMAKE_BINARY_DIR}
        VERBATIM)
endif()

# Packaging belongs to a separate game project, not to the engine repository.

