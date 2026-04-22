# ELLA Developer Setup (Windows)

This guide gets a contributor machine ready to build and test ELLA from source.

## 1. Required Tools

- Windows 10/11
- [Git for Windows](https://git-scm.com/download/win)
- [CMake](https://cmake.org/download/) (3.16+)
- Qt 6.11.0 (MinGW 64-bit) with modules:
  - `qtmultimedia`
  - `qtpdf`
  - `qtshadertools`
- MinGW toolchain (installed via Qt Online Installer)

Recommended:

- Visual Studio Code or Qt Creator
- Inno Setup (only needed for installer packaging flows)

## 2. Clone and Enter the Repo

```powershell
git clone <your-fork-or-repo-url>
cd ella_v1
```

## 3. Run Local Environment Checks

Use the setup script to validate prerequisites:

```powershell
.\scripts\setup_dev_windows.ps1
```

To also bootstrap bundled Tesseract into `tools\tesseract`:

```powershell
.\scripts\setup_dev_windows.ps1 -InstallTesseract
```

## 4. Configure and Build

```powershell
cmake -S . -B build-dev -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Debug `
  -DELLA_RELEASE_VERSION=0.9.0-dev `
  -DELLA_RELEASE_CHANNEL=dev `
  -DELLA_BUILD_ID=local-dev

cmake --build build-dev --target appSecondBrain ellaMemoryBrowserTests --config Debug -j 4
```

## 5. Run Tests

```powershell
ctest --test-dir build-dev --output-on-failure -C Debug
```

## 6. Optional Runtime Tool Bundles

ELLA can integrate with external tools for richer extraction/transcoding workflows.
These bundles are intentionally not stored in source control.

- `tools\tesseract`: can be bootstrapped via `scripts\setup_tesseract.ps1`
- `tools\libreoffice`, `tools\ffmpeg`, `tools\whisper`: optional local/runtime bundles

If a tool folder exists, build/package scripts may include it automatically.

## 7. Common Troubleshooting

- `mingw32-make not found`:
  - ensure Qt MinGW `bin` directory is on `PATH`
- Qt module errors during configure:
  - verify Qt version and required modules are installed
- Runtime DLL missing:
  - run from configured build folder and verify Qt deployment step
