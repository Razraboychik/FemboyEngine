# Bundled UI fonts

These fonts are the engine's cross-platform text stack. They are not system
fonts, so a packaged game does not depend on Segoe UI, Microsoft YaHei, PingFang
or any other OS font being installed.

All files are from the Noto project and are licensed under the SIL Open Font
License 1.1. The full license text is in `OFL.txt`.

| File | Scripts |
| --- | --- |
| `NotoSans-Regular.ttf` | Latin, Cyrillic, Greek, Vietnamese |
| `NotoSansCJK-Regular.ttc` | Chinese (Simplified/Traditional), Japanese, Korean |
| `NotoSansArabic-Regular.ttf` | Arabic |
| `NotoSansHebrew-Regular.ttf` | Hebrew |
| `NotoSansThai-Regular.ttf` | Thai |
| `NotoSansDevanagari-Regular.ttf` | Hindi and other Devanagari scripts |
| `NotoSansBengali-Regular.ttf` | Bengali |
| `NotoSansTelugu-Regular.ttf` | Telugu |
| `NotoSansTamil-Regular.ttf` | Tamil |

Source: https://github.com/notofonts/noto-fonts and
https://github.com/notofonts/noto-cjk

The engine loads `NotoSans-Regular.ttf` as the primary UI face and picks a
bundled fallback that covers every code point in the string. The game package
copies this directory to `assets/fonts`; the editor uses `Data/Fonts` directly.
