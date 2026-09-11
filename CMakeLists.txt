cmake_minimum_required(VERSION 3.24)
project(ELLA VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ELLA_RELEASE_VERSION "1.0.0-rc.1" CACHE STRING "ELLA release version")
set(ELLA_RELEASE_CHANNEL "preview" CACHE STRING "ELLA release channel")
set(ELLA_BUILD_ID "local-dev" CACHE STRING "ELLA build identifier")
string(TIMESTAMP ELLA_BUILD_DATE_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)
option(ELLA_ENABLE_MEDIA_PLAYBACK "Include optional media playback runtime" OFF)
option(ELLA_LOCAL_WINDEPLOYQT "Deploy local Qt runtime after build" OFF)
option(ELLA_ENABLE_IPO "Use supported release link-time optimization" ON)
option(ELLA_BUILD_SEMANTIC_HELPER "Build optional native semantic component" OFF)
option(ELLA_DEVELOPER_CLOUD_SYNC "Expose experimental cloud integration" OFF)
option(ELLA_BUILD_SCALE_BENCHMARKS "Build the opt-in 25k/250k retrieval benchmark" OFF)
include(CTest)
find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 Concurrent Sql Pdf PdfQuick)
find_package(Qt6 6.11.1 REQUIRED COMPONENTS CorePrivate)
if(BUILD_TESTING)
    find_package(Qt6 6.11.1 REQUIRED COMPONENTS Test)
endif()
if(ELLA_ENABLE_MEDIA_PLAYBACK)
    find_package(Qt6 6.11.1 REQUIRED COMPONENTS Multimedia)
endif()
qt_standard_project_setup(REQUIRES 6.5)
# Isolate Qt's private ZIP API in extraction/backup; the toolchain is pinned to 6.11.1.
add_library(ellaCore STATIC
    src/core/AppConfig.cpp
    src/core/AppConfig.h
    src/core/SecureStorage.cpp
    src/core/SecureStorage.h
    src/core/Types.h
    src/core/WorkspaceService.cpp
    src/core/WorkspaceService.h
    src/database/DatabaseManager.cpp
    src/database/DatabaseManager.h
    src/library/AnnotationRepository.cpp
    src/library/AnnotationRepository.h
    src/library/FileRecord.h
    src/library/FileRepository.cpp
    src/library/FileRepository.h
    src/library/LibraryService.cpp
    src/library/LibraryService.h
    src/models/FileListModel.cpp
    src/models/FileListModel.h
    src/search/ContentExtractor.cpp
    src/search/ContentExtractor.h
    src/search/IndexingService.cpp
    src/search/IndexingService.h
    src/search/SemanticSearchService.cpp
    src/search/SemanticSearchService.h
    src/sync/CloudProvider.cpp
    src/sync/CloudProvider.h
    src/sync/CloudSyncService.cpp
    src/sync/CloudSyncService.h
    src/sync/OAuthCallbackServer.cpp
    src/sync/OAuthCallbackServer.h
    src/notes/NoteManager.cpp
    src/notes/NoteManager.h
    src/notes/RichTextFormatter.cpp
    src/notes/RichTextFormatter.h
    src/components/ComponentManager.cpp
    src/components/ComponentManager.h
)
target_include_directories(ellaCore PUBLIC src src/core src/database src/library src/models src/notes)
target_link_libraries(ellaCore PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent Qt6::Sql Qt6::Pdf Qt6::Quick PRIVATE Qt6::CorePrivate)
target_compile_definitions(ellaCore PUBLIC
    ELLA_RELEASE_VERSION="${ELLA_RELEASE_VERSION}"
    ELLA_RELEASE_CHANNEL="${ELLA_RELEASE_CHANNEL}"
    ELLA_BUILD_DATE_UTC="${ELLA_BUILD_DATE_UTC}"
    ELLA_BUILD_ID="${ELLA_BUILD_ID}"
    ELLA_DEVELOPER_CLOUD_SYNC=$<BOOL:${ELLA_DEVELOPER_CLOUD_SYNC}>
)
if(WIN32)
    target_link_libraries(ellaCore PUBLIC crypt32)
endif()
qt_add_executable(appSecondBrain src/main.cpp)
if(WIN32)
    set(ELLA_APP_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/packaging/assets/ella_icon.ico")
    configure_file(packaging/ella_app_icon.rc.in "${CMAKE_CURRENT_BINARY_DIR}/ella_app_icon.rc" @ONLY)
    target_sources(appSecondBrain PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ella_app_icon.rc")
endif()
qt_add_qml_module(appSecondBrain
    URI SecondBrain
    VERSION 1.0
    QML_FILES
        src/ui/EllaTokens.qml
        src/ui/EllaButton.qml
        src/ui/EllaTextField.qml
        src/ui/EllaChip.qml
        src/ui/EllaPanel.qml
        src/ui/EllaModalOverlay.qml
        src/ui/NotesPage.qml
        src/ui/SettingsPage.qml
        src/ui/ImportDialog.qml
        src/ui/ActivityDrawer.qml
        src/ui/Main.qml
        src/ui/BrowserPage.qml
        src/ui/ReaderPage.qml
        src/ui/HomePage.qml
        src/ui/MediaPreviewPanel.qml
    RESOURCES
        src/ui/assets/ASSET_MANIFEST.md
        packaging/assets/ella_icon.ico
        src/ui/assets/ella_icon_256.png
        src/ui/assets/vendor/search.svg
        src/ui/assets/vendor/file-info.svg
        src/ui/assets/vendor/notes.svg
        src/ui/assets/vendor/video-off.svg
        src/ui/assets/vendor/link-off.svg
        src/ui/assets/vendor/database.svg
        src/ui/assets/vendor/empty-memory-state.svg
        src/ui/assets/vendor/no-results-recovery.svg
        src/ui/assets/vendor/preview-unavailable.svg
        src/ui/assets/placeholders/empty-selection.svg
        src/ui/assets/placeholders/no-results.svg
        src/ui/assets/placeholders/media-unavailable.svg
        src/ui/assets/placeholders/unsupported-file.svg
)


target_link_libraries(appSecondBrain PRIVATE ellaCore Qt6::Qml Qt6::QuickControls2 Qt6::PdfQuick)
if(ELLA_ENABLE_MEDIA_PLAYBACK)
    target_link_libraries(appSecondBrain PRIVATE Qt6::Multimedia)
endif()
set_target_properties(appSecondBrain PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)
if(ELLA_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ella_ipo_supported OUTPUT ella_ipo_error)
    if(ella_ipo_supported)
        set_property(TARGET ellaCore appSecondBrain PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endif()
if(BUILD_TESTING)
    foreach(test IN ITEMS MemoryBrowser LibraryBackend Workspace)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/${test}Tests.cpp")
            add_executable(ella${test}Tests tests/${test}Tests.cpp)
            target_link_libraries(ella${test}Tests PRIVATE ellaCore Qt6::Test Qt6::CorePrivate)
            add_test(NAME ella_${test}_tests COMMAND ella${test}Tests)
            set_tests_properties(ella_${test}_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
        endif()
    endforeach()
endif()
if(BUILD_TESTING AND ELLA_BUILD_SCALE_BENCHMARKS)
    add_executable(ellaRetrievalBenchmark tests/RetrievalBenchmark.cpp)
    target_link_libraries(ellaRetrievalBenchmark PRIVATE ellaCore Qt6::Test)
endif()
if(ELLA_BUILD_SEMANTIC_HELPER)
    add_subdirectory(src/components/semantic)
endif()
if(WIN32 AND ELLA_LOCAL_WINDEPLOYQT)
    find_program(ELLA_WINDEPLOYQT_EXECUTABLE NAMES windeployqt HINTS "${Qt6_DIR}/../../../bin" REQUIRED)
    add_custom_command(TARGET appSecondBrain POST_BUILD
        COMMAND "${ELLA_WINDEPLOYQT_EXECUTABLE}" --release --no-translations --qmldir "${CMAKE_SOURCE_DIR}/src/ui" "$<TARGET_FILE:appSecondBrain>" VERBATIM)
endif()
include(GNUInstallDirs)
install(TARGETS appSecondBrain RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION .)
install(FILES packaging/qt.conf packaging/assets/ella_icon.ico DESTINATION ${CMAKE_INSTALL_BINDIR})
