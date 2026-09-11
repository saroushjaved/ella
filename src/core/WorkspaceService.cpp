#include "WorkspaceService.h"
#include "AppConfig.h"
#include "database/DatabaseManager.h"
#include "notes/NoteManager.h"
#include <QtConcurrent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

namespace {
QVariantMap failure(const QString& error) { return {{"ok",false},{"error",error}}; }
QString localPath(const QString& value) { const QUrl url(value); return url.isLocalFile() ? url.toLocalFile() : value; }
bool writeAtomic(const QString& path, const QByteArray& data) {
    QSaveFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
QByteArray readBytes(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
qint64 directorySize(const QString& path) {
    qint64 bytes=0; QDirIterator it(path,QDir::Files|QDir::NoSymLinks,QDirIterator::Subdirectories);
    while(it.hasNext()) { it.next(); bytes+=it.fileInfo().size(); } return bytes;
}
struct Connection {
    QString name = "workspace_" + QUuid::createUuid().toString(QUuid::Id128);
    QSqlDatabase db;
    explicit Connection(const QString& path) {
        db=QSqlDatabase::addDatabase("QSQLITE",name); db.setDatabaseName(path);
        db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=10000"); db.open();
    }
    ~Connection() { db.close(); db=QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
};
QJsonArray rows(QSqlDatabase db, const QString& sql) {
    QJsonArray result; QSqlQuery q(db); if (!q.exec(sql)) return result;
    while(q.next()) { QJsonObject row; for(int i=0;i<q.record().count();++i) row[q.record().fieldName(i)]=QJsonValue::fromVariant(q.value(i)); result.append(row); }
    return result;
}
QString safeRestoredNoteName(int id, const QString& token) {
    return QStringLiteral("restored-%1-%2.ellanote").arg(token, QString::number(id));
}
QVariantMap backup(const QString& path, bool knowledgeOnly) {
    if(path.isEmpty()) return failure("Choose a destination file.");
    QTemporaryDir temp(QDir(AppConfig::appDataDirectory()).filePath("backup-work-XXXXXX"));
    if(!temp.isValid()) return failure("Cannot create backup working directory.");
    const QString snapshot = temp.filePath("library.sqlite");
    {
        Connection source(AppConfig::databasePath()); if(!source.db.isOpen()) return failure("Cannot open library for backup.");
        QSqlQuery q(source.db); q.prepare("VACUUM INTO ?"); q.addBindValue(snapshot);
        if(!q.exec()) return failure(q.lastError().text());
    }
    Connection source(snapshot);
    const QString archivePath = temp.filePath("archive.zip");
    QZipWriter zip(archivePath); zip.setCompressionPolicy(QZipWriter::AutoCompress);
    QJsonObject manifest{{"format","ella-library"},{"version",1},{"includesOriginalFiles",false},{"createdAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    QJsonArray noteEntries;
    QSqlQuery notes(source.db); notes.exec("SELECT id,path,name FROM files WHERE lower(extension)='ellanote'");
    while(notes.next()) {
        const int id=notes.value(0).toInt(); QFile note(notes.value(1).toString());
        if(!note.open(QIODevice::ReadOnly)) return failure(QStringLiteral("Note %1 cannot be read; backup was not saved.").arg(id));
        const QString entry=QStringLiteral("notes/%1.md").arg(id);
        zip.addFile(entry,&note);
        noteEntries.append(QJsonObject{{"id",id},{"entry",entry},{"title",notes.value(2).toString()}});
    }
    manifest["notes"]=noteEntries;
    if(!knowledgeOnly) { QFile dbFile(snapshot); if(!dbFile.open(QIODevice::ReadOnly)) return failure("Cannot read snapshot."); zip.addFile("library.sqlite",&dbFile); }
    QJsonObject exports{{"annotations",rows(source.db,"SELECT a.*,f.path AS source_path FROM annotations a JOIN files f ON f.id=a.file_id")},
                        {"documentNotes",rows(source.db,"SELECT n.*,f.path AS source_path FROM document_notes n JOIN files f ON f.id=n.file_id")},
                        {"sourceLinks",rows(source.db,"SELECT * FROM note_links")}};
    zip.addFile("annotations.json",QJsonDocument(exports).toJson());
    zip.addFile("manifest.json",QJsonDocument(manifest).toJson());
    zip.close();
    if(zip.status()!=QZipWriter::NoError) return failure("Could not finish the archive; destination was left unchanged.");
    const QByteArray archiveBytes = readBytes(archivePath);
    if (archiveBytes.isEmpty() || !writeAtomic(path, archiveBytes)) return failure("Could not save the archive; destination was left unchanged.");
    return {{"ok",true},{"path",path},{"includesOriginalFiles",false}};
}
QVariantMap stageRestore(const QString& path) {
    QZipReader zip(path);
    if(!zip.isReadable()) return failure("Cannot read backup archive.");
    qint64 bytes=0; QSet<QString> entries;
    for(const auto& info:zip.fileInfoList()) {
        bytes+=info.size;
        if(!info.isFile || info.isSymLink || info.size<0 || info.size>512ll*1024*1024 || bytes>1024ll*1024*1024 || entries.contains(info.filePath))
            return failure("Backup has unsafe, duplicate, or oversized entries.");
        entries.insert(info.filePath);
    }
    const auto manifest=QJsonDocument::fromJson(zip.fileData("manifest.json")).object();
    if(manifest["format"]!="ella-library" || manifest["version"].toInt()!=1 || !entries.contains("library.sqlite")) return failure("Not a supported ELLA library backup.");
    const QString stage=QDir(AppConfig::appDataDirectory()).filePath("restore-"+QUuid::createUuid().toString(QUuid::Id128));
    if(!QDir().mkpath(stage+"/notes")) return failure("Cannot create restore staging directory.");
    if(!writeAtomic(stage+"/library.sqlite",zip.fileData("library.sqlite"))) return failure("Not enough space to stage the library.");
    const QString restoreToken = QUuid::createUuid().toString(QUuid::Id128);
    QJsonArray restoredNotes;
    {
        Connection db(stage+"/library.sqlite");
        QSqlQuery check(db.db);
        if(!db.db.isOpen() || !check.exec("PRAGMA quick_check") || !check.next() || check.value(0)!="ok") return failure("Backup database failed its integrity check.");
        if(!check.exec("PRAGMA user_version") || !check.next() || check.value(0).toInt()>2) return failure("Backup requires a newer ELLA version.");
        if(!db.db.transaction()) return failure("Cannot prepare restored notes.");
        QSet<int> ids;
        for(const auto& value:manifest["notes"].toArray()) {
            const auto note=value.toObject(); const int id=note["id"].toInt(-1);
            const QString entry=QStringLiteral("notes/%1.md").arg(id);
            if(id<0 || ids.contains(id) || note["entry"].toString()!=entry || !entries.contains(entry)) return failure("Invalid note manifest.");
            ids.insert(id);
            const QString stagedNotePath=stage+QStringLiteral("/notes/%1.ellanote").arg(id);
            const QString finalNotePath=QDir(AppConfig::notesDirectory()).filePath(safeRestoredNoteName(id, restoreToken));
            if(!writeAtomic(stagedNotePath,zip.fileData(entry))) return failure("Cannot restore note; current library is unchanged.");
            restoredNotes.append(QJsonObject{{"staged",stagedNotePath},{"final",finalNotePath}});
            QSqlQuery update(db.db); update.prepare("UPDATE files SET path=? WHERE id=? AND lower(extension)='ellanote'"); update.addBindValue(finalNotePath); update.addBindValue(id);
            if(!update.exec() || update.numRowsAffected()!=1) return failure("Backup notes do not match its catalog.");
        }
        if(!db.db.commit()) return failure("Cannot commit staged restore.");
    }
    if(!writeAtomic(AppConfig::appDataDirectory()+"/pending-restore.json",QJsonDocument(QJsonObject{{"stage",stage},{"notes",restoredNotes}}).toJson())) return failure("Cannot schedule restore.");
    return {{"ok",true},{"restartRequired",true},{"path",path}};
}
}

WorkspaceService::WorkspaceService(QObject* parent):QObject(parent) {
    connect(&m_worker,&QFutureWatcher<QVariantMap>::finished,this,[this] {
        const auto result=m_worker.result(); m_busy=false; m_status=result.value("ok").toBool()?"Complete":result.value("error").toString();
        emit busyChanged(); emit statusChanged(); emit operationFinished(result);
    });
}
WorkspaceService::~WorkspaceService() { m_worker.waitForFinished(); }
QVariantMap WorkspaceService::launch(const QString& label,std::function<QVariantMap()> operation) {
    if(m_busy) return failure("Another library operation is running.");
    m_busy=true; m_status=label; emit busyChanged(); emit statusChanged();
    m_worker.setFuture(QtConcurrent::run(std::move(operation))); return {{"ok",true},{"queued",true}};
}
QVariantMap WorkspaceService::backupLibrary(const QString& path) { return launch("Backing up library…",[path]{return backup(localPath(path),false);}); }
QVariantMap WorkspaceService::exportKnowledge(const QString& path) { return launch("Exporting notes and annotations…",[path]{return backup(localPath(path),true);}); }
QVariantMap WorkspaceService::restoreLibrary(const QString& path) { return launch("Checking and staging restore…",[path]{return stageRestore(localPath(path));}); }
QVariantMap WorkspaceService::storageStatus() const {
    return {{"databaseBytes",QFileInfo(AppConfig::databasePath()).size()}, {"cacheBytes",directorySize(AppConfig::cacheDirectory())},
            {"componentBytes",directorySize(AppConfig::appDataDirectory()+"/components")},{"notesBytes",directorySize(AppConfig::notesDirectory())}};
}
QVariantMap WorkspaceService::clearDerivedCaches() {
    return launch("Clearing preview caches…",[]{
        // Only managed preview files are removable; content indexes and personal notes are untouched.
        QDirIterator it(AppConfig::cacheDirectory(),QDir::Files|QDir::NoSymLinks,QDirIterator::Subdirectories);
        int failed=0; while(it.hasNext()) if(!QFile::remove(it.next())) ++failed;
        return failed ? failure("Some cache files are in use. Close their previews and retry.") : QVariantMap{{"ok",true}};
    });
}
bool WorkspaceService::clearHistory() { QSqlQuery q(DatabaseManager::instance().database()); return q.exec("DELETE FROM retrieval_events"); }
QVariantMap WorkspaceService::createSourceNote(int fileId,const QString& title,const QString& quote,const QVariantMap& anchor) {
    QSqlQuery source(DatabaseManager::instance().database()); source.prepare("SELECT name,path FROM files WHERE id=?"); source.addBindValue(fileId);
    if(!source.exec() || !source.next()) return failure("Source is no longer in the library.");
    NoteManager notes;
    const QString html=QStringLiteral("<h1>%1</h1><blockquote>%2</blockquote><p>Source: @file_%3 — %4</p><p>%5</p>")
        .arg(title.toHtmlEscaped(),quote.toHtmlEscaped()).arg(fileId).arg(source.value(0).toString().toHtmlEscaped(),anchor.value("locator").toString().toHtmlEscaped());
    const auto note=notes.createNote(title,"","","","",html);
    if(note.isEmpty()) return failure("Could not save note.");
    QSqlQuery link(DatabaseManager::instance().database()); link.prepare("INSERT INTO note_links(note_file_id,source_file_id,anchor_json) VALUES(?,?,?)");
    link.addBindValue(note.value("id")); link.addBindValue(fileId); link.addBindValue(QString::fromUtf8(QJsonDocument::fromVariant(anchor).toJson(QJsonDocument::Compact)));
    if(!link.exec()) return failure("Note saved, but the source link could not be saved.");
    emit notesChanged(); return {{"ok",true},{"id",note.value("id")}};
}
QVariantList WorkspaceService::backlinks(int fileId) const {
    QSqlQuery q(DatabaseManager::instance().database()); q.prepare("SELECT f.id,f.name,l.anchor_json FROM note_links l JOIN files f ON f.id=l.note_file_id WHERE l.source_file_id=?"); q.addBindValue(fileId);
    QVariantList result; if(q.exec()) while(q.next()) result.append(QVariantMap{{"id",q.value(0)},{"name",q.value(1)},{"anchor",QJsonDocument::fromJson(q.value(2).toByteArray()).toVariant()}}); return result;
}
bool WorkspaceService::openReleasePage() const { return QDesktopServices::openUrl(QUrl("https://github.com/saroushjaved/ella/releases")); }
bool WorkspaceService::applyPendingRestore(QString* error) {
    const QString pending=AppConfig::appDataDirectory()+"/pending-restore.json";
    if(!QFile::exists(pending)) return true;
    const QJsonObject request=QJsonDocument::fromJson(readBytes(pending)).object();
    const QString stage=request["stage"].toString();
    const QString root=QDir(AppConfig::appDataDirectory()).canonicalPath();
    const QFileInfo staged(stage);
    if(staged.isSymLink() || staged.dir().canonicalPath()!=root || !staged.fileName().startsWith("restore-") || !QFile::exists(stage+"/library.sqlite")) { *error="Invalid pending restore. Existing library was preserved."; return false; }
    QStringList installedNotes;
    for (const QJsonValue& value : request["notes"].toArray()) {
        const QJsonObject note=value.toObject();
        const QString staged=QFileInfo(note["staged"].toString()).absoluteFilePath();
        const QString final=QFileInfo(note["final"].toString()).absoluteFilePath();
        const QString stageNotes=QDir(stage+"/notes").canonicalPath()+QDir::separator();
        const QString finalRoot=QDir(AppConfig::notesDirectory()).canonicalPath()+QDir::separator();
        if(!staged.startsWith(stageNotes,Qt::CaseInsensitive) || !final.startsWith(finalRoot,Qt::CaseInsensitive)
           || QFileInfo(staged).isSymLink() || QFileInfo(final).exists() || !writeAtomic(final,readBytes(staged))) {
            for(const QString& installed:installedNotes) QFile::remove(installed);
            *error="Cannot restore notes safely. Existing library was preserved."; return false;
        }
        installedNotes.append(final);
    }
    const QString dbPath=AppConfig::databasePath();
    const QString previous=dbPath+".before-restore-"+QUuid::createUuid().toString(QUuid::Id128);
    QStringList moved;
    for(const QString& suffix:QStringList{"","-wal","-shm"}) {
        if(!QFile::exists(dbPath+suffix)) continue;
        if(!QFile::rename(dbPath+suffix,previous+suffix)) {
            for(const QString& done:moved) QFile::rename(previous+done,dbPath+done);
            for(const QString& installed:installedNotes) QFile::remove(installed);
            *error="Cannot preserve current database before restore."; return false;
        }
        moved.append(suffix);
    }
    if(!QFile::rename(stage+"/library.sqlite",dbPath)) {
        for(const QString& done:moved) QFile::rename(previous+done,dbPath+done);
        for(const QString& installed:installedNotes) QFile::remove(installed);
        *error="Cannot install restored database. Existing library was preserved."; return false;
    }
    QFile::remove(pending); return true;
}
