# ELLA UI Asset Manifest

## Vendored External Assets

1. `src/ui/assets/vendor/search.svg`
- Source: <https://raw.githubusercontent.com/tabler/tabler-icons/master/icons/outline/search.svg>
- Upstream project: Tabler Icons (<https://github.com/tabler/tabler-icons>)
- License: MIT License
- Notes: Used for search/no-result iconography in browser-like states.

2. `src/ui/assets/vendor/file-info.svg`
- Source: <https://raw.githubusercontent.com/tabler/tabler-icons/master/icons/outline/file-info.svg>
- Upstream project: Tabler Icons (<https://github.com/tabler/tabler-icons>)
- License: MIT License
- Notes: Used for details/metadata visual cues.

3. `src/ui/assets/vendor/notes.svg`
- Source: <https://raw.githubusercontent.com/tabler/tabler-icons/master/icons/outline/notes.svg>
- Upstream project: Tabler Icons (<https://github.com/tabler/tabler-icons>)
- License: MIT License
- Notes: Used for note/annotation visual cues.

4. `src/ui/assets/vendor/video-off.svg`
- Source: <https://raw.githubusercontent.com/tabler/tabler-icons/master/icons/outline/video-off.svg>
- Upstream project: Tabler Icons (<https://github.com/tabler/tabler-icons>)
- License: MIT License
- Notes: Used for media preview fallback states.

5. `src/ui/assets/vendor/link-off.svg`
- Source: <https://raw.githubusercontent.com/tabler/tabler-icons/master/icons/outline/link-off.svg>
- Upstream project: Tabler Icons (<https://github.com/tabler/tabler-icons>)
- License: MIT License
- Notes: Used for disconnected/link-broken states.

6. `src/ui/assets/vendor/database.svg`
- Source: <https://raw.githubusercontent.com/tabler/tabler-icons/master/icons/outline/database.svg>
- Upstream project: Tabler Icons (<https://github.com/tabler/tabler-icons>)
- License: MIT License
- Notes: Used for local-index/local-first status cues.

7. `src/ui/assets/vendor/empty-memory-state.svg`
- Source: Generated locally in this repository.
- Upstream project: N/A
- License: Project-owned artwork.
- Notes: Production empty-state illustration for browser details panel when no memory is selected.

8. `src/ui/assets/vendor/no-results-recovery.svg`
- Source: Generated locally in this repository.
- Upstream project: N/A
- License: Project-owned artwork.
- Notes: Production empty-state illustration for memory browser zero-results state.

9. `src/ui/assets/vendor/preview-unavailable.svg`
- Source: Generated locally in this repository.
- Upstream project: N/A
- License: Project-owned artwork.
- Notes: Production fallback illustration for reader PDF/media/unsupported preview unavailable states.

## Local Placeholder Assets

1. `src/ui/assets/placeholders/empty-selection.svg`
- Source: Generated locally in this repo.
- License: Project local placeholder (replaceable).
- Notes: Placeholder used for "no details selection" state.

2. `src/ui/assets/placeholders/no-results.svg`
- Source: Generated locally in this repo.
- License: Project local placeholder (replaceable).
- Notes: Placeholder used for browser empty-results state.

3. `src/ui/assets/placeholders/media-unavailable.svg`
- Source: Generated locally in this repo.
- License: Project local placeholder (replaceable).
- Notes: Placeholder used for media preview unavailable state.

4. `src/ui/assets/placeholders/unsupported-file.svg`
- Source: Generated locally in this repo.
- License: Project local placeholder (replaceable).
- Notes: Placeholder used for unsupported file preview state.

## TODO Placeholder Replacement Guidance

- Replace placeholder SVG files with final branded artwork while preserving file names to avoid code changes.
- If replacing with raster images, keep dimensions roughly equivalent to avoid layout shifts.
- Keep any replacement assets vendored locally and update this manifest with source URL and license details.
