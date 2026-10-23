# Third-party notices

Neon Triangle ships as one executable with its assets. This file lists the
libraries that end up inside that executable and the licences they come with.
The full licence text of each one is in the matching folder under `libs/` in the
source tree.

## Linked into the shipped game

| Library | Licence | Copyright |
| --- | --- | --- |
| SFML 3.1 (Graphics, Window, System, Audio) | zlib | Laurent Gomila and contributors |
| miniaudio (microphone capture; playback stays on the SFML audio device) | public domain / MIT-0 | David Reid |
| Opus (voice-chat codec) | BSD-3-Clause | Xiph.Org Foundation and contributors |
| FreeType | FreeType License (FTL) or GPLv2 | The FreeType Project |
| HarfBuzz | MIT | HarfBuzz contributors |
| libogg, libvorbis | BSD-3-Clause | Xiph.Org Foundation |
| libFLAC | BSD-3-Clause | Xiph.Org Foundation |
| zlib | zlib | Jean-loup Gailly, Mark Adler |
| EnTT | MIT | Michele Caini |
| nlohmann/json | MIT | Niels Lohmann |
| ENet | MIT | Lee Salzman |
| Box2D v3 | MIT | Erin Catto |
| FlatBuffers | Apache 2.0 | Google LLC |
| Asio | Boost Software License 1.0 | Christopher M. Kohlhoff |
| stb (image, vorbis helpers) | public domain / MIT | Sean Barrett and contributors |
| Dear ImGui and ImGui-SFML | MIT | Omar Cornut, Elias Daler, contributors |
| Visual C++ runtime (`vcruntime140*.dll`, `msvcp140*.dll`, `concrt140.dll`) | Microsoft Visual C++ redistributable terms | Microsoft Corporation |
| Noto Sans, Noto Sans CJK, Noto Sans Arabic/Hebrew/Thai/Devanagari/Bengali/Telugu/Tamil | SIL Open Font License 1.1 | The Noto Project Authors |

### Required attributions

* **SFML** - "This software uses the SFML library, copyright (c) Laurent Gomila
  and contributors, distributed under the zlib licence."
* **FreeType** - "Portions of this software are copyright (c) The FreeType
  Project (www.freetype.org). All rights reserved."
* **Dear ImGui / ImGui-SFML** - "This software uses Dear ImGui, copyright (c)
  Omar Cornut, and ImGui-SFML, copyright (c) Elias Daler, both under the MIT
  licence."
* **libogg / libvorbis / libFLAC** - "This software uses libraries from the
  Xiph.Org Foundation, distributed under the BSD 3-Clause licence."
* **zlib** - "This software uses the zlib library, copyright (c) Jean-loup
  Gailly and Mark Adler."
* **FlatBuffers** - "This software uses FlatBuffers, copyright (c) Google LLC,
  distributed under the Apache License 2.0." The licence text and NOTICE file
  are reproduced under `libs/flatbuffers-master/`.
* **miniaudio** - public domain (Unlicense, alternatively MIT-0). No attribution
  is legally required; it is listed here anyway.
* **Opus** - "This software uses the Opus audio codec, copyright (c) Xiph.Org
  Foundation and contributors, distributed under the BSD 3-Clause licence."
* **Noto fonts** - "This software uses the Noto font family, copyright (c) The
  Noto Project Authors, distributed under the SIL Open Font License 1.1." The
  full licence text is in `Data/Fonts/OFL.txt` and in the packaged
  `assets/fonts/OFL.txt`.

The MIT and BSD licences also require the full licence text and the disclaimer
to be reproduced. They are reproduced verbatim in the `LICENSE`-style files
under `libs/`, which travel with the source distribution. If the game is shipped
as a binary only, copy those files next to this notice.

## Build-, editor- and test-time only

Compiled or used while building or while running the editor, absent from the
shipped game executable:

* Catch2 - Boost Software License 1.0 (test suite)
* nativefiledialog - zlib (editor file dialogs)
* recastnavigation - zlib (optional navigation authoring)
* ImGuiFileDialog, ImGuizmo, ImPlot, ImGuiColorTextEdit - MIT (editor tools)

## Licensing boundary

Everything linked into the shipped build uses a licence compatible with MPL 2.0
and with proprietary game code. No library with a different or incompatible
licence is present.

## Notes on the binary

* SFML ships as the four release DLLs next to the executable
  (`FEMBOY_STATIC_SFML=OFF`), while `femboyengine` itself is linked statically.
  Static SFML is not used with voice chat because both SFML Audio and VoiceChat
  embed miniaudio; keeping SFML in DLLs gives each side its own implementation.
* The Visual C++ runtime DLLs next to the executable are Microsoft
  redistributables, which the Visual Studio licence permits redistributing
  app-local. They are not third-party open source.
* Windows system libraries (OpenGL, WinMM, WS2_32, ole32, SAPI and so on) are
  part of the operating system and are not redistributed.
