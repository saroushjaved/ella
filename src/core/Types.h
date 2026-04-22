#pragma once

#include <QString>
#include <QDateTime>

enum class FileStatus
{
    Active = 0,
    Missing = 1
};

struct FileRecord
{
    int id = -1;

    QString path;
    QString name;
    QString extension;
    QString mimeType;

    qint64 sizeBytes = 0;

    QDateTime createdAt;
    QDateTime modifiedAt;
    QDateTime indexedAt;

    FileStatus status = FileStatus::Active;

    QString technicalDomain;
    QString subject;
    QString subtopic;
    QString location;
    QString source;
    QString author;
    QString documentType;
    QString remarks;

    QString searchSnippet;
    QString searchMatchReason;
    double searchScore = 0.0;
};
