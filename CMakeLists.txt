cmake_minimum_required(VERSION 3.16)

project(SecondBrain VERSION 0.9.0 LANGUAGES CXX)

set(ELLA_RELEASE_VERSION "0.9.0-beta.1" CACHE STRING "ELLA release version")
set(ELLA_RELEASE_CHANNEL "beta" CACHE STRING "ELLA release channel")
set(ELLA_BUILD_ID "local-dev" CACHE STRING "ELLA build identifier")
string(TIMESTAMP ELLA_BUILD_DATE_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(ELLA_LOCAL_WINDEPLOYQT "Run windeployqt after build for local Windows execution" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Multimedia
    Network
    Qml
    Quick
    QuickControls2
    Concurrent
    Sql
    Pdf
    PdfQuick
    Test
)
find_package(Qt6 6.5 QUIET COMPONENTS MultimediaQuick)

qt_standard_project_setup(REQUIRES 6.5)
include(CTest)

qt_add_executable(appSecondBrain
    src/main.cpp
    src/core/AppConfig.h
    src/core/AppConfig.cpp
    src/core/SecureStorage.h
    src/core/SecureStorage.cpp
    src/core/Types.h
    src/database/DatabaseManager.h
    src/database/DatabaseManager.cpp
    src/library/FileRecord.h
    src/library/FileRepository.h
    src/library/FileRepository.cpp
    src/library/AnnotationRepository.h
    src/library/AnnotationRepository.cpp
    src/models/FileListModel.h
    src/models/FileListModel.cpp
    src/search/ContentExtractor.h
    src/search/ContentExtractor.cpp
    src/search/IndexingService.h
    src/search/IndexingService.cpp
    src/sync/CloudSyncService.h
    src/sync/CloudSyncService.cpp
    src/sync/CloudProvider.h
    src/sync/CloudProvider.cpp
    src/sync/OAuthCallbackServer.h
    src/sync/OAuthCallbackServer.cpp
    src/notes/NoteManager.h
    src/notes/NoteManager.cpp
    src/notes/RichTextFormatter.h
    src/notes/RichTextFormatter.cpp
)

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
        src/ui/Main.qml
        src/ui/BrowserPage.qml
        src/ui/ReaderPage.qml
        src/ui/HomePage.qml
        src/ui/MediaPreviewPanel.qml
    RESOURCES
        src/ui/assets/ASSET_MANIFEST.md
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

target_include_directories(appSecondBrain PRIVATE
    src
    src/core
    src/database
    src/library
    src/models
    src/notes
)

target_link_libraries(appSecondBrain PRIVATE
    Qt6::Core
    Qt6::Multimedia
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Concurrent
    Qt6::Sql
    Qt6::Pdf
    Qt6::PdfQuick
)

target_compile_definitions(appSecondBrain PRIVATE
    ELLA_RELEASE_VERSION="${ELLA_RELEASE_VERSION}"
    ELLA_RELEASE_CHANNEL="${ELLA_RELEASE_CHANNEL}"
    ELLA_BUILD_DATE_UTC="${ELLA_BUILD_DATE_UTC}"
    ELLA_BUILD_ID="${ELLA_BUILD_ID}"
)

if (WIN32)
    target_link_libraries(appSecondBrain PRIVATE crypt32)
endif()

if (BUILD_TESTING)
    add_executable(ellaMemoryBrowserTests
        tests/MemoryBrowserTests.cpp
        src/core/AppConfig.h
        src/core/AppConfig.cpp
        src/core/SecureStorage.h
        src/core/SecureStorage.cpp
        src/core/Types.h
        src/database/DatabaseManager.h
        src/database/DatabaseManager.cpp
        src/library/FileRecord.h
        src/library/FileRepository.h
        src/library/FileRepository.cpp
        src/library/AnnotationRepository.h
        src/library/AnnotationRepository.cpp
        src/models/FileListModel.h
        src/models/FileListModel.cpp
        src/search/ContentExtractor.h
        src/search/ContentExtractor.cpp
        src/search/IndexingService.h
        src/search/IndexingService.cpp
        src/sync/CloudSyncService.h
        src/sync/CloudSyncService.cpp
        src/sync/CloudProvider.h
        src/sync/CloudProvider.cpp
    )

    target_include_directories(ellaMemoryBrowserTests PRIVATE
        src
        src/core
        src/database
        src/library
        src/models
        src/search
        src/sync
    )

    target_link_libraries(ellaMemoryBrowserTests PRIVATE
        Qt6::Core
        Qt6::Multimedia
        Qt6::Network
        Qt6::Concurrent
        Qt6::Sql
        Qt6::Pdf
        Qt6::Test
    )

    target_compile_definitions(ellaMemoryBrowserTests PRIVATE
        ELLA_RELEASE_VERSION="${ELLA_RELEASE_VERSION}"
        ELLA_RELEASE_CHANNEL="${ELLA_RELEASE_CHANNEL}"
        ELLA_BUILD_DATE_UTC="${ELLA_BUILD_DATE_UTC}"
        ELLA_BUILD_ID="${ELLA_BUILD_ID}"
    )

    if (WIN32)
        target_link_libraries(ellaMemoryBrowserTests PRIVATE crypt32)
    endif()

    add_test(NAME ella_memory_browser_tests COMMAND ellaMemoryBrowserTests)
endif()

if (TARGET Qt6::MultimediaQuick)
    target_link_libraries(appSecondBrain PRIVATE Qt6::MultimediaQuick)
    if (BUILD_TESTING)
        target_link_libraries(ellaMemoryBrowserTests PRIVATE Qt6::MultimediaQuick)
    endif()
endif()

set_target_properties(appSecondBrain PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

set(ELLA_QT_CONF "${CMAKE_SOURCE_DIR}/packaging/qt.conf")

if (WIN32)
    # Ensure dynamic media preview can always resolve the QtMultimedia QML module.
    add_custom_command(TARGET appSecondBrain POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Multimedia>"
            "$<TARGET_FILE_DIR:appSecondBrain>"
    )

    add_custom_command(TARGET appSecondBrain POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:appSecondBrain>/qml/QtMultimedia"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:Qt6::Multimedia>/../qml/QtMultimedia"
            "$<TARGET_FILE_DIR:appSecondBrain>/qml/QtMultimedia"
    )

    add_custom_command(TARGET appSecondBrain POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:appSecondBrain>/multimedia"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:Qt6::Multimedia>/../plugins/multimedia"
            "$<TARGET_FILE_DIR:appSecondBrain>/multimedia"
    )

    if (ELLA_LOCAL_WINDEPLOYQT)
        find_program(ELLA_WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt.exe windeployqt
            HINTS
                "${Qt6_DIR}/../../../bin"
                "${Qt6_DIR}/../../../../bin"
        )

        if (ELLA_WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET appSecondBrain POST_BUILD
                COMMAND "${ELLA_WINDEPLOYQT_EXECUTABLE}"
                    --qmldir "${CMAKE_SOURCE_DIR}/src/ui"
                    --no-translations
                    "$<TARGET_FILE:appSecondBrain>"
                COMMENT "Deploying Qt runtime for local appSecondBrain execution"
                VERBATIM
            )
        else()
            message(WARNING "windeployqt not found; running appSecondBrain.exe directly may require Qt bin on PATH.")
        endif()
    endif()
endif()

set(ELLA_TESSERACT_DIR "${CMAKE_SOURCE_DIR}/tools/tesseract")
if (EXISTS "${ELLA_TESSERACT_DIR}/tesseract.exe")
    add_custom_command(TARGET appSecondBrain POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:appSecondBrain>/tools/tesseract"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ELLA_TESSERACT_DIR}"
            "$<TARGET_FILE_DIR:appSecondBrain>/tools/tesseract"
    )
endif()

set(ELLA_FFMPEG_DIR "${CMAKE_SOURCE_DIR}/tools/ffmpeg")
if (EXISTS "${ELLA_FFMPEG_DIR}")
    add_custom_command(TARGET appSecondBrain POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:appSecondBrain>/tools/ffmpeg"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ELLA_FFMPEG_DIR}"
            "$<TARGET_FILE_DIR:appSecondBrain>/tools/ffmpeg"
    )
endif()

set(ELLA_WHISPER_DIR "${CMAKE_SOURCE_DIR}/tools/whisper")
if (EXISTS "${ELLA_WHISPER_DIR}")
    add_custom_command(TARGET appSecondBrain POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:appSecondBrain>/tools/whisper"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ELLA_WHISPER_DIR}"
            "$<TARGET_FILE_DIR:appSecondBrain>/tools/whisper"
    )
endif()

set(ELLA_LIBREOFFICE_DIR "${CMAKE_SOURCE_DIR}/tools/libreoffice")
if (EXISTS "${ELLA_LIBREOFFICE_DIR}")
    add_custom_command(TARGET appSecondBrain POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:appSecondBrain>/tools/libreoffice"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ELLA_LIBREOFFICE_DIR}"
            "$<TARGET_FILE_DIR:appSecondBrain>/tools/libreoffice"
    )
endif()

include(GNUInstallDirs)

install(TARGETS appSecondBrain
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if (EXISTS "${ELLA_QT_CONF}")
    install(FILES "${ELLA_QT_CONF}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if (WIN32)
    install(FILES "$<TARGET_FILE:Qt6::Multimedia>" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY "$<TARGET_FILE_DIR:Qt6::Multimedia>/../qml/QtMultimedia"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/qml")
    install(DIRECTORY "$<TARGET_FILE_DIR:Qt6::Multimedia>/../plugins/multimedia"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if (EXISTS "${ELLA_TESSERACT_DIR}/tesseract.exe")
    install(DIRECTORY "${ELLA_TESSERACT_DIR}/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/tools/tesseract")
endif()

if (EXISTS "${ELLA_FFMPEG_DIR}")
    install(DIRECTORY "${ELLA_FFMPEG_DIR}/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/tools/ffmpeg")
endif()

if (EXISTS "${ELLA_WHISPER_DIR}")
    install(DIRECTORY "${ELLA_WHISPER_DIR}/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/tools/whisper")
endif()

if (EXISTS "${ELLA_LIBREOFFICE_DIR}")
    install(DIRECTORY "${ELLA_LIBREOFFICE_DIR}/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/tools/libreoffice")
endif()
