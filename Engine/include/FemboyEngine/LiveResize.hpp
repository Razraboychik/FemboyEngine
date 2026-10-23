// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
//
// Live window resizing.
//
// On Windows, dragging a window border puts the UI thread inside the system's
// modal resize loop (`WM_ENTERSIZEMOVE` .. `WM_EXITSIZEMOVE`). The engine's own
// `pollEvent()` loop is blocked for the whole drag, so without help the window
// either freezes on the last frame or shows a stretched bitmap and only snaps to
// the new size when the mouse is released.
//
// This helper subclasses the window procedure and, *only while the drag lasts*,
// starts a timer that renders one frame every `intervalMs`. Outside a drag no
// timer exists and the cost is exactly zero. That is the "optimal" behaviour:
// live feedback while resizing, nothing when idle.
#pragma once

#include <functional>

namespace Fem
{

class LiveResize
{
public:
    static LiveResize& Instance();

    // `nativeHandle` is an HWND on Windows and is ignored elsewhere.
    // `onTick` renders one frame from the current state; it must not advance
    // the simulation.
    void Attach(void* nativeHandle, std::function<void()> onTick);
    void Detach();

    bool Active() const
    {
        return active_;
    }
    bool Supported() const;

    // Frame interval used during a drag. 16 ms keeps the resize visually live
    // without competing with the system's own repaint work.
    void SetIntervalMs(unsigned ms);

    // Internal: driven by the subclassed window procedure.
    void Begin(void* nativeHandle);
    void End(void* nativeHandle);
    void Tick();
    void* PreviousProc() const
    {
        return previousProc_;
    }

private:
    LiveResize() = default;
    ~LiveResize();

    bool active_ = false;
    unsigned intervalMs_ = 16;
    std::function<void()> onTick_;
    void* handle_ = nullptr;
    void* previousProc_ = nullptr;
    bool ticking_ = false;
};

} // namespace Fem
