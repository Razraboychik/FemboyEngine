// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
//
// Text measurement, fitting and script handling.
//
// A draw list that only stores strings cannot know how wide a label will be;
// guessing (characters * size * 0.5) falls apart the moment the text is not
// English. These helpers ask the actual sf::Font, so a button can shrink a
// German label or wrap a Chinese one without spilling outside its panel.
//
// They also provide the two script services a UI needs to survive non-Latin
// languages:
//   * UTF-8 decoding into sf::String (SFML's std::string constructor is ANSI),
//   * right-to-left detection plus a compact Arabic shaper, because SFML does
//     not shape Arabic itself and unshaped letters render disconnected.
#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

namespace Fem::Text
{

// Decodes UTF-8 into an SFML string. Invalid bytes degrade to '?' instead of
// throwing, so a broken translation file cannot crash the renderer.
sf::String FromUtf8(const std::string& value);

// UTF-8 with a UTF-8 BOM and a leading "lang:" marker stripped, useful when a
// translation file is pasted with a byte-order mark.
std::string CleanUtf8(const std::string& value);

// True when the first strong character is Hebrew or Arabic.
bool IsRightToLeft(const std::string& value);

// Reorders an RTL paragraph for display: RTL runs are reversed and moved to the
// right, LTR runs (numbers, Latin words) keep their order. This is the "level
// 1" bidi algorithm - enough for UI strings, not for mixed poetry.
std::string ToVisualOrder(const std::string& value);

// Replaces Arabic letters with their contextual presentation forms so SFML
// renders connected script. Latin text passes through untouched.
std::string ShapeArabic(const std::string& value);

// shapeArabic + toVisualOrder, skipped when the text is already LTR.
std::string PrepareForDisplay(const std::string& value);

// Width of `value` in pixels at `size`, using the real glyph advances of the
// font. The returned value includes the left bearing, so centring with it is
// exact.
float Measure(const std::string& value, const std::string& fontId, unsigned size);

// The largest character size <= preferred that keeps the line within maxWidth.
// Never goes below minSize; the caller can then ellipsize.
unsigned FitSize(const std::string& value, const std::string& fontId, unsigned preferred, float maxWidth,
                 unsigned minSize = 9);

// Greedy word wrap. Words longer than maxWidth are split by code point, which
// is what makes Chinese and Japanese (no spaces) wrap at all.
std::vector<std::string> Wrap(const std::string& value, const std::string& fontId, unsigned size, float maxWidth);

// Shortens `value` with "..." until it fits (or returns an ellipsis alone).
std::string Ellipsize(const std::string& value, const std::string& fontId, unsigned size, float maxWidth);

// fitSize + ellipsize, the usual "label in a fixed box" combination.
std::string FitOrEllipsize(const std::string& value, const std::string& fontId, unsigned size, float maxWidth);

} // namespace Fem::Text
