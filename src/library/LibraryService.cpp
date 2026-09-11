#include "library/LibraryService.h"
#include "library/FileRepository.h"
#include "database/DatabaseManager.h"
#include "core/AppConfig.h"
#include "search/IndexingService.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThread>
#include <QUuid>
#include <QUrl>
#include <QtConcurrent>

namespace {
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QString json(const QVariantMap& map) { return QString::fromUtf8(QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact)); }
QString jsonList(const QStringList& list) { return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(list)).toJson(QJsonDocument::Compact)); }
QVariantList rows(const QString& sql,const QVariantList& values={}) {
    QSqlQuery q(DatabaseManager::instance().database()); q.prepare(sql);
    for(const auto& v:values)q.addBindValue(v);
    QVariantList result;
    if(q.exec())while(q.next()) { QVariantMap item;const auto record=q.record();for(int i=0;i<record.count();++i)item[record.fieldName(i)]=q.value(i);result<<item; }
    return result;
}
bool execute(const QString& sql,const QVariantList& values={}) {
    QSqlQuery q(DatabaseManager::instance().database());q.prepare(sql);for(const auto& v:values)q.addBindValue(v);return q.exec();
}
QString pathOf(const QVariant& v) {
    const QUrl url=v.toUrl();const QString value=url.isLocalFile()?url.toLocalFile():v.toString();
    return QDir::fromNativeSeparators(QFileInfo(value).absoluteFilePath());
}
QStringList supportedExtensions() {
    return {"pdf","txt","md","markdown","csv","tsv","json","xml","html","htm","rtf","docx","pptx","doc","ppt","odt","odp","png","jpg","jpeg","bmp","tif","tiff","webp","mp3","wav","m4a","flac","ogg","mp4","mkv","mov","avi","webm"};
}
struct ScanResult { QList<QFileInfo> files; QStringList directories; int skipped=0; qint64 bytes=0; QStringList errors; };
bool proceed(const std::shared_ptr<std::atomic_int>& control) {
    if(!control)return true;
    while(control->load()==1)QThread::msleep(50);
    return control->load()<2;
}
ScanResult enumerate(const QVariantList& roots,const QStringList& exclusions,const std::shared_ptr<std::atomic_int>& control={},const QVariantMap& rootExclusions={}) {
    ScanResult result;QSet<QString> seen;
    QStringList excluded=exclusions;excluded<<".git"<<"node_modules"<<".venv"<<"$RECYCLE.BIN"<<"System Volume Information";
    auto isExcluded=[&](const QString& path,const QString& relative) {
        const QStringList parts=relative.split('/');
        for(const QString& pattern:excluded) {
            const QRegularExpression expression(QRegularExpression::wildcardToRegularExpression(pattern.trimmed()),QRegularExpression::CaseInsensitiveOption);
            if(expression.match(relative).hasMatch()||expression.match(path).hasMatch())return true;
            for(const auto& part:parts)if(expression.match(part).hasMatch())return true;
        }return false;
    };
    const auto supported=supportedExtensions();
    auto add=[&](const QFileInfo& info) {
        const QString path=QDir::fromNativeSeparators(info.absoluteFilePath());
        if(seen.contains(path.toCaseFolded()))return;
        seen.insert(path.toCaseFolded());
        if(!supported.contains(info.suffix().toLower())){++result.skipped;return;}
        result.files<<info;result.bytes+=info.size();
    };
    for(const auto& rootValue:roots) {
        if(!proceed(control))break;
        const QString root=pathOf(rootValue);QFileInfo info(root);
        excluded=rootExclusions.contains(root)?rootExclusions.value(root).toStringList():exclusions;
        excluded<<".git"<<"node_modules"<<".venv"<<"$RECYCLE.BIN"<<"System Volume Information";
        if(!info.exists()){result.errors<<QStringLiteral("Folder or file unavailable: ")+root;continue;}
        if(info.isFile()){if(!isExcluded(root,info.fileName()))add(info);continue;}
        QStringList pending{root};
        while(!pending.isEmpty()&&proceed(control)) {
            const QString directory=pending.takeLast();result.directories<<directory;
            QDirIterator entries(directory,QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System);
            while(entries.hasNext()&&proceed(control)) {
                entries.next();const auto child=entries.fileInfo();
                const QString path=QDir::fromNativeSeparators(child.absoluteFilePath());
                if(child.isSymLink()||isExcluded(path,QDir(root).relativeFilePath(path))){++result.skipped;continue;}
                if(child.isDir())pending<<path;else if(child.isFile())add(child);
            }
        }
    }return result;
}
struct JobResult { QVariantMap summary; QList<FileRecord> files; QStringList directories; };
JobResult importWorker(int jobId,const QVariantMap& payload,const std::shared_ptr<std::atomic_int>& control) {
    JobResult result;const QString connection="library_"+QUuid::createUuid().toString(QUuid::Id128);
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE",connection);db.setDatabaseName(AppConfig::databasePath());db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=10000");
        if(!db.open())result.summary={{"error",db.lastError().text()},{"status","failed"}};
        else {
            QSqlQuery config(db);config.exec("PRAGMA foreign_keys=ON");
            auto update=[&](const QString& state,int completed,int total,const QString& error={}) {
                QSqlQuery q(db);q.prepare("UPDATE library_jobs SET status=?,completed=?,total=?,last_error=?,updated_at=? WHERE id=?");
                for(const QVariant& value:QVariantList{state,completed,total,error,now(),jobId})q.addBindValue(value);q.exec();
            };
            const ScanResult scan=enumerate(payload.value("paths").toList(),payload.value("exclusions").toStringList(),control,payload.value("rootExclusions").toMap());
            result.directories=scan.directories;
            int imported=0,updated=0,moved=0,failed=scan.errors.size(),completed=0;QString error=scan.errors.join('\n');
            update("running",0,scan.files.size(),error);
            // Hash source bytes on the worker, never extracted text. This makes exact
            // duplicate detection valid for images and binary Office files as well.
            QHash<QString,QString> hashes;QHash<QString,int> hashCounts;
            for(const auto& info:scan.files) {
                if(!proceed(control))break;
                QString hash;
                QSqlQuery cached(db);cached.prepare("SELECT content_hash,size_bytes,modified_at FROM files WHERE path=?");cached.addBindValue(QDir::fromNativeSeparators(info.absoluteFilePath()));
                if(cached.exec()&&cached.next()&&cached.value(1).toLongLong()==info.size()&&QDateTime::fromString(cached.value(2).toString(),Qt::ISODate)==info.lastModified())hash=cached.value(0).toString();
                if(hash.isEmpty()) {
                    QFile file(info.absoluteFilePath());QCryptographicHash hasher(QCryptographicHash::Sha256);
                    if(file.open(QIODevice::ReadOnly)) {
                        while(!file.atEnd()&&proceed(control)){const auto block=file.read(1024*1024);if(block.isEmpty()&&file.error()!=QFile::NoError)break;hasher.addData(block);}
                        if(file.error()==QFile::NoError&&control->load()<2)hash=QString::fromLatin1(hasher.result().toHex());
                    }
                }
                hashes.insert(info.absoluteFilePath(),hash);if(!hash.isEmpty())++hashCounts[hash];
            }
            QMimeDatabase mime;
            for(const auto& info:scan.files) {
                if(!proceed(control))break;
                const QString path=QDir::fromNativeSeparators(info.absoluteFilePath()),hash=hashes.value(info.absoluteFilePath());
                QSqlQuery existing(db);existing.prepare("SELECT id,removed_at FROM files WHERE path=?");existing.addBindValue(path);
                int id=-1;bool removed=false;if(existing.exec()&&existing.next()){id=existing.value(0).toInt();removed=!existing.value(1).isNull();}
                if(removed){++completed;continue;}
                bool wasMove=false;
                if(id<0&&!hash.isEmpty()&&hashCounts.value(hash)==1) {
                    QSqlQuery match(db);match.prepare("SELECT id,path FROM files WHERE content_hash=? AND removed_at IS NULL");match.addBindValue(hash);
                    QList<int> missing;int alive=0;if(match.exec())while(match.next()) {if(!QFileInfo::exists(match.value(1).toString()))missing<<match.value(0).toInt();else ++alive;}
                    if(missing.size()==1&&alive==0){id=missing.first();wasMove=true;}
                }
                QSqlQuery write(db);
                if(id>=0) {
                    write.prepare("UPDATE files SET path=?,name=?,extension=?,mime_type=?,size_bytes=?,modified_at=?,status=0,content_hash=? WHERE id=?");
                    for(const QVariant& value:QVariantList{path,info.fileName(),info.suffix().toLower(),mime.mimeTypeForFile(info,QMimeDatabase::MatchExtension).name(),info.size(),info.lastModified().toString(Qt::ISODateWithMs),hash,id})write.addBindValue(value);
                } else {
                    write.prepare("INSERT INTO files(path,name,extension,mime_type,size_bytes,created_at,modified_at,indexed_at,status,source,document_type,content_hash) VALUES(?,?,?,?,?,?,?,?,0,'Local library',?,?)");
                    for(const QVariant& value:QVariantList{path,info.fileName(),info.suffix().toLower(),mime.mimeTypeForFile(info,QMimeDatabase::MatchExtension).name(),info.size(),info.birthTime().toString(Qt::ISODateWithMs),info.lastModified().toString(Qt::ISODateWithMs),now(),info.suffix().toUpper(),hash})write.addBindValue(value);
                }
                if(!write.exec()){++failed;error=write.lastError().text();}
                else if(wasMove)++moved;else if(id>=0)++updated;else ++imported;
                ++completed;if(completed%20==0)update(control->load()==1?"paused":"running",completed,scan.files.size(),error);
            }
            // Disconnected roots are preserved without classifying every source as
            // deleted. Reconcile only roots that were actually reachable this scan.
            for(const auto& rootValue:payload.value("paths").toList()) {
                const QString root=pathOf(rootValue);if(!QFileInfo(root).isDir())continue;
                QSqlQuery sources(db);sources.prepare("SELECT id,path FROM files WHERE removed_at IS NULL AND (path=? OR substr(REPLACE(path,'\\','/'),1,?)=?)");
                sources.addBindValue(root);sources.addBindValue(root.size()+1);sources.addBindValue(root+'/');
                if(sources.exec())while(sources.next()) {
                    if(!proceed(control))break;
                    QSqlQuery status(db);status.prepare("UPDATE files SET status=? WHERE id=?");status.addBindValue(QFileInfo::exists(sources.value(1).toString())?0:1);status.addBindValue(sources.value(0));status.exec();
                }
                QSqlQuery stamp(db);stamp.prepare("UPDATE watched_roots SET last_scan_at=?,last_error=? WHERE path=?");stamp.addBindValue(now());stamp.addBindValue(error);stamp.addBindValue(root);stamp.exec();
            }
            const QString state=control->load()==3?"queued":control->load()==2?"cancelled":(failed>0?"completed_with_errors":"completed");
            update(state,completed,scan.files.size(),error);
            result.summary={{"status",state},{"running",false},{"completed",completed},{"total",scan.files.size()},{"imported",imported},{"updated",updated},{"moved",moved},{"failed",failed},{"skipped",scan.skipped},{"lastError",error}};
            FileRepository repository(connection);result.files=repository.queryFiles({},-1,{},{},{},{},{},{},{},{},{},"indexedAt",false);
        }
        db.close();
    }QSqlDatabase::removeDatabase(connection);return result;
}
}

LibraryService::LibraryService(QObject* parent):QObject(parent),m_control(std::make_shared<std::atomic_int>(0)) {
    m_activity={{"running",false},{"status","idle"},{"completed",0},{"total",0}};
    m_reconcile.setInterval(15*60*1000);connect(&m_reconcile,&QTimer::timeout,this,&LibraryService::scanNow);m_reconcile.start();
    m_watchDebounce.setInterval(1200);m_watchDebounce.setSingleShot(true);connect(&m_watchDebounce,&QTimer::timeout,this,&LibraryService::scanNow);
    connect(&m_watcher,&QFileSystemWatcher::directoryChanged,this,[this]{m_watchDebounce.start();});
    m_progressPoll.setInterval(300);connect(&m_progressPoll,&QTimer::timeout,this,[this] {
        const auto job=rows("SELECT status,completed,total,last_error AS lastError FROM library_jobs WHERE id=?",{m_jobId});
        if(!job.isEmpty()){m_activity=job.first().toMap();m_activity["running"]=true;emit activityChanged();}
    });
    QTimer::singleShot(0,this,[this]{rebuildWatches();restoreJobs();});
}
LibraryService::~LibraryService(){m_control->store(3);}
QVariantList LibraryService::watchedFolders()const{return rows("SELECT id,path,exclusions_json AS exclusionsJson,enabled,last_scan_at AS lastScanAt,last_error AS lastError FROM watched_roots ORDER BY path");}
bool LibraryService::removeWatchedFolder(int id){const bool ok=execute("DELETE FROM watched_roots WHERE id=?",{id});if(ok){rebuildWatches();emit changed();}return ok;}
void LibraryService::previewImport(const QVariantList& paths,const QStringList& exclusions) {
    auto* watcher=new QFutureWatcher<QVariantMap>(this);connect(watcher,&QFutureWatcher<QVariantMap>::finished,this,[this,watcher]{emit previewReady(watcher->result());watcher->deleteLater();});
    watcher->setFuture(QtConcurrent::run([paths,exclusions]{const auto scan=enumerate(paths,exclusions);return QVariantMap{{"supportedFiles",scan.files.size()},{"totalCandidates",scan.files.size()},{"unsupportedFiles",scan.skipped},{"totalBytes",scan.bytes},{"estimatedSeconds",qMax<qint64>(1,scan.bytes/(10*1024*1024)+scan.files.size()/5)},{"errors",scan.errors},{"paths",paths},{"exclusions",exclusions}};}));
}
void LibraryService::startImport(const QVariantList& paths,const QStringList& exclusions,bool watchFolders) {
    if(paths.isEmpty()||m_jobId>=0)return;
    if(watchFolders)for(const auto& value:paths){const QString path=pathOf(value);if(QFileInfo(path).isDir())execute("INSERT INTO watched_roots(path,exclusions_json) VALUES(?,?) ON CONFLICT(path) DO UPDATE SET exclusions_json=excluded.exclusions_json,enabled=1",{path,jsonList(exclusions)});}
    const QVariantMap payload{{"paths",paths},{"exclusions",exclusions}};
    QSqlQuery insert(DatabaseManager::instance().database());insert.prepare("INSERT INTO library_jobs(kind,payload_json,status,updated_at) VALUES('import',?,'queued',?)");insert.addBindValue(json(payload));insert.addBindValue(now());
    if(!insert.exec()){m_activity={{"status","failed"},{"lastError",insert.lastError().text()}};emit activityChanged();return;}
    emit changed();runJob(insert.lastInsertId().toInt(),payload);
}
void LibraryService::runJob(int id,const QVariantMap& payload) {
    m_jobId=id;m_control->store(0);m_activity={{"running",true},{"status","running"},{"completed",0},{"total",0}};emit activityChanged();m_progressPoll.start();
    auto* watcher=new QFutureWatcher<JobResult>(this);connect(watcher,&QFutureWatcher<JobResult>::finished,this,[this,watcher] {
        const auto result=watcher->result();watcher->deleteLater();m_progressPoll.stop();m_jobId=-1;m_activity=result.summary;
        if(m_indexing)m_indexing->scheduleIncremental(result.files);
        // Watch directories as well as roots so nested edits trigger reconciliation.
        rebuildWatches();if(!result.directories.isEmpty())m_watcher.addPaths(result.directories);
        emit activityChanged();emit changed();emit filesChanged();
    });watcher->setFuture(QtConcurrent::run(importWorker,id,payload,m_control));
}
void LibraryService::rebuildWatches(){if(!m_watcher.directories().isEmpty())m_watcher.removePaths(m_watcher.directories());QStringList paths;for(const auto& v:watchedFolders()){const auto r=v.toMap();if(r.value("enabled").toBool()&&QFileInfo(r.value("path").toString()).isDir())paths<<r.value("path").toString();}if(!paths.isEmpty())m_watcher.addPaths(paths);}
void LibraryService::restoreJobs(){const auto jobs=rows("SELECT id,payload_json,status FROM library_jobs WHERE status IN ('queued','running','paused') ORDER BY id LIMIT 1");if(jobs.isEmpty()){scanNow();return;}const auto row=jobs.first().toMap();const auto payload=QJsonDocument::fromJson(row.value("payload_json").toString().toUtf8()).toVariant().toMap();runJob(row.value("id").toInt(),payload);if(row.value("status")=="paused")pause();}
void LibraryService::scanNow()
{
    if(m_jobId>=0)return;
    QVariantList paths;QVariantMap rootExclusions;
    for(const auto& v:watchedFolders()) {
        const auto root=v.toMap();if(!root.value("enabled").toBool())continue;
        const QString path=root.value("path").toString();paths<<path;
        const auto array=QJsonDocument::fromJson(root.value("exclusionsJson").toString().toUtf8()).array();
        QStringList excluded;for(const auto& pattern:array)excluded<<pattern.toString();rootExclusions[path]=excluded;
    }
    if(paths.isEmpty())return;
    const QVariantMap payload{{"paths",paths},{"rootExclusions",rootExclusions}};
    QSqlQuery insert(DatabaseManager::instance().database());
    insert.prepare("INSERT INTO library_jobs(kind,payload_json,status,updated_at) VALUES('reconcile',?,'queued',?)");
    insert.addBindValue(json(payload));insert.addBindValue(now());
    if(insert.exec())runJob(insert.lastInsertId().toInt(),payload);
}void LibraryService::updateJobStatus(const QString& status){if(m_jobId>=0){execute("UPDATE library_jobs SET status=?,updated_at=? WHERE id=?",{status,now(),m_jobId});m_activity["status"]=status;emit activityChanged();}}
void LibraryService::pause(){m_control->store(1);updateJobStatus("paused");if(m_indexing)m_indexing->pause();}
void LibraryService::resume(){m_control->store(0);updateJobStatus("running");if(m_indexing)m_indexing->resume();}
void LibraryService::cancel(){m_control->store(2);updateJobStatus("cancelled");if(m_indexing)m_indexing->cancel();}
QVariantList LibraryService::favorites()const{return rows("SELECT f.id,f.name,f.path,f.extension FROM favorites v JOIN files f ON f.id=v.file_id WHERE f.removed_at IS NULL ORDER BY v.created_at DESC");}
bool LibraryService::isFavorite(int id)const{return !rows("SELECT file_id FROM favorites WHERE file_id=?",{id}).isEmpty();}
bool LibraryService::setFavorite(int id,bool value){const bool ok=value?execute("INSERT OR IGNORE INTO favorites(file_id,created_at) SELECT id,? FROM files WHERE id=? AND removed_at IS NULL",{now(),id}):execute("DELETE FROM favorites WHERE file_id=?",{id});if(ok){emit changed();emit filesChanged();}return ok;}
QStringList LibraryService::tags(int id)const{QStringList result;for(const auto& row:rows("SELECT tag FROM file_tags WHERE file_id=? ORDER BY tag",{id}))result<<row.toMap().value("tag").toString();return result;}
bool LibraryService::setTags(int id,const QStringList& values){auto db=DatabaseManager::instance().database();if(!db.transaction())return false;bool ok=execute("DELETE FROM file_tags WHERE file_id=?",{id});for(const auto& tag:values)if(!tag.trimmed().isEmpty())ok=ok&&execute("INSERT OR IGNORE INTO file_tags(file_id,tag) SELECT id,? FROM files WHERE id=?",{tag.trimmed().left(80),id});if(!ok||!db.commit()){db.rollback();return false;}emit changed();emit filesChanged();return true;}
QVariantList LibraryService::savedSearches()const{return rows("SELECT id,name,query_text AS query,filters_json AS filtersJson FROM saved_searches ORDER BY name COLLATE NOCASE");}
bool LibraryService::saveSearch(const QString& name,const QString& query){if(name.trimmed().isEmpty())return false;const bool ok=execute("INSERT INTO saved_searches(name,query_text,created_at) VALUES(?,?,?) ON CONFLICT(name) DO UPDATE SET query_text=excluded.query_text",{name.trimmed(),query,now()});if(ok)emit changed();return ok;}
bool LibraryService::deleteSavedSearch(int id){const bool ok=execute("DELETE FROM saved_searches WHERE id=?",{id});if(ok)emit changed();return ok;}
QVariantMap LibraryService::readingPosition(int id)const{const auto values=rows("SELECT anchor_json FROM reading_positions WHERE file_id=?",{id});return values.isEmpty()?QVariantMap():QJsonDocument::fromJson(values.first().toMap().value("anchor_json").toString().toUtf8()).toVariant().toMap();}
bool LibraryService::saveReadingPosition(int id,const QVariantMap& anchor){const bool ok=execute("INSERT INTO reading_positions(file_id,anchor_json,opened_at) SELECT id,?,? FROM files WHERE id=? ON CONFLICT(file_id) DO UPDATE SET anchor_json=excluded.anchor_json,opened_at=excluded.opened_at",{json(anchor),now(),id});if(ok)emit changed();return ok;}
QVariantList LibraryService::duplicates()const{return rows("SELECT f.id,f.name,f.path,f.content_hash AS contentHash,f.size_bytes AS sizeBytes FROM files f WHERE removed_at IS NULL AND content_hash IS NOT NULL AND content_hash<>'' AND content_hash IN(SELECT content_hash FROM files WHERE removed_at IS NULL GROUP BY content_hash HAVING COUNT(*)>1) ORDER BY content_hash,path");}
QVariantList LibraryService::missingSources()const{return rows("SELECT id,name,path FROM files WHERE status=1 AND removed_at IS NULL ORDER BY name");}
bool LibraryService::relinkFile(int id,const QString& path){FileRepository repository;const bool ok=repository.relinkFile(id,pathOf(path));if(ok){emit filesChanged();scanNow();}return ok;}
bool LibraryService::removeFile(int id){const bool ok=execute("UPDATE files SET removed_at=? WHERE id=?",{now(),id});if(ok){m_removedId=id;emit filesChanged();emit changed();}return ok;}
bool LibraryService::undoRemoval(){if(m_removedId<0)return false;const bool ok=execute("UPDATE files SET removed_at=NULL WHERE id=?",{m_removedId});if(ok){m_removedId=-1;emit filesChanged();emit changed();}return ok;}
bool LibraryService::clearHistory(){auto db=DatabaseManager::instance().database();if(!db.transaction())return false;const bool ok=execute("DELETE FROM retrieval_events")&&execute("DELETE FROM reading_positions");if(!ok||!db.commit()){db.rollback();return false;}emit changed();return true;}
