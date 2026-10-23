// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <cstddef>
#include <string>

namespace Fem
{

// Small UTF-8-safe single-line text editor used by UI search fields.
//
// It deliberately has no font or window dependency: the caller owns the
// rectangle, clipping and caret rendering. Input::TextEntered() supplies the
// characters and the caller supplies the editing-key edges.
class TextInput
{
public:
    void Clear();
    void SetText(const std::string& value);
    const std::string& Text() const
    {
        return text_;
    }
    bool Empty() const
    {
        return text_.empty();
    }
    std::size_t Cursor() const
    {
        return cursor_;
    }
    // Text before the caret, used to measure where the caret is drawn.
    std::string BeforeCursor() const;
    void SetMaxLength(std::size_t value)
    {
        maxLength_ = value;
    }

    // Applies one frame of input. `entered` is UTF-8, already filtered for
    // control characters by Input. Returns true when the text or caret moved.
    bool Process(const std::string& entered, bool backspace, bool deleteKey, bool left, bool right);

private:
    static std::size_t PreviousBoundary(const std::string& text, std::size_t at);
    static std::size_t NextBoundary(const std::string& text, std::size_t at);

    std::string text_;
    std::size_t cursor_ = 0;
    std::size_t maxLength_ = 96;
};

} // namespace Fem
