# ELLA Closed Beta Operations Runbook

## Rollout Phases
1. Internal sanity (day 0).
2. Canary rollout (5 testers).
3. General rollout (20 additional testers, total 25).

## Weekly Cadence
1. Monday: triage + candidate selection.
2. Tuesday: fix merge cutoff.
3. Wednesday: release build + verification.
4. Thursday: beta drop + tester communication.
5. Friday: hotfix window for blockers only.

## Triage Rhythm
- Daily triage during canary week.
- Three triage passes per week after general rollout.

## Severity Policy
- P0: startup crash/data-loss/cannot import. Hotfix immediately.
- P1: core flow broken without workaround. Fix in current beta cycle.
- P2: functional issue with workaround. Schedule next beta.
- P3: polish/non-critical visual issue. backlog unless high-frequency.

## Triage SLA
- Initial acknowledgment: < 4 business hours.
- Repro status update: < 1 business day.
- Fix ETA for P0/P1: same day target.

## Required Evidence Per Issue
- App version/build id.
- Repro steps.
- Diagnostics bundle.
- Optional media evidence (screenshot/video).

## Freeze Rule
- During beta, only blocker fixes, reliability, and support tooling ship.
- New feature development remains frozen until beta exit criteria are met.
