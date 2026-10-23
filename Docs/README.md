# Documentation

Markdown is the source format. It is diffable, reviewable and merges cleanly;
DOCX/HTML/PDF are generated distribution artifacts and live outside this
directory. The canonical naming and architecture standard is:

- [STANDARD.md](STANDARD.md) вЂ” English
- [STANDARD.ru.md](STANDARD.ru.md) вЂ” Russian
- [API.md](API.md) вЂ” canonical API index with inline platform notes
- [API.ru.md](API.ru.md) вЂ” Russian API index
- [PLATFORMS.md](PLATFORMS.md) вЂ” build, support matrix and deployment

Topic references:

- [RENDERING.md](RENDERING.md) вЂ” projections, DrawList, depth
- [CLASS_DEFAULTS.md](CLASS_DEFAULTS.md) вЂ” reflected fields and project defaults
- [SETTINGS.md](SETTINGS.md) вЂ” settings registry and display/accessibility
- [ASSET_PIPELINE.md](ASSET_PIPELINE.md) вЂ” import, atlas and cooking
- [VOLUMES.md](VOLUMES.md) вЂ” volume effects
- [VULKAN_BACKEND.md](VULKAN_BACKEND.md) вЂ” future backend notes
- [DESIGN_NOTES.md](DESIGN_NOTES.md) вЂ” historical design decisions

Do not add a new topic document when the material belongs in the standard or in
an existing topic. Generated documentation is produced from these sources; it is
never edited by hand.

## Generated distribution docs

- `Docs/docx/{en,ru}/` вЂ” Word documents
- `Docs/pdf/{en,ru}/` вЂ” PDF documents
- `Docs/gen_docs.py` вЂ” generator; it reads the Markdown files above

Regenerate with a Python that has `python-docx` (for example the LibreOffice
Python) and let LibreOffice headless produce the PDFs:

```text
"C:/Program Files/LibreOffice/program/python.exe" Docs/gen_docs.py
```

