// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>

namespace Fem
{

// Optional OS console for early diagnostics. It is never opened implicitly in
// release builds; call AttachIfRequested(argc, argv) from main or Attach()
// explicitly from a debug tool. The in-game developer console is separate and
// does not need an OS window.
class DebugConsole
{
public:
    static bool Attach();
    static void Detach();
    static bool IsAttached();
    // Recognizes --console and --dev-console.
    static bool AttachIfRequested(int argc, char** argv, const std::string& title = "FemboyEngine Console");
};

} // namespace Fem
