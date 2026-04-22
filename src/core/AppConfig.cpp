#include "AppConfig.h"

#include <QDir>
#include <QStandardPaths>
#include <QSysInfo>

#ifndef ELLA_RELEASE_VERSION
#define ELLA_RELEASE_VERSION "0.9.0-beta.1"
#endif

#ifndef ELLA_RELEASE_CHANNEL
#define ELLA_RELEASE_CHANNEL "beta"
#endif

#ifndef ELLA_BUILD_DATE_UTC
#define ELLA_BUILD_DATE_UTC __DATE__ " " __TIME__
#endif

#ifndef ELLA_BUILD_ID
#define ELLA_BUILD_ID "local"
#endif

namespace
{
QString ensureDirectory(const QString& path)
{
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.absolutePath();
}
}

QString AppConfig::appDataDirectory()
{
    const QString overridePath = qEnvironmentVariable("ELLA_APP_DATA_DIR").trimmed();
    if (!overridePath.isEmpty()) {
        QDir overrideDir(overridePath);
        if (!overrideDir.exists()) {
            overrideDir.mkpath(QStringLiteral("."));
        }
        return overrideDir.absolutePath();
    }

    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    QDir dir(basePath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    return basePath;
}

QString AppConfig::databasePath()
{
    return appDataDirectory() + "/ella.db";
}

QString AppConfig::cacheDirectory()
{
    return ensureDirectory(appDataDirectory() + "/cache");
}

QString AppConfig::transcriptCacheDirectory()
{
    return ensureDirectory(cacheDirectory() + "/transcripts");
}

QString AppConfig::presentationCacheDirectory()
{
    return ensureDirectory(cacheDirectory() + "/presentations");
}

QString AppConfig::logsDirectory()
{
    return ensureDirectory(appDataDirectory() + "/logs");
}

QString AppConfig::diagnosticsDirectory()
{
    return ensureDirectory(appDataDirectory() + "/diagnostics");
}

QString AppConfig::activeLogFilePath()
{
    return QDir(logsDirectory()).filePath(QStringLiteral("ella.log"));
}

QString AppConfig::defaultNoteAuthor()
{
    return QStringLiteral("Your Name");
}

QString AppConfig::notesDirectory()
{
    const QString documentsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString notesPath = documentsPath + "/Ella Notes";

    QDir dir(notesPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    return notesPath;
}

QString AppConfig::releaseVersion()
{
    return QStringLiteral(ELLA_RELEASE_VERSION);
}

QString AppConfig::releaseChannel()
{
    return QStringLiteral(ELLA_RELEASE_CHANNEL);
}

QString AppConfig::buildDateUtc()
{
    return QStringLiteral(ELLA_BUILD_DATE_UTC);
}

QString AppConfig::buildId()
{
    return QStringLiteral(ELLA_BUILD_ID);
}

QVariantMap AppConfig::releaseMetadata()
{
    QVariantMap map;
    map[QStringLiteral("version")] = releaseVersion();
    map[QStringLiteral("channel")] = releaseChannel();
    map[QStringLiteral("buildDateUtc")] = buildDateUtc();
    map[QStringLiteral("buildId")] = buildId();
    map[QStringLiteral("qtVersion")] = QString::fromLatin1(qVersion());
    map[QStringLiteral("osProduct")] = QSysInfo::prettyProductName();
    map[QStringLiteral("osBuildAbi")] = QSysInfo::buildAbi();
    map[QStringLiteral("cpuArch")] = QSysInfo::currentCpuArchitecture();
    return map;
}
