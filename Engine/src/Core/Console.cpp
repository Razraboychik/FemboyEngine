// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Console.hpp"

#include "FemboyEngine/Engine.hpp"
#include "FemboyEngine/Profiler.hpp"
#include "FemboyEngine/SettingsRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

namespace Fem
{
namespace
{
std::string Trim(std::string Value)
{
    const auto NotSpace = [](unsigned char Character) { return !std::isspace(Character); };
    Value.erase(Value.begin(), std::find_if(Value.begin(), Value.end(), NotSpace));
    Value.erase(std::find_if(Value.rbegin(), Value.rend(), NotSpace).base(), Value.end());
    return Value;
}

std::vector<std::string> Split(const std::string& Line)
{
    std::istringstream Stream(Line);
    std::vector<std::string> Parts;
    std::string Part;
    while (Stream >> Part)
    {
        Parts.push_back(Part);
    }
    return Parts;
}

std::string Lower(std::string Value)
{
    std::transform(Value.begin(), Value.end(), Value.begin(),
                   [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
    return Value;
}
} // namespace

Console& Console::Instance()
{
    static Console Instance;
    return Instance;
}

Console::Console()
{
    RegisterBuiltins();
    AddLine("FemboyEngine console. Type 'help' for commands.");
}

void Console::RegisterCommand(const std::string& Name, const std::string& Description, ConsoleCommand Handler)
{
    if (Name.empty() || !Handler)
    {
        return;
    }
    for (auto& [ExistingName, Entry] : Commands_)
    {
        if (ExistingName == Name)
        {
            Entry.Description = Description;
            Entry.Handler = std::move(Handler);
            return;
        }
    }
    Commands_.push_back({Name, {Description, std::move(Handler)}});
    std::sort(Commands_.begin(), Commands_.end(),
              [](const auto& A, const auto& B) { return A.first < B.first; });
}

bool Console::Execute(const std::string& Line)
{
    const std::string CommandLine = Trim(Line);
    if (CommandLine.empty())
    {
        return false;
    }

    AddLine("> " + CommandLine);
    History_.push_back(CommandLine);
    HistoryCursor_ = static_cast<int>(History_.size());

    const std::vector<std::string> Parts = Split(CommandLine);
    const std::string Name = Lower(Parts.front());
    for (auto& [CommandName, Entry] : Commands_)
    {
        if (Lower(CommandName) == Name)
        {
            try
            {
                Entry.Handler(Parts);
            }
            catch (const std::exception& Error)
            {
                AddLine(std::string("error: ") + Error.what());
            }
            catch (...)
            {
                AddLine("error: command failed");
            }
            return true;
        }
    }

    AddLine("unknown command: " + Parts.front() + " (try 'help')");
    return false;
}

void Console::Print(const std::string& Line)
{
    AddLine(Line);
}

void Console::AddLine(std::string Line)
{
    std::size_t Start = 0;
    while (Start <= Line.size())
    {
        const std::size_t End = Line.find('\n', Start);
        Lines_.push_back(Line.substr(Start, End == std::string::npos ? std::string::npos : End - Start));
        if (End == std::string::npos)
        {
            break;
        }
        Start = End + 1;
    }
    constexpr std::size_t MaxLines = 512;
    if (Lines_.size() > MaxLines)
    {
        Lines_.erase(Lines_.begin(), Lines_.begin() + static_cast<std::ptrdiff_t>(Lines_.size() - MaxLines));
    }
}

void Console::Clear()
{
    Lines_.clear();
}

void Console::Toggle()
{
    SetOpen(!Open_);
}

void Console::SetOpen(bool Value)
{
    Open_ = Value;
    InputLine_.clear();
    HistoryCursor_ = static_cast<int>(History_.size());
}

std::string Console::Autocomplete(const std::string& Prefix) const
{
    const std::string Needle = Lower(Prefix);
    for (const auto& [Name, Entry] : Commands_)
    {
        if (Lower(Name).rfind(Needle, 0) == 0)
        {
            return Name;
        }
    }
    return Prefix;
}

bool Console::ProcessEvent(const sf::Event& Event)
{
    if (const auto* Key = Event.getIf<sf::Event::KeyPressed>())
    {
        if (Key->code == sf::Keyboard::Key::F1 || Key->code == sf::Keyboard::Key::Grave)
        {
            Toggle();
            return true;
        }
        if (!Open_)
        {
            return false;
        }

        switch (Key->code)
        {
            case sf::Keyboard::Key::Escape:
                SetOpen(false);
                return true;
            case sf::Keyboard::Key::Enter:
                Execute(InputLine_);
                InputLine_.clear();
                return true;
            case sf::Keyboard::Key::Backspace:
                if (!InputLine_.empty())
                {
                    InputLine_.pop_back();
                }
                return true;
            case sf::Keyboard::Key::Tab:
                InputLine_ = Autocomplete(InputLine_);
                return true;
            case sf::Keyboard::Key::Up:
                if (!History_.empty())
                {
                    HistoryCursor_ = std::max(0, HistoryCursor_ - 1);
                    InputLine_ = History_[static_cast<std::size_t>(HistoryCursor_)];
                }
                return true;
            case sf::Keyboard::Key::Down:
                if (!History_.empty())
                {
                    HistoryCursor_ = std::min(static_cast<int>(History_.size()), HistoryCursor_ + 1);
                    InputLine_ = HistoryCursor_ < static_cast<int>(History_.size())
                                     ? History_[static_cast<std::size_t>(HistoryCursor_)]
                                     : std::string{};
                }
                return true;
            default:
                return true;
        }
    }

    if (const auto* Text = Event.getIf<sf::Event::TextEntered>())
    {
        if (!Open_)
        {
            return false;
        }
        const char32_t Character = Text->unicode;
        if (Character >= 32 && Character < 127)
        {
            InputLine_.push_back(static_cast<char>(Character));
        }
        return true;
    }

    return false;
}

void Console::Render(DrawList& Out, float ViewportWidth, float ViewportHeight) const
{
    if (!Open_ || ViewportWidth <= 1.f || ViewportHeight <= 1.f)
    {
        return;
    }

    constexpr float FontSize = 16.f;
    constexpr float LineHeight = 20.f;
    const float PanelHeight = std::min(ViewportHeight * 0.58f, 520.f);
    const float PanelY = ViewportHeight - PanelHeight;
    const sf::FloatRect Panel({0.f, PanelY}, {ViewportWidth, PanelHeight});
    Out.Rect(Panel, sf::Color(8, 8, 12, 235), sf::Color(240, 120, 190, 255), 1.f, 100000.f,
             DrawLayer::Overlay);

    Out.Text("Femboy Console   F1/` close   Esc close   Tab complete   Up/Down history", "", {12.f, PanelY + 8.f},
             FontSize, sf::Color(255, 160, 220), 100001.f, DrawLayer::Overlay);

    const int MaxVisible = std::max(1, static_cast<int>((PanelHeight - 62.f) / LineHeight));
    const int First = std::max(0, static_cast<int>(Lines_.size()) - MaxVisible);
    float Y = PanelY + 34.f;
    for (int Index = First; Index < static_cast<int>(Lines_.size()); ++Index)
    {
        Out.Text(Lines_[static_cast<std::size_t>(Index)], "", {12.f, Y}, FontSize, sf::Color(220, 220, 230), 100001.f,
                 DrawLayer::Overlay);
        Y += LineHeight;
    }

    const float InputY = ViewportHeight - 26.f;
    Out.Rect(sf::FloatRect({0.f, InputY - 3.f}, {ViewportWidth, 26.f}), sf::Color(20, 20, 28, 240),
             sf::Color::Transparent,
             DrawCommand::kNoThickness, 100002.f, DrawLayer::Overlay);
    Out.Text("> " + InputLine_ + "_", "", {12.f, InputY}, FontSize, sf::Color(255, 255, 255), 100003.f,
             DrawLayer::Overlay);
}

void Console::RegisterBuiltins()
{
    RegisterCommand("help", "List registered commands",
                    [this](const std::vector<std::string>&)
                    {
                        for (const auto& [Name, Entry] : Commands_)
                        {
                            AddLine(Name + " - " + Entry.Description);
                        }
                    });
    RegisterCommand("clear", "Clear console output", [this](const std::vector<std::string>&) { Clear(); });
    RegisterCommand("echo", "Print text",
                    [this](const std::vector<std::string>& Args)
                    {
                        std::string Text;
                        for (std::size_t Index = 1; Index < Args.size(); ++Index)
                        {
                            if (!Text.empty())
                            {
                                Text += ' ';
                            }
                            Text += Args[Index];
                        }
                        AddLine(Text);
                    });
    RegisterCommand("cvar.get", "Read a settings-registry value",
                    [this](const std::vector<std::string>& Args)
                    {
                        if (Args.size() < 2)
                        {
                            AddLine("usage: cvar.get <key>");
                            return;
                        }
                        const nlohmann::json Value = SettingsRegistry::Instance().GetJson(Args[1]);
                        AddLine(Args[1] + " = " + Value.dump());
                    });
    RegisterCommand("cvar.set", "Set a settings-registry value",
                    [this](const std::vector<std::string>& Args)
                    {
                        if (Args.size() < 3)
                        {
                            AddLine("usage: cvar.set <key> <value>");
                            return;
                        }
                        nlohmann::json Value = Args[2];
                        try
                        {
                            Value = nlohmann::json::parse(Args[2]);
                        }
                        catch (...)
                        {
                        }
                        if (!SettingsRegistry::Instance().Set(Args[1], Value))
                        {
                            AddLine("cannot set " + Args[1]);
                        }
                    });
    RegisterCommand("stat.fps", "Print average FPS",
                    [this](const std::vector<std::string>&)
                    {
                        AddLine("FPS: " + std::to_string(Profiler::Instance().AverageFps(120)));
                    });
    RegisterCommand("quit", "Quit the application", [](const std::vector<std::string>&)
                    { Application::Instance().Quit(); });
}

} // namespace Fem
