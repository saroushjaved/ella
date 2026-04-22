# ELLA Beta Release Pipeline (Windows)

## One-Command Flow
- Run:
  - `powershell -ExecutionPolicy Bypass -File .\scripts\beta_release.ps1 -Version 0.9.0-beta.1`

## What the script does
1. Configures Release build with beta metadata.
2. Builds app + tests.
3. Runs tests (unless `-SkipTests` is set).
4. Stages runtime files.
5. Runs `windeployqt` when available.
6. Produces portable zip artifact.
7. Produces installer via Inno Setup when `iscc.exe` is available.
8. Generates `SHA256SUMS.txt` and `release-manifest.json`.

## Smoke Matrix
- Run:
  - `powershell -ExecutionPolicy Bypass -File .\scripts\beta_smoke_matrix.ps1 -CleanAppDataEachCycle`

## Build Recovery Fallback
- If Windows build artifacts are locked, run:
  - `powershell -ExecutionPolicy Bypass -File .\scripts\build_with_recovery.ps1 -BuildDir build/Desktop_Qt_6_11_0_MinGW_64_bit-Debug`
- The script stops stale processes, rotates/removes lock-prone artifacts, and retries once.

## Rollback Rule
- Keep previous installer and checksum in artifact storage for each beta drop.
- Do not overwrite previous known-good artifacts.
