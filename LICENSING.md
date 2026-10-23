# Licensing

The repository contains the engine. Games built with it, including Neon
Triangle, are separate works and are not stored here. Engine code is licensed
under MPL 2.0; game code, art and writing remain the property of their authors
and may use any licence they choose.

## 1. The engine: Mozilla Public License 2.0

`Engine/`, the build files in `CMake/`, the top-level `CMakeLists.txt` and the
public headers in `Engine/include/FemboyEngine/` are licensed under the Mozilla
Public License, version 2.0. The full text is in [LICENSE](LICENSE).

What that means in practice:

* **Commercial use is allowed.** You can ship a paid game built on the engine,
  on Steam, itch.io, consoles or a store of your own, and you do not pay the
  engine's author anything.
* **Modification is allowed.** You can change any engine file, delete features,
  add your own subsystems, or fork the project.
* **Engine changes stay open.** Every file you add to or modify inside
  `Engine/`, and every new file that contains engine code, must be distributed
  under MPL 2.0 вЂ” source included вЂ” when you distribute the engine or a
  product that contains it. You cannot take the engine closed-source.
* **Your game stays yours.** Game code, art, audio, levels and writing in a
  separate game repository are a *Larger Work* (MPL 2.0 §3.3). They may be
  licensed however you like, including proprietary, and you do not have to
  publish them.
* **Patents are covered.** Contributors grant a patent license for their
  contributions, and the license terminates if you sue over them.
* **No warranty.** The engine is provided "as is" (MPL 2.0 В§6).

### Why MPL 2.0 and not LGPL/AGPL

* LGPL 2.1/3 was written for shared libraries. The engine is statically linked
  into the game and is full of templates and inline code, so the LGPL's
  relinking requirement is hard to satisfy honestly and even harder to explain
  to a player.
* AGPL would force every game built on the engine to publish its own source if
  it is ever played over a network, which rules out most commercial projects.
* GPL would force every game to be GPL, which rules out paid closed-source
  games entirely.
* MPL 2.0 gives exactly the requested behaviour: the engine and every change
  to it stay free and open, while games built with it can be sold and kept
  closed.

If you *want* a stronger copyleft (all games must be open too), the engine can
be relicensed to GPLv3 or AGPLv3 by the copyright holder; that is a one-line
change in this file and in the source headers.

### Header notice

Each engine source and header carries the MPL Exhibit A notice:

```
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
```

Do not remove it (MPL 2.0 В§3.4). Adding your own copyright line next to it is
expected when you contribute.

## 2. Games built with the engine

A game, including Neon Triangle, is a separate work. Its code, art, audio,
levels and writing are not open source merely because it links the engine. They
may be proprietary and distributed under their own End User License Agreement.

The engine may be used under MPL 2.0; that does not make the game's own source
public. Anyone who redistributes a modified *engine* still has to publish the
engine part.

## 3. Third-party components

The engine links third-party libraries with their own licenses (SFML under
zlib, nlohmann/json under MIT, Catch2 under BSL-1.0, and others). Their notices
are collected in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and are
shipped inside every engine release archive along with `LICENSE` and this file.

Every linked dependency is compatible with MPL 2.0 and with proprietary game
code; no dependency with an incompatible license is present or required. The
animation system is native engine code, so there is no extra animation-runtime
license to buy or attribute.

## 4. Assets

The engine's bundled UI fonts in `Data/Fonts` are provided by the Noto project
under the SIL Open Font License 1.1; the license travels in
`Data/Fonts/OFL.txt`. Art, audio and writing created for a game belong to that
game's authors and are not part of the engine repository.

## 5. What a fork has to do

1. Keep `LICENSE` and the header notices in every engine file.
2. Publish the modified engine source (a public repository, a source archive
   next to the build, or a written offer are all acceptable under В§3.2).
3. State clearly where the source can be obtained.
4. Keep `THIRD-PARTY-NOTICES.md` accurate if dependencies change.

Nothing else is required: no royalty, no permission request, no contributor
agreement.
