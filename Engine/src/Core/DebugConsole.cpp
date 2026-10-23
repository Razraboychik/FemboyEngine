// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/DebugConsole.hpp"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace Fem
{
namespace
{
bool g_attached = false;
}

bool DebugConsole::Attach()
{
#if defined(_WIN32)
    if (g_attached)
    {
        return true;
    }
    if (AllocConsole() == 0)
    {
        return false;
    }

    FILE* Dummy = nullptr;
    freopen_s(&Dummy, "CONOUT$", "w", stdout);
    freopen_s(&Dummy, "CONOUT$", "w", stderr);
    freopen_s(&Dummy, "CONIN$", "r", stdin);
    g_attached = true;
    return true;
#else
    return false;
#endif
}

void DebugConsole::Detach()
{
#if defined(_WIN32)
    if (!g_attached)
    {
        return;
    }
    fflush(stdout);
    fflush(stderr);
    FreeConsole();
    g_attached = false;
#endif
}

bool DebugConsole::IsAttached()
{
    return g_attached;
}

bool DebugConsole::AttachIfRequested(int argc, char** argv, const std::string& title)
{
    for (int Index = 1; Index < argc; ++Index)
    {
        if (argv[Index] == nullptr)
        {
            continue;
        }
        if (std::strcmp(argv[Index], "--console") == 0 || std::strcmp(argv[Index], "--dev-console") == 0)
        {
#if defined(_WIN32)
            SetConsoleTitleA(title.c_str());
#endif
            return Attach();
        }
    }
    return false;
}

} // namespace Fem
