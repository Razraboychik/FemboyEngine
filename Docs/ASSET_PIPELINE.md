# Asset pipeline

`AssetDatabase`, `AtlasPacker` and `AssetCooker` live in
`FemboyEngine/AssetPipeline.hpp`. They are CPU-side and do not need a window or a
GL context, so the editor and a build script can run the same code.

## Manifest forms

The original `AssetManager` manifest keeps working:

```json
{
  "textures": { "player": "art/player.png" },
  "fonts": { "default": "C:/Windows/Fonts/segoeui.ttf" },
  "sounds": { "jump": "audio/jump.wav" },
  "music": { "menu": "audio/menu.ogg" }
}
```

The pipeline form adds import settings and dependencies:

```json
{
  "assets": {
    "player": {
      "type": "texture",
      "path": "art/player.png",
      "atlas": "actors",
      "smooth": false,
      "padding": 2
    },
    "ui_frame": {
      "type": "texture",
      "path": "art/ui/frame.png",
      "atlas": "ui",
      "trim": true
    },
    "menu_theme": {
      "type": "music",
      "path": "audio/menu.ogg"
    }
  },
  "dependencies": {
    "ui_frame": ["player"]
  }
}
```

`import` overrides can attach pipeline settings to a legacy manifest:

```json
{
  "textures": { "player": "art/player.png" },
  "import": {
    "player": { "atlas": "actors", "premultiplyAlpha": true }
  }
}
```

Paths are relative to the manifest directory. Supported imports:

| Field | Meaning |
| --- | --- |
| `atlas` | Non-empty groups textures into shared pages. |
| `smooth` | Filtering metadata written into the cooked manifest. |
| `premultiplyAlpha` | Multiply RGB by alpha before upload. |
| `powerOfTwo` | Round each dimension up to a power of two. |
| `maxSize` | Downscale so the largest dimension is at most this. |
| `padding` | Atlas gutter in pixels. |
| `trim` | Trim fully transparent atlas borders and keep the offset. |
| `output` | Optional cooked path override. |

Unknown fields are preserved in `extra`, so a project can carry its own importer
options through a save.

## Validation and order

`validate()` reports missing source files, duplicate ids, unknown dependency ids
and cycles. `topologicalOrder()` returns dependencies before dependants. The
cooker refuses to write when validation fails.

`stale(outputDir)` compares each source file's FNV-1a 64 hash with the
`cooked.json` written by the previous cook. An editor can show only the assets
that need re-cooking without decoding them.

## Cooking

```cpp
Fem::AssetDatabase database;
database.load("assets/manifest.json");

std::vector<std::string> errors;
if (!database.validate(&errors)) {
    // report errors
}

Fem::CookOptions options;
options.outputDir = "cooked";
options.createPack = true;

Fem::CookResult result = Fem::AssetCooker::Cook(database, options);
```

The output directory contains:

- `manifest.json` вЂ” the same shape `AssetManager::LoadManifest()` consumes;
  atlas texture entries are objects with `path`, `rect`, `sourceSize`, `offset`
  and `smooth`.
- `cooked.json` вЂ” source hashes and kinds for incremental cooks.
- `atlases/<group>_<page>.png` вЂ” shared atlas pages.
- `fonts/`, `sounds/`, `music/`, `data/` вЂ” copied or transformed assets.
- `assets.fempak` вЂ” optional `ResourcePack` archive of every output file,
  including `manifest.json` and `cooked.json`. Entry paths are relative to the
  cook output directory, so extracting the archive reproduces the cooked tree.

The shelf packer sorts by height, then width, then id, so the same inputs always
produce the same pages. Page width grows from an area estimate up to
`AtlasOptions::maxSize`; a full page starts a new one.

## Runtime

`AssetManager::LoadManifest()` accepts the cooked object form:

```cpp
assets.LoadManifest("cooked/manifest.json");
const sf::IntRect region = assets.textureRect("player");
sf::Texture& page = assets.texture("player"); // shared atlas page
```

Every id mapped to the same atlas page shares one `sf::Texture`.
`textureRect()` returns the sub-rectangle for a sprite; for a standalone texture
it returns the full image size.

CPU decode can be moved off the main thread:

```cpp
assets.LoadTextureAsync("huge_background", "art/huge.png");
// while pending, texture() returns the placeholder
assets.PumpAsync(); // main thread: uploads completed images
```

`Engine::Run()` calls `PumpAsync()` once per frame. Tools that do not run the
engine must pump it themselves.

## Mounting a cooked pack

A shipped build can carry only `assets.fempak` and mount it at startup:

```cpp
Fem::AssetManager::Instance().LoadPackedManifest("assets.fempak", "cache");
```

`LoadPackedManifest()` extracts the archive into a per-build subdirectory of the
given cache directory (the system temporary directory when the argument is
empty) and then loads the cooked manifest from there. The cache key uses the
pack size and timestamp, so rebuilding the pack never reuses stale extracted
files. The extracted tree is exactly the one described by the manifest, which is
why `textureRect()`, atlas pages, fonts and sounds keep working without a second
resource abstraction.

## Command line

The editor exposes the same code without opening a window:

```text
FemboyEditor --validate-assets path/to/manifest.json
FemboyEditor --cook-assets path/to/manifest.json [output-dir]
```

`--validate-assets` exits with `0` when the graph is valid and `1` when
validation fails; missing files, duplicate ids, unknown dependencies and cycles
are printed. `--cook-assets` additionally writes the cooked tree and
`assets.fempak`; with no output directory it uses `cooked-assets`. Both commands
are suitable for CI and packaging scripts.
