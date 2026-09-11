# Reproducible lean Windows releases

The authoritative runtime policy is `packaging/core-runtime.json`: Qt 6.11.1,
MinGW 13.1 x64, explicit application-owned files, required Qt runtime files,
forbidden heavy dependencies, and decimal-byte size limits. Local tools are not
copied into builds or releases. The core is built with optional media playback
and semantic helper targets disabled; those belong in separately audited
packages.

```powershell
./scripts/beta_release.ps1 -Version 1.0.0-rc.1
```

The historical script name is retained for existing callers. Supply `-QtRoot`
and `-MingwBin` when using non-default toolchain locations. CMake, the pinned Qt
SDK/compiler, and Inno Setup 6 must be available. The script configures Release,
builds and runs CTest, constructs a fresh staging directory from the explicit
manifest, deploys dependency-discovered Qt files, strips the shipping executable,
copies license texts, validates the runtime and writes per-file sizes/hashes.
An existing development stage or `tools/` directory is never reused.

The installer uses solid `lzma2/max`. The portable ZIP and installer use the same
verified staging tree. Release outputs include SHA256SUMS.txt,
release-manifest.json, size-report.json and an SPDX file inventory. The installed
core must stay below 300,000,000 bytes and the installer below 150,000,000 bytes;
100,000,000 is the installer target. A supplied `-PreviousSizeReport` warns on
installed-size growth above 5%. These are enforced budgets, not measured claims
until artifacts have actually been produced. `-PortableOnly`, `-SkipBuild` and
`-SkipTests` are diagnostic options; skipped tests are recorded in the release
manifest.

Before a public tag, inspect dependency notices, resolve SPDX NOASSERTION entries,
verify installation/upgrade/uninstallation and offline operation in a clean
Windows VM, measure installation time/memory, complete visual/accessibility and
retrieval benchmarks, and record the two-week daily-use trial. CI validates
artifacts and automated tests but cannot certify those human release gates.

Build and run the deterministic 25,000-document/250,000-passage keyword workload
with `scripts/benchmark_retrieval.ps1`. It executes 100 labeled exact-recall
queries, requires at least 90 top-five successes, and enforces warm p95 below
300 ms. Semantic p95 and paraphrase gain are measured separately against an
audited optional component and the labeled release dataset.

No script signs, uploads or publicly publishes an artifact. Source-code signing
and hosting credentials are not required to run local builds.
