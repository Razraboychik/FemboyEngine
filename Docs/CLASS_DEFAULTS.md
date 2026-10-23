# Reflected fields and class defaults

FemboyEngine has one metadata path shared by the editor, scenes, prefabs and
game code. `Mirror` registrations describe both serialization and editing.

## Field metadata

```cpp
Mirror::Instance()
    .Component<HealthComponent>("HealthComponent", "health")
    .AddField("hp", &HealthComponent::hp,
              FieldOptions()
                  .Display("Health")
                  .Category("Stats")
                  .Tooltip("0 means dead.")
                  .Range(0.f, 10000.f, 1.f)
                  .Edit(EditFlags::EditAnywhere))
    .AddField("team", &HealthComponent::team,
              FieldOptions().Display("Team").Enum({{"Neutral", 0}, {"Player", 1}, {"Enemy", 2}}))
    .AddField("portrait", &HealthComponent::portraitId,
              FieldOptions().Display("Portrait").Asset("texture"))
    .Done();
```

The first `AddField` argument remains the JSON key. Existing scenes, prefabs and
`FieldKind` values keep their meaning; new kinds (`Enum`, `Asset`) are appended
and only change the widget.

`Done()` captures each field's default from a default-constructed component.
An explicit `FieldOptions().Default(value)` wins. The editor uses that value for
"Reset" and the project-defaults panel.

`EditFlags::VisibleAnywhere` is shown but read-only.
`EditFlags::EditDefaultsOnly` is editable in the Class Defaults panel and
read-only on an instance. `EditFlags::NoEdit` is hidden from the value loop.

## Resolution order

```
C++ field default -> ClassDefaults -> prefab parent -> prefab override -> instance
```

`ClassDefaults` stores only fields a designer changed:

```json
{
  "health": { "hp": 250, "team": 2 },
  "sprite": { "r": 255, "g": 80, "b": 160, "a": 255 }
}
```

Add, instantiate and load all go through `ClassDefaults::ApplyTo()`. The editor
panel uses `Projects/<Project>/class_defaults.json` by default. The file is
optional; missing files are ignored on startup.

Flattened fields (currently `SpriteComponent::tint`) keep their historical
top-level `r/g/b/a` shape. Class defaults use the same shape, so old scenes do
not need migration.
