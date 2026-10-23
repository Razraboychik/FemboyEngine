// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/TextLayout.hpp"

#include "FemboyEngine/Assets.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Fem::Text
{

namespace
{
// UTF-8

std::u32string Decode(const std::string& value)
{
    std::u32string out;
    out.reserve(value.size());
    std::size_t i = 0;
    while (i < value.size())
    {
        const auto byte = static_cast<unsigned char>(value[i]);
        char32_t cp = 0;
        int extra = 0;
        if (byte < 0x80)
        {
            cp = byte;
        }
        else if ((byte & 0xE0) == 0xC0)
        {
            cp = byte & 0x1F;
            extra = 1;
        }
        else if ((byte & 0xF0) == 0xE0)
        {
            cp = byte & 0x0F;
            extra = 2;
        }
        else if ((byte & 0xF8) == 0xF0)
        {
            cp = byte & 0x07;
            extra = 3;
        }
        else
        {
            out.push_back(U'?');
            ++i;
            continue;
        }

        if (i + static_cast<std::size_t>(extra) >= value.size() + 0 && extra > 0 &&
            i + static_cast<std::size_t>(extra) >= value.size())
        {
            out.push_back(U'?');
            break;
        }

        bool valid = true;
        for (int k = 1; k <= extra; ++k)
        {
            const auto next = static_cast<unsigned char>(value[i + static_cast<std::size_t>(k)]);
            if ((next & 0xC0) != 0x80)
            {
                valid = false;
                break;
            }
            cp = (cp << 6) | (next & 0x3F);
        }
        if (!valid)
        {
            out.push_back(U'?');
            ++i;
            continue;
        }
        out.push_back(cp);
        i += static_cast<std::size_t>(extra) + 1;
    }
    return out;
}

std::string Encode(const std::u32string& value)
{
    std::string out;
    out.reserve(value.size() * 2);
    for (const char32_t cp : value)
    {
        if (cp < 0x80)
        {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// Script helpers

bool IsArabicBase(char32_t cp)
{
    return (cp >= 0x0621 && cp <= 0x064A) || (cp >= 0x0660 && cp <= 0x0669);
}

bool IsRtlCodepoint(char32_t cp)
{
    return (cp >= 0x0590 && cp <= 0x05FF) || // Hebrew
           (cp >= 0x0600 && cp <= 0x06FF) || // Arabic
           (cp >= 0x0700 && cp <= 0x074F) || // Syriac
           (cp >= 0x0750 && cp <= 0x077F) || // Arabic supplement
           (cp >= 0x08A0 && cp <= 0x08FF) || // Arabic extended-A
           (cp >= 0xFB50 && cp <= 0xFDFF) || // Arabic presentation A
           (cp >= 0xFE70 && cp <= 0xFEFF);   // Arabic presentation B
}

bool IsNeutral(char32_t cp)
{
    return cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'.' || cp == U',' || cp == U':' || cp == U';' ||
           cp == U'!' || cp == U'?' || cp == U'-' || cp == U'(' || cp == U')' || cp == U'/' || cp == U'%' ||
           cp == U'+' || cp == U'*' || cp == U'"' || cp == U'\'' || cp == U'|' || cp == U'&';
}

// Arabic shaping table
// base, isolated, final, initial, medial. A zero form means "this letter does
// not take that shape"; final == 0 would mean it cannot connect to the previous
// letter, which no Arabic letter in the base block does.
struct ArabicLetter
{
    char32_t base;
    char32_t isolated;
    char32_t finalForm;
    char32_t initial;
    char32_t medial;
};

constexpr ArabicLetter kArabic[] = {
    {0x0621, 0xFE80, 0x0000, 0x0000, 0x0000}, // hamza
    {0x0622, 0xFE81, 0xFE82, 0x0000, 0x0000}, // alef with madda
    {0x0623, 0xFE83, 0xFE84, 0x0000, 0x0000}, // alef with hamza above
    {0x0624, 0xFE85, 0xFE86, 0x0000, 0x0000}, // waw with hamza
    {0x0625, 0xFE87, 0xFE88, 0x0000, 0x0000}, // alef with hamza below
    {0x0626, 0xFE89, 0xFE8A, 0xFE8B, 0xFE8C}, // yeh with hamza
    {0x0627, 0xFE8D, 0xFE8E, 0x0000, 0x0000}, // alef
    {0x0628, 0xFE8F, 0xFE90, 0xFE91, 0xFE92}, // beh
    {0x0629, 0xFE93, 0xFE94, 0x0000, 0x0000}, // teh marbuta
    {0x062A, 0xFE95, 0xFE96, 0xFE97, 0xFE98}, // teh
    {0x062B, 0xFE99, 0xFE9A, 0xFE9B, 0xFE9C}, // theh
    {0x062C, 0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0}, // jeem
    {0x062D, 0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4}, // hah
    {0x062E, 0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8}, // khah
    {0x062F, 0xFEA9, 0xFEAA, 0x0000, 0x0000}, // dal
    {0x0630, 0xFEAB, 0xFEAC, 0x0000, 0x0000}, // thal
    {0x0631, 0xFEAD, 0xFEAE, 0x0000, 0x0000}, // reh
    {0x0632, 0xFEAF, 0xFEB0, 0x0000, 0x0000}, // zain
    {0x0633, 0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4}, // seen
    {0x0634, 0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8}, // sheen
    {0x0635, 0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC}, // sad
    {0x0636, 0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0}, // dad
    {0x0637, 0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4}, // tah
    {0x0638, 0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8}, // zah
    {0x0639, 0xFEC9, 0xFECA, 0xFECB, 0xFECC}, // ain
    {0x063A, 0xFECD, 0xFECE, 0xFECF, 0xFED0}, // ghain
    {0x0641, 0xFED1, 0xFED2, 0xFED3, 0xFED4}, // feh
    {0x0642, 0xFED5, 0xFED6, 0xFED7, 0xFED8}, // qaf
    {0x0643, 0xFED9, 0xFEDA, 0xFEDB, 0xFEDC}, // kaf
    {0x0644, 0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0}, // lam
    {0x0645, 0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4}, // meem
    {0x0646, 0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8}, // noon
    {0x0647, 0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC}, // heh
    {0x0648, 0xFEED, 0xFEEE, 0x0000, 0x0000}, // waw
    {0x0649, 0xFEEF, 0xFEF0, 0x0000, 0x0000}, // alef maksura
    {0x064A, 0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4}, // yeh
};

const ArabicLetter* FindArabic(char32_t base)
{
    for (const ArabicLetter& letter : kArabic)
    {
        if (letter.base == base)
        {
            return &letter;
        }
    }
    return nullptr;
}

bool ConnectsForward(char32_t base)
{
    const ArabicLetter* letter = FindArabic(base);
    return letter != nullptr && letter->initial != 0;
}

bool ConnectsBackward(char32_t base)
{
    const ArabicLetter* letter = FindArabic(base);
    return letter != nullptr && letter->finalForm != 0;
}

// Lam + alef ligatures: (lam, alef variant) -> (isolated, final).
char32_t Ligature(char32_t base, char32_t next, bool connected)
{
    if (base != 0x0644)
    {
        return 0;
    }
    switch (next)
    {
        case 0x0622:
            return connected ? 0xFEF6 : 0xFEF5;
        case 0x0623:
            return connected ? 0xFEF8 : 0xFEF7;
        case 0x0625:
            return connected ? 0xFEFA : 0xFEF9;
        case 0x0627:
            return connected ? 0xFEFC : 0xFEFB;
        default:
            return 0;
    }
}

} // namespace

// UTF-8 / script

sf::String FromUtf8(const std::string& value)
{
    return sf::String::fromUtf8(value.begin(), value.end());
}

std::string CleanUtf8(const std::string& value)
{
    std::string out = value;
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF && static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF)
    {
        out.erase(0, 3);
    }
    return out;
}

bool IsRightToLeft(const std::string& value)
{
    for (const char32_t cp : Decode(value))
    {
        if (IsNeutral(cp) || cp == U'\n')
        {
            continue;
        }
        return IsRtlCodepoint(cp);
    }
    return false;
}

std::string ShapeArabic(const std::string& value)
{
    std::u32string cps = Decode(value);
    if (cps.empty())
    {
        return value;
    }

    bool hasArabic = false;
    for (const char32_t cp : cps)
    {
        if (IsArabicBase(cp) || cp == 0x0640)
        {
            hasArabic = true;
            break;
        }
    }
    if (!hasArabic)
    {
        return value;
    }

    std::u32string out;
    out.reserve(cps.size());

    for (std::size_t i = 0; i < cps.size(); ++i)
    {
        const char32_t cp = cps[i];
        if (!IsArabicBase(cp) || cp >= 0x0660)
        {
            out.push_back(cp);
            continue;
        }

        // Previous letter can connect to us?
        char32_t previous = 0;
        for (std::size_t k = i; k > 0; --k)
        {
            const char32_t candidate = cps[k - 1];
            if (IsArabicBase(candidate) && candidate < 0x0660)
            {
                previous = candidate;
                break;
            }
            if (!IsNeutral(candidate))
            {
                break;
            }
        }
        const bool connectedPrevious = previous != 0 && ConnectsForward(previous);

        // Next letter can connect from us?
        char32_t next = 0;
        std::size_t nextIndex = 0;
        for (std::size_t k = i + 1; k < cps.size(); ++k)
        {
            const char32_t candidate = cps[k];
            if (IsArabicBase(candidate) && candidate < 0x0660)
            {
                next = candidate;
                nextIndex = k;
                break;
            }
            if (!IsNeutral(candidate))
            {
                break;
            }
        }
        const bool connectedNext = next != 0 && ConnectsBackward(next) && ConnectsForward(cp);

        if (const char32_t lig = Ligature(cp, next, connectedPrevious))
        {
            out.push_back(lig);
            // Skip the alef we just consumed (leave any marks between alone).
            for (std::size_t k = nextIndex; k < cps.size(); ++k)
            {
                if (cps[k] == next)
                {
                    i = k;
                    break;
                }
            }
            continue;
        }

        const ArabicLetter* letter = FindArabic(cp);
        if (!letter)
        {
            out.push_back(cp);
            continue;
        }

        char32_t form = letter->isolated;
        if (connectedPrevious && connectedNext && letter->medial != 0)
        {
            form = letter->medial;
        }
        else if (connectedPrevious && letter->finalForm != 0)
        {
            form = letter->finalForm;
        }
        else if (connectedNext && letter->initial != 0)
        {
            form = letter->initial;
        }

        out.push_back(form);
    }

    return Encode(out);
}

std::string ToVisualOrder(const std::string& value)
{
    const std::u32string cps = Decode(value);
    if (cps.empty())
    {
        return value;
    }

    // Build directional runs. Neutrals attach to the run that is open, which is
    // what keeps "100%" next to the word it belongs to.
    struct Run
    {
        bool rtl = false;
        std::u32string text;
    };
    std::vector<Run> runs;
    bool haveRun = false;

    for (const char32_t cp : cps)
    {
        const bool rtl = IsRtlCodepoint(cp);
        const bool neutral = IsNeutral(cp);
        if (!haveRun || (!neutral && rtl != runs.back().rtl))
        {
            runs.push_back(Run{rtl, {}});
            haveRun = true;
        }
        runs.back().text.push_back(cp);
    }

    std::u32string out;
    out.reserve(cps.size());
    for (auto it = runs.rbegin(); it != runs.rend(); ++it)
    {
        if (it->rtl)
        {
            out.append(it->text.rbegin(), it->text.rend());
        }
        else
        {
            out.append(it->text);
        }
    }
    return Encode(out);
}

std::string PrepareForDisplay(const std::string& value)
{
    if (value.empty())
    {
        return value;
    }

    bool rtl = false;
    for (const char32_t cp : Decode(value))
    {
        if (IsRtlCodepoint(cp))
        {
            rtl = true;
            break;
        }
    }
    if (!rtl)
    {
        return value;
    }

    return ToVisualOrder(ShapeArabic(value));
}

// measurement

sf::Text MakeText(const std::string& value, const std::string& fontId, unsigned size)
{
    const sf::String string = FromUtf8(PrepareForDisplay(value));
    sf::Font& font = AssetManager::Instance().FontForText(fontId, string);
    return sf::Text(font, string, size);
}

float Measure(const std::string& value, const std::string& fontId, unsigned size)
{
    if (value.empty())
    {
        return 0.f;
    }

    // Wrapping and fitting call Measure once per word (and sometimes once per
    // code point). Building an sf::Text and asking for its bounds every frame
    // dominated menu frames. The strings are overwhelmingly static, so a
    // thread-local cache keeps the measurement path cheap without changing the
    // layout result.
    thread_local std::unordered_map<std::string, float> cache;
    std::string key;
    key.reserve(fontId.size() + value.size() + 12);
    key.append(fontId);
    key.push_back('\x1f');
    key.append(std::to_string(size));
    key.push_back('\x1f');
    key.append(value);

    if (const auto found = cache.find(key); found != cache.end())
    {
        return found->second;
    }

    sf::Text text = MakeText(value, fontId, size);
    const sf::FloatRect bounds = text.getLocalBounds();
    const float measured = bounds.position.x + bounds.size.x;
    if (cache.size() >= 8192)
    {
        cache.clear();
    }
    cache.emplace(std::move(key), measured);
    return measured;
}

unsigned FitSize(const std::string& value, const std::string& fontId, unsigned preferred, float maxWidth,
                 unsigned minSize)
{
    if (maxWidth <= 0.f || value.empty())
    {
        return preferred;
    }
    unsigned size = std::max(preferred, minSize);
    while (size > minSize && Measure(value, fontId, size) > maxWidth)
    {
        --size;
    }
    return size;
}

std::vector<std::string> Wrap(const std::string& value, const std::string& fontId, unsigned size, float maxWidth)
{
    std::vector<std::string> lines;
    if (value.empty() || maxWidth <= 0.f)
    {
        lines.push_back(value);
        return lines;
    }

    std::string line;
    std::string word;

    const auto flushWord = [&]()
    {
        if (word.empty())
        {
            return;
        }
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (Measure(candidate, fontId, size) <= maxWidth)
        {
            line = candidate;
        }
        else
        {
            if (!line.empty())
            {
                lines.push_back(line);
            }
            // A single word wider than the box: break it by code point.
            std::u32string remainder = Decode(word);
            std::u32string piece;
            while (!remainder.empty())
            {
                piece.push_back(remainder.front());
                remainder.erase(remainder.begin());
                if (Measure(Encode(piece), fontId, size) > maxWidth && piece.size() > 1)
                {
                    remainder.insert(remainder.begin(), piece.back());
                    piece.pop_back();
                    lines.push_back(Encode(piece));
                    piece.clear();
                }
            }
            line = Encode(piece);
        }
        word.clear();
    };

    for (const char ch : value)
    {
        if (ch == ' ' || ch == '\n')
        {
            flushWord();
            if (ch == '\n')
            {
                lines.push_back(line);
                line.clear();
            }
        }
        else
        {
            word.push_back(ch);
        }
    }
    flushWord();
    if (!line.empty())
    {
        lines.push_back(line);
    }
    if (lines.empty())
    {
        lines.push_back(std::string());
    }
    return lines;
}

std::string Ellipsize(const std::string& value, const std::string& fontId, unsigned size, float maxWidth)
{
    if (Measure(value, fontId, size) <= maxWidth)
    {
        return value;
    }

    const std::string dots = "...";
    std::u32string cps = Decode(value);
    while (!cps.empty())
    {
        cps.pop_back();
        const std::string candidate = Encode(cps) + dots;
        if (Measure(candidate, fontId, size) <= maxWidth)
        {
            return candidate;
        }
    }
    return dots;
}

std::string FitOrEllipsize(const std::string& value, const std::string& fontId, unsigned size, float maxWidth)
{
    if (Measure(value, fontId, size) <= maxWidth)
    {
        return value;
    }
    return Ellipsize(value, fontId, size, maxWidth);
}

} // namespace Fem::Text
