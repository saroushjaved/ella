# ELLA Memory Browser Plan (V1 Slice)

## 1. Current State Audit

### Stack and Architecture
- Desktop app built with `Qt 6.5+`, `QML`, and `C++17`.
- Build system: `CMake` + MinGW.
- Core app entry in [`src/main.cpp`](../src/main.cpp), wiring QML pages and C++ context models/services.

### Storage Model
- Local SQLite DB managed by [`DatabaseManager`](../src/database/DatabaseManager.cpp).
- Existing schema includes:
  - `files` catalog with metadata and local file path.
  - `file_content_fts` (FTS5 virtual table) for content search.
  - `file_index_state` for indexing health/error tracking.
  - `collections`, `file_collections`, `collection_rules`.
  - `document_notes` and `annotations`.
  - Cloud sync tables (`cloud_connections`, `cloud_file_map`, `sync_jobs`).

### Ingestion and Indexing
- Current UI ingestion is single-file (`Add File`) and metadata-gated in [`BrowserPage.qml`](../src/ui/BrowserPage.qml).
- Catalog writes are done via [`FileRepository::addFile`](../src/library/FileRepository.cpp).
- Asynchronous indexing pipeline in [`IndexingService`](../src/search/IndexingService.cpp):
  - Incremental queue, rebuild support, per-file reindex.
  - Stores extraction state and errors.
- Content extraction in [`ContentExtractor`](../src/search/ContentExtractor.cpp):
  - Text-like files, PDF text extraction.
  - OCR fallback for image/PDF using bundled/system Tesseract.

### Search and Retrieval
- Search query path: QML search field -> `FileListModel::search` -> `FileRepository::queryFiles`.
- Ranking combines:
  - FTS content hits (snippet + bm25 rank).
  - Metadata/path boosts (name/path/domain/subject/etc).
- Existing result cards already show title/path/type and snippet when available.

### UI and Trust Signals
- Home page, Browser page, Reader page already exist and are functional.
- Browser already exposes index/search/sync health data via:
  - `fileListModel.indexStatus()`
  - `fileListModel.searchHealth()`
  - `cloudSyncModel.status()`
- Reader already supports opening original files/folders and source-aware preview.

### Tests and Tooling Constraints
- No committed source-level test target currently in repo.
- Build artifacts show existing successful app build, but test harness is not yet defined in `CMakeLists.txt`.

## 2. What Can Be Reused

- **Data layer**: `FileRepository`, `DatabaseManager`, `AnnotationRepository`.
- **Search/index core**: `IndexingService`, `ContentExtractor`, existing FTS schema.
- **State/model layer**: `FileListModel` as QML boundary.
- **UI shell**: existing Browser/Reader layouts and actions.
- **Trust plumbing**: index/search/sync status maps and health checks.
- **Local-first foundation**: local DB, local path opening, optional cloud sync already secondary.

## 3. What Needs to Change

1. Add **required planning deliverable** (this doc) and align copy around Memory Browser.
2. Add **fast ingestion APIs**:
   - Multi-file import.
   - Recursive folder import.
   - Auto metadata defaults (edit later).
3. Add **retrieval trust/evidence improvements**:
   - Explicit match reason (`why matched`) in search results.
   - Source field visibility in result cards.
   - Explicit `Open Original` action label.
4. Add **retrieval feedback instrumentation**:
   - Local `retrieval_events` table.
   - Query, TTFR, open-source/result-opened, useful/not-useful events.
5. Add **import status visibility** in UI.
6. Add **basic QtTest target** for core milestone behavior.

## 4. Proposed Phased Implementation Plan

### Phase A: Data + Model Foundation
- Add DB migration for `retrieval_events`.
- Extend `FileRecord` + query output with match reason.
- Extend `FileListModel`:
  - `importFiles(...)`
  - `importFolder(...)` (recursive)
  - `importStatus()`
  - `trackRetrievalEvent(...)`
  - role for match reason/source metadata.
- Keep current indexing queue behavior; new imports enqueue indexing via existing hooks.

### Phase B: Retrieval-First UX
- Home copy and Browser copy shift to Memory Browser language.
- Search placeholder to: `What are you trying to recover?`
- Replace metadata-blocking add flow with:
  - `Import Files`
  - `Import Folder`
- Keep metadata editor in details panel for refinement.
- Result cards show source + snippet + why matched.
- Rename/open actions to emphasize `Open Original`.

### Phase C: Trust + Feedback
- Surface import status summary chip.
- Keep and foreground local trust chips (index/search/ocr, sync secondary).
- Add explicit useful/not useful controls in Browser and Reader.
- Log retrieval feedback/open/query events locally.

### Phase D: Tests
- Add QtTest target validating:
  - Recursive folder import counts.
  - Search result evidence (snippet/reason) after indexing.
  - Retrieval event persistence.

## 5. Risks / Migration Considerations

- **Risk: large folder imports** can be slow in synchronous UI path.
  - Mitigation: keep import summary + index queue visibility; background import worker deferred.
- **Risk: metadata quality drops with fast import defaults**.
  - Mitigation: preserve existing edit metadata flow and collection tooling.
- **Risk: duplicate imports overriding curated metadata**.
  - Mitigation: skip already-indexed paths during batch import by default.
- **Migration safety**:
  - Additive schema only (`CREATE TABLE IF NOT EXISTS retrieval_events`).
  - No destructive table rewrite.

## 6. First Shippable Milestone Acceptance Criteria

1. User can import multiple files and full folders (recursive) from local archive.
2. Imported files enter catalog and are indexed using existing index pipeline.
3. Search UI is retrieval-first and shows:
   - title/source
   - snippet/preview
   - why matched
   - file type metadata.
4. User can open exact source quickly via explicit `Open Original` action.
5. User can mark result usefulness (`useful` / `not useful`) in Browser and Reader.
6. Local retrieval events are persisted for:
   - query
   - time_to_first_result
   - result/open source
   - usefulness feedback.
7. Trust messaging clearly communicates local-first behavior and current index/import/search state.
