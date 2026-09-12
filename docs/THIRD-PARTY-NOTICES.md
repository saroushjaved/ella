# ELLA runtime and optional component notices

ELLA source code is licensed under Apache-2.0; see `LICENSE`. Dependencies retain
their own licenses. This notice is not a grant to redistribute every tool that a
developer has installed on their computer.

## Core runtime

The release build uses dynamically linked Qt 6.11.1 modules: Core, Gui, Qml,
Quick, QuickControls2, Network, Concurrent, Sql, Pdf and PdfQuick, together with
their deployed plugins and compiler runtime libraries. Qt contains additional
third-party code (including PDFium in Qt PDF). The packaging script copies the
Qt distribution's license texts into `licenses/`; those texts and Qt's own
third-party attributions apply. The repository carries the Qt open-source
license text used when aqtinstall does not provide the Qt Installer's global
license directory, and packaging copies the MinGW toolchain's own notices.
Preserve the ability to replace dynamically
linked Qt libraries when distributing under the applicable LGPL terms.

- Qt source and license overview: https://www.qt.io/licensing/open-source-lgpl-obligations
- Qt third-party code notices: https://doc.qt.io/qt-6/licenses-used-in-qt.html
- MinGW-w64 runtime: https://www.mingw-w64.org/license/
- GCC runtime library exception: https://www.gnu.org/licenses/gcc-exception-3.1.html

The release emits `ella-runtime.spdx.json`, an SPDX 2.3 **file inventory** with
SHA-256 hashes. `NOASSERTION` means that license attribution still needs review;
this file inventory is not a completed package-level legal audit. Release
maintainers must resolve the inventory against the actual Qt/compiler artifact
licenses before publicly redistributing a release.

## Optional components

No OCR, Office-rendering, speech model, FFmpeg, ONNX Runtime, E5 model, or HNSW
binary is bundled with the core. Choosing an existing local tool does not copy
or redistribute it. No official downloadable component packages are published
in this repository yet. The catalogue installer accepts only an explicitly
selected, audited manifest with pinned URLs, checksums and size bounds.

Before publishing component ZIPs, include the exact source/version, build flags,
license texts, redistribution obligations, model card, and the upstream notices
in each package. FFmpeg licensing depends on the build configuration; do not
assume a random downloaded FFmpeg build is covered by one license.

Expected upstream dependencies for component builds:

| Component | Upstream | License to verify for the selected artifact |
| --- | --- | --- |
| OCR | https://github.com/tesseract-ocr/tesseract | Apache-2.0 and dependency notices |
| Office rendering | https://www.libreoffice.org/ | MPL-2.0 and bundled dependency notices |
| Media conversion | https://ffmpeg.org/legal.html | LGPL/GPL depending on enabled components |
| Speech transcription | https://github.com/ggml-org/whisper.cpp | MIT and model notices |
| Embedding inference | https://github.com/microsoft/onnxruntime | MIT and dependency notices |
| E5 small v2 | https://huggingface.co/intfloat/e5-small-v2 | MIT model card and exported-model provenance |
| HNSW index | https://github.com/nmslib/hnswlib | Apache-2.0 |

Existing UI asset attribution remains in `src/ui/assets/ASSET_MANIFEST.md`.
