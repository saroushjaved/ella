# ELLA

![ELLA Logo](docs/assets/ella_logo_1100by500.png)

ELLA is a local-first desktop memory browser built with Qt/QML and C++17.  
It helps you import files, index content, search quickly, and review results with source-aware context.

## Key Features

- Local-first knowledge workspace with your file catalog and search index stored on your machine.
- Fast retrieval with indexed search, ranking, and snippets so you can quickly recover relevant material.
- Broad content ingestion with support for text-like files and PDFs, with OCR fallback for scanned/image content.
- Source-aware reading flow with quick actions to open the original file or its folder directly.
- Built-in note and annotation capabilities tied to your documents for better long-term recall.
- Trust and health visibility through indexing and search status signals directly in the app.
- Optional cloud sync integrations available as experimental features.

## Project Status

- Current state: **Beta (Windows-first)**
- CI: GitHub Actions (`Windows CI`)
- Focus: reliable local indexing/search and memory-browser workflows

## Quick Start (Windows)

1. Follow the setup guide: [docs/developer-setup-windows.md](docs/developer-setup-windows.md)
2. Configure:

```powershell
cmake -S . -B build-dev -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Debug `
  -DELLA_RELEASE_VERSION=0.9.0-dev `
  -DELLA_RELEASE_CHANNEL=dev `
  -DELLA_BUILD_ID=local-dev
```

3. Build app and tests:

```powershell
cmake --build build-dev --target appSecondBrain ellaMemoryBrowserTests --config Debug -j 4
```

4. Run tests:

```powershell
ctest --test-dir build-dev --output-on-failure -C Debug
```

5. Run app:

```powershell
.\build-dev\appSecondBrain.exe
```

## Contributing

We welcome open-source contributions through issues and pull requests.

- Read [CONTRIBUTING.md](CONTRIBUTING.md)
- Use issue/PR templates under `.github/`
- Ensure `Windows CI` passes before requesting review

## Releases

- Source code is hosted in this repository.
- Installers and other heavy binary artifacts are published through GitHub Releases.

## Security

Please report vulnerabilities privately according to [SECURITY.md](SECURITY.md).

## License

This project is licensed under the Apache License 2.0. See [LICENSE](LICENSE).
