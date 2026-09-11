#include "search/IndexingService.h"

#include "core/AppConfig.h"
#include "search/ContentExtractor.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QFile>
#include <QSettings>
#include <QTimer>
#include "library/FileRepository.h"
#include "database/DatabaseManager.h"
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
    return value.isValid() ? value.toString(Qt::ISODateWithMs) : QString();
}

IndexTaskResult runIndexTask(const FileRecord& catalogFile, bool force)
{
    FileRecord file=catalogFile;
    const QFileInfo actual(file.path);
    file.sizeBytes=actual.size();
    file.modifiedAt=actual.lastModified();
    IndexTaskResult result;
    result.fileId = file.id;

    const QString connectionName = QStringLiteral("index_worker_%1_%2")
                                       .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
                                       .arg(QDateTime::currentMSecsSinceEpoch());

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(AppConfig::databasePath());
        db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=10000");

        if (!db.open()) {
            result.error = QStringLiteral("Index DB open failed: %1").arg(db.lastError().text());
        } else {
            QSqlQuery config(db); config.exec("PRAGMA foreign_keys=ON");
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
            QSqlQuery source(db); source.prepare("SELECT id FROM files WHERE id=? AND removed_at IS NULL"); source.addBindValue(file.id);
            const bool sourceActive=source.exec() && source.next();
            if(!sourceActive){ result.success=true; result.skipped=true; }
            else if (!force &&
                actual.isFile() && indexedStatus == QStringLiteral("indexed") &&
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
                        QSqlQuery removePassageFts(db);
                        removePassageFts.prepare("DELETE FROM passage_fts WHERE file_id=?");
                        removePassageFts.addBindValue(file.id);ok=removePassageFts.exec();
                    }
                    if (ok) {
                        QSqlQuery removePassages(db);
                        removePassages.prepare("DELETE FROM passages WHERE file_id=?");
                        removePassages.addBindValue(file.id);ok=removePassages.exec();
                        for(const QVariant& value:extraction.passages) {
                            if(!ok)break;
                            const auto passage=value.toMap();QSqlQuery insert(db);
                            insert.prepare("INSERT INTO passages(file_id,ordinal,text,anchor_type,page_number,char_start,char_end,time_start_ms,locator) VALUES(?,?,?,?,?,?,?,?,?)");
                            for(const QVariant& v:QVariantList{file.id,passage.value("ordinal"),passage.value("text"),passage.value("anchorType","text"),passage.value("pageNumber"),passage.value("charStart"),passage.value("charEnd"),passage.value("timeStartMs"),passage.value("locator")})insert.addBindValue(v);
                            ok=insert.exec();if(!ok)result.error=insert.lastError().text();
                            if(ok){QSqlQuery fts(db);fts.prepare("INSERT INTO passage_fts(passage_id,file_id,content_text) VALUES(?,?,?)");fts.addBindValue(insert.lastInsertId());fts.addBindValue(file.id);fts.addBindValue(passage.value("text"));ok=fts.exec();if(!ok)result.error=fts.lastError().text();}
                        }
                    }
                    if(ok) {
                        const QFileInfo after(file.path);
                        if(after.size()!=file.sizeBytes||after.lastModified()!=file.modifiedAt) {
                            ok=false;result.error=QStringLiteral("Source changed while indexing; it will be retried on the next scan.");
                        }
                    }
                    if(ok) {
                        QSqlQuery catalog(db);catalog.prepare("UPDATE files SET size_bytes=?,modified_at=?,status=?,indexed_at=? WHERE id=?");
                        catalog.addBindValue(file.sizeBytes);catalog.addBindValue(fileModifiedAt);catalog.addBindValue(actual.isFile()?0:1);catalog.addBindValue(now);catalog.addBindValue(file.id);
                        ok=catalog.exec();if(!ok)result.error=catalog.lastError().text();
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
    m_paused=QSettings().value("library/indexingPaused",false).toBool();
    QTimer::singleShot(0,this,[this] {
        QSqlQuery jobs(DatabaseManager::instance().database());
        if(jobs.exec("SELECT file_id,force FROM index_jobs WHERE status IN('queued','running')")) {
            FileRepository repository;
            const auto files=repository.queryFiles({},-1,{},{},{},{},{},{},{},{},{},"indexedAt",false);
            QHash<int,bool> pending;while(jobs.next())pending[jobs.value(0).toInt()]=jobs.value(1).toBool();
            for(const auto& file:files)if(pending.contains(file.id))enqueue(file,pending.value(file.id));
            processNext();
        }
    });
}

void IndexingService::pause(){m_paused=true;QSettings().setValue("library/indexingPaused",true);emit statusChanged();}
void IndexingService::resume(){m_paused=false;QSettings().setValue("library/indexingPaused",false);processNext();emit statusChanged();}
void IndexingService::cancel(){m_queue.clear();m_queuedFileIds.clear();QSqlQuery q(DatabaseManager::instance().database());q.exec("UPDATE index_jobs SET status='cancelled' WHERE status='queued'");emit statusChanged();}

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
    map[QStringLiteral("paused")] = m_paused;
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
        if(force)for(auto& item:m_queue)if(item.file.id==file.id)item.force=true;
        return;
    }

    QueueItem item;
    item.file = file;
    item.force = force;
    m_queue.enqueue(item);
    m_queuedFileIds.insert(file.id);
    ++m_total;
    QSqlQuery persist(DatabaseManager::instance().database());
    persist.prepare("INSERT INTO index_jobs(file_id,force,status) VALUES(?,?,'queued') ON CONFLICT(file_id) DO UPDATE SET force=MAX(force,excluded.force),status='queued'");
    persist.addBindValue(file.id);persist.addBindValue(force);persist.exec();
}

void IndexingService::processNext()
{
    if (m_running || m_paused) {
        return;
    }

    if (m_queue.isEmpty()) {
        emit statusChanged();
        return;
    }

    QueueItem item = m_queue.dequeue();
    m_queuedFileIds.remove(item.file.id);
    m_running = true;
    QSqlQuery running(DatabaseManager::instance().database());running.prepare("UPDATE index_jobs SET status='running' WHERE file_id=?");running.addBindValue(item.file.id);running.exec();
    emit statusChanged();

    auto* watcher = new QFutureWatcher<IndexTaskResult>(this);
    connect(watcher, &QFutureWatcher<IndexTaskResult>::finished, this, [this, watcher]() {
        const IndexTaskResult result = watcher->result();
        watcher->deleteLater();

        m_running = false;
        QSqlQuery completed(DatabaseManager::instance().database());completed.prepare("UPDATE index_jobs SET status=?,last_error=? WHERE file_id=?");
        completed.addBindValue(result.success?"completed":"failed");completed.addBindValue(result.error);completed.addBindValue(result.fileId);completed.exec();
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
