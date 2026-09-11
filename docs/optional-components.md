# Optional components

ELLA's core searches local files without a download, account or service. Settings
lists optional OCR, Office rendering, media conversion, transcription, speech
model and semantic helper capabilities. A missing component leaves the core
available. Model and tool installation is always an explicit action.

Use **Choose local tool** for an existing Windows executable (or `.bin` speech
model). This stores a local path; ELLA never deletes that source installation.
The component's normal processing operation reports incompatible tools. The
configuration label means a file was found, not that every tool/version was
certified. Whisper needs FFmpeg, whisper-cli and a model configured separately.

There are currently no published, audited ELLA component downloads. The Settings
page shows that state and does not invent download sizes or attempt network
access. Maintainers can load an audited local JSON catalogue with this shape:

```json
{ "schemaVersion": 1, "components": [] }
```

Each entry in `components` must supply `id`, `version`, `url`, `sha256`,
`downloadBytes`, `installedBytes` and `entrypoint`. IDs are `ocr`, `office`,
`ffmpeg`, `transcription`, `speech-model`, and `semantic`. The URL must identify
an HTTPS ZIP. Use its exact compressed byte count and lower-case SHA-256 digest;
`installedBytes` is the maximum sum of uncompressed file contents. The entrypoint
is a relative path within the ZIP, for example `bin/tesseract.exe`. Do not put
absolute paths, credentials, placeholders or mutable unverified URLs in a
published catalogue. Loading a catalogue trusts its publisher; checksums bind
the download to that chosen catalogue, not to an independent code signature.

Installation streams data to disk, enforces the compressed-size bound, verifies
SHA-256, and validates all ZIP entry paths before extraction. Absolute paths,
parent traversal, alternate data streams, Windows reserved names, symlinks,
duplicate case-insensitive paths and oversized archives are rejected. No
downloaded installer or script runs. Extraction uses Windows PowerShell/.NET
outside the UI thread. The package becomes active by renaming the validated
directory; an existing component is preserved if replacement fails. Startup
recovers an interrupted rename and discards incomplete downloads. Cancellation
leaves existing installed versions untouched.

Installed components live under ELLA's app-data `components` directory. Remove
deletes only ELLA's own component directory and clears its configured local path.
It does not delete notes, source documents, or an external tool installation.

The semantic helper build contract is documented in
`src/components/semantic/README.md`. A release must publish an audited helper +
model package and pass retrieval benchmarks before claiming semantic-search
release readiness.
