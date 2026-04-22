#pragma once

#include "library/FileRecord.h"

#include <QFutureWatcher>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QVariantMap>

class IndexingService : public QObject
{
    Q_OBJECT

public:
    explicit IndexingService(QObject* parent = nullptr);

    void scheduleIncremental(const QList<FileRecord>& files);
    void reindexFile(const FileRecord& file, bool force);
    void rebuildIndex(const QList<FileRecord>& files);

    QVariantMap status() const;
    int indexedCount() const;

signals:
    void statusChanged();

private:
    struct QueueItem
    {
        FileRecord file;
        bool force = false;
    };

    void enqueue(const FileRecord& file, bool force);
    void processNext();

    QQueue<QueueItem> m_queue;
    QSet<int> m_queuedFileIds;
    bool m_running = false;
    int m_total = 0;
    int m_processed = 0;
    int m_failed = 0;
    int m_skipped = 0;
    QString m_lastError;
    QString m_lastIndexedAt;
};

