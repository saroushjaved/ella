#pragma once

#include <QString>
#include <QVariantMap>

class AppConfig
{
public:
    static QString appDataDirectory();
    static QString databasePath();
    static QString cacheDirectory();
    static QString transcriptCacheDirectory();
    static QString presentationCacheDirectory();
    static QString logsDirectory();
    static QString diagnosticsDirectory();
    static QString activeLogFilePath();

    static QString defaultNoteAuthor();
    static QString notesDirectory();

    static QString releaseVersion();
    static QString releaseChannel();
    static QString buildDateUtc();
    static QString buildId();
    static QVariantMap releaseMetadata();
};
