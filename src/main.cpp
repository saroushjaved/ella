#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSqlDatabase>
#include <QStringConverter>
#include <QSqlQuery>
#include <QTextStream>
#include <QWindow>
#include <QTimer>

#include "core/AppConfig.h"
#include "core/WorkspaceService.h"
#include "library/LibraryService.h"
#include "components/ComponentManager.h"
#include "database/DatabaseManager.h"
#include "models/FileListModel.h"
#include "notes/NoteManager.h"
#include "notes/RichTextFormatter.h"
#include "search/IndexingService.h"
#include "search/SemanticSearchService.h"
#include "sync/CloudSyncService.h"
#include "sync/OAuthCallbackServer.h"

namespace
{
QMutex gLogMutex;
QtMessageHandler gPreviousHandler = nullptr;

QString messageTypeToString(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    default:
        return QStringLiteral("LOG");
    }
}

void rotateLogsIfNeeded(const QString& logPath)
{
    QFileInfo info(logPath);
    constexpr qint64 kMaxLogBytes = 2 * 1024 * 1024;
    constexpr int kMaxBackups = 5;

    if (!info.exists() || info.size() < kMaxLogBytes) {
        return;
    }

    for (int i = kMaxBackups - 1; i >= 1; --i) {
        const QString from = QStringLiteral("%1.%2").arg(logPath).arg(i);
        const QString to = QStringLiteral("%1.%2").arg(logPath).arg(i + 1);
        if (QFile::exists(to)) {
            QFile::remove(to);
        }
        if (QFile::exists(from)) {
            QFile::rename(from, to);
        }
    }

    const QString firstBackup = QStringLiteral("%1.1").arg(logPath);
    if (QFile::exists(firstBackup)) {
        QFile::remove(firstBackup);
    }
    QFile::rename(logPath, firstBackup);
}

void fileMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ"));
    const QString level = messageTypeToString(type);
    const QString source = context.file ? QStringLiteral("%1:%2").arg(QString::fromUtf8(context.file)).arg(context.line)
                                        : QStringLiteral("unknown");
    const QString line = QStringLiteral("%1 [%2] %3 | %4\n").arg(timestamp, level, source, msg);

    {
        QMutexLocker locker(&gLogMutex);
        const QString logPath = AppConfig::activeLogFilePath();
        rotateLogsIfNeeded(logPath);

        QFile logFile(logPath);
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QTextStream stream(&logFile);
            stream.setEncoding(QStringConverter::Utf8);
            stream << line;
            logFile.close();
        }
    }

    if (gPreviousHandler) {
        gPreviousHandler(type, context, msg);
    }

    if (type == QtFatalMsg) {
        abort();
    }
}

void initializeLogging()
{
    AppConfig::logsDirectory();
    rotateLogsIfNeeded(AppConfig::activeLogFilePath());
    gPreviousHandler = qInstallMessageHandler(fileMessageHandler);
}

QIcon createAppIcon()
{
    // Prefer PNG for runtime reliability; fall back to ICO in resources.
    QIcon icon(QStringLiteral("qrc:/qt/qml/SecondBrain/src/ui/assets/ella_icon_256.png"));
    if (!icon.isNull()) {
        return icon;
    }
    return QIcon(QStringLiteral("qrc:/qt/qml/SecondBrain/packaging/assets/ella_icon.ico"));
}
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // ELLA owns its control visuals and focus states. The native Windows style
    // rejects custom backgrounds/content items, so select the supported base
    // style before any QML is loaded.
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    const QIcon appIcon = createAppIcon();
    app.setWindowIcon(appIcon);

    app.setOrganizationName("Ella");
    app.setApplicationName("Ella");
    initializeLogging();

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString qmlDir = QDir(appDir).filePath(QStringLiteral("qml"));

    QStringList runtimePluginPaths = QCoreApplication::libraryPaths();
    if (!runtimePluginPaths.contains(appDir)) {
        runtimePluginPaths.prepend(appDir);
    }
    QCoreApplication::setLibraryPaths(runtimePluginPaths);

    QString restoreError;
    if (!WorkspaceService::applyPendingRestore(&restoreError)) {
        qCritical() << restoreError;
        return -1;
    }
    if (!DatabaseManager::instance().initialize()) {
        qCritical() << "ELLA startup failed during database initialization:"
                    << DatabaseManager::instance().lastError();
        return -1;
    }

    ComponentManager componentManager;
    WorkspaceService workspaceService;
    FileListModel fileListModel;
    NoteManager noteManager;
    RichTextFormatter richTextFormatter;
    IndexingService indexingService;
    SemanticSearchService semanticSearchService;
    QObject::connect(&componentManager, &ComponentManager::stateChanged,
                     &semanticSearchService, &SemanticSearchService::refresh);
    LibraryService libraryService;
    libraryService.setIndexingService(&indexingService);
    QObject::connect(&libraryService, &LibraryService::filesChanged, &fileListModel, &FileListModel::refreshCurrentView);
    QObject::connect(&workspaceService, &WorkspaceService::notesChanged, &fileListModel, &FileListModel::reload);
    CloudSyncService cloudSyncModel;
    OAuthCallbackServer oauthCallbackServer;

    fileListModel.setIndexingService(&indexingService);
    fileListModel.setCloudSyncService(&cloudSyncModel);

    QQmlApplicationEngine engine;
    const QStringList arguments = QCoreApplication::arguments();
    const int screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    const QString screenshotPath = screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size() ? arguments.at(screenshotIndex + 1) : QString();
    const int pageIndex = arguments.indexOf(QStringLiteral("--screenshot-page"));
    const QString screenshotPage = pageIndex >= 0 && pageIndex + 1 < arguments.size() ? arguments.at(pageIndex + 1) : QString();
    const bool screenshotDark = arguments.contains(QStringLiteral("--screenshot-dark"));
    int screenshotFileId = -1;
    QString screenshotSamplePath;
    if (!screenshotPath.isEmpty() && screenshotPage == QStringLiteral("reader")) {
        const QString samplePath = QDir(AppConfig::appDataDirectory()).filePath(QStringLiteral("sample-research.md"));
        screenshotSamplePath = samplePath;
        QFile sample(samplePath);
        if (sample.open(QIODevice::WriteOnly | QIODevice::Text)) {
            sample.write("# A useful thread\n\nGood research becomes useful when the source, context, and your own conclusion stay connected.\n\n## Working note\n\nELLA keeps this passage close to the document it came from.\n");
            sample.close();
            fileListModel.addFile(samplePath, QStringLiteral("Research"), QStringLiteral("Knowledge work"), {}, {},
                                  QStringLiteral("Local library"), QStringLiteral("ELLA"), QStringLiteral("Markdown"), {});
            QSqlQuery query(DatabaseManager::instance().database()); query.prepare(QStringLiteral("SELECT id FROM files WHERE path=?")); query.addBindValue(QFileInfo(samplePath).absoluteFilePath());
            if (query.exec() && query.next()) screenshotFileId = query.value(0).toInt();
        }
    }
    QStringList importPaths = engine.importPathList();
    const QString qtQmlImportsDir = QLibraryInfo::path(QLibraryInfo::QmlImportsPath);
    if (!qtQmlImportsDir.isEmpty() && !importPaths.contains(qtQmlImportsDir)) {
        importPaths.prepend(qtQmlImportsDir);
    }
    if (!importPaths.contains(QStringLiteral("qrc:/qt/qml"))) {
        importPaths.prepend(QStringLiteral("qrc:/qt/qml"));
    }
    if (!importPaths.contains(qmlDir)) {
        importPaths.prepend(qmlDir);
    }
    engine.setImportPathList(importPaths);
    engine.rootContext()->setContextProperty("fileListModel", &fileListModel);
    engine.rootContext()->setContextProperty("workspaceService", &workspaceService);
    engine.rootContext()->setContextProperty("libraryService", &libraryService);
    engine.rootContext()->setContextProperty("componentManager", &componentManager);
    engine.rootContext()->setContextProperty("semanticSearchService", &semanticSearchService);
    engine.rootContext()->setContextProperty("cloudSyncModel", &cloudSyncModel);
    engine.rootContext()->setContextProperty("oauthCallbackServer", &oauthCallbackServer);
    engine.rootContext()->setContextProperty("noteManager", &noteManager);
    engine.rootContext()->setContextProperty("richTextFormatter", &richTextFormatter);
    engine.rootContext()->setContextProperty("releaseMetadata", AppConfig::releaseMetadata());
    engine.rootContext()->setContextProperty("applicationScreenshotMode", !screenshotPath.isEmpty());
    engine.rootContext()->setContextProperty("applicationScreenshotDark", screenshotDark);
    engine.rootContext()->setContextProperty("applicationStartupPage", screenshotPage);
    engine.rootContext()->setContextProperty("applicationStartupFileId", screenshotFileId);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
        );

    engine.loadFromModule("SecondBrain", "Main");
    if (!engine.rootObjects().isEmpty()) {
        if (auto* window = qobject_cast<QWindow*>(engine.rootObjects().constFirst())) {
            window->setIcon(appIcon);
        }
    }
    if (!screenshotPath.isEmpty() && !engine.rootObjects().isEmpty()) {
        QTimer::singleShot(1800, &app, [&app, &engine, screenshotPath, screenshotFileId, screenshotSamplePath] {
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
            if (!window || !window->grabWindow().save(screenshotPath)) qCritical() << "Could not save UI screenshot" << screenshotPath;
            if (screenshotFileId >= 0) {
                QSqlDatabase db = DatabaseManager::instance().database();
                db.transaction();
                const QStringList oneIdTables = {
                    QStringLiteral("passage_fts"), QStringLiteral("passages"), QStringLiteral("file_content_fts"),
                    QStringLiteral("file_index_state"), QStringLiteral("index_jobs"), QStringLiteral("file_collections"),
                    QStringLiteral("document_notes"), QStringLiteral("annotations"), QStringLiteral("favorites"),
                    QStringLiteral("file_tags"), QStringLiteral("reading_positions"), QStringLiteral("retrieval_events")
                };
                for (const QString& table : oneIdTables) {
                    QSqlQuery cleanup(db);
                    cleanup.prepare(QStringLiteral("DELETE FROM %1 WHERE file_id=?").arg(table));
                    cleanup.addBindValue(screenshotFileId);
                    cleanup.exec();
                }
                QSqlQuery links(db);
                links.prepare(QStringLiteral("DELETE FROM note_links WHERE note_file_id=? OR source_file_id=?"));
                links.addBindValue(screenshotFileId); links.addBindValue(screenshotFileId); links.exec();
                QSqlQuery file(db); file.prepare(QStringLiteral("DELETE FROM files WHERE id=?"));
                file.addBindValue(screenshotFileId); file.exec();
                db.commit();
                QFile::remove(screenshotSamplePath);
            }
            app.quit();
        });
    }

    return app.exec();
}
