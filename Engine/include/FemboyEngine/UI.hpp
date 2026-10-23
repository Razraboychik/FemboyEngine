// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <nlohmann/json.hpp>
#include <SFML/Graphics.hpp>
#include <imgui.h>
#include "FemboyEngine/Core.hpp"

namespace Fem
{

class DrawList;

// Pink: UE5-like widget builder. UI defined as JSON trees, assembled at runtime.
// Widgets are composable elements: Text, Button, Image, Panel, Slider, etc.
struct UIWidget
{
    std::string id;
    std::string type; // "Text","Button","Image","Panel","Slider","Canvas",...
    std::map<std::string, nlohmann::json> props;
    std::vector<std::shared_ptr<UIWidget>> children;

    // runtime state
    bool visible = true;
    sf::FloatRect rect{};
    // Filled by UpdateLayout(): absolute screen-space rectangle.
    sf::FloatRect screenRect{};

    // Container state. ScrollBox keeps a vertical offset in design units;
    // WidgetSwitcher keeps the index of the child it shows.
    float scroll = 0.f;
    int index = 0;

    // Convenience property readers with defaults.
    float PropFloat(const std::string& key, float def = 0.f) const;
    int PropInt(const std::string& key, int def = 0) const;
    std::string PropString(const std::string& key, const std::string& def = "") const;
    bool PropBool(const std::string& key, bool def = false) const;
    sf::Color PropColor(const std::string& key, const sf::Color& def = sf::Color::White) const;
};

class PinkUI
{
public:
    static PinkUI& Instance();

    std::shared_ptr<UIWidget> LoadFromJson(const nlohmann::json& j);
    std::shared_ptr<UIWidget> LoadFromFile(const std::string& path);

    // Computes rects for the whole tree. Call after loading and when the window
    // resizes. Children are laid out vertically inside their parent panel.
    void UpdateLayout(const std::shared_ptr<UIWidget>& root, const sf::FloatRect& viewport);
    // Sets the viewport and re-lays out the last tree passed to UpdateLayout()
    // (used by Engine on resize).
    void SetViewport(const sf::FloatRect& viewport);
    const sf::FloatRect& Viewport() const
    {
        return viewport_;
    }
    void Relayout();

    void Render(const std::shared_ptr<UIWidget>& root, sf::RenderWindow& window);
    // Single-draw-list path: submits UI widgets on DrawLayer::UI instead of
    // drawing directly to the window.
    void Render(const std::shared_ptr<UIWidget>& root, DrawList& FrameDrawList);
    void RenderImGui(const std::shared_ptr<UIWidget>& root); // editor preview

    // injection: inject widgets/properties at runtime (mods)
    bool InjectWidget(const std::string& parentId, std::shared_ptr<UIWidget> w, const std::shared_ptr<UIWidget>& root);
    std::shared_ptr<UIWidget> FindById(const std::shared_ptr<UIWidget>& root, const std::string& id);

    // ScrollBox and WidgetSwitcher controls. Both are no-ops when the id is not
    // found, so a UI file can change shape without breaking the caller.
    bool ScrollBy(const std::shared_ptr<UIWidget>& root, const std::string& id, float delta);
    bool ScrollTo(const std::shared_ptr<UIWidget>& root, const std::string& id, float offset);
    bool SetSwitch(const std::shared_ptr<UIWidget>& root, const std::string& id, int index);

    // Optional font used for text widgets; falls back to the asset manager.
    void SetFont(const std::string& fontId)
    {
        fontId_ = fontId;
    }

    // UI scale, applied to every laid-out rect. Values outside [0.5, 4] are
    // clamped by Accessibility::SetUiScale().
    void SetScale(float scale)
    {
        scale_ = (std::isfinite(scale) && scale > 0.f) ? scale : 1.f;
    }
    float Scale() const
    {
        return scale_;
    }

private:
    void RenderWidget(const std::shared_ptr<UIWidget>& w, sf::RenderWindow& window);
    void RenderWidgetList(const std::shared_ptr<UIWidget>& w, DrawList& FrameDrawList, int& z,
                           const sf::FloatRect* clip = nullptr);
    void LayoutWidget(const std::shared_ptr<UIWidget>& w, const sf::FloatRect& parentRect);
    void PlaceWidget(const std::shared_ptr<UIWidget>& w, const sf::FloatRect& rect);
    void LayoutChildren(const std::shared_ptr<UIWidget>& w, const sf::FloatRect& rect);
    void RenderImGuiWidget(const std::shared_ptr<UIWidget>& w);

    std::string fontId_ = "default";
    float scale_ = 1.f;
    sf::FloatRect viewport_{sf::Vector2f{0.f, 0.f}, sf::Vector2f{1280.f, 720.f}};
    std::weak_ptr<UIWidget> root_;
};

} // namespace Fem
