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
#include <QStringConverter>
#include <QTextStream>
#include <QWindow>

#include "core/AppConfig.h"
#include "database/DatabaseManager.h"
#include "models/FileListModel.h"
#include "notes/NoteManager.h"
#include "notes/RichTextFormatter.h"
#include "search/IndexingService.h"
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

    if (!DatabaseManager::instance().initialize()) {
        qCritical() << "ELLA startup failed during database initialization:"
                    << DatabaseManager::instance().lastError();
        return -1;
    }

    FileListModel fileListModel;
    NoteManager noteManager;
    RichTextFormatter richTextFormatter;
    IndexingService indexingService;
    CloudSyncService cloudSyncModel;
    OAuthCallbackServer oauthCallbackServer;

    fileListModel.setIndexingService(&indexingService);
    fileListModel.setCloudSyncService(&cloudSyncModel);

    QQmlApplicationEngine engine;
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
    engine.rootContext()->setContextProperty("cloudSyncModel", &cloudSyncModel);
    engine.rootContext()->setContextProperty("oauthCallbackServer", &oauthCallbackServer);
    engine.rootContext()->setContextProperty("noteManager", &noteManager);
    engine.rootContext()->setContextProperty("richTextFormatter", &richTextFormatter);
    engine.rootContext()->setContextProperty("releaseMetadata", AppConfig::releaseMetadata());

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

    return app.exec();
}
