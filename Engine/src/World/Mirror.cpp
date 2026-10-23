// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Mirror.hpp"

namespace Fem
{

Mirror& Mirror::Instance()
{
    static Mirror m;
    return m;
}

Mirror::Mirror()
{
    // Built-in components. Games register their own with the same builder, e.g.
    //
    //   Mirror::Instance()
    //       .component<HealthComponent>("HealthComponent", "health")
    //       .AddField("hp", &HealthComponent::hp)
    //       .AddField("maxHp", &HealthComponent::maxHp)
    //       .Done();
    //
    // after which Scene, Prefab and the inspector see those fields automatically.
    Component<TransformComponent>("TransformComponent", "transform")
        .AddField("x", &TransformComponent::x, FieldOptions().Display("X").Category("Transform"))
        .AddField("y", &TransformComponent::y, FieldOptions().Display("Y").Category("Transform"))
        .AddField("sx", &TransformComponent::scaleX,
                  FieldOptions().Display("Scale X").Category("Transform").Range(-100.f, 100.f, 0.01f))
        .AddField("sy", &TransformComponent::scaleY,
                  FieldOptions().Display("Scale Y").Category("Transform").Range(-100.f, 100.f, 0.01f))
        .AddField("rot", &TransformComponent::rotation,
                  FieldOptions().Display("Rotation").Category("Transform").Tooltip("Degrees, clockwise."))
        .Done();

    Component<SpriteComponent>("SpriteComponent", "sprite")
        .AddField("texture", &SpriteComponent::textureId,
                  FieldOptions().Display("Texture").Category("Sprite").Asset("texture"))
        .AddField("z", &SpriteComponent::z,
                  FieldOptions().Display("Depth").Category("Sprite")
                      .Tooltip("Fractional draw depth inside the Entities layer.")
                      .Range(-100000.f, 100000.f, 0.01f))
        // Flat r/g/b/a, not a nested "tint" object: existing scene files use
        // the flat layout and must keep loading.
        .AddField("tint", &SpriteComponent::tint,
                  FieldOptions().Display("Tint").Category("Sprite").Flattened())
        .AddField("srcX", &SpriteComponent::srcX,
                  FieldOptions().Display("Source X").Category("Sprite Sheet").Advanced())
        .AddField("srcY", &SpriteComponent::srcY,
                  FieldOptions().Display("Source Y").Category("Sprite Sheet").Advanced())
        .AddField("srcW", &SpriteComponent::srcW,
                  FieldOptions().Display("Source W").Category("Sprite Sheet").Advanced())
        .AddField("srcH", &SpriteComponent::srcH,
                  FieldOptions().Display("Source H").Category("Sprite Sheet").Advanced())
        .AddField("originX", &SpriteComponent::originX,
                  FieldOptions().Display("Pivot X").Category("Sprite").Range(0.f, 1.f, 0.01f))
        .AddField("originY", &SpriteComponent::originY,
                  FieldOptions().Display("Pivot Y").Category("Sprite").Range(0.f, 1.f, 0.01f))
        .AddField("flipX", &SpriteComponent::flipX, FieldOptions().Display("Flip X").Category("Sprite"))
        .AddField("flipY", &SpriteComponent::flipY, FieldOptions().Display("Flip Y").Category("Sprite"))
        .Done();

    // A marker/owner component for an Animator. Clips themselves are runtime
    // data (loaded from assets or registered by the game), so the component has
    // no serialized fields of its own.
    Component<AnimationComponent>("AnimationComponent", "animation").Done();

    // Tag marshals as a bare string, matching the on-disk format.
    Component<TagComponent>("TagComponent", "tag", /*scalar=*/true).AddField("tag", &TagComponent::tag).Done();

    // The tag set is an object with a "tags" array of names, so a scene keeps its
    // classification and the editor inspector can show it. It coexists with the
    // single-string TagComponent above, which keeps old scenes loading.
    Component<GameplayTags>("GameplayTags", "gameplayTags").AddField("tags", &GameplayTags::tags).Done();

    // Scene organization: parent/child and persistent named groups. The parent
    // field is read-only in the inspector; the Hierarchy owns the UX for it.
    Component<HierarchyComponent>("HierarchyComponent", "hierarchy")
        .AddField("parent", &HierarchyComponent::parent,
                  FieldOptions().Display("Parent").Category("Hierarchy").Advanced().ReadOnly())
        .AddField("expanded", &HierarchyComponent::expanded,
                  FieldOptions().Display("Expanded").Category("Hierarchy").Advanced())
        .Done();
    Component<GroupComponent>("GroupComponent", "group")
        .AddField("group", &GroupComponent::group,
                  FieldOptions().Display("Group").Category("Hierarchy")
                      .Tooltip("Named editor collection. Use it to select and transform related objects together."))
        .Done();
}

void Mirror::Add(ComponentReflection component)
{
    // Re-registering the same key (hot reload of a game module) replaces the
    // old description instead of producing duplicates.
    for (auto& existing : components_)
    {
        if (existing.jsonKey == component.jsonKey)
        {
            existing = std::move(component);
            return;
        }
    }
    components_.push_back(std::move(component));
}

const ComponentReflection* Mirror::Find(const std::string& nameOrKey) const
{
    for (const auto& c : components_)
    {
        if (c.jsonKey == nameOrKey || c.name == nameOrKey)
        {
            return &c;
        }
    }
    return nullptr;
}

void Mirror::Clear()
{
    components_.clear();
}

} // namespace Fem
