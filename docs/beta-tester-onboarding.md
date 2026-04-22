# ELLA Closed Beta Tester Onboarding (Windows)

## 1. Install
1. Download the installer artifact and checksum from the beta drop message.
2. Verify checksum:
   - `Get-FileHash .\ella-beta-win64-<version>.exe -Algorithm SHA256`
3. Run installer.
4. If Windows SmartScreen appears, choose:
   - `More info` -> `Run anyway`

## 2. First Launch
1. Confirm app opens and shows beta version in footer.
2. Read Beta Scope notice.
3. Import a small folder and run a quick search.

## 3. What Is In Scope
- Local-first workflows:
  - import
  - indexing
  - search
  - memory browser + reader
  - notes/annotations
  - remove file from ELLA

## 4. What Is Experimental
- Cloud sync features are labeled **Experimental**.
- Cloud sync behavior is visible but not release-gating for this beta.

## 5. Report an Issue
1. Reproduce the issue once.
2. Use `Tools -> Export Diagnostics`.
3. Share:
   - diagnostics bundle path (zip),
   - exact repro steps,
   - expected vs actual behavior,
   - screenshot/video if possible.

## 6. Local Logs
- Active log file:
  - `%APPDATA%\\Ella\\logs\\ella.log`
- Rotated logs:
  - `%APPDATA%\\Ella\\logs\\ella.log.1` ... `.5`

## 7. Fast Triage Template
- Build version:
- Scenario:
- Steps to reproduce:
- Expected result:
- Actual result:
- Crash? (yes/no):
- Diagnostics attached: (yes/no):
