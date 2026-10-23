// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/UI.hpp"
#include "FemboyEngine/Assets.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Skirt.hpp"
#include <fstream>
#include <algorithm>
#include <cmath>

namespace Fem
{

namespace
{
bool JsonNumber(const nlohmann::json& j, float& out)
{
    if (j.is_number())
    {
        out = j.get<float>();
        return true;
    }
    if (j.is_array() && j.size() == 2 && j[0].is_number() && j[1].is_number())
    {
        out = j[0].get<float>();
        return true;
    }
    return false;
}

sf::FloatRect Intersection(const sf::FloatRect& a, const sf::FloatRect& b)
{
    const float x0 = std::max(a.position.x, b.position.x);
    const float y0 = std::max(a.position.y, b.position.y);
    const float x1 = std::min(a.position.x + a.size.x, b.position.x + b.size.x);
    const float y1 = std::min(a.position.y + a.size.y, b.position.y + b.size.y);
    return sf::FloatRect({x0, y0}, {std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)});
}
} // namespace

float UIWidget::PropFloat(const std::string& key, float def) const
{
    auto it = props.find(key);
    if (it == props.end())
    {
        return def;
    }
    float v = def;
    return JsonNumber(it->second, v) ? v : def;
}

int UIWidget::PropInt(const std::string& key, int def) const
{
    auto it = props.find(key);
    if (it == props.end())
    {
        return def;
    }
    if (!it->second.is_number())
    {
        return def;
    }
    // Read as double and clamp: an out-of-range or huge unsigned value would
    // otherwise overflow the int conversion.
    const double d = it->second.get<double>();
    if (!std::isfinite(d))
    {
        return def;
    }
    return static_cast<int>(std::clamp(d, -2147483000.0, 2147483000.0));
}

std::string UIWidget::PropString(const std::string& key, const std::string& def) const
{
    auto it = props.find(key);
    if (it == props.end() || !it->second.is_string())
    {
        return def;
    }
    return it->second.get<std::string>();
}

bool UIWidget::PropBool(const std::string& key, bool def) const
{
    auto it = props.find(key);
    if (it == props.end() || !it->second.is_boolean())
    {
        return def;
    }
    return it->second.get<bool>();
}

sf::Color UIWidget::PropColor(const std::string& key, const sf::Color& def) const
{
    auto it = props.find(key);
    if (it == props.end())
    {
        return def;
    }
    const auto& j = it->second;
    if (j.is_string())
    {
        const std::string s = j.get<std::string>();
        if (s.size() == 7 && s[0] == '#')
        {
            try
            {
                const unsigned long v = std::stoul(s.substr(1), nullptr, 16);
                return sf::Color(static_cast<std::uint8_t>((v >> 16) & 0xFF),
                                 static_cast<std::uint8_t>((v >> 8) & 0xFF), static_cast<std::uint8_t>(v & 0xFF));
            }
            catch (...)
            {
                return def;
            }
        }
        return def;
    }
    if (j.is_array() && j.size() >= 3)
    {
        const auto c = [&](size_t i) -> std::uint8_t
        {
            if (!j[i].is_number())
            {
                return 255;
            }
            const double d = j[i].get<double>();
            if (!std::isfinite(d))
            {
                return 255;
            }
            return static_cast<std::uint8_t>(std::clamp(d, 0.0, 255.0));
        };
        return sf::Color(c(0), c(1), c(2), j.size() >= 4 ? c(3) : 255);
    }
    return def;
}

PinkUI& PinkUI::Instance()
{
    static PinkUI p;
    return p;
}

std::shared_ptr<UIWidget> PinkUI::LoadFromJson(const nlohmann::json& j)
{
    if (!j.is_object())
    {
        BOY_ERROR("PinkUI", "widget JSON must be an object");
        return nullptr;
    }

    auto w = std::make_shared<UIWidget>();
    // json::value() throws a type_error when the stored value has the wrong
    // type, so a malformed UI file used to take the whole engine down. Read
    // defensively instead.
    if (j.contains("id") && j["id"].is_string())
    {
        w->id = j["id"].get<std::string>();
    }
    if (j.contains("type") && j["type"].is_string())
    {
        w->type = j["type"].get<std::string>();
    }
    if (j.contains("visible") && j["visible"].is_boolean())
    {
        w->visible = j["visible"].get<bool>();
    }

    if (j.contains("props") && j["props"].is_object())
    {
        try
        {
            w->props = j["props"].get<std::map<std::string, nlohmann::json>>();
        }
        catch (const std::exception& e)
        {
            BOY_WARN("PinkUI", std::string("props parse error: ") + e.what());
        }
    }

    // Geometry may live at the top level or inside props.
    const auto readNumber = [](const nlohmann::json& src, const char* key, float& out)
    {
        if (!src.is_object() || !src.contains(key))
        {
            return;
        }
        float v = 0.f;
        if (JsonNumber(src[key], v))
        {
            out = v;
        }
    };
    const auto readRect = [&](const nlohmann::json& src)
    {
        readNumber(src, "x", w->rect.position.x);
        readNumber(src, "y", w->rect.position.y);
        readNumber(src, "w", w->rect.size.x);
        if (w->rect.size.x == 0.f)
        {
            readNumber(src, "width", w->rect.size.x);
        }
        readNumber(src, "h", w->rect.size.y);
        if (w->rect.size.y == 0.f)
        {
            readNumber(src, "height", w->rect.size.y);
        }
    };
    readRect(j);
    if (j.contains("props") && j["props"].is_object())
    {
        readRect(j["props"]);
    }

    if (j.contains("children") && j["children"].is_array())
    {
        for (const auto& c : j["children"])
        {
            if (auto child = LoadFromJson(c))
            {
                w->children.push_back(child);
            }
        }
    }
    return w;
}

std::shared_ptr<UIWidget> PinkUI::LoadFromFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        BOY_ERROR("PinkUI", "cannot open: " + path);
        return nullptr;
    }

    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (const std::exception& e)
    {
        BOY_ERROR("PinkUI", std::string("parse error: ") + e.what());
        return nullptr;
    }

    auto root = LoadFromJson(j);
    if (root)
    {
        UpdateLayout(root, viewport_);
    }
    return root;
}

bool PinkUI::InjectWidget(const std::string& parentId, std::shared_ptr<UIWidget> w,
                          const std::shared_ptr<UIWidget>& root)
{
    if (!w)
    {
        return false;
    }

    if (!root)
    {
        BOY_WARN("PinkUI", "injectWidget: no root widget");
        return false;
    }

    auto parent = FindById(root, parentId);
    if (!parent)
    {
        BOY_WARN("PinkUI", "injectWidget: parent not found: " + parentId);
        return false;
    }

    parent->children.push_back(std::move(w));
    Relayout();
    return true;
}

std::shared_ptr<UIWidget> PinkUI::FindById(const std::shared_ptr<UIWidget>& root, const std::string& id)
{
    if (!root)
    {
        return nullptr;
    }
    if (root->id == id)
    {
        return root;
    }
    for (const auto& c : root->children)
    {
        if (auto r = FindById(c, id))
        {
            return r;
        }
    }
    return nullptr;
}

void PinkUI::LayoutWidget(const std::shared_ptr<UIWidget>& w, const sf::FloatRect& parentRect)
{
    if (!w)
    {
        return;
    }

    // Design units are converted to screen units exactly once per axis: offsets
    // and sizes use the same scale factor, and a "fill parent" widget simply
    // inherits the parent's already-scaled size. Scaling offsets and sizes
    // independently (the old behaviour) made children drift when UI scale != 1.
    sf::FloatRect rect;
    rect.position.x = parentRect.position.x + w->rect.position.x * scale_;
    rect.position.y = parentRect.position.y + w->rect.position.y * scale_;
    rect.size.x = (w->rect.size.x > 0.f) ? w->rect.size.x * scale_ : parentRect.size.x;
    rect.size.y = (w->rect.size.y > 0.f) ? w->rect.size.y * scale_ : parentRect.size.y;

    // A Spacer has no content, only the space it reserves.
    if (w->type == "Spacer")
    {
        const float size = w->PropFloat("size", 0.f) * scale_;
        rect.size.x = size;
        rect.size.y = size;
    }

    PlaceWidget(w, rect);
}

void PinkUI::PlaceWidget(const std::shared_ptr<UIWidget>& w, const sf::FloatRect& rect)
{
    if (!w)
    {
        return;
    }
    w->screenRect = rect;
    LayoutChildren(w, rect);
}

void PinkUI::LayoutChildren(const std::shared_ptr<UIWidget>& w, const sf::FloatRect& rect)
{
    const std::string& type = w->type;
    const float pad = std::max(0.f, w->PropFloat("padding", 0.f)) * scale_;
    const float gap = std::max(0.f, w->PropFloat("gap", 0.f)) * scale_;
    const sf::FloatRect inner({rect.position.x + pad, rect.position.y + pad},
                              {std::max(0.f, rect.size.x - pad * 2.f), std::max(0.f, rect.size.y - pad * 2.f)});

    // Absolute placement
    if (type == "Canvas" || type == "CanvasPanel")
    {
        for (auto& c : w->children)
        {
            if (c && c->visible)
            {
                LayoutWidget(c, inner);
            }
        }
        return;
    }

    // Stacked placement (every child fills the same rect)
    if (type == "Overlay")
    {
        for (auto& c : w->children)
        {
            if (c && c->visible)
            {
                LayoutWidget(c, inner);
            }
        }
        return;
    }

    // One visible child
    if (type == "WidgetSwitcher")
    {
        if (w->children.empty())
        {
            return;
        }
        if (w->index < 0)
        {
            w->index = 0;
        }
        if (w->index >= static_cast<int>(w->children.size()))
        {
            w->index = static_cast<int>(w->children.size()) - 1;
        }
        if (auto& c = w->children[static_cast<size_t>(w->index)])
        {
            LayoutWidget(c, inner);
        }
        return;
    }

    if (type == "Border" || type == "SizeBox")
    {
        for (auto& c : w->children)
        {
            if (c && c->visible)
            {
                LayoutWidget(c, inner);
            }
        }
        return;
    }

    // Grids
    if (type == "GridPanel" || type == "Grid")
    {
        const int columns = std::max(1, w->PropInt("columns", 2));
        int visibleCount = 0;
        for (const auto& c : w->children)
        {
            if (c && c->visible)
            {
                ++visibleCount;
            }
        }
        const int rows = std::max(1, (visibleCount + columns - 1) / columns);
        const float cellWidth = (inner.size.x - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
        float cellHeight = w->PropFloat("rowHeight", 0.f) * scale_;
        if (cellHeight <= 0.f)
        {
            cellHeight = (inner.size.y - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows);
        }

        int i = 0;
        for (auto& c : w->children)
        {
            if (!c || !c->visible)
            {
                continue;
            }
            const int row = i / columns;
            const int column = i % columns;
            PlaceWidget(c, {{inner.position.x + static_cast<float>(column) * (cellWidth + gap),
                             inner.position.y + static_cast<float>(row) * (cellHeight + gap)},
                            {cellWidth, cellHeight}});
            ++i;
        }
        return;
    }

    // Horizontal flow
    if (type == "HBox" || type == "HorizontalBox")
    {
        std::vector<std::shared_ptr<UIWidget>> kids;
        std::vector<float> fixed;
        std::vector<float> weight;
        float fixedSum = 0.f;
        float weightSum = 0.f;
        for (auto& c : w->children)
        {
            if (!c || !c->visible)
            {
                continue;
            }
            float width = c->rect.size.x > 0.f ? c->rect.size.x * scale_ : 0.f;
            if (c->type == "Spacer")
            {
                width = std::max(width, c->PropFloat("size", 0.f) * scale_);
            }
            kids.push_back(c);
            fixed.push_back(width);
            weight.push_back(width > 0.f ? 0.f : std::max(0.f, c->PropFloat("fill", 1.f)));
            fixedSum += width;
            weightSum += weight.back();
        }

        const float gapSum = gap * std::max(0.f, static_cast<float>(kids.size()) - 1.f);
        const float leftover = std::max(0.f, inner.size.x - gapSum - fixedSum);
        float x = inner.position.x;
        for (size_t i = 0; i < kids.size(); ++i)
        {
            const float width = fixed[i] + (weightSum > 0.f ? leftover * weight[i] / weightSum : 0.f);
            const float y = inner.position.y + kids[i]->rect.position.y * scale_;
            const float h = kids[i]->rect.size.y > 0.f ? kids[i]->rect.size.y * scale_ : inner.size.y;
            PlaceWidget(kids[i], {{x + kids[i]->rect.position.x * scale_, y}, {width, h}});
            x += width + gap;
        }
        return;
    }

    // Vertical flow: VBox, Panel (legacy spelling), ScrollBox
    if (type == "VBox" || type == "VerticalBox" || type == "Panel" || type == "ScrollBox" || type.empty())
    {
        const bool scroll = (type == "ScrollBox");
        std::vector<std::shared_ptr<UIWidget>> kids;
        std::vector<float> fixed;
        std::vector<float> weight;
        float fixedSum = 0.f;
        float weightSum = 0.f;
        const float naturalItem = std::max(0.f, w->PropFloat("itemHeight", 0.f)) * scale_;

        for (auto& c : w->children)
        {
            if (!c || !c->visible)
            {
                continue;
            }
            float height = c->rect.size.y > 0.f ? c->rect.size.y * scale_ : 0.f;
            if (c->type == "Spacer")
            {
                height = std::max(height, c->PropFloat("size", 0.f) * scale_);
            }
            // Inside a ScrollBox an unsized child still needs a row; outside it
            // shares the leftover space instead.
            if (scroll && height <= 0.f)
            {
                height = naturalItem > 0.f ? naturalItem : 28.f * scale_;
            }
            kids.push_back(c);
            fixed.push_back(height);
            weight.push_back(height > 0.f ? 0.f : std::max(0.f, c->PropFloat("fill", 1.f)));
            fixedSum += height;
            weightSum += weight.back();
        }

        const float gapSum = gap * std::max(0.f, static_cast<float>(kids.size()) - 1.f);
        const float contentHeight = fixedSum + gapSum;

        // The offset is stored in design units so a UI authored for 720p keeps
        // its scroll position when the window (and the UI scale) changes.
        if (scroll)
        {
            const float maxScroll = std::max(0.f, contentHeight - inner.size.y);
            const float maxScrollDesign = scale_ > 0.f ? maxScroll / scale_ : maxScroll;
            w->scroll = std::clamp(w->scroll, 0.f, maxScrollDesign);
        }

        const float leftover = scroll ? 0.f : std::max(0.f, inner.size.y - gapSum - fixedSum);
        float y = inner.position.y - (scroll ? w->scroll * scale_ : 0.f);
        for (size_t i = 0; i < kids.size(); ++i)
        {
            const float height = fixed[i] + (weightSum > 0.f ? leftover * weight[i] / weightSum : 0.f);
            const float x = inner.position.x + kids[i]->rect.position.x * scale_;
            const float width = kids[i]->rect.size.x > 0.f ? kids[i]->rect.size.x * scale_ : inner.size.x;
            // The child's own y is a nudge applied on top of the flow cursor,
            // exactly like the pre-container behaviour.
            PlaceWidget(kids[i], {{x, y + kids[i]->rect.position.y * scale_}, {width, height}});
            y += height + gap;
        }
        return;
    }

    // Unknown container: keep the old behaviour rather than leave children
    // stacked at the origin.
    for (auto& c : w->children)
    {
        if (c && c->visible)
        {
            LayoutWidget(c, inner);
        }
    }
}

bool PinkUI::ScrollBy(const std::shared_ptr<UIWidget>& root, const std::string& id, float delta)
{
    const auto widget = FindById(root, id);
    if (!widget || widget->type != "ScrollBox")
    {
        return false;
    }
    widget->scroll += delta;
    Relayout();
    return true;
}

bool PinkUI::ScrollTo(const std::shared_ptr<UIWidget>& root, const std::string& id, float offset)
{
    const auto widget = FindById(root, id);
    if (!widget || widget->type != "ScrollBox")
    {
        return false;
    }
    widget->scroll = offset;
    Relayout();
    return true;
}

bool PinkUI::SetSwitch(const std::shared_ptr<UIWidget>& root, const std::string& id, int index)
{
    const auto widget = FindById(root, id);
    if (!widget || widget->type != "WidgetSwitcher")
    {
        return false;
    }
    widget->index = index;
    Relayout();
    return true;
}

void PinkUI::UpdateLayout(const std::shared_ptr<UIWidget>& root, const sf::FloatRect& viewport)
{
    if (!root)
    {
        return;
    }

    viewport_ = viewport;
    root_ = root;

    // The root fills the viewport; its own rect describes the canvas, not an
    // offset. Children are laid out relative to the viewport.
    root->screenRect = viewport;
    LayoutChildren(root, viewport);
}

void PinkUI::SetViewport(const sf::FloatRect& viewport)
{
    viewport_ = viewport;
    Relayout();
}

void PinkUI::Relayout()
{
    if (auto root = root_.lock())
    {
        UpdateLayout(root, viewport_);
    }
}

void PinkUI::Render(const std::shared_ptr<UIWidget>& root, sf::RenderWindow& window)
{
    if (!root)
    {
        return;
    }
    RenderWidget(root, window);
}

void PinkUI::Render(const std::shared_ptr<UIWidget>& root, DrawList& FrameDrawList)
{
    if (!root)
    {
        return;
    }
    int z = 0;
    RenderWidgetList(root, FrameDrawList, z);
}

void PinkUI::RenderWidget(const std::shared_ptr<UIWidget>& w, sf::RenderWindow& window)
{
    if (!w || !w->visible)
    {
        return;
    }

    const sf::FloatRect& r = w->screenRect;

    // A negative or absurdly large font size would be converted to unsigned and
    // either render nothing or allocate huge glyph pages. The UI scale applies
    // to text too, otherwise scaled layouts would keep tiny labels.
    const auto fontSize = [&](int def)
    {
        const float scaled = static_cast<float>(w->PropInt("size", def)) * scale_;
        return static_cast<unsigned>(std::clamp(static_cast<int>(std::lround(scaled)), 1, 512));
    };

    if (w->type == "Panel" || w->type == "Canvas")
    {
        sf::RectangleShape shape({r.size.x, r.size.y});
        shape.setPosition({r.position.x, r.position.y});
        shape.setFillColor(w->PropColor("color", sf::Color(40, 44, 60, 200)));
        window.draw(shape);
    }
    else if (w->type == "Text")
    {
        sf::Font& font = AssetManager::Instance().Font(fontId_);
        sf::Text text(font, w->PropString("text", ""), fontSize(16));
        text.setPosition({r.position.x, r.position.y});
        text.setFillColor(w->PropColor("color", sf::Color::White));
        window.draw(text);
    }
    else if (w->type == "Button")
    {
        sf::RectangleShape shape({r.size.x, r.size.y});
        shape.setPosition({r.position.x, r.position.y});
        shape.setFillColor(w->PropColor("color", sf::Color(80, 120, 200)));
        window.draw(shape);

        sf::Font& font = AssetManager::Instance().Font(fontId_);
        sf::Text text(font, w->PropString("text", "Button"), fontSize(16));
        text.setPosition({r.position.x + 8.f, r.position.y + r.size.y * 0.25f});
        text.setFillColor(w->PropColor("textColor", sf::Color::White));
        window.draw(text);
    }
    else if (w->type == "Image")
    {
        const std::string textureId = w->PropString("texture", "");
        if (!textureId.empty())
        {
            sf::Sprite sp(AssetManager::Instance().Texture(textureId));
            sp.setPosition({r.position.x, r.position.y});
            const sf::Vector2u texSize = sp.getTexture().getSize();
            if (texSize.x > 0 && texSize.y > 0 && r.size.x > 0.f && r.size.y > 0.f)
            {
                sp.setScale({r.size.x / static_cast<float>(texSize.x), r.size.y / static_cast<float>(texSize.y)});
            }
            window.draw(sp);
        }
    }
    else if (w->type == "Slider")
    {
        const float value = std::clamp(w->PropFloat("value", 0.f), 0.f, 1.f);
        sf::RectangleShape back({r.size.x, r.size.y});
        back.setPosition({r.position.x, r.position.y});
        back.setFillColor(sf::Color(60, 60, 70));
        window.draw(back);

        sf::RectangleShape fill({r.size.x * value, r.size.y});
        fill.setPosition({r.position.x, r.position.y});
        fill.setFillColor(w->PropColor("color", sf::Color(220, 120, 200)));
        window.draw(fill);
    }
    else if (w->type == "Border" || w->type == "ScrollBox")
    {
        sf::RectangleShape shape({r.size.x, r.size.y});
        shape.setPosition({r.position.x, r.position.y});
        shape.setFillColor(w->PropColor("color", sf::Color(40, 44, 60, 180)));
        window.draw(shape);
    }
    else if (w->type == "ProgressBar")
    {
        const float value = std::clamp(w->PropFloat("value", 0.f), 0.f, 1.f);
        sf::RectangleShape back({r.size.x, r.size.y});
        back.setPosition({r.position.x, r.position.y});
        back.setFillColor(w->PropColor("backColor", sf::Color(30, 33, 44)));
        window.draw(back);

        sf::RectangleShape fill({r.size.x * value, r.size.y});
        fill.setPosition({r.position.x, r.position.y});
        fill.setFillColor(w->PropColor("color", sf::Color(120, 220, 160)));
        window.draw(fill);
    }
    else if (w->type == "CheckBox")
    {
        const float box = std::min(r.size.y, r.size.x);
        sf::RectangleShape shape({box, box});
        shape.setPosition({r.position.x, r.position.y});
        shape.setFillColor(w->PropColor("color", sf::Color(36, 40, 54)));
        window.draw(shape);
        if (w->PropBool("checked", false))
        {
            sf::RectangleShape tick({box * 0.55f, box * 0.55f});
            tick.setPosition({r.position.x + box * 0.225f, r.position.y + box * 0.225f});
            tick.setFillColor(w->PropColor("checkColor", sf::Color(220, 120, 200)));
            window.draw(tick);
        }
        sf::Font& font = AssetManager::Instance().Font(fontId_);
        sf::Text text(font, w->PropString("text", ""), fontSize(16));
        text.setPosition({r.position.x + box + 6.f * scale_, r.position.y + box * 0.5f - fontSize(16) * 0.6f});
        text.setFillColor(w->PropColor("textColor", sf::Color::White));
        window.draw(text);
    }
    else if (w->type == "ComboBox")
    {
        sf::RectangleShape shape({r.size.x, r.size.y});
        shape.setPosition({r.position.x, r.position.y});
        shape.setFillColor(w->PropColor("color", sf::Color(40, 44, 60)));
        window.draw(shape);
        sf::Font& font = AssetManager::Instance().Font(fontId_);
        sf::Text text(font, w->PropString("text", ""), fontSize(16));
        text.setPosition({r.position.x + 6.f * scale_, r.position.y + r.size.y * 0.5f - fontSize(16) * 0.6f});
        text.setFillColor(w->PropColor("textColor", sf::Color::White));
        window.draw(text);
    }

    if (w->type == "WidgetSwitcher")
    {
        if (!w->children.empty())
        {
            const int index = std::clamp(w->index, 0, static_cast<int>(w->children.size()) - 1);
            RenderWidget(w->children[static_cast<size_t>(index)], window);
        }
        return;
    }

    for (const auto& c : w->children)
    {
        RenderWidget(c, window);
    }
}

void PinkUI::RenderWidgetList(const std::shared_ptr<UIWidget>& w, DrawList& FrameDrawList, int& z, const sf::FloatRect* clip)
{
    if (!w || !w->visible)
    {
        return;
    }

    // ScrollBox clips its content. Skirt stores a scissor per submitted command,
    // so setting it around the draw calls is enough and the commands keep their
    // clip even though the list is sorted later.
    if (clip)
    {
        FrameDrawList.SetClip(*clip);
    }

    const sf::FloatRect& r = w->screenRect;
    const auto fontSize = [&](int def)
    {
        const float scaled = static_cast<float>(w->PropInt("size", def)) * scale_;
        return static_cast<unsigned>(std::clamp(static_cast<int>(std::lround(scaled)), 1, 512));
    };

    if (w->type == "Panel" || w->type == "Canvas" || w->type == "CanvasPanel" || w->type == "Border" ||
        w->type == "ScrollBox")
    {
        FrameDrawList.Rect(r, w->PropColor("color", sf::Color(40, 44, 60, 200)), sf::Color::Transparent,
                   DrawCommand::kNoThickness, z++, DrawLayer::UI);
    }
    else if (w->type == "Text")
    {
        FrameDrawList.Text(w->PropString("text", ""), fontId_, r.position, fontSize(16),
                   w->PropColor("color", sf::Color::White), z++, DrawLayer::UI);
    }
    else if (w->type == "Button")
    {
        FrameDrawList.Rect(r, w->PropColor("color", sf::Color(80, 120, 200)), sf::Color::Transparent, DrawCommand::kNoThickness,
                   z++, DrawLayer::UI);
        FrameDrawList.Text(w->PropString("text", "Button"), fontId_,
                   {r.position.x + 8.f * scale_, r.position.y + r.size.y * 0.25f}, fontSize(16),
                   w->PropColor("textColor", sf::Color::White), z++, DrawLayer::UI);
    }
    else if (w->type == "Image")
    {
        const std::string textureId = w->PropString("texture", "");
        if (!textureId.empty())
        {
            const sf::Vector2u texSize = AssetManager::Instance().Texture(textureId).getSize();
            if (texSize.x > 0 && texSize.y > 0 && r.size.x > 0.f && r.size.y > 0.f)
            {
                const sf::Vector2f spriteScale(r.size.x / static_cast<float>(texSize.x),
                                               r.size.y / static_cast<float>(texSize.y));
                FrameDrawList.Sprite(textureId, r.position, spriteScale, 0.f, sf::Color::White, z++, DrawLayer::UI,
                             sf::FloatRect{}, sf::Vector2f(0.f, 0.f));
            }
        }
    }
    else if (w->type == "Slider")
    {
        const float value = std::clamp(w->PropFloat("value", 0.f), 0.f, 1.f);
        FrameDrawList.Rect(r, sf::Color(60, 60, 70), sf::Color::Transparent, DrawCommand::kNoThickness, z++, DrawLayer::UI);
        FrameDrawList.Rect(sf::FloatRect(r.position, {r.size.x * value, r.size.y}),
                   w->PropColor("color", sf::Color(220, 120, 200)), sf::Color::Transparent, DrawCommand::kNoThickness,
                   z++, DrawLayer::UI);
    }
    else if (w->type == "ProgressBar")
    {
        const float value = std::clamp(w->PropFloat("value", 0.f), 0.f, 1.f);
        FrameDrawList.Rect(r, w->PropColor("backColor", sf::Color(30, 33, 44)), sf::Color::Transparent,
                   DrawCommand::kNoThickness, z++, DrawLayer::UI);
        FrameDrawList.Rect(sf::FloatRect(r.position, {r.size.x * value, r.size.y}),
                   w->PropColor("color", sf::Color(120, 220, 160)), sf::Color::Transparent, DrawCommand::kNoThickness,
                   z++, DrawLayer::UI);
    }
    else if (w->type == "CheckBox")
    {
        const float box = std::min(r.size.y, r.size.x);
        FrameDrawList.Rect({r.position, {box, box}}, w->PropColor("color", sf::Color(36, 40, 54)), sf::Color::Transparent,
                   DrawCommand::kNoThickness, z++, DrawLayer::UI);
        if (w->PropBool("checked", false))
        {
            FrameDrawList.Rect({r.position + sf::Vector2f(box * 0.225f, box * 0.225f), {box * 0.55f, box * 0.55f}},
                       w->PropColor("checkColor", sf::Color(220, 120, 200)), sf::Color::Transparent,
                       DrawCommand::kNoThickness, z++, DrawLayer::UI);
        }
        FrameDrawList.Text(
            w->PropString("text", ""), fontId_,
            {r.position.x + box + 6.f * scale_, r.position.y + box * 0.5f - static_cast<float>(fontSize(16)) * 0.6f},
            fontSize(16), w->PropColor("textColor", sf::Color::White), z++, DrawLayer::UI);
    }
    else if (w->type == "ComboBox")
    {
        FrameDrawList.Rect(r, w->PropColor("color", sf::Color(40, 44, 60)), sf::Color::Transparent, DrawCommand::kNoThickness,
                   z++, DrawLayer::UI);
        FrameDrawList.Text(
            w->PropString("text", ""), fontId_,
            {r.position.x + 6.f * scale_, r.position.y + r.size.y * 0.5f - static_cast<float>(fontSize(16)) * 0.6f},
            fontSize(16), w->PropColor("textColor", sf::Color::White), z++, DrawLayer::UI);
    }

    if (w->type == "WidgetSwitcher")
    {
        if (!w->children.empty())
        {
            const int index = std::clamp(w->index, 0, static_cast<int>(w->children.size()) - 1);
            RenderWidgetList(w->children[static_cast<size_t>(index)], FrameDrawList, z, clip);
        }
        if (clip)
        {
            FrameDrawList.ClearClip();
        }
        return;
    }

    // A ScrollBox hands its own rect down as the clip. Nested scroll boxes
    // intersect, so a list inside a panel never paints outside either.
    sf::FloatRect childClip{};
    const sf::FloatRect* nextClip = clip;
    if (w->type == "ScrollBox")
    {
        childClip = clip ? Intersection(*clip, r) : r;
        nextClip = &childClip;
    }

    for (const auto& c : w->children)
    {
        RenderWidgetList(c, FrameDrawList, z, nextClip);
    }

    if (clip)
    {
        FrameDrawList.ClearClip();
    }
}

void PinkUI::RenderImGuiWidget(const std::shared_ptr<UIWidget>& w)
{
    if (!w || !w->visible)
    {
        return;
    }

    const std::string label = (w->id.empty() ? w->type : w->id) + "##" + w->type;
    if (w->type == "Panel" || w->type == "Canvas")
    {
        if (ImGui::TreeNode(label.c_str()))
        {
            for (const auto& c : w->children)
            {
                RenderImGuiWidget(c);
            }
            ImGui::TreePop();
        }
        return;
    }

    if (w->type == "Text")
    {
        ImGui::TextUnformatted(w->PropString("text", "").c_str());
    }
    else if (w->type == "Button")
    {
        if (ImGui::Button(w->PropString("text", "Button").c_str()))
        {
            // Preview only; runtime callbacks are application-side.
        }
    }
    else if (w->type == "Slider")
    {
        float v = std::clamp(w->PropFloat("value", 0.f), 0.f, 1.f);
        ImGui::SliderFloat(label.c_str(), &v, 0.f, 1.f);
    }
    else if (w->type == "Image")
    {
        ImGui::Text("[Image: %s]", w->PropString("texture", "").c_str());
    }
    else
    {
        ImGui::Text("[%s]", w->type.c_str());
    }

    for (const auto& c : w->children)
    {
        RenderImGuiWidget(c);
    }
}

void PinkUI::RenderImGui(const std::shared_ptr<UIWidget>& root)
{
    if (!root)
    {
        return;
    }
    RenderImGuiWidget(root);
}

} // namespace Fem
