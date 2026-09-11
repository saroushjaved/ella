#include <QtTest/QtTest>

#include "core/AppConfig.h"
#include "database/DatabaseManager.h"
#include "models/FileListModel.h"
#include "search/ContentExtractor.h"
#include "search/IndexingService.h"
#include "library/LibraryService.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>

class MemoryBrowserTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void importFolderRecursive_ImportsNestedFiles();
    void search_ProvidesSnippetAndMatchReason();
    void importFiles_TracksInvalidAndLongPaths();
    void retrievalEvents_ArePersisted();
    void homeRecentActivity_UsesRetrievalEvents();
    void contentExtractor_DetectsMediaAndPresentationTypes();
    void contentExtractor_RuntimeStatusHonorsEnvOverrides();
    void transcriptNormalization_AddsTimestampTags();
    void presentationCachePath_ChangesWhenSourceChanges();
    void mediaTimelineAnnotations_ArePersisted();
    void mediaTimelineAnnotations_RejectInvalidTimeValues();
    void searchHealth_ExposesMediaToolingKeys();
    void removeFile_CleansAssociatedRows();
    void exportDiagnosticsBundle_CreatesArchive();
    void releaseMetadata_IsAvailable();
    void sqlSearch_PhrasesPaginationFiltersAndRules();
    void asyncSearch_DiscardsStaleQueries();
    void incrementalIndex_UsesCurrentFileMetadata();
    void libraryJobs_MoveIdentityDuplicateReviewAndUndo();
    void libraryPreview_ExcludesFolders();
    void hybridSearch_FusesRanksAndKeepsPassageAnchor();

private:
    void clearTables();
    QString writeTextFile(const QString& path, const QString& content);
    int countRows(const QString& sql) const;
    int countEvents(const QString& eventType) const;
};

void MemoryBrowserTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("EllaTests"));
    QCoreApplication::setApplicationName(QStringLiteral("EllaMemoryBrowserTests"));

    const QString testAppDataDir = QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("test_runtime/ella_memory_browser_tests"));
    QDir runtimeRoot(testAppDataDir);
    if (runtimeRoot.exists()) {
        runtimeRoot.removeRecursively();
    }
    QVERIFY(QDir().mkpath(testAppDataDir));

    qputenv("ELLA_APP_DATA_DIR", QDir(testAppDataDir).absolutePath().toUtf8());

    const QString dbPath = AppConfig::databasePath();
    QVERIFY2(!dbPath.trimmed().isEmpty(), "DB path is empty");
    const QFileInfo dbInfo(dbPath);
    QDir().mkpath(dbInfo.absolutePath());
    QFile::remove(dbPath);
    QVERIFY2(DatabaseManager::instance().initialize(), "Failed to initialize test database");
    clearTables();
}

void MemoryBrowserTests::init()
{
    clearTables();
}

void MemoryBrowserTests::importFolderRecursive_ImportsNestedFiles()
{
    QTemporaryDir tempDir(QDir(AppConfig::appDataDirectory()).filePath(
        QStringLiteral("tmp-import-folder-XXXXXX")));
    QVERIFY(tempDir.isValid());

    QDir root(tempDir.path());
    QVERIFY(root.mkpath(QStringLiteral("semester1/control/week2")));
    QVERIFY(root.mkpath(QStringLiteral("semester1/signals")));

    QVERIFY(!writeTextFile(root.filePath(QStringLiteral("semester1/control/overview.txt")),
                           QStringLiteral("control systems overview"))
                 .isEmpty());
    QVERIFY(!writeTextFile(root.filePath(QStringLiteral("semester1/control/week2/laplace.md")),
                           QStringLiteral("laplace transform refresher"))
                 .isEmpty());
    QVERIFY(!writeTextFile(root.filePath(QStringLiteral("semester1/signals/bode.txt")),
                           QStringLiteral("bode plot notes"))
                 .isEmpty());

    FileListModel model;
    const QVariantMap result = model.importFolder(tempDir.path());

    QCOMPARE(result.value(QStringLiteral("importedCount")).toInt(), 3);
    QCOMPARE(result.value(QStringLiteral("failedCount")).toInt(), 0);
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) FROM files")), 3);
}

void MemoryBrowserTests::search_ProvidesSnippetAndMatchReason()
{
    QTemporaryDir tempDir(QDir(AppConfig::appDataDirectory()).filePath(
        QStringLiteral("tmp-search-snippet-XXXXXX")));
    QVERIFY(tempDir.isValid());

    const QString filePath = writeTextFile(
        QDir(tempDir.path()).filePath(QStringLiteral("control_memory.txt")),
        QStringLiteral("Laplace transform tables and control-system intuition."));
    QVERIFY(!filePath.isEmpty());

    IndexingService indexingService;
    FileListModel model;
    model.setIndexingService(&indexingService);

    QVariantList paths;
    paths.append(filePath);
    const QVariantMap importResult = model.importFiles(paths);
    QCOMPARE(importResult.value(QStringLiteral("importedCount")).toInt(), 1);

    QTRY_VERIFY_WITH_TIMEOUT(
        !model.indexStatus().value(QStringLiteral("running")).toBool()
            && model.indexStatus().value(QStringLiteral("queued")).toInt() == 0,
        20000);

    model.search(QStringLiteral("laplace"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.rowCount() > 0, 5000);

    const QVariantMap first = model.get(0);
    QVERIFY(!first.value(QStringLiteral("searchMatchReason")).toString().trimmed().isEmpty());
    QVERIFY(!first.value(QStringLiteral("searchSnippet")).toString().trimmed().isEmpty());
}

void MemoryBrowserTests::importFiles_TracksInvalidAndLongPaths()
{
    QTemporaryDir tempDir(QDir(AppConfig::appDataDirectory()).filePath(
        QStringLiteral("tmp-import-guardrails-XXXXXX")));
    QVERIFY(tempDir.isValid());

    const QString validPath = writeTextFile(
        QDir(tempDir.path()).filePath(QStringLiteral("valid-entry.txt")),
        QStringLiteral("valid"));
    QVERIFY(!validPath.isEmpty());

    const QString longPath = QString(1100, QChar('a')) + QStringLiteral(".txt");

    QVariantList candidates;
    candidates.append(validPath);
    candidates.append(QString());             // invalid
    candidates.append(longPath);             // too long
    candidates.append(QStringLiteral("file:///")); // invalid

    FileListModel model;
    const QVariantMap result = model.importFiles(candidates);

    QCOMPARE(result.value(QStringLiteral("importedCount")).toInt(), 1);
    QVERIFY(result.value(QStringLiteral("failedCount")).toInt() >= 2);
    QVERIFY(result.value(QStringLiteral("pathTooLongCount")).toInt() >= 1);
    QVERIFY(result.value(QStringLiteral("invalidPathCount")).toInt() >= 1);
    QVERIFY(result.contains(QStringLiteral("processedCandidates")));
    QVERIFY(result.contains(QStringLiteral("truncated")));
}

void MemoryBrowserTests::retrievalEvents_ArePersisted()
{
    QTemporaryDir tempDir(QDir(AppConfig::appDataDirectory()).filePath(
        QStringLiteral("tmp-retrieval-events-XXXXXX")));
    QVERIFY(tempDir.isValid());

    const QString filePath = writeTextFile(
        QDir(tempDir.path()).filePath(QStringLiteral("signals.txt")),
        QStringLiteral("Bode plot, gain margin, phase margin."));
    QVERIFY(!filePath.isEmpty());

    IndexingService indexingService;
    FileListModel model;
    model.setIndexingService(&indexingService);

    QVariantList paths;
    paths.append(filePath);
    const QVariantMap importResult = model.importFiles(paths);
    QCOMPARE(importResult.value(QStringLiteral("importedCount")).toInt(), 1);

    QTRY_VERIFY_WITH_TIMEOUT(
        !model.indexStatus().value(QStringLiteral("running")).toBool()
            && model.indexStatus().value(QStringLiteral("queued")).toInt() == 0,
        20000);

    model.search(QStringLiteral("bode"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.rowCount() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(countEvents(QStringLiteral("query")) >= 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(countEvents(QStringLiteral("time_to_first_result")) >= 1, 5000);

    const int fileId = model.get(0).value(QStringLiteral("id")).toInt();
    QVERIFY(fileId >= 0);

    QVERIFY(model.trackRetrievalEvent(
        QStringLiteral("opened_source"),
        QStringLiteral("bode"),
        fileId,
        -1,
        QString(),
        QStringLiteral("{\"surface\":\"test\"}")));
    QVERIFY(model.trackRetrievalEvent(
        QStringLiteral("useful_result"),
        QStringLiteral("bode"),
        fileId,
        -1,
        QStringLiteral("useful"),
        QStringLiteral("{\"surface\":\"test\"}")));

    QCOMPARE(countEvents(QStringLiteral("opened_source")), 1);
    QCOMPARE(countEvents(QStringLiteral("useful_result")), 1);
}

void MemoryBrowserTests::homeRecentActivity_UsesRetrievalEvents()
{
    QTemporaryDir tempDir(QDir(AppConfig::appDataDirectory()).filePath(
        QStringLiteral("tmp-home-activity-XXXXXX")));
    QVERIFY(tempDir.isValid());

    const QString filePath = writeTextFile(
        QDir(tempDir.path()).filePath(QStringLiteral("memory_item.txt")),
        QStringLiteral("Archive memory for home activity checks."));
    QVERIFY(!filePath.isEmpty());

    FileListModel model;
    QVariantList paths;
    paths.append(filePath);
    const QVariantMap importResult = model.importFiles(paths);
    QCOMPARE(importResult.value(QStringLiteral("importedCount")).toInt(), 1);

    model.search(QString());
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.rowCount() > 0, 5000);
    const int fileId = model.get(0).value(QStringLiteral("id")).toInt();
    QVERIFY(fileId >= 0);

    QVERIFY(model.trackRetrievalEvent(
        QStringLiteral("query"),
        QStringLiteral("laplace"),
        -1,
        -1,
        QString(),
        QStringLiteral("{\"surface\":\"test\"}")));
    QVERIFY(model.trackRetrievalEvent(
        QStringLiteral("query"),
        QStringLiteral("laplace"),
        -1,
        -1,
        QString(),
        QStringLiteral("{\"surface\":\"test\"}")));
    QVERIFY(model.trackRetrievalEvent(
        QStringLiteral("query"),
        QStringLiteral("bode"),
        -1,
        -1,
        QString(),
        QStringLiteral("{\"surface\":\"test\"}")));
    QVERIFY(model.trackRetrievalEvent(
        QStringLiteral("opened_source"),
        QStringLiteral("laplace"),
        fileId,
        -1,
        QString(),
        QStringLiteral("{\"surface\":\"test\"}")));

    const QVariantList recentQueries = model.recentRetrievalQueries(5);
    QVERIFY(!recentQueries.isEmpty());

    bool foundLaplace = false;
    for (const QVariant& item : recentQueries) {
        const QVariantMap map = item.toMap();
        if (map.value(QStringLiteral("queryText")).toString() == QStringLiteral("laplace")) {
            foundLaplace = true;
            QVERIFY(map.value(QStringLiteral("hitCount")).toInt() >= 2);
            break;
        }
    }
    QVERIFY(foundLaplace);

    const QVariantList recentOpened = model.recentOpenedSources(5);
    QVERIFY(!recentOpened.isEmpty());

    bool foundOpened = false;
    for (const QVariant& item : recentOpened) {
        const QVariantMap map = item.toMap();
        if (map.value(QStringLiteral("fileId")).toInt() == fileId) {
            foundOpened = true;
            QVERIFY(!map.value(QStringLiteral("name")).toString().trimmed().isEmpty());
            break;
        }
    }
    QVERIFY(foundOpened);
}

void MemoryBrowserTests::contentExtractor_DetectsMediaAndPresentationTypes()
{
    QTemporaryDir tempDir(QDir(AppConfig::appDataDirectory()).filePath(
        QStringLiteral("tmp-media-detection-XXXXXX")));
    QVERIFY(tempDir.isValid());

    const QString videoPath =
        writeTextFile(QDir(tempDir.path()).filePath(QStringLiteral("lecture.mp4")), QStringLiteral("video"));
    const QString audioPath =
        writeTextFile(QDir(tempDir.path()).filePath(QStringLiteral("lecture.mp3")), QStringLiteral("audio"));
    const QString pptPath =
        writeTextFile(QDir(tempDir.path()).filePath(QStringLiteral("slides.pptx")), QStringLiteral("pptx"));

    QVERIFY(!videoPath.isEmpty());
    QVERIFY(!audioPath.isEmpty());
    QVERIFY(!pptPath.isEmpty());

    ContentExtractor extractor;

    const ContentExtractor::Result videoResult = extractor.extract(videoPath, QString(), QStringLiteral("mp4"));
    const ContentExtractor::Result audioResult = extractor.extract(audioPath, QString(), QStringLiteral("mp3"));
    const ContentExtractor::Result pptResult = extractor.extract(
        pptPath,
        QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.presentation"),
        QStringLiteral("pptx"));

    QCOMPARE(videoResult.extractor, QStringLiteral("video-whisper"));
    QCOMPARE(audioResult.extractor, QStringLiteral("audio-whisper"));
    QCOMPARE(pptResult.extractor, QStringLiteral("pptx-xml"));
    QVERIFY(!pptResult.error.isEmpty());
}

void MemoryBrowserTests::contentExtractor_RuntimeStatusHonorsEnvOverrides()
{
    const bool hadFfmpegPath = qEnvironmentVariableIsSet("ELLA_FFMPEG_PATH");
    const bool hadWhisperPath = qEnvironmentVariableIsSet("ELLA_WHISPER_PATH");
    const bool hadWhisperModelPath = qEnvironmentVariableIsSet("ELLA_WHISPER_MODEL_PATH");
    const bool hadLibreOfficePath = qEnvironmentVariableIsSet("ELLA_LIBREOFFICE_PATH");

    const QByteArray oldFfmpegPath = qgetenv("ELLA_FFMPEG_PATH");
    const QByteArray oldWhisperPath = qgetenv("ELLA_WHISPER_PATH");
    const QByteArray oldWhisperModelPath = qgetenv("ELLA_WHISPER_MODEL_PATH");
    const QByteArray oldLibreOfficePath = qgetenv("ELLA_LIBREOFFICE_PATH");

    QTemporaryDir tempDir(QDir(AppConfig::appDataDirectory()).filePath(
        QStringLiteral("tmp-tool-overrides-XXXXXX")));
    QVERIFY(tempDir.isValid());

    const QString ffmpegPath =
        writeTextFile(QDir(tempDir.path()).filePath(QStringLiteral("ffmpeg.exe")), QStringLiteral("ffmpeg"));
    const QString whisperPath = writeTextFile(
        QDir(tempDir.path()).filePath(QStringLiteral("whisper-cli.exe")),
        QStringLiteral("whisper"));
    const QString whisperModelPath = writeTextFile(
        QDir(tempDir.path()).filePath(QStringLiteral("ggml-base.en.bin")),
        QStringLiteral("model"));
    const QString libreOfficePath =
        writeTextFile(QDir(tempDir.path()).filePath(QStringLiteral("soffice.exe")), QStringLiteral("soffice"));

    QVERIFY(!ffmpegPath.isEmpty());
    QVERIFY(!whisperPath.isEmpty());
    QVERIFY(!whisperModelPath.isEmpty());
    QVERIFY(!libreOfficePath.isEmpty());

    qputenv("ELLA_FFMPEG_PATH", ffmpegPath.toUtf8());
    qputenv("ELLA_WHISPER_PATH", whisperPath.toUtf8());
    qputenv("ELLA_WHISPER_MODEL_PATH", whisperModelPath.toUtf8());
    qputenv("ELLA_LIBREOFFICE_PATH", libreOfficePath.toUtf8());

    const QVariantMap status = ContentExtractor::runtimeEnvironmentStatus();

    QCOMPARE(status.value(QStringLiteral("ffmpegPath")).toString(), QFileInfo(ffmpegPath).absoluteFilePath());
    QCOMPARE(status.value(QStringLiteral("whisperPath")).toString(), QFileInfo(whisperPath).absoluteFilePath());
    QCOMPARE(status.value(QStringLiteral("whisperModelPath")).toString(),
             QFileInfo(whisperModelPath).absoluteFilePath());
    QCOMPARE(status.value(QStringLiteral("libreOfficePath")).toString(),
             QFileInfo(libreOfficePath).absoluteFilePath());

    QVERIFY(status.value(QStringLiteral("ffmpegAvailable")).toBool());
    QVERIFY(status.value(QStringLiteral("whisperAvailable")).toBool());
    QVERIFY(status.value(QStringLiteral("whisperModelAvailable")).toBool());
    QVERIFY(status.value(QStringLiteral("mediaTranscriptionReady")).toBool());
    QVERIFY(status.value(QStringLiteral("libreOfficeAvailable")).toBool());
    QVERIFY(status.value(QStringLiteral("pptConversionReady")).toBool());

    if (hadFfmpegPath) {
        qputenv("ELLA_FFMPEG_PATH", oldFfmpegPath);
    } else {
        qunsetenv("ELLA_FFMPEG_PATH");
    }
    if (hadWhisperPath) {
        qputenv("ELLA_WHISPER_PATH", oldWhisperPath);
    } else {
        qunsetenv("ELLA_WHISPER_PATH");
    }
    if (hadWhisperModelPath) {
        qputenv("ELLA_WHISPER_MODEL_PATH", oldWhisperModelPath);
    } else {
        qunsetenv("ELLA_WHISPER_MODEL_PATH");
    }
    if (hadLibreOfficePath) {
        qputenv("ELLA_LIBREOFFICE_PATH", oldLibreOfficePath);
    } else {
        qunsetenv("ELLA_LIBREOFFICE_PATH");
    }
}

void MemoryBrowserTests::transcriptNormalization_AddsTimestampTags()
{
    const QString srt = QStringLiteral(
        "1\n"
        "00:00:01,120 --> 00:00:02,640\n"
        "Hello world\n"
        "\n"
        "2\n"
        "00:00:03,000 --> 00:00:04,000\n"
        "Second line\n");

    const QString normalized = ContentExtractor::normalizeTranscriptFromSrt(srt);
    QVERIFY(normalized.contains(QStringLiteral("[00:00:01] Hello world")));
    QVERIFY(normalized.contains(QStringLiteral("[00:00:03] Second line")));
}

void MemoryBrowserTests::presentationCachePath_ChangesWhenSourceChanges()
{
    QTemporaryDir tempDir(QDir(AppConfig::appDataDirectory()).filePath(
        QStringLiteral("tmp-presentation-cache-XXXXXX")));
    QVERIFY(tempDir.isValid());

    const QString pptPath =
        writeTextFile(QDir(tempDir.path()).filePath(QStringLiteral("deck.pptx")), QStringLiteral("v1"));
    QVERIFY(!pptPath.isEmpty());

    const QString firstCachePath = ContentExtractor::presentationCachePathForFile(pptPath);
    QVERIFY(!firstCachePath.trimmed().isEmpty());

    // Change both size and modified time to force a new cache key.
    QVERIFY(!writeTextFile(pptPath, QStringLiteral("v2-more-content")).isEmpty());

    const QString secondCachePath = ContentExtractor::presentationCachePathForFile(pptPath);
    QVERIFY(!secondCachePath.trimmed().isEmpty());
    QVERIFY(firstCachePath != secondCachePath);
}

void MemoryBrowserTests::mediaTimelineAnnotations_ArePersisted()
{
    QTemporaryDir tempDir(QDir(AppConfig::appDataDirectory()).filePath(
        QStringLiteral("tmp-media-annotation-XXXXXX")));
    QVERIFY(tempDir.isValid());

    const QString audioPath =
        QDir(tempDir.path()).filePath(QStringLiteral("lecture-note.mp3"));
    QFile audioFile(audioPath);
    QVERIFY(audioFile.open(QIODevice::WriteOnly));
    audioFile.write("dummy-media");
    audioFile.close();

    FileListModel model;
    QVariantList paths;
    paths.append(audioPath);
    const QVariantMap importResult = model.importFiles(paths);
    QCOMPARE(importResult.value(QStringLiteral("importedCount")).toInt(), 1);

    model.search(QString());
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.rowCount() > 0, 5000);
    const int fileId = model.get(0).value(QStringLiteral("id")).toInt();
    QVERIFY(fileId >= 0);

    QVERIFY(model.addAnnotation(fileId,
                                QStringLiteral("audio"),
                                QStringLiteral("time-note"),
                                -1,
                                -1,
                                -1,
                                QString(),
                                0.0,
                                0.0,
                                0.0,
                                0.0,
                                QStringLiteral("#00ffaa"),
                                QStringLiteral("Intro"),
                                12000,
                                12000));

    QVERIFY(model.addAnnotation(fileId,
                                QStringLiteral("audio"),
                                QStringLiteral("time-range"),
                                -1,
                                -1,
                                -1,
                                QString(),
                                0.0,
                                0.0,
                                0.0,
                                0.0,
                                QStringLiteral("#00ffaa"),
                                QStringLiteral("Key section"),
                                30000,
                                45000));

    QVERIFY(!model.addAnnotation(fileId,
                                 QStringLiteral("audio"),
                                 QStringLiteral("time-range"),
                                 -1,
                                 -1,
                                 -1,
                                 QString(),
                                 0.0,
                                 0.0,
                                 0.0,
                                 0.0,
                                 QStringLiteral("#00ffaa"),
                                 QStringLiteral("Invalid range"),
                                 50000,
                                 40000));

    const QVariantList annotations = model.getAnnotations(fileId, QStringLiteral("audio"));
    QCOMPARE(annotations.size(), 2);

    bool sawPointNote = false;
    bool sawRangeNote = false;
    for (const QVariant& entry : annotations) {
        const QVariantMap item = entry.toMap();
        if (item.value(QStringLiteral("annotationType")).toString() == QStringLiteral("time-note")) {
            sawPointNote = true;
            QCOMPARE(item.value(QStringLiteral("timeStartMs")).toLongLong(), 12000ll);
            QCOMPARE(item.value(QStringLiteral("timeEndMs")).toLongLong(), 12000ll);
        } else if (item.value(QStringLiteral("annotationType")).toString() == QStringLiteral("time-range")) {
            sawRangeNote = true;
            QCOMPARE(item.value(QStringLiteral("timeStartMs")).toLongLong(), 30000ll);
            QCOMPARE(item.value(QStringLiteral("timeEndMs")).toLongLong(), 45000ll);
        }
    }

    QVERIFY(sawPointNote);
    QVERIFY(sawRangeNote);
}

void MemoryBrowserTests::mediaTimelineAnnotations_RejectInvalidTimeValues()
{
    QTemporaryDir tempDir(QDir(AppConfig::appDataDirectory()).filePath(
        QStringLiteral("tmp-media-invalid-annotation-XXXXXX")));
    QVERIFY(tempDir.isValid());

    const QString audioPath =
        QDir(tempDir.path()).filePath(QStringLiteral("lecture-invalid.mp3"));
    QFile audioFile(audioPath);
    QVERIFY(audioFile.open(QIODevice::WriteOnly));
    audioFile.write("dummy-media");
    audioFile.close();

    FileListModel model;
    QVariantList paths;
    paths.append(audioPath);
    const QVariantMap importResult = model.importFiles(paths);
    QCOMPARE(importResult.value(QStringLiteral("importedCount")).toInt(), 1);

    model.search(QString());
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.rowCount() > 0, 5000);
    const int fileId = model.get(0).value(QStringLiteral("id")).toInt();
    QVERIFY(fileId >= 0);

    QVERIFY(!model.addAnnotation(fileId,
                                 QStringLiteral("audio"),
                                 QStringLiteral("time-note"),
                                 -1,
                                 -1,
                                 -1,
                                 QString(),
                                 0.0,
                                 0.0,
                                 0.0,
                                 0.0,
                                 QStringLiteral("#00ffaa"),
                                 QStringLiteral("Invalid time note"),
                                 -1,
                                 -1));

    QVERIFY(!model.addAnnotation(fileId,
                                 QStringLiteral("audio"),
                                 QStringLiteral("time-range"),
                                 -1,
                                 -1,
                                 -1,
                                 QString(),
                                 0.0,
                                 0.0,
                                 0.0,
                                 0.0,
                                 QStringLiteral("#00ffaa"),
                                 QStringLiteral("Equal start/end"),
                                 25000,
                                 25000));

    QVERIFY(!model.addAnnotation(fileId,
                                 QStringLiteral("audio"),
                                 QStringLiteral("time-range"),
                                 -1,
                                 -1,
                                 -1,
                                 QString(),
                                 0.0,
                                 0.0,
                                 0.0,
                                 0.0,
                                 QStringLiteral("#00ffaa"),
                                 QStringLiteral("End before start"),
                                 42000,
                                 41000));

    const QVariantList annotations = model.getAnnotations(fileId, QStringLiteral("audio"));
    QVERIFY(annotations.isEmpty());
}

void MemoryBrowserTests::searchHealth_ExposesMediaToolingKeys()
{
    FileListModel model;
    const QVariantMap health = model.searchHealth();

    QVERIFY(health.contains(QStringLiteral("ffmpegAvailable")));
    QVERIFY(health.contains(QStringLiteral("whisperAvailable")));
    QVERIFY(health.contains(QStringLiteral("whisperModelAvailable")));
    QVERIFY(health.contains(QStringLiteral("mediaTranscriptionReady")));
    QVERIFY(health.contains(QStringLiteral("libreOfficeAvailable")));
    QVERIFY(health.contains(QStringLiteral("pptConversionReady")));
    QVERIFY(health.contains(QStringLiteral("videoWhisperIndexed")));
    QVERIFY(health.contains(QStringLiteral("audioWhisperIndexed")));
    QVERIFY(health.contains(QStringLiteral("pptPdfIndexed")));
    QVERIFY(health.contains(QStringLiteral("pptPdfOcrIndexed")));
}

void MemoryBrowserTests::removeFile_CleansAssociatedRows()
{
    QTemporaryDir tempDir(QDir(AppConfig::appDataDirectory()).filePath(
        QStringLiteral("tmp-remove-file-XXXXXX")));
    QVERIFY(tempDir.isValid());

    const QString filePath = writeTextFile(
        QDir(tempDir.path()).filePath(QStringLiteral("cleanup-target.txt")),
        QStringLiteral("cleanup target"));
    QVERIFY(!filePath.isEmpty());

    FileListModel model;
    QVariantList paths;
    paths.append(filePath);
    const QVariantMap importResult = model.importFiles(paths);
    QCOMPARE(importResult.value(QStringLiteral("importedCount")).toInt(), 1);

    model.search(QString());
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.rowCount() > 0, 5000);
    const int fileId = model.get(0).value(QStringLiteral("id")).toInt();
    QVERIFY(fileId >= 0);

    QVERIFY(model.addCollection(QStringLiteral("QA Collection"), -1));
    const QVariantList pickerOptions = model.getCollectionPickerOptions();
    QVERIFY(!pickerOptions.isEmpty());
    const int collectionId = pickerOptions.first().toMap().value(QStringLiteral("id")).toInt();
    QVERIFY(collectionId >= 0);

    QVERIFY(model.assignCollection(0, collectionId));
    QVERIFY(model.addDocumentNote(fileId, QStringLiteral("title"), QStringLiteral("body")));
    QVERIFY(model.addAnnotation(fileId,
                                QStringLiteral("text"),
                                QStringLiteral("highlight"),
                                1,
                                0,
                                4,
                                QStringLiteral("clea"),
                                0.0,
                                0.0,
                                0.0,
                                0.0,
                                QStringLiteral("#fff59d"),
                                QStringLiteral("note")));
    QVERIFY(model.trackRetrievalEvent(QStringLiteral("opened_source"),
                                      QStringLiteral("cleanup"),
                                      fileId,
                                      -1,
                                      QString(),
                                      QStringLiteral("{\"surface\":\"test\"}")));

    QVERIFY(model.removeFile(0));
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) FROM files WHERE id = %1").arg(fileId)), 0);
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) FROM file_collections WHERE file_id = %1").arg(fileId)), 0);
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) FROM annotations WHERE file_id = %1").arg(fileId)), 0);
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) FROM document_notes WHERE file_id = %1").arg(fileId)), 0);
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) FROM retrieval_events WHERE file_id = %1").arg(fileId)), 0);
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) FROM file_index_state WHERE file_id = %1").arg(fileId)), 0);
}

void MemoryBrowserTests::exportDiagnosticsBundle_CreatesArchive()
{
    FileListModel model;
    const QVariantMap diagnosticsResult = model.exportDiagnosticsBundle();

    QCOMPARE(diagnosticsResult.value(QStringLiteral("bundleVersion")).toString(),
             QStringLiteral("1.0"));

    const QString path = diagnosticsResult.value(QStringLiteral("path")).toString();
    QVERIFY2(!path.trimmed().isEmpty(), "Diagnostics path should not be empty");
    QVERIFY2(QFileInfo::exists(path), "Diagnostics artifact path does not exist");

    if (diagnosticsResult.value(QStringLiteral("ok")).toBool()) {
        QVERIFY(path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive));
    }
}

void MemoryBrowserTests::releaseMetadata_IsAvailable()
{
    FileListModel model;
    const QVariantMap metadata = model.releaseMetadata();

    QVERIFY(!metadata.value(QStringLiteral("version")).toString().trimmed().isEmpty());
    QVERIFY(!metadata.value(QStringLiteral("channel")).toString().trimmed().isEmpty());
    QVERIFY(!metadata.value(QStringLiteral("buildDateUtc")).toString().trimmed().isEmpty());
    QVERIFY(!metadata.value(QStringLiteral("buildId")).toString().trimmed().isEmpty());
    QCOMPARE(model.cloudSyncExperimental(), bool(ELLA_DEVELOPER_CLOUD_SYNC));
}

void MemoryBrowserTests::clearTables()
{
    QSqlDatabase db = DatabaseManager::instance().database();
    QVERIFY(db.isOpen());

    const QStringList statements = {
        QStringLiteral("DELETE FROM library_jobs"),
        QStringLiteral("DELETE FROM watched_roots"),
        QStringLiteral("DELETE FROM index_jobs"),
        QStringLiteral("DELETE FROM passages"),
        QStringLiteral("DELETE FROM passage_fts"),
        QStringLiteral("DELETE FROM favorites"),
        QStringLiteral("DELETE FROM file_tags"),
        QStringLiteral("DELETE FROM saved_searches"),
        QStringLiteral("DELETE FROM reading_positions"),
        QStringLiteral("DELETE FROM note_links"),
        QStringLiteral("DELETE FROM retrieval_events"),
        QStringLiteral("DELETE FROM annotations"),
        QStringLiteral("DELETE FROM document_notes"),
        QStringLiteral("DELETE FROM file_collections"),
        QStringLiteral("DELETE FROM collection_rules"),
        QStringLiteral("DELETE FROM collections"),
        QStringLiteral("DELETE FROM file_content_fts"),
        QStringLiteral("DELETE FROM file_index_state"),
        QStringLiteral("DELETE FROM files")
    };

    for (const QString& sql : statements) {
        QSqlQuery query(db);
        QVERIFY2(query.exec(sql), qPrintable(query.lastError().text()));
    }
}

QString MemoryBrowserTests::writeTextFile(const QString& path, const QString& content)
{
    QFileInfo info(path);
    QDir dir = info.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return QString();
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return QString();
    }
    file.write(content.toUtf8());
    file.close();
    return QFileInfo(path).absoluteFilePath();
}

int MemoryBrowserTests::countRows(const QString& sql) const
{
    QSqlQuery query(DatabaseManager::instance().database());
    if (!query.exec(sql) || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

int MemoryBrowserTests::countEvents(const QString& eventType) const
{
    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM retrieval_events WHERE event_type = ?"));
    query.addBindValue(eventType.trimmed().toLower());
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

void MemoryBrowserTests::sqlSearch_PhrasesPaginationFiltersAndRules()
{
    QTemporaryDir dir;QVERIFY(dir.isValid());FileRepository repository;
    for(int i=0;i<125;++i) {
        const auto path=writeTextFile(dir.filePath(QStringLiteral("entry-%1.txt").arg(i,3,10,QChar('0'))),"Library");
        QVERIFY(repository.addFile(path,"Learning","Physics",{},{},{},{},"Text",{}));
    }
    int count=-1;
    auto first=repository.queryFiles({},-1,{},{},{},{},{},{},{},{},{},"name",true,100,0,&count);
    QCOMPARE(count,125);QCOMPARE(first.size(),100);
    const auto second=repository.queryFiles({},-1,{},{},{},{},{},{},{},{},{},"name",true,100,100,&count);
    QCOMPARE(second.size(),25);QVERIFY(first.last().id!=second.first().id);
    const int id=first.first().id;
    QSqlQuery passage(DatabaseManager::instance().database());passage.prepare("INSERT INTO passages(file_id,ordinal,text,anchor_type,page_number,locator) VALUES(?,0,?,'paragraph',1,'Paragraph 1')");passage.addBindValue(id);passage.addBindValue("The quiet modern library preserves a useful source.");QVERIFY(passage.exec());
    QSqlQuery fts(DatabaseManager::instance().database());fts.prepare("INSERT INTO passage_fts(passage_id,file_id,content_text) VALUES(?,?,?)");fts.addBindValue(passage.lastInsertId());fts.addBindValue(id);fts.addBindValue("The quiet modern library preserves a useful source.");QVERIFY(fts.exec());
    auto matches=repository.queryFiles("\"quiet modern\"",-1,{},{},{},{},{},{},{},{},{},"name",false,100,0,&count);
    QCOMPARE(count,1);QCOMPARE(matches.first().id,id);QVERIFY(!matches.first().searchSnippet.isEmpty());QCOMPARE(matches.first().searchAnchor.value("pageNumber").toInt(),1);
    matches=repository.queryFiles("\"quiet library\"",-1,{},{},{},{},{},{},{},{},{},"name",false,100,0,&count);QCOMPARE(count,0);
    LibraryService service;QVERIFY(service.setFavorite(id,true));QVERIFY(service.setTags(id,{"Research","reading"}));
    matches=repository.queryFiles({},-1,{},{},{},{},{},{},{},{},{},"name",true,100,0,&count,true,dir.path(),"research");QCOMPARE(count,1);QCOMPARE(matches.first().id,id);
    QVERIFY(service.saveSearch("My sources","\"quiet modern\""));QCOMPARE(service.savedSearches().size(),1);
    QVERIFY(repository.addCollection("Physics",-1));const int collection=repository.getCollectionPickerOptions().last().toMap().value("id").toInt();
    QVERIFY(repository.addCollectionRule(collection,"subject","exact","Physics"));
    matches=repository.queryFiles({},collection,{},{},{},{},{},{},{},{},{},"name",true,100,0,&count);QCOMPARE(count,125);
}

void MemoryBrowserTests::asyncSearch_DiscardsStaleQueries()
{
    QTemporaryDir dir;FileListModel model;
    const auto alpha=writeTextFile(dir.filePath("alpha.txt"),"first");const auto beta=writeTextFile(dir.filePath("beta.txt"),"second");
    model.importFiles({alpha,beta});QTRY_VERIFY_WITH_TIMEOUT(!model.searching(),5000);
    model.search("alpha");model.search("beta");
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching(),5000);QCOMPARE(model.rowCount(),1);
    QCOMPARE(model.get(0).value("name").toString(),QStringLiteral("beta.txt"));
    const int betaId=model.get(0).value("id").toInt();
    model.search("alpha");QTRY_VERIFY_WITH_TIMEOUT(!model.searching(),5000);
    QCOMPARE(model.getDetailsById(betaId).value("name").toString(),QStringLiteral("beta.txt"));
    QCOMPARE(model.readTextFileById(betaId),QStringLiteral("second"));
}

void MemoryBrowserTests::incrementalIndex_UsesCurrentFileMetadata()
{
    QTemporaryDir dir;FileRepository repository;const auto path=writeTextFile(dir.filePath("fresh.txt"),"old content");
    QVERIFY(repository.addFile(path,{},{},{},{},{},{},{},{}));
    const auto stale=repository.queryFiles({},-1,{},{},{},{},{},{},{},{},{},"name",true).first();
    IndexingService indexing;indexing.resume();indexing.reindexFile(stale,false);
    QTRY_VERIFY_WITH_TIMEOUT(!indexing.status().value("running").toBool()&&indexing.status().value("queued").toInt()==0,15000);
    QVERIFY(!writeTextFile(path,"replacement contains changedword and extra bytes").isEmpty());
    indexing.reindexFile(stale,false); // Intentionally reuse stale catalog metadata.
    QTRY_VERIFY_WITH_TIMEOUT(!indexing.status().value("running").toBool()&&indexing.status().value("queued").toInt()==0,15000);
    const auto matches=repository.queryFiles("changedword",-1,{},{},{},{},{},{},{},{},{},"name",true);QCOMPARE(matches.size(),1);
    QCOMPARE(countRows("SELECT COUNT(*) FROM passages"),1);
    indexing.pause();QCOMPARE(indexing.status().value("paused").toBool(),true);indexing.resume();
}

void MemoryBrowserTests::libraryJobs_MoveIdentityDuplicateReviewAndUndo()
{
    QTemporaryDir dir;const QString original=writeTextFile(dir.filePath("original.md"),"unique source passage");LibraryService service;
    service.startImport({dir.path()},{},true);QTRY_VERIFY_WITH_TIMEOUT(!service.activity().value("running").toBool(),15000);
    FileRepository repository;const int id=repository.getFileDetailsByPath(original).value("id").toInt();QVERIFY(id>0);
    QVERIFY(service.setFavorite(id,true));QVERIFY(service.saveReadingPosition(id,{{"charStart",12}}));
    const QString moved=dir.filePath("renamed.md");QVERIFY(QFile::rename(original,moved));
    service.scanNow();QTRY_VERIFY_WITH_TIMEOUT(!service.activity().value("running").toBool(),15000);
    QCOMPARE(repository.getFileDetailsByPath(moved).value("id").toInt(),id);QVERIFY(service.isFavorite(id));
    QCOMPARE(service.readingPosition(id).value("charStart").toInt(),12);
    QVERIFY(service.removeFile(id));QVERIFY(QFileInfo::exists(moved));
    QCOMPARE(repository.queryFiles({},-1,{},{},{},{},{},{},{},{},{},"name",true).size(),0);
    QVERIFY(service.undoRemoval());QCOMPARE(repository.queryFiles({},-1,{},{},{},{},{},{},{},{},{},"name",true).size(),1);
    QVERIFY(QFile::copy(moved,dir.filePath("duplicate.md")));service.scanNow();QTRY_VERIFY_WITH_TIMEOUT(!service.activity().value("running").toBool(),15000);
    QCOMPARE(service.duplicates().size(),2);
}

void MemoryBrowserTests::libraryPreview_ExcludesFolders()
{
    QTemporaryDir dir;writeTextFile(dir.filePath("keep.txt"),"keep");writeTextFile(dir.filePath("private/skip.txt"),"private");writeTextFile(dir.filePath("node_modules/skip.txt"),"node");writeTextFile(dir.filePath("binary.exe"),"binary");
    LibraryService service;QSignalSpy ready(&service,&LibraryService::previewReady);service.previewImport({dir.path()},{"private"});
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,5000);const auto preview=ready.first().first().toMap();QCOMPARE(preview.value("supportedFiles").toInt(),1);QVERIFY(preview.value("unsupportedFiles").toInt()>=3);
    service.startImport({dir.path()},{"private"},true);service.pause();QCOMPARE(service.activity().value("status").toString(),QStringLiteral("paused"));
    QCOMPARE(countRows("SELECT COUNT(*) FROM library_jobs WHERE status='paused'"),1);service.resume();
    QTRY_VERIFY_WITH_TIMEOUT(!service.activity().value("running").toBool(),15000);QCOMPARE(countRows("SELECT COUNT(*) FROM files"),1);
}

void MemoryBrowserTests::hybridSearch_FusesRanksAndKeepsPassageAnchor()
{
    QTemporaryDir dir; QVERIFY(dir.isValid()); FileRepository repository;
    const QString keywordPath=writeTextFile(dir.filePath("keyword.txt"),"needle exact wording");
    const QString meaningPath=writeTextFile(dir.filePath("meaning.txt"),"a paraphrased explanation");
    QVERIFY(repository.addFile(keywordPath,{},{},{},{},{},{},{},{}));
    QVERIFY(repository.addFile(meaningPath,{},{},{},{},{},{},{},{}));
    const int keywordId=repository.getFileDetailsByPath(keywordPath).value("id").toInt();
    const int meaningId=repository.getFileDetailsByPath(meaningPath).value("id").toInt();
    QSqlQuery passage(DatabaseManager::instance().database());passage.prepare("INSERT INTO passages(file_id,ordinal,text,anchor_type,locator) VALUES(?,0,?,'text','Text 0')");passage.addBindValue(keywordId);passage.addBindValue("needle exact wording");QVERIFY(passage.exec());
    QSqlQuery fts(DatabaseManager::instance().database());fts.prepare("INSERT INTO passage_fts(passage_id,file_id,content_text) VALUES(?,?,?)");fts.addBindValue(passage.lastInsertId());fts.addBindValue(keywordId);fts.addBindValue("needle exact wording");QVERIFY(fts.exec());
    FileListModel model;model.search("needle");QTRY_VERIFY_WITH_TIMEOUT(!model.searching(),5000);QCOMPARE(model.rowCount(),1);
    const QVariantMap meaning{{"fileId",meaningId},{"text","Equivalent concept in different words"},{"locator","Paragraph 3"},{"passageOrdinal",2},{"anchorType","paragraph"}};
    const QVariantMap both{{"fileId",keywordId},{"text","Needle with semantic evidence"},{"locator","Paragraph 1"},{"passageOrdinal",0},{"anchorType","paragraph"}};
    model.applySemanticResults("needle",{meaning,both});QCOMPARE(model.rowCount(),2);QCOMPARE(model.get(0).value("id").toInt(),keywordId);
    const int meaningIndex=model.indexOfFileId(meaningId);QVERIFY(meaningIndex>=0);
    const auto item=model.get(meaningIndex);QVERIFY(item.value("searchMatchReason").toString().contains("Meaning"));
    QCOMPARE(item.value("searchAnchor").toMap().value("passageOrdinal").toInt(),2);
}

QTEST_GUILESS_MAIN(MemoryBrowserTests)
#include "MemoryBrowserTests.moc"
