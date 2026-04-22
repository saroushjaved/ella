# ELLA Closed Beta Release Checklist

## Release Identity
- Version follows `0.9.0-beta.N` format.
- Artifact prefix is `ella-beta-win64-0.9.0-beta.N`.
- Build metadata is visible in-app and included in diagnostics.
- SHA256 checksum generated for each distributable artifact.

## Quality Gates (Must Pass)
- `mingw32-make -j4 appSecondBrain` passes for Debug and Release.
- `mingw32-make -j4 ellaMemoryBrowserTests` passes.
- `ellaMemoryBrowserTests.exe` passes on clean runtime directories.
- Smoke script passes repeated start/import/search/read cycles.
- No open P0 bugs.
- No open P1 bugs.

## Core Workflow Verification (Beta-Critical)
- Folder import (recursive) works and does not crash.
- Indexing completes and status chips update.
- Search shows relevant snippets and reasons.
- Reader opens PDF/image/text reliably.
- Media unsupported/unavailable paths fall back gracefully.
- Notes and annotations add/edit/delete round-trip.
- Remove-from-ELLA removes file record and linked artifacts.

## Experimental Workflow Verification (Non-Blocking)
- Cloud connect/disconnect flow is visible and labeled Experimental.
- Sync status chip reflects connected vs disconnected.
- Cloud errors surface as non-crashing UI messages.

## Packaging + Distribution
- Installer generated for Windows x64.
- Installer includes required runtime assets and `qt.conf`.
- SmartScreen onboarding note included for unsigned installer.
- Rollback installer and checksum for previous known-good build are available.

## Diagnostics + Supportability
- `Export Diagnostics` creates bundle successfully.
- Bundle includes release metadata, environment/tool availability, recent errors, and timeline.
- Log rotation works and log location is documented.
- "Report an Issue" flow text references diagnostic bundle + repro steps.

## Beta Ops Readiness
- Canary cohort (5 testers) list finalized.
- Full cohort (25 testers) list finalized.
- Reporting channel and triage SLA communicated.
- Weekly beta drop cadence and owner on-call rotation assigned.

## Signoffs
- Engineering Lead: _pending_
- QA Lead: _pending_
- Product Owner: _pending_
- Beta Operations Lead: _pending_
