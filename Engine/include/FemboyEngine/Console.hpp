// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <SFML/Window/Event.hpp>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "FemboyEngine/Skirt.hpp"

namespace Fem
{

using ConsoleCommand = std::function<void(const std::vector<std::string>&)>;

// In-game developer console. It is engine-owned, renders through DrawList and
// never creates a second OS window. Games register commands during startup.
class Console
{
public:
    static Console& Instance();

    void RegisterCommand(const std::string& Name, const std::string& Description, ConsoleCommand Handler);
    bool Execute(const std::string& Line);
    void Print(const std::string& Line);
    void Clear();

    void Toggle();
    void SetOpen(bool Value);
    bool IsOpen() const
    {
        return Open_;
    }

    // Consumes keyboard/text events while open. F1 and ` toggle it. Returns true
    // when the event belongs to the console and must not reach gameplay input.
    bool ProcessEvent(const sf::Event& Event);

    // Draws the overlay in screen pixels. Call before IRenderer::EndFrame().
    void Render(DrawList& Out, float ViewportWidth, float ViewportHeight) const;

    const std::vector<std::string>& Output() const
    {
        return Lines_;
    }

private:
    Console();
    void RegisterBuiltins();
    void AddLine(std::string Line);
    std::string Autocomplete(const std::string& Prefix) const;

    struct CommandEntry
    {
        std::string Description;
        ConsoleCommand Handler;
    };

    std::vector<std::pair<std::string, CommandEntry>> Commands_;
    std::vector<std::string> Lines_;
    std::vector<std::string> History_;
    int HistoryCursor_ = -1;
    std::string InputLine_;
    bool Open_ = false;
};

} // namespace Fem
