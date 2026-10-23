// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/LiveResize.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace Fem
{

namespace
{
constexpr unsigned kTimerId = 0xFE40; // arbitrary, only used by this window
} // namespace

LiveResize& LiveResize::Instance()
{
    static LiveResize resize;
    return resize;
}

LiveResize::~LiveResize()
{
    Detach();
}

bool LiveResize::Supported() const
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

void LiveResize::SetIntervalMs(unsigned ms)
{
    intervalMs_ = ms < 4 ? 4 : ms;
}

#ifdef _WIN32

namespace
{
LRESULT CALLBACK LiveResizeProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

void CALLBACK LiveResizeTimer(HWND hwnd, UINT, UINT_PTR id, DWORD)
{
    if (id != kTimerId)
    {
        return;
    }
    LiveResize& self = LiveResize::Instance();
    // WM_TIMER is dispatched from the modal loop, which means the engine's own
    // update loop is parked. Render one frame and return to the system.
    self.Tick();
}

LRESULT CALLBACK LiveResizeProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    LiveResize& self = LiveResize::Instance();

    switch (msg)
    {
        case WM_ENTERSIZEMOVE:
            self.Begin(hwnd);
            break;
        case WM_EXITSIZEMOVE:
            self.End(hwnd);
            break;
        default:
            break;
    }

    return CallWindowProcW(reinterpret_cast<WNDPROC>(self.PreviousProc()), hwnd, msg, wparam, lparam);
}
} // namespace

void LiveResize::Attach(void* nativeHandle, std::function<void()> onTick)
{
    if (!nativeHandle)
    {
        return;
    }

    Detach();

    handle_ = nativeHandle;
    onTick_ = std::move(onTick);

    HWND hwnd = static_cast<HWND>(handle_);
    previousProc_ =
        reinterpret_cast<void*>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&LiveResizeProc)));
}

void LiveResize::Detach()
{
    if (!handle_)
    {
        onTick_ = nullptr;
        return;
    }

    HWND hwnd = static_cast<HWND>(handle_);
    KillTimer(hwnd, kTimerId);
    if (previousProc_)
    {
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previousProc_));
    }

    active_ = false;
    ticking_ = false;
    handle_ = nullptr;
    previousProc_ = nullptr;
    onTick_ = nullptr;
}

void LiveResize::Begin(void* hwnd)
{
    if (!onTick_ || active_)
    {
        return;
    }
    active_ = true;
    SetTimer(static_cast<HWND>(hwnd), kTimerId, intervalMs_, &LiveResizeTimer);
}

void LiveResize::End(void* hwnd)
{
    if (!active_)
    {
        return;
    }
    KillTimer(static_cast<HWND>(hwnd), kTimerId);
    active_ = false;
    // One final frame at the exact size the user let go of.
    Tick();
}

void LiveResize::Tick()
{
    if (!onTick_ || ticking_)
    {
        return;
    }
    ticking_ = true;
    onTick_();
    ticking_ = false;
}

#else

void LiveResize::Attach(void*, std::function<void()> onTick)
{
    onTick_ = std::move(onTick);
}

void LiveResize::Detach()
{
    onTick_ = nullptr;
}

void LiveResize::Begin(void*)
{
}

void LiveResize::End(void*)
{
}

void LiveResize::Tick()
{
    if (!onTick_ || ticking_)
    {
        return;
    }
    ticking_ = true;
    onTick_();
    ticking_ = false;
}

#endif

} // namespace Fem
