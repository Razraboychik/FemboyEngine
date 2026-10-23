// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/RichText.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string_view>

namespace Fem
{

namespace
{
bool ParseHex(std::string_view text, sf::Color& out)
{
    if (text.empty() || text.front() != '#')
    {
        return false;
    }
    text.remove_prefix(1);
    auto nibble = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        return -1;
    };

    auto byteAt = [&](std::size_t index) -> int
    {
        const int hi = nibble(text[index]);
        const int lo = nibble(text[index + 1]);
        return (hi < 0 || lo < 0) ? -1 : hi * 16 + lo;
    };

    if (text.size() == 3)
    {
        const int r = nibble(text[0]);
        const int g = nibble(text[1]);
        const int b = nibble(text[2]);
        if (r < 0 || g < 0 || b < 0)
        {
            return false;
        }
        out = sf::Color(static_cast<std::uint8_t>(r * 17), static_cast<std::uint8_t>(g * 17),
                        static_cast<std::uint8_t>(b * 17));
        return true;
    }
    if (text.size() == 6 || text.size() == 8)
    {
        const int r = byteAt(0);
        const int g = byteAt(2);
        const int b = byteAt(4);
        if (r < 0 || g < 0 || b < 0)
        {
            return false;
        }
        const int a = text.size() == 8 ? byteAt(6) : 255;
        if (a < 0)
        {
            return false;
        }
        out = sf::Color(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b),
                        static_cast<std::uint8_t>(a));
        return true;
    }
    return false;
}

bool ParseColor(std::string value, sf::Color& out)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; }),
                value.end());
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ParseHex(value, out))
    {
        return true;
    }

    if (value == "white")
    {
        out = sf::Color::White;
    }
    else if (value == "black")
    {
        out = sf::Color::Black;
    }
    else if (value == "red")
    {
        out = sf::Color::Red;
    }
    else if (value == "green")
    {
        out = sf::Color::Green;
    }
    else if (value == "blue")
    {
        out = sf::Color::Blue;
    }
    else if (value == "yellow")
    {
        out = sf::Color::Yellow;
    }
    else if (value == "cyan")
    {
        out = sf::Color::Cyan;
    }
    else if (value == "magenta")
    {
        out = sf::Color::Magenta;
    }
    else if (value == "orange")
    {
        out = sf::Color(255, 150, 40);
    }
    else if (value == "gray" || value == "grey")
    {
        out = sf::Color(150, 150, 150);
    }
    else if (value == "transparent")
    {
        out = sf::Color::Transparent;
    }
    else
    {
        return false;
    }
    return true;
}

bool StartsWith(const std::string& value, const char* prefix)
{
    return value.rfind(prefix, 0) == 0;
}

enum class TagAction
{
    Unknown,
    Open,
    Close,
    Reset
};
} // namespace

RichText ParseRichText(const std::string& source, const TextStyle& base)
{
    RichText result;
    TextStyle style = base;
    std::vector<TextStyle> stack;
    std::string current;

    auto flush = [&]()
    {
        if (current.empty())
        {
            return;
        }
        if (!result.runs.empty() && result.runs.back().style == style)
        {
            result.runs.back().text += current;
        }
        else
        {
            result.runs.push_back(TextRun{current, style});
        }
        current.clear();
    };

    for (std::size_t i = 0; i < source.size(); ++i)
    {
        const char c = source[i];

        // Literal braces are doubled; this is the only escape, because a backslash
        // in a translated string is too easy to lose.
        if (c == '{' && i + 1 < source.size() && source[i + 1] == '{')
        {
            current += '{';
            ++i;
            continue;
        }
        if (c == '}' && i + 1 < source.size() && source[i + 1] == '}')
        {
            current += '}';
            ++i;
            continue;
        }

        if (c != '{')
        {
            current += c;
            continue;
        }

        const std::size_t close = source.find('}', i + 1);
        if (close == std::string::npos)
        {
            current += source.substr(i);
            break;
        }

        const std::string tag = source.substr(i + 1, close - i - 1);
        if (tag.empty())
        {
            current += "{}";
            i = close;
            continue;
        }

        TagAction action = TagAction::Unknown;
        TextStyle next = style;
        bool recognized = true;

        if (tag == "b" || tag == "/b")
        {
            action = tag == "b" ? TagAction::Open : TagAction::Close;
            next.bold = tag == "b";
        }
        else if (tag == "i" || tag == "/i")
        {
            action = tag == "i" ? TagAction::Open : TagAction::Close;
            next.italic = tag == "i";
        }
        else if (tag == "u" || tag == "/u")
        {
            action = tag == "u" ? TagAction::Open : TagAction::Close;
            next.underline = tag == "u";
        }
        else if (tag == "s" || tag == "/s")
        {
            action = tag == "s" ? TagAction::Open : TagAction::Close;
            next.strike = tag == "s";
        }
        else if (tag == "reset" || tag == "/")
        {
            action = TagAction::Reset;
            next = base;
        }
        else if (StartsWith(tag, "color="))
        {
            sf::Color color;
            if (!ParseColor(tag.substr(6), color))
            {
                recognized = false;
            }
            else
            {
                action = TagAction::Open;
                next.color = color;
            }
        }
        else if (tag == "/color")
        {
            action = TagAction::Close;
            next.color = base.color;
        }
        else if (StartsWith(tag, "size="))
        {
            try
            {
                const int size = std::stoi(tag.substr(5));
                action = TagAction::Open;
                next.size = static_cast<unsigned>(std::clamp(size, 4, 512));
            }
            catch (...)
            {
                recognized = false;
            }
        }
        else if (tag == "/size")
        {
            action = TagAction::Close;
            next.size = base.size;
        }
        else if (StartsWith(tag, "font="))
        {
            action = TagAction::Open;
            next.fontId = tag.substr(5);
        }
        else if (tag == "/font")
        {
            action = TagAction::Close;
            next.fontId = base.fontId;
        }
        else
        {
            recognized = false;
        }

        if (!recognized || action == TagAction::Unknown)
        {
            // Preserve the tag verbatim. A translator who writes {n} for a
            // placeholder must still see it in the label.
            current += source.substr(i, close - i + 1);
            i = close;
            continue;
        }

        flush();
        result.hasMarkup = true;

        switch (action)
        {
            case TagAction::Open:
                stack.push_back(style);
                style = next;
                break;
            case TagAction::Close:
                if (!stack.empty())
                {
                    style = stack.back();
                    stack.pop_back();
                }
                else
                {
                    style = next;
                }
                break;
            case TagAction::Reset:
                stack.clear();
                style = base;
                break;
            case TagAction::Unknown:
                break;
        }
        i = close;
    }

    flush();
    for (const TextRun& run : result.runs)
    {
        result.plain += run.text;
    }
    return result;
}

std::string StripRichText(const std::string& source)
{
    return ParseRichText(source, TextStyle{}).plain;
}

bool HasRichTextMarkup(const std::string& source)
{
    // Avoid the full parser for the common case. A brace at the end of the
    // string cannot start a tag, so it does not count.
    if (source.find('{') == std::string::npos)
    {
        return false;
    }
    return ParseRichText(source, TextStyle{}).hasMarkup;
}

} // namespace Fem
