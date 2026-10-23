// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/TextInput.hpp"

#include <algorithm>

namespace Fem
{

namespace
{
bool IsContinuation(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}
} // namespace

void TextInput::Clear()
{
    text_.clear();
    cursor_ = 0;
}

void TextInput::SetText(const std::string& value)
{
    text_ = value;
    cursor_ = text_.size();
}

std::string TextInput::BeforeCursor() const
{
    return text_.substr(0, cursor_);
}

std::size_t TextInput::PreviousBoundary(const std::string& text, std::size_t at)
{
    if (at == 0)
    {
        return 0;
    }
    --at;
    while (at > 0 && IsContinuation(static_cast<unsigned char>(text[at])))
    {
        --at;
    }
    return at;
}

std::size_t TextInput::NextBoundary(const std::string& text, std::size_t at)
{
    if (at >= text.size())
    {
        return text.size();
    }
    ++at;
    while (at < text.size() && IsContinuation(static_cast<unsigned char>(text[at])))
    {
        ++at;
    }
    return at;
}

bool TextInput::Process(const std::string& entered, bool backspace, bool deleteKey, bool left, bool right)
{
    const std::string before = text_;
    const std::size_t oldCursor = cursor_;

    if (!entered.empty() && text_.size() < maxLength_)
    {
        const std::size_t room = maxLength_ - text_.size();
        std::string accepted;
        accepted.reserve(std::min(room, entered.size()));
        for (std::size_t i = 0; i < entered.size() && accepted.size() < room;)
        {
            const std::size_t start = i;
            const unsigned char lead = static_cast<unsigned char>(entered[i]);
            std::size_t length = 1;
            if ((lead & 0xE0) == 0xC0)
            {
                length = 2;
            }
            else if ((lead & 0xF0) == 0xE0)
            {
                length = 3;
            }
            else if ((lead & 0xF8) == 0xF0)
            {
                length = 4;
            }
            if (start + length > entered.size())
            {
                break;
            }
            accepted.append(entered, start, length);
            i += length;
        }
        text_.insert(cursor_, accepted);
        cursor_ += accepted.size();
    }

    if (backspace)
    {
        const std::size_t previous = PreviousBoundary(text_, cursor_);
        text_.erase(previous, cursor_ - previous);
        cursor_ = previous;
    }
    if (deleteKey)
    {
        const std::size_t next = NextBoundary(text_, cursor_);
        text_.erase(cursor_, next - cursor_);
    }
    if (left)
    {
        cursor_ = PreviousBoundary(text_, cursor_);
    }
    if (right)
    {
        cursor_ = NextBoundary(text_, cursor_);
    }

    return before != text_ || oldCursor != cursor_;
}

} // namespace Fem
