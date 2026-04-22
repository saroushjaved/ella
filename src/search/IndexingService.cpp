#include "search/IndexingService.h"

#include "core/AppConfig.h"
#include "search/ContentExtractor.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QtConcurrent>

namespace
{
struct IndexTaskResult
{
    int fileId = -1;
    bool success = false;
    bool skipped = false;
    QString error;
    QString indexedAt;
};

QString isoDate(const QDateTime& value)
{
    return value.isValid() ? value.toString(Qt::ISODate) : QString();
}

IndexTaskResult runIndexTask(const FileRecord& file, bool force)
{
    IndexTaskResult result;
    result.fileId = file.id;

    const QString connectionName = QStringLiteral("index_worker_%1_%2")
                                       .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
                                       .arg(QDateTime::currentMSecsSinceEpoch());

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(AppConfig::databasePath());

        if (!db.open()) {
            result.error = QStringLiteral("Index DB open failed: %1").arg(db.lastError().text());
        } else {
            QString indexedStatus;
            QString indexedModifiedAt;
            qint64 indexedSize = -1;

            QSqlQuery stateQuery(db);
            stateQuery.prepare(QStringLiteral(
                "SELECT status, file_modified_at, file_size_bytes FROM file_index_state WHERE file_id = ?"));
            stateQuery.addBindValue(file.id);
            if (stateQuery.exec() && stateQuery.next()) {
                indexedStatus = stateQuery.value(0).toString();
                indexedModifiedAt = stateQuery.value(1).toString();
                indexedSize = stateQuery.value(2).toLongLong();
            }

            const QString fileModifiedAt = isoDate(file.modifiedAt);
            if (!force &&
                indexedStatus == QStringLiteral("indexed") &&
                indexedModifiedAt == fileModifiedAt &&
                indexedSize == file.sizeBytes) {
                result.success = true;
                result.skipped = true;
                result.indexedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
            } else {
                ContentExtractor extractor;
                ContentExtractor::Result extraction =
                    extractor.extract(file.path, file.mimeType, file.extension);

                QString content = extraction.text;
                if (content.size() > 1024 * 1024 * 12) {
                    content = content.left(1024 * 1024 * 12);
                }

                const QString contentHash = QString::fromLatin1(
                    QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256).toHex());
                const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
                const QString status =
                    (!content.trimmed().isEmpty() || extraction.error.trimmed().isEmpty())
                        ? QStringLiteral("indexed")
                        : QStringLiteral("error");
                const QString lastError =
                    status == QStringLiteral("indexed") ? QString() : extraction.error;

                if (!db.transaction()) {
                    result.error = QStringLiteral("Failed to start index transaction: %1")
                                       .arg(db.lastError().text());
                } else {
                    bool ok = true;
                    QSqlQuery removeExisting(db);
                    removeExisting.prepare(QStringLiteral("DELETE FROM file_content_fts WHERE file_id = ?"));
                    removeExisting.addBindValue(file.id);
                    ok = removeExisting.exec();

                    if (ok && !content.trimmed().isEmpty()) {
                        QSqlQuery insertContent(db);
                        insertContent.prepare(
                            QStringLiteral("INSERT INTO file_content_fts(file_id, content_text) VALUES(?, ?)"));
                        insertContent.addBindValue(file.id);
                        insertContent.addBindValue(content);
                        ok = insertContent.exec();
                        if (!ok) {
                            result.error = insertContent.lastError().text();
                        }
                    }

                    if (ok) {
                        QSqlQuery upsertState(db);
                        upsertState.prepare(QStringLiteral(R"(
                            INSERT INTO file_index_state(
                                file_id, file_path, file_size_bytes, file_modified_at,
                                content_hash, indexed_at, status, last_error, extractor
                            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                            ON CONFLICT(file_id) DO UPDATE SET
                                file_path = excluded.file_path,
                                file_size_bytes = excluded.file_size_bytes,
                                file_modified_at = excluded.file_modified_at,
                                content_hash = excluded.content_hash,
                                indexed_at = excluded.indexed_at,
                                status = excluded.status,
                                last_error = excluded.last_error,
                                extractor = excluded.extractor
                        )"));
                        upsertState.addBindValue(file.id);
                        upsertState.addBindValue(file.path);
                        upsertState.addBindValue(file.sizeBytes);
                        upsertState.addBindValue(fileModifiedAt);
                        upsertState.addBindValue(contentHash);
                        upsertState.addBindValue(now);
                        upsertState.addBindValue(status);
                        upsertState.addBindValue(lastError);
                        upsertState.addBindValue(extraction.extractor);
                        ok = upsertState.exec();
                        if (!ok) {
                            result.error = upsertState.lastError().text();
                        }
                    }

                    if (ok && db.commit()) {
                        result.success = true;
                        result.skipped = false;
                        result.indexedAt = now;
                        if (status == QStringLiteral("error")) {
                            result.success = false;
                            result.error = lastError;
                        }
                    } else {
                        db.rollback();
                        if (result.error.trimmed().isEmpty()) {
                            result.error = QStringLiteral("Index transaction failed");
                        }
                    }
                }
            }
        }

        db.close();
    }

    QSqlDatabase::removeDatabase(connectionName);
    return result;
}
}

IndexingService::IndexingService(QObject* parent)
    : QObject(parent)
{
}

void IndexingService::scheduleIncremental(const QList<FileRecord>& files)
{
    if (files.isEmpty()) {
        return;
    }

    for (const FileRecord& file : files) {
        enqueue(file, false);
    }
    processNext();
}

void IndexingService::reindexFile(const FileRecord& file, bool force)
{
    enqueue(file, force);
    processNext();
}

void IndexingService::rebuildIndex(const QList<FileRecord>& files)
{
    m_total = 0;
    m_processed = 0;
    m_failed = 0;
    m_skipped = 0;
    m_lastError.clear();

    for (const FileRecord& file : files) {
        enqueue(file, true);
    }
    emit statusChanged();
    processNext();
}

QVariantMap IndexingService::status() const
{
    QVariantMap map;
    map[QStringLiteral("running")] = m_running;
    map[QStringLiteral("queued")] = m_queue.size();
    map[QStringLiteral("total")] = m_total;
    map[QStringLiteral("processed")] = m_processed;
    map[QStringLiteral("failed")] = m_failed;
    map[QStringLiteral("skipped")] = m_skipped;
    map[QStringLiteral("lastError")] = m_lastError;
    map[QStringLiteral("lastIndexedAt")] = m_lastIndexedAt;
    map[QStringLiteral("indexedCount")] = indexedCount();
    return map;
}

int IndexingService::indexedCount() const
{
    QSqlQuery query(QSqlDatabase::database(QStringLiteral("secondbrain_connection")));
    if (!query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM file_index_state WHERE status = 'indexed'"))) {
        return 0;
    }

    if (!query.next()) {
        return 0;
    }

    return query.value(0).toInt();
}

void IndexingService::enqueue(const FileRecord& file, bool force)
{
    if (file.id < 0 || file.path.trimmed().isEmpty()) {
        return;
    }

    if (m_queuedFileIds.contains(file.id)) {
        return;
    }

    QueueItem item;
    item.file = file;
    item.force = force;
    m_queue.enqueue(item);
    m_queuedFileIds.insert(file.id);
    ++m_total;
}

void IndexingService::processNext()
{
    if (m_running) {
        return;
    }

    if (m_queue.isEmpty()) {
        emit statusChanged();
        return;
    }

    QueueItem item = m_queue.dequeue();
    m_queuedFileIds.remove(item.file.id);
    m_running = true;
    emit statusChanged();

    auto* watcher = new QFutureWatcher<IndexTaskResult>(this);
    connect(watcher, &QFutureWatcher<IndexTaskResult>::finished, this, [this, watcher]() {
        const IndexTaskResult result = watcher->result();
        watcher->deleteLater();

        m_running = false;
        ++m_processed;
        if (result.skipped) {
            ++m_skipped;
        } else if (!result.success) {
            ++m_failed;
            m_lastError = result.error;
        } else {
            m_lastIndexedAt = result.indexedAt;
        }

        emit statusChanged();
        processNext();
    });

    watcher->setFuture(QtConcurrent::run(runIndexTask, item.file, item.force));
}

