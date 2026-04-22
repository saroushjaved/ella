
#include "models/FileListModel.h"
#include "core/AppConfig.h"
#include "database/DatabaseManager.h"
#include "search/ContentExtractor.h"
#include "search/IndexingService.h"
#include "sync/CloudSyncService.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QSettings>
#include <QVariant>
#include <QTextStream>
#include <QUrl>
#include <QSet>
#include <algorithm>

namespace
{
constexpr int kMaxImportCandidatesPerBatch = 25000;
constexpr int kMaxImportPathLength = 1024;

QString defaultAnnotationColor(const QString& color)
{
    return color.trimmed().isEmpty() ? QStringLiteral("#fff59d") : color.trimmed();
}

QString nowIso()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

QString normalizedImportPath(const QVariant& value)
{
    QString path = value.toString().trimmed();
    if (value.metaType().id() == QMetaType::QUrl) {
        path = value.toUrl().toLocalFile().trimmed();
    } else if (path.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
        path = QUrl(path).toLocalFile().trimmed();
    }

    if (path.isEmpty()) {
        return QString();
    }

    return QFileInfo(path).absoluteFilePath();
}

QVariantList queryRows(const QString& sql, const QVariantList& binds = QVariantList())
{
    QVariantList rows;
    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(sql);
    for (const QVariant& bind : binds) {
        query.addBindValue(bind);
    }

    if (!query.exec()) {
        qWarning() << "Diagnostics query failed:" << query.lastError().text();
        return rows;
    }

    const QSqlRecord record = query.record();
    while (query.next()) {
        QVariantMap row;
        for (int i = 0; i < record.count(); ++i) {
            row[record.fieldName(i)] = query.value(i);
        }
        rows.append(row);
    }
    return rows;
}

bool copyFileIfExists(const QString& sourcePath, const QString& destinationPath)
{
    if (!QFile::exists(sourcePath)) {
        return false;
    }

    QFile::remove(destinationPath);
    return QFile::copy(sourcePath, destinationPath);
}

bool containsControlCharacters(const QString& value)
{
    static const QRegularExpression controlChars(QStringLiteral("[\\x00-\\x1F]"));
    return controlChars.match(value).hasMatch();
}

bool zipDirectoryWithPowerShell(const QString& sourceDirPath, const QString& destinationZipPath, QString* error)
{
#ifdef Q_OS_WIN
    QStringList args;
    args << QStringLiteral("-NoProfile")
         << QStringLiteral("-ExecutionPolicy")
         << QStringLiteral("Bypass")
         << QStringLiteral("-Command")
         << QStringLiteral("Compress-Archive -Path '%1\\*' -DestinationPath '%2' -Force")
                .arg(QDir::toNativeSeparators(sourceDirPath),
                     QDir::toNativeSeparators(destinationZipPath));

    QProcess process;
    process.start(QStringLiteral("powershell"), args);
    if (!process.waitForFinished(30000)) {
        if (error) {
            *error = QStringLiteral("Timed out while creating diagnostics zip");
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
            if (error->isEmpty()) {
                *error = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
            }
            if (error->isEmpty()) {
                *error = QStringLiteral("Compress-Archive failed");
            }
        }
        return false;
    }

    return QFileInfo::exists(destinationZipPath);
#else
    Q_UNUSED(sourceDirPath)
    Q_UNUSED(destinationZipPath)
    if (error) {
        *error = QStringLiteral("Zip export is currently implemented for Windows only");
    }
    return false;
#endif
}
}

FileListModel::FileListModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_importStatus[QStringLiteral("totalCandidates")] = 0;
    m_importStatus[QStringLiteral("importedCount")] = 0;
    m_importStatus[QStringLiteral("skippedCount")] = 0;
    m_importStatus[QStringLiteral("failedCount")] = 0;
    m_importStatus[QStringLiteral("lastImportedAt")] = QString();
    m_importStatus[QStringLiteral("lastMessage")] = QStringLiteral("No imports yet");

    reload();
}

void FileListModel::setIndexingService(IndexingService* indexingService)
{
    if (m_indexingService == indexingService) {
        return;
    }

    if (m_indexingService) {
        QObject::disconnect(m_indexingService, nullptr, this, nullptr);
    }

    m_indexingService = indexingService;
    if (m_indexingService) {
        connect(m_indexingService,
                &IndexingService::statusChanged,
                this,
                &FileListModel::indexStatusChanged,
                Qt::UniqueConnection);
        m_indexingService->scheduleIncremental(allFilesForBackgroundJobs());
    }

    emit indexStatusChanged();
}

void FileListModel::setCloudSyncService(CloudSyncService* cloudSyncService)
{
    m_cloudSyncService = cloudSyncService;
}

int FileListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return m_files.count();
}

QVariant FileListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_files.count()) {
        return {};
    }

    const FileRecord& file = m_files.at(index.row());

    switch (role) {
    case IdRole:
        return file.id;
    case NameRole:
        return file.name;
    case PathRole:
        return file.path;
    case ExtensionRole:
        return file.extension;
    case MimeTypeRole:
        return file.mimeType;
    case SizeRole:
        return file.sizeBytes;
    case IndexedAtRole:
        return file.indexedAt.toString(Qt::ISODate);
    case StatusRole:
        return static_cast<int>(file.status);
    case SourceRole:
        return file.source;
    case DocumentTypeRole:
        return file.documentType;
    case SearchSnippetRole:
        return file.searchSnippet;
    case SearchMatchReasonRole:
        return file.searchMatchReason;
    case SearchScoreRole:
        return file.searchScore;
    default:
        return {};
    }
}

QHash<int, QByteArray> FileListModel::roleNames() const
{
    return {
        { IdRole, "fileId" },
        { NameRole, "name" },
        { PathRole, "path" },
        { ExtensionRole, "extension" },
        { MimeTypeRole, "mimeType" },
        { SizeRole, "sizeBytes" },
        { IndexedAtRole, "indexedAt" },
        { StatusRole, "statusValue" },
        { SourceRole, "source" },
        { DocumentTypeRole, "documentType" },
        { SearchSnippetRole, "searchSnippet" },
        { SearchMatchReasonRole, "searchMatchReason" },
        { SearchScoreRole, "searchScore" }
    };
}

void FileListModel::refreshFiles()
{
    beginResetModel();
    m_files = m_repository.queryFiles(
        m_searchText,
        m_currentCollectionId,
        m_currentTechnicalDomain,
        m_currentSubject,
        m_currentSubtopic,
        m_statusFilters,
        m_extensionFilters,
        m_documentTypeFilters,
        m_dateFieldFilter,
        m_dateFromFilter,
        m_dateToFilter,
        m_sortField,
        m_sortAscending
        );
    endResetModel();
}

void FileListModel::reload()
{
    m_searchText.clear();
    m_currentCollectionId = -1;
    m_currentTechnicalDomain.clear();
    m_currentSubject.clear();
    m_currentSubtopic.clear();

    m_statusFilters.clear();
    m_extensionFilters.clear();
    m_documentTypeFilters.clear();
    m_dateFieldFilter.clear();
    m_dateFromFilter.clear();
    m_dateToFilter.clear();

    m_sortField = "indexedAt";
    m_sortAscending = false;
    m_lastTrackedQuery.clear();
    m_pendingTtfrQuery.clear();
    m_queryTimer.invalidate();

    refreshFiles();

    if (m_indexingService) {
        m_indexingService->scheduleIncremental(allFilesForBackgroundJobs());
        emit indexStatusChanged();
    }
}

void FileListModel::refreshCurrentView()
{
    refreshFiles();
}

void FileListModel::search(const QString& text)
{
    m_searchText = text;
    const QString trimmedQuery = text.trimmed();

    if (trimmedQuery.isEmpty()) {
        m_pendingTtfrQuery.clear();
        m_queryTimer.invalidate();
    } else if (trimmedQuery != m_lastTrackedQuery) {
        m_lastTrackedQuery = trimmedQuery;
        m_pendingTtfrQuery = trimmedQuery;
        m_queryTimer.start();
        trackRetrievalEventInternal(
            QStringLiteral("query"),
            trimmedQuery,
            -1,
            -1,
            QString(),
            QStringLiteral("{\"surface\":\"browser\"}"));
    }

    refreshFiles();

    if (!m_pendingTtfrQuery.isEmpty() && !m_files.isEmpty()) {
        const int firstFileId = m_files.first().id;
        const int latencyMs = m_queryTimer.isValid() ? static_cast<int>(m_queryTimer.elapsed()) : -1;
        const QString metadata = QStringLiteral("{\"surface\":\"browser\",\"resultCount\":%1}")
                                     .arg(m_files.count());
        trackRetrievalEventInternal(
            QStringLiteral("time_to_first_result"),
            m_pendingTtfrQuery,
            firstFileId,
            latencyMs,
            QString(),
            metadata);
        m_pendingTtfrQuery.clear();
    }
}

void FileListModel::setAdvancedFilters(int statusValue,
                                       const QString& extension,
                                       const QString& documentType,
                                       const QString& dateField,
                                       const QString& dateFrom,
                                       const QString& dateTo)
{
    QVariantList statusFilters;
    if (statusValue == 0) {
        statusFilters.append(QStringLiteral("active"));
    } else if (statusValue == 1) {
        statusFilters.append(QStringLiteral("failed"));
    }

    QVariantList extensionFilters;
    const QString trimmedExtension = extension.trimmed();
    if (!trimmedExtension.isEmpty()) {
        extensionFilters.append(trimmedExtension);
    }

    QVariantList documentTypeFilters;
    const QString trimmedDocumentType = documentType.trimmed();
    if (!trimmedDocumentType.isEmpty()) {
        documentTypeFilters.append(trimmedDocumentType);
    }

    setAdvancedFiltersV2(statusFilters,
                         extensionFilters,
                         documentTypeFilters,
                         dateField,
                         dateFrom,
                         dateTo);
}

void FileListModel::setAdvancedFiltersV2(const QVariantList& statusFilters,
                                         const QVariantList& extensionFilters,
                                         const QVariantList& documentTypeFilters,
                                         const QString& dateField,
                                         const QString& dateFrom,
                                         const QString& dateTo)
{
    m_statusFilters.clear();
    for (const QVariant& value : statusFilters) {
        const QString token = value.toString().trimmed().toLower();
        if (!token.isEmpty() && !m_statusFilters.contains(token)) {
            m_statusFilters.append(token);
        }
    }

    m_extensionFilters.clear();
    for (const QVariant& value : extensionFilters) {
        QString token = value.toString().trimmed().toLower();
        if (token.startsWith('.')) {
            token.remove(0, 1);
        }
        if (!token.isEmpty() && !m_extensionFilters.contains(token)) {
            m_extensionFilters.append(token);
        }
    }

    m_documentTypeFilters.clear();
    for (const QVariant& value : documentTypeFilters) {
        const QString token = value.toString().trimmed();
        if (!token.isEmpty() && !m_documentTypeFilters.contains(token, Qt::CaseInsensitive)) {
            m_documentTypeFilters.append(token);
        }
    }

    m_dateFieldFilter = dateField.trimmed();
    m_dateFromFilter = dateFrom.trimmed();
    m_dateToFilter = dateTo.trimmed();
    refreshFiles();
}

void FileListModel::clearAdvancedFilters()
{
    m_statusFilters.clear();
    m_extensionFilters.clear();
    m_documentTypeFilters.clear();
    m_dateFieldFilter.clear();
    m_dateFromFilter.clear();
    m_dateToFilter.clear();
    refreshFiles();
}

void FileListModel::setSort(const QString& fieldName, bool ascending)
{
    m_sortField = fieldName.trimmed().isEmpty() ? QStringLiteral("indexedAt") : fieldName.trimmed();
    m_sortAscending = ascending;
    refreshFiles();
}

bool FileListModel::addFile(const QString& filePath,
                            const QString& technicalDomain,
                            const QString& subject,
                            const QString& subtopic,
                            const QString& location,
                            const QString& source,
                            const QString& author,
                            const QString& documentType,
                            const QString& remarks)
{
    const bool success = m_repository.addFile(
        filePath,
        technicalDomain,
        subject,
        subtopic,
        location,
        source,
        author,
        documentType,
        remarks
        );

    refreshFiles();

    if (success) {
        const int fileId = m_repository.getFileDetailsByPath(QFileInfo(filePath).absoluteFilePath())
                               .value(QStringLiteral("id"))
                               .toInt();
        if (fileId >= 0) {
            notifyFileChanged(fileId, true, true);
        } else if (m_indexingService) {
            m_indexingService->scheduleIncremental(allFilesForBackgroundJobs());
            emit indexStatusChanged();
        }
    }

    return success;
}

QVariantMap FileListModel::importFiles(const QVariantList& filePaths)
{
    QVariantMap result;
    int importedCount = 0;
    int skippedCount = 0;
    int failedCount = 0;
    int invalidPathCount = 0;
    int tooLongPathCount = 0;
    bool truncated = false;

    QSet<QString> seenPaths;
    int acceptedCandidates = 0;
    for (const QVariant& entry : filePaths) {
        if (acceptedCandidates >= kMaxImportCandidatesPerBatch) {
            truncated = true;
            break;
        }

        const QString absolutePath = normalizedImportPath(entry);
        if (absolutePath.isEmpty()) {
            ++failedCount;
            ++invalidPathCount;
            continue;
        }

        if (absolutePath.size() > kMaxImportPathLength) {
            ++failedCount;
            ++tooLongPathCount;
            continue;
        }

        if (containsControlCharacters(absolutePath) || QFileInfo(absolutePath).fileName().trimmed().isEmpty()) {
            ++failedCount;
            ++invalidPathCount;
            continue;
        }

        if (seenPaths.contains(absolutePath)) {
            ++skippedCount;
            continue;
        }
        seenPaths.insert(absolutePath);
        ++acceptedCandidates;

        const QFileInfo info(absolutePath);
        if (!info.exists() || !info.isFile()) {
            ++failedCount;
            continue;
        }

        const QVariantMap existing = m_repository.getFileDetailsByPath(absolutePath);
        if (!existing.isEmpty()) {
            ++skippedCount;
            continue;
        }

        const QVariantMap defaults = defaultMetadataForImport(absolutePath);
        const bool ok = m_repository.addFile(
            absolutePath,
            defaults.value(QStringLiteral("technicalDomain")).toString(),
            defaults.value(QStringLiteral("subject")).toString(),
            defaults.value(QStringLiteral("subtopic")).toString(),
            defaults.value(QStringLiteral("location")).toString(),
            defaults.value(QStringLiteral("source")).toString(),
            defaults.value(QStringLiteral("author")).toString(),
            defaults.value(QStringLiteral("documentType")).toString(),
            QString());

        if (!ok) {
            ++failedCount;
            continue;
        }

        ++importedCount;
        const int fileId = m_repository.getFileDetailsByPath(absolutePath)
                               .value(QStringLiteral("id"))
                               .toInt();
        if (fileId >= 0) {
            notifyFileChanged(fileId, true, true);
        }
    }

    refreshFiles();
    if (m_indexingService) {
        emit indexStatusChanged();
    }

    const int totalCandidates = filePaths.size();
    const QString importedAt = nowIso();
    QString summary = QStringLiteral("Imported %1, skipped %2, failed %3")
                                .arg(importedCount)
                                .arg(skippedCount)
                                .arg(failedCount);
    if (truncated) {
        summary += QStringLiteral(" (limited to first %1 candidates)").arg(kMaxImportCandidatesPerBatch);
    }
    if (tooLongPathCount > 0) {
        summary += QStringLiteral(", %1 long-path entries ignored").arg(tooLongPathCount);
    }
    if (invalidPathCount > 0) {
        summary += QStringLiteral(", %1 invalid-path entries ignored").arg(invalidPathCount);
    }

    m_importStatus[QStringLiteral("totalCandidates")] = totalCandidates;
    m_importStatus[QStringLiteral("processedCandidates")] = acceptedCandidates;
    m_importStatus[QStringLiteral("importedCount")] = importedCount;
    m_importStatus[QStringLiteral("skippedCount")] = skippedCount;
    m_importStatus[QStringLiteral("failedCount")] = failedCount;
    m_importStatus[QStringLiteral("pathTooLongCount")] = tooLongPathCount;
    m_importStatus[QStringLiteral("invalidPathCount")] = invalidPathCount;
    m_importStatus[QStringLiteral("truncated")] = truncated;
    m_importStatus[QStringLiteral("lastImportedAt")] = importedAt;
    m_importStatus[QStringLiteral("lastMessage")] = summary;

    result = m_importStatus;

    trackRetrievalEventInternal(
        QStringLiteral("import_batch"),
        QString(),
        -1,
        -1,
        QString(),
        QStringLiteral("{\"total\":%1,\"imported\":%2,\"skipped\":%3,\"failed\":%4}")
            .arg(totalCandidates)
            .arg(importedCount)
            .arg(skippedCount)
            .arg(failedCount));

    return result;
}

QVariantMap FileListModel::importFolder(const QString& folderPath)
{
    QVariantList paths;
    int scannedEntries = 0;
    int invalidPathCount = 0;
    int tooLongPathCount = 0;
    bool truncated = false;

    const QString normalizedFolder = QFileInfo(folderPath.trimmed()).absoluteFilePath();
    QDir root(normalizedFolder);
    if (!root.exists()) {
        QVariantMap result;
        result[QStringLiteral("totalCandidates")] = 0;
        result[QStringLiteral("importedCount")] = 0;
        result[QStringLiteral("skippedCount")] = 0;
        result[QStringLiteral("failedCount")] = 1;
        result[QStringLiteral("processedCandidates")] = 0;
        result[QStringLiteral("scannedCount")] = 0;
        result[QStringLiteral("pathTooLongCount")] = 0;
        result[QStringLiteral("invalidPathCount")] = 0;
        result[QStringLiteral("truncated")] = false;
        result[QStringLiteral("lastImportedAt")] = nowIso();
        result[QStringLiteral("lastMessage")] = QStringLiteral("Folder does not exist");
        m_importStatus = result;
        return result;
    }

    QDirIterator it(
        normalizedFolder,
        QDir::Files | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);

    while (it.hasNext()) {
        const QString candidatePath = QFileInfo(it.next()).absoluteFilePath();
        ++scannedEntries;

        if (candidatePath.size() > kMaxImportPathLength) {
            ++tooLongPathCount;
            continue;
        }

        if (containsControlCharacters(candidatePath) || QFileInfo(candidatePath).fileName().trimmed().isEmpty()) {
            ++invalidPathCount;
            continue;
        }

        paths.append(candidatePath);
        if (paths.size() >= kMaxImportCandidatesPerBatch) {
            truncated = true;
            break;
        }
    }

    QVariantMap result = importFiles(paths);
    result[QStringLiteral("scannedCount")] = scannedEntries;
    result[QStringLiteral("pathTooLongCount")] =
        result.value(QStringLiteral("pathTooLongCount")).toInt() + tooLongPathCount;
    result[QStringLiteral("invalidPathCount")] =
        result.value(QStringLiteral("invalidPathCount")).toInt() + invalidPathCount;
    result[QStringLiteral("truncated")] =
        result.value(QStringLiteral("truncated")).toBool() || truncated;

    QString summary = result.value(QStringLiteral("lastMessage")).toString();
    if (truncated) {
        summary += QStringLiteral(" | Folder scan limit reached at %1 files").arg(kMaxImportCandidatesPerBatch);
    }
    if (tooLongPathCount > 0) {
        summary += QStringLiteral(" | %1 overlong paths skipped").arg(tooLongPathCount);
    }
    if (invalidPathCount > 0) {
        summary += QStringLiteral(" | %1 invalid paths skipped").arg(invalidPathCount);
    }
    result[QStringLiteral("lastMessage")] = summary;

    m_importStatus = result;
    return result;
}

QVariantMap FileListModel::importStatus() const
{
    return m_importStatus;
}

QVariantList FileListModel::recentRetrievalQueries(int limit) const
{
    QVariantList result;
    const int capped = std::max(1, limit);

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral(R"(
        SELECT query_text,
               MAX(created_at) AS last_seen,
               COUNT(*) AS hit_count
        FROM retrieval_events
        WHERE event_type = 'query'
          AND IFNULL(TRIM(query_text), '') <> ''
          AND LENGTH(TRIM(query_text)) >= 3
        GROUP BY LOWER(TRIM(query_text))
        ORDER BY MAX(created_at) DESC
        LIMIT ?
    )"));
    query.addBindValue(capped);

    if (!query.exec()) {
        qWarning() << "Failed to load recent retrieval queries:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        QVariantMap item;
        item[QStringLiteral("queryText")] = query.value(0).toString();
        item[QStringLiteral("lastSeen")] = query.value(1).toString();
        item[QStringLiteral("hitCount")] = query.value(2).toInt();
        result.append(item);
    }

    return result;
}

QVariantList FileListModel::recentOpenedSources(int limit) const
{
    QVariantList result;
    const int capped = std::max(1, limit);

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral(R"(
        SELECT re.file_id,
               f.name,
               f.path,
               f.extension,
               MAX(re.created_at) AS last_opened
        FROM retrieval_events re
        LEFT JOIN files f ON f.id = re.file_id
        WHERE re.event_type IN ('opened_source', 'result_opened')
          AND re.file_id IS NOT NULL
        GROUP BY re.file_id
        ORDER BY MAX(re.created_at) DESC
        LIMIT ?
    )"));
    query.addBindValue(capped);

    if (!query.exec()) {
        qWarning() << "Failed to load recent opened sources:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        QVariantMap item;
        item[QStringLiteral("fileId")] = query.value(0).toInt();
        item[QStringLiteral("name")] = query.value(1).toString();
        item[QStringLiteral("path")] = query.value(2).toString();
        item[QStringLiteral("extension")] = query.value(3).toString();
        item[QStringLiteral("lastOpened")] = query.value(4).toString();
        result.append(item);
    }

    return result;
}

bool FileListModel::trackRetrievalEvent(const QString& eventType,
                                        const QString& queryText,
                                        int fileId,
                                        int latencyMs,
                                        const QString& usefulState,
                                        const QString& metadataJson)
{
    return trackRetrievalEventInternal(
        eventType,
        queryText,
        fileId,
        latencyMs,
        usefulState,
        metadataJson);
}

QVariantMap FileListModel::get(int index) const
{
    if (index < 0 || index >= m_files.size()) {
        return {};
    }

    const FileRecord& file = m_files.at(index);

    QVariantMap map;
    map["id"] = file.id;
    map["name"] = file.name;
    map["path"] = file.path;
    map["extension"] = file.extension;
    map["mimeType"] = file.mimeType;
    map["sizeBytes"] = file.sizeBytes;
    map["indexedAt"] = file.indexedAt.toString(Qt::ISODate);
    map["createdAt"] = file.createdAt.isValid() ? file.createdAt.toString(Qt::ISODate) : "";
    map["modifiedAt"] = file.modifiedAt.isValid() ? file.modifiedAt.toString(Qt::ISODate) : "";
    map["statusValue"] = static_cast<int>(file.status);
    map["source"] = file.source;
    map["documentType"] = file.documentType;
    map["searchSnippet"] = file.searchSnippet;
    map["searchMatchReason"] = file.searchMatchReason;
    map["searchScore"] = file.searchScore;

    return map;
}

QVariantMap FileListModel::getDetails(int index) const
{
    if (index < 0 || index >= m_files.size()) {
        return {};
    }

    QVariantMap details = m_repository.getFileDetails(m_files.at(index).id);
    details[QStringLiteral("searchSnippet")] = m_files.at(index).searchSnippet;
    details[QStringLiteral("searchMatchReason")] = m_files.at(index).searchMatchReason;
    details[QStringLiteral("searchScore")] = m_files.at(index).searchScore;
    return details;
}



QVariantMap FileListModel::getDetailsById(int fileId) const
{
    if (fileId < 0) {
        return {};
    }

    QVariantMap details = m_repository.getFileDetails(fileId);
    for (const FileRecord& file : m_files) {
        if (file.id == fileId) {
            details[QStringLiteral("searchSnippet")] = file.searchSnippet;
            details[QStringLiteral("searchMatchReason")] = file.searchMatchReason;
            details[QStringLiteral("searchScore")] = file.searchScore;
            break;
        }
    }
    return details;
}

int FileListModel::indexOfFileId(int fileId) const
{
    for (int i = 0; i < m_files.size(); ++i) {
        if (m_files.at(i).id == fileId) {
            return i;
        }
    }

    return -1;
}

bool FileListModel::updateFileMetadata(int fileIndex,
                                       const QString& technicalDomain,
                                       const QString& subject,
                                       const QString& subtopic,
                                       const QString& location,
                                       const QString& source,
                                       const QString& author,
                                       const QString& documentType,
                                       const QString& remarks)
{
    if (fileIndex < 0 || fileIndex >= m_files.size()) {
        return false;
    }

    const int fileId = m_files.at(fileIndex).id;
    const bool success = m_repository.updateFileMetadata(
        fileId,
        technicalDomain,
        subject,
        subtopic,
        location,
        source,
        author,
        documentType,
        remarks
        );

    refreshFiles();
    if (success) {
        notifyFileChanged(fileId, false, true);
    }
    return success;
}

QVariantMap FileListModel::runIntegrityScan()
{
    const QVariantMap result = m_repository.runIntegrityScan();
    refreshFiles();
    if (m_indexingService) {
        m_indexingService->scheduleIncremental(allFilesForBackgroundJobs());
        emit indexStatusChanged();
    }
    notifyCatalogChanged();
    return result;
}

bool FileListModel::relinkFile(int fileIndex, const QString& newFilePath)
{
    if (fileIndex < 0 || fileIndex >= m_files.size()) {
        return false;
    }

    const int fileId = m_files.at(fileIndex).id;
    const bool success = m_repository.relinkFile(fileId, newFilePath);
    refreshFiles();
    if (success) {
        notifyFileChanged(fileId, true, true);
    }
    return success;
}

bool FileListModel::removeFile(int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= m_files.size()) {
        return false;
    }

    const int fileId = m_files.at(fileIndex).id;
    const bool success = m_repository.removeFile(fileId);
    refreshFiles();
    if (success) {
        notifyCatalogChanged();
        emit indexStatusChanged();
    }
    return success;
}

bool FileListModel::addCollection(const QString& name, int parentCollectionId)
{
    const bool success = m_repository.addCollection(name, parentCollectionId);
    if (success) {
        notifyCatalogChanged();
    }
    return success;
}

bool FileListModel::renameCollection(int collectionId, const QString& newName)
{
    const bool success = m_repository.renameCollection(collectionId, newName);
    if (success) {
        notifyCatalogChanged();
    }
    return success;
}

bool FileListModel::deleteCollection(int collectionId)
{
    const bool success = m_repository.deleteCollection(collectionId);
    refreshFiles();
    if (success) {
        notifyCatalogChanged();
    }
    return success;
}

QVariantList FileListModel::getCollectionTree() const
{
    return m_repository.getCollectionTreeFlat();
}

QVariantList FileListModel::getCollectionPickerOptions() const
{
    return m_repository.getCollectionPickerOptions();
}

bool FileListModel::assignCollection(int fileIndex, int collectionId)
{
    if (fileIndex < 0 || fileIndex >= m_files.size() || collectionId < 0) {
        return false;
    }

    const bool success = m_repository.assignFileToCollection(m_files.at(fileIndex).id, collectionId);
    if (success) {
        notifyCatalogChanged();
    }
    return success;
}

bool FileListModel::removeFileFromCollection(int fileIndex, int collectionId)
{
    if (fileIndex < 0 || fileIndex >= m_files.size() || collectionId < 0) {
        return false;
    }

    const bool success = m_repository.removeFileFromCollection(m_files.at(fileIndex).id, collectionId);
    refreshFiles();
    if (success) {
        notifyCatalogChanged();
    }
    return success;
}

bool FileListModel::addCollectionRule(int collectionId,
                                      const QString& fieldName,
                                      const QString& operatorType,
                                      const QString& value)
{
    const bool success = m_repository.addCollectionRule(collectionId, fieldName, operatorType, value);
    if (success) {
        notifyCatalogChanged();
    }
    return success;
}

bool FileListModel::deleteCollectionRule(int ruleId)
{
    const bool success = m_repository.deleteCollectionRule(ruleId);
    if (success) {
        notifyCatalogChanged();
    }
    return success;
}

QVariantList FileListModel::getCollectionRules(int collectionId) const
{
    return m_repository.getCollectionRules(collectionId);
}

QVariantList FileListModel::getHierarchyTree() const
{
    return m_repository.getHierarchyTreeFlat();
}

void FileListModel::filterByCollection(int collectionId)
{
    m_currentCollectionId = collectionId;
    m_currentTechnicalDomain.clear();
    m_currentSubject.clear();
    m_currentSubtopic.clear();
    refreshFiles();
}

void FileListModel::clearCollectionFilter()
{
    m_currentCollectionId = -1;
    refreshFiles();
}

int FileListModel::currentCollectionId() const
{
    return m_currentCollectionId;
}

void FileListModel::filterByHierarchy(const QString& technicalDomain,
                                      const QString& subject,
                                      const QString& subtopic)
{
    m_currentCollectionId = -1;
    m_currentTechnicalDomain = technicalDomain;
    m_currentSubject = subject;
    m_currentSubtopic = subtopic;
    refreshFiles();
}

void FileListModel::clearHierarchyFilter()
{
    m_currentTechnicalDomain.clear();
    m_currentSubject.clear();
    m_currentSubtopic.clear();
    refreshFiles();
}

bool FileListModel::openFile(int fileIndex) const
{
    if (fileIndex < 0 || fileIndex >= m_files.size()) {
        return false;
    }

    const QFileInfo info(m_files.at(fileIndex).path);
    if (!info.exists() || !info.isFile()) {
        return false;
    }

    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
    if (opened) {
        const FileRecord& file = m_files.at(fileIndex);
        trackRetrievalEventInternal(
            QStringLiteral("opened_source"),
            m_searchText.trimmed(),
            file.id,
            -1,
            QString(),
            QStringLiteral("{\"surface\":\"open_file\"}"));
    }

    return opened;
}

bool FileListModel::openContainingFolder(int fileIndex) const
{
    if (fileIndex < 0 || fileIndex >= m_files.size()) {
        return false;
    }

    const QFileInfo info(m_files.at(fileIndex).path);
    if (!info.exists()) {
        return false;
    }

    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
    if (opened) {
        const FileRecord& file = m_files.at(fileIndex);
        trackRetrievalEventInternal(
            QStringLiteral("opened_source"),
            m_searchText.trimmed(),
            file.id,
            -1,
            QString(),
            QStringLiteral("{\"surface\":\"open_folder\"}"));
    }

    return opened;
}

QString FileListModel::readTextFile(int fileIndex) const
{
    if (fileIndex < 0 || fileIndex >= m_files.size()) {
        return QStringLiteral("No file selected.");
    }

    const QFileInfo info(m_files.at(fileIndex).path);
    if (!info.exists() || !info.isFile()) {
        return QStringLiteral("The selected file does not exist.");
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("Unable to open file for reading.");
    }

    constexpr qint64 maxPreviewBytes = 1024 * 1024 * 2;
    QByteArray data = file.read(maxPreviewBytes + 1);
    const bool truncated = data.size() > maxPreviewBytes;
    if (truncated) {
        data.chop(1);
    }

    QString text = QString::fromUtf8(data);
    if (text.isEmpty() && !data.isEmpty()) {
        text = QString::fromLocal8Bit(data);
    }

    if (truncated) {
        text += QStringLiteral("\n\n[Preview truncated to 2 MB]");
    }

    return text;
}

QString FileListModel::fileUrl(int fileIndex) const
{
    if (fileIndex < 0 || fileIndex >= m_files.size()) {
        return QString();
    }

    const QFileInfo info(m_files.at(fileIndex).path);
    if (!info.exists()) {
        return QString();
    }

    return QUrl::fromLocalFile(info.absoluteFilePath()).toString();
}

QString FileListModel::presentationPdfUrl(int fileIndex) const
{
    return presentationPdfPreview(fileIndex).value(QStringLiteral("url")).toString();
}

QVariantMap FileListModel::presentationPdfPreview(int fileIndex) const
{
    QVariantMap result;
    result[QStringLiteral("url")] = QString();
    result[QStringLiteral("error")] = QString();

    if (fileIndex < 0 || fileIndex >= m_files.size()) {
        result[QStringLiteral("error")] = QStringLiteral("Invalid file selection");
        return result;
    }

    const QFileInfo info(m_files.at(fileIndex).path);
    if (!info.exists() || !info.isFile()) {
        result[QStringLiteral("error")] = QStringLiteral("Presentation file is missing");
        return result;
    }

    ContentExtractor extractor;
    QString error;
    const QString convertedPdfPath = extractor.ensurePresentationPdf(info.absoluteFilePath(), &error);
    if (convertedPdfPath.trimmed().isEmpty()) {
        qWarning() << "Presentation preview conversion failed:" << error;
        result[QStringLiteral("error")] = error.trimmed().isEmpty()
                                              ? QStringLiteral("Presentation conversion to PDF failed")
                                              : error.trimmed();
        return result;
    }

    result[QStringLiteral("url")] = QUrl::fromLocalFile(convertedPdfPath).toString();
    return result;
}



bool FileListModel::isEllaNote(int fileIndex) const
{
    if (fileIndex < 0 || fileIndex >= m_files.size()) {
        return false;
    }

    const FileRecord& file = m_files.at(fileIndex);
    return file.documentType.compare(QStringLiteral("ella note"), Qt::CaseInsensitive) == 0
           || file.extension.compare(QStringLiteral("ellanote"), Qt::CaseInsensitive) == 0;
}

bool FileListModel::isEllaNoteFileId(int fileId) const
{
    if (fileId < 0) {
        return false;
    }

    const QVariantMap details = m_repository.getFileDetails(fileId);
    return details.value("documentType").toString().compare(QStringLiteral("ella note"), Qt::CaseInsensitive) == 0
           || details.value("extension").toString().compare(QStringLiteral("ellanote"), Qt::CaseInsensitive) == 0;
}

QVariantList FileListModel::searchReferenceTargets(const QString& queryText, int limit) const
{
    return m_repository.searchReferenceTargets(queryText, limit);
}

QVariantList FileListModel::getDocumentNotes(int fileId) const
{
    return m_annotationRepository.getDocumentNotes(fileId);
}

bool FileListModel::addDocumentNote(int fileId, const QString& title, const QString& body)
{
    const bool success = m_annotationRepository.addDocumentNote(fileId, title, body);
    if (success) {
        notifyCatalogChanged();
    }
    return success;
}

bool FileListModel::updateDocumentNote(int noteId, const QString& title, const QString& body)
{
    const bool success = m_annotationRepository.updateDocumentNote(noteId, title, body);
    if (success) {
        notifyCatalogChanged();
    }
    return success;
}

bool FileListModel::deleteDocumentNote(int noteId)
{
    const bool success = m_annotationRepository.deleteDocumentNote(noteId);
    if (success) {
        notifyCatalogChanged();
    }
    return success;
}

QVariantList FileListModel::getAnnotations(int fileId, const QString& targetType) const
{
    return m_annotationRepository.getAnnotations(fileId, targetType);
}

bool FileListModel::addAnnotation(int fileId,
                                  const QString& targetType,
                                  const QString& annotationType,
                                  int pageNumber,
                                  int charStart,
                                  int charEnd,
                                  const QString& anchorText,
                                  double x,
                                  double y,
                                  double width,
                                  double height,
                                  const QString& color,
                                  const QString& content,
                                  qint64 timeStartMs,
                                  qint64 timeEndMs)
{
    const bool success = m_annotationRepository.addAnnotation(
        fileId,
        targetType,
        annotationType,
        pageNumber,
        charStart,
        charEnd,
        anchorText,
        x,
        y,
        width,
        height,
        color,
        content,
        timeStartMs,
        timeEndMs
        );
    if (success) {
        notifyCatalogChanged();
    }
    return success;
}

bool FileListModel::updateAnnotation(int annotationId, const QString& color, const QString& content)
{
    const bool success = m_annotationRepository.updateAnnotation(annotationId, color, content);
    if (success) {
        notifyCatalogChanged();
    }
    return success;
}

bool FileListModel::deleteAnnotation(int annotationId)
{
    const bool success = m_annotationRepository.deleteAnnotation(annotationId);
    if (success) {
        notifyCatalogChanged();
    }
    return success;
}

QVariantMap FileListModel::indexStatus() const
{
    if (m_indexingService) {
        return m_indexingService->status();
    }

    QVariantMap statusMap;
    statusMap[QStringLiteral("running")] = false;
    statusMap[QStringLiteral("queued")] = 0;
    statusMap[QStringLiteral("total")] = 0;
    statusMap[QStringLiteral("processed")] = 0;
    statusMap[QStringLiteral("failed")] = 0;
    statusMap[QStringLiteral("skipped")] = 0;
    statusMap[QStringLiteral("lastError")] = QString();
    statusMap[QStringLiteral("lastIndexedAt")] = QString();
    statusMap[QStringLiteral("indexedCount")] = 0;
    return statusMap;
}

QVariantMap FileListModel::searchHealth() const
{
    QVariantMap health = indexStatus();

    auto countQuery = [](const QString& sql) -> int {
        QSqlQuery query(DatabaseManager::instance().database());
        if (!query.exec(sql) || !query.next()) {
            return 0;
        }
        return query.value(0).toInt();
    };

    health[QStringLiteral("ftsDocuments")] =
        countQuery(QStringLiteral("SELECT COUNT(*) FROM file_content_fts"));
    health[QStringLiteral("pdfIndexed")] =
        countQuery(QStringLiteral("SELECT COUNT(*) FROM file_index_state WHERE status='indexed' AND extractor='pdf'"));
    health[QStringLiteral("pdfOcrIndexed")] =
        countQuery(QStringLiteral("SELECT COUNT(*) FROM file_index_state WHERE status='indexed' AND extractor='pdf-ocr'"));
    health[QStringLiteral("imageOcrIndexed")] =
        countQuery(QStringLiteral("SELECT COUNT(*) FROM file_index_state WHERE status='indexed' AND extractor='image-ocr'"));
    health[QStringLiteral("videoWhisperIndexed")] =
        countQuery(QStringLiteral("SELECT COUNT(*) FROM file_index_state WHERE status='indexed' AND extractor='video-whisper'"));
    health[QStringLiteral("audioWhisperIndexed")] =
        countQuery(QStringLiteral("SELECT COUNT(*) FROM file_index_state WHERE status='indexed' AND extractor='audio-whisper'"));
    health[QStringLiteral("pptPdfIndexed")] =
        countQuery(QStringLiteral("SELECT COUNT(*) FROM file_index_state WHERE status='indexed' AND extractor='ppt-pdf'"));
    health[QStringLiteral("pptPdfOcrIndexed")] =
        countQuery(QStringLiteral("SELECT COUNT(*) FROM file_index_state WHERE status='indexed' AND extractor='ppt-pdf-ocr'"));
    health[QStringLiteral("indexErrors")] =
        countQuery(QStringLiteral("SELECT COUNT(*) FROM file_index_state WHERE status='error'"));

    QSqlQuery lastErrorQuery(DatabaseManager::instance().database());
    lastErrorQuery.prepare(QStringLiteral(R"(
        SELECT last_error
        FROM file_index_state
        WHERE status = 'error' AND IFNULL(last_error, '') <> ''
        ORDER BY indexed_at DESC
        LIMIT 1
    )"));
    if (lastErrorQuery.exec() && lastErrorQuery.next()) {
        health[QStringLiteral("lastIndexError")] = lastErrorQuery.value(0).toString();
    } else {
        health[QStringLiteral("lastIndexError")] = QString();
    }

    const QVariantMap toolingInfo = ContentExtractor::runtimeEnvironmentStatus();
    for (auto it = toolingInfo.constBegin(); it != toolingInfo.constEnd(); ++it) {
        health[it.key()] = it.value();
    }

    return health;
}

void FileListModel::rebuildSearchIndex()
{
    if (!m_indexingService) {
        return;
    }

    m_indexingService->rebuildIndex(allFilesForBackgroundJobs());
    emit indexStatusChanged();
}

QVariantMap FileListModel::exportDiagnosticsBundle() const
{
    QVariantMap result;
    result[QStringLiteral("ok")] = false;
    result[QStringLiteral("path")] = QString();
    result[QStringLiteral("error")] = QString();
    result[QStringLiteral("bundleVersion")] = QStringLiteral("1.0");

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString bundleDir = QDir(AppConfig::diagnosticsDirectory())
                                  .filePath(QStringLiteral("ella-diagnostics-%1").arg(timestamp));

    QDir dir;
    if (!dir.mkpath(bundleDir)) {
        result[QStringLiteral("error")] = QStringLiteral("Unable to create diagnostics directory");
        return result;
    }

    const QString logsDir = QDir(bundleDir).filePath(QStringLiteral("logs"));
    dir.mkpath(logsDir);

    const QDir sourceLogsDir(AppConfig::logsDirectory());
    const QStringList logFiles = sourceLogsDir.entryList(QStringList() << QStringLiteral("ella.log*"),
                                                         QDir::Files,
                                                         QDir::Name);
    for (const QString& fileName : logFiles) {
        const QString sourcePath = sourceLogsDir.filePath(fileName);
        const QString destinationPath = QDir(logsDir).filePath(fileName);
        copyFileIfExists(sourcePath, destinationPath);
    }

    QVariantMap diagnostics;
    diagnostics[QStringLiteral("bundleVersion")] = QStringLiteral("1.0");
    diagnostics[QStringLiteral("generatedAt")] = nowIso();
    diagnostics[QStringLiteral("release")] = releaseMetadata();
    diagnostics[QStringLiteral("indexStatus")] = indexStatus();
    diagnostics[QStringLiteral("searchHealth")] = searchHealth();
    diagnostics[QStringLiteral("importStatus")] = importStatus();
    diagnostics[QStringLiteral("appDataDirectory")] = AppConfig::appDataDirectory();
    diagnostics[QStringLiteral("databasePath")] = AppConfig::databasePath();
    diagnostics[QStringLiteral("logsDirectory")] = AppConfig::logsDirectory();
    diagnostics[QStringLiteral("cloudExperimental")] = cloudSyncExperimental();
    diagnostics[QStringLiteral("cloudStatus")] = m_cloudSyncService ? m_cloudSyncService->status() : QVariantMap();

    auto countQuery = [](const QString& sql) -> int {
        QSqlQuery query(DatabaseManager::instance().database());
        if (!query.exec(sql) || !query.next()) {
            return 0;
        }
        return query.value(0).toInt();
    };

    QVariantMap dbSummary;
    dbSummary[QStringLiteral("files")] = countQuery(QStringLiteral("SELECT COUNT(*) FROM files"));
    dbSummary[QStringLiteral("indexRows")] = countQuery(QStringLiteral("SELECT COUNT(*) FROM file_index_state"));
    dbSummary[QStringLiteral("annotations")] = countQuery(QStringLiteral("SELECT COUNT(*) FROM annotations"));
    dbSummary[QStringLiteral("documentNotes")] = countQuery(QStringLiteral("SELECT COUNT(*) FROM document_notes"));
    dbSummary[QStringLiteral("retrievalEvents")] = countQuery(QStringLiteral("SELECT COUNT(*) FROM retrieval_events"));
    diagnostics[QStringLiteral("databaseSummary")] = dbSummary;

    diagnostics[QStringLiteral("recentIndexErrors")] = queryRows(QStringLiteral(R"(
        SELECT file_id, extractor, last_error, indexed_at
        FROM file_index_state
        WHERE status = 'error' AND IFNULL(TRIM(last_error), '') <> ''
        ORDER BY indexed_at DESC
        LIMIT 50
    )"));

    diagnostics[QStringLiteral("recentSyncErrors")] = queryRows(QStringLiteral(R"(
        SELECT provider, job_type, last_error, updated_at
        FROM sync_jobs
        WHERE status = 'failed' AND IFNULL(TRIM(last_error), '') <> ''
        ORDER BY updated_at DESC
        LIMIT 50
    )"));

    diagnostics[QStringLiteral("recentOperationTimeline")] = queryRows(QStringLiteral(R"(
        SELECT event_type, query_text, file_id, latency_ms, useful_state, created_at
        FROM retrieval_events
        ORDER BY created_at DESC
        LIMIT 200
    )"));

    const QString diagnosticsJsonPath = QDir(bundleDir).filePath(QStringLiteral("diagnostics.json"));
    QSaveFile diagnosticsFile(diagnosticsJsonPath);
    if (!diagnosticsFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result[QStringLiteral("error")] = QStringLiteral("Unable to open diagnostics.json for writing");
        result[QStringLiteral("path")] = bundleDir;
        return result;
    }

    diagnosticsFile.write(QJsonDocument::fromVariant(diagnostics).toJson(QJsonDocument::Indented));
    if (!diagnosticsFile.commit()) {
        result[QStringLiteral("error")] = QStringLiteral("Unable to write diagnostics.json");
        result[QStringLiteral("path")] = bundleDir;
        return result;
    }

    const QString zipPath = QDir(AppConfig::diagnosticsDirectory())
                                .filePath(QStringLiteral("ella-diagnostics-%1.zip").arg(timestamp));
    QFile::remove(zipPath);

    QString zipError;
    if (zipDirectoryWithPowerShell(bundleDir, zipPath, &zipError)) {
        QDir(bundleDir).removeRecursively();
        result[QStringLiteral("ok")] = true;
        result[QStringLiteral("path")] = zipPath;
        return result;
    }

    result[QStringLiteral("error")] = zipError.isEmpty()
                                          ? QStringLiteral("Diagnostics created but zip export failed")
                                          : zipError;
    result[QStringLiteral("path")] = bundleDir;
    return result;
}

QVariantMap FileListModel::releaseMetadata() const
{
    return AppConfig::releaseMetadata();
}

bool FileListModel::cloudSyncExperimental() const
{
    return true;
}

bool FileListModel::shouldShowBetaScopeNotice() const
{
    QSettings settings;
    return !settings.value(QStringLiteral("betaScopeNoticeSeen"), false).toBool();
}

void FileListModel::markBetaScopeNoticeSeen()
{
    QSettings settings;
    settings.setValue(QStringLiteral("betaScopeNoticeSeen"), true);
}

QList<FileRecord> FileListModel::allFilesForBackgroundJobs() const
{
    return m_repository.queryFiles(
        QString(),
        -1,
        QString(),
        QString(),
        QString(),
        QStringList(),
        QStringList(),
        QStringList(),
        QString(),
        QString(),
        QString(),
        QStringLiteral("indexedAt"),
        false
        );
}

bool FileListModel::findFileRecordById(int fileId, FileRecord* outFile) const
{
    if (fileId < 0 || !outFile) {
        return false;
    }

    const QList<FileRecord> files = allFilesForBackgroundJobs();
    for (const FileRecord& file : files) {
        if (file.id == fileId) {
            *outFile = file;
            return true;
        }
    }

    return false;
}

void FileListModel::notifyFileChanged(int fileId, bool forceReindex, bool syncCatalog)
{
    if (fileId < 0) {
        return;
    }

    if (m_indexingService) {
        FileRecord file;
        if (findFileRecordById(fileId, &file)) {
            m_indexingService->reindexFile(file, forceReindex);
            emit indexStatusChanged();
        }
    }

    if (m_cloudSyncService) {
        m_cloudSyncService->enqueueFileUpload(
            fileId,
            forceReindex ? QStringLiteral("file_changed_force") : QStringLiteral("file_changed"));
        if (syncCatalog) {
            m_cloudSyncService->enqueueCatalogSync(QStringLiteral("file_changed"));
        }
    }
}

void FileListModel::notifyCatalogChanged()
{
    if (m_cloudSyncService) {
        m_cloudSyncService->enqueueCatalogSync(QStringLiteral("catalog_changed"));
    }
}

bool FileListModel::trackRetrievalEventInternal(const QString& eventType,
                                                const QString& queryText,
                                                int fileId,
                                                int latencyMs,
                                                const QString& usefulState,
                                                const QString& metadataJson) const
{
    const QString normalizedType = eventType.trimmed().toLower();
    if (normalizedType.isEmpty()) {
        return false;
    }

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral(R"(
        INSERT INTO retrieval_events(
            event_type,
            query_text,
            file_id,
            latency_ms,
            useful_state,
            metadata_json,
            created_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?)
    )"));
    query.addBindValue(normalizedType);
    query.addBindValue(queryText.trimmed());
    if (fileId >= 0) {
        query.addBindValue(fileId);
    } else {
        query.addBindValue(QVariant());
    }
    if (latencyMs >= 0) {
        query.addBindValue(latencyMs);
    } else {
        query.addBindValue(QVariant());
    }
    query.addBindValue(usefulState.trimmed().toLower());
    query.addBindValue(metadataJson.trimmed());
    query.addBindValue(nowIso());

    if (!query.exec()) {
        qWarning() << "Failed to insert retrieval event:" << query.lastError().text();
        return false;
    }

    return true;
}

QVariantMap FileListModel::defaultMetadataForImport(const QString& absoluteFilePath) const
{
    QVariantMap map;
    const QFileInfo info(absoluteFilePath);
    const QString folderName = info.dir().dirName().trimmed();
    const QString sourcePath = info.absolutePath();

    map[QStringLiteral("technicalDomain")] = QStringLiteral("Personal Archive");
    map[QStringLiteral("subject")] =
        folderName.isEmpty() ? QStringLiteral("General") : folderName;
    map[QStringLiteral("subtopic")] = folderName.isEmpty() ? QStringLiteral("Imported") : folderName;
    map[QStringLiteral("location")] = sourcePath;
    map[QStringLiteral("source")] = sourcePath;
    map[QStringLiteral("author")] = QStringLiteral("Unknown");
    map[QStringLiteral("documentType")] = info.suffix().trimmed().isEmpty()
                                              ? QStringLiteral("file")
                                              : QStringLiteral("%1 file").arg(info.suffix().toLower());
    return map;
}

QString FileListModel::buildAnnotatedTextHtml(int fileId, const QString& plainText) const
{
    const QVariantList rawAnnotations = m_annotationRepository.getTextAnnotations(fileId);

    struct TextAnnotation
    {
        int start = -1;
        int end = -1;
        QString color;
        QString content;
    };

    QList<TextAnnotation> annotations;
    annotations.reserve(rawAnnotations.size());

    for (const QVariant& value : rawAnnotations) {
        const QVariantMap item = value.toMap();
        const int start = item.value("charStart").toInt();
        const int end = item.value("charEnd").toInt();

        if (start < 0 || end <= start || start >= plainText.size()) {
            continue;
        }

        TextAnnotation annotation;
        annotation.start = start;
        annotation.end = std::min(end, static_cast<int>(plainText.size()));
        annotation.color = defaultAnnotationColor(item.value("color").toString());
        annotation.content = item.value("content").toString();
        annotations.append(annotation);
    }

    std::sort(annotations.begin(), annotations.end(), [](const TextAnnotation& a, const TextAnnotation& b) {
        if (a.start != b.start) {
            return a.start < b.start;
        }
        return a.end > b.end;
    });

    QString html;
    html.reserve(plainText.size() * 2 + 128);
    int cursor = 0;

    for (const TextAnnotation& annotation : annotations) {
        const int start = std::max(annotation.start, cursor);
        const int end = std::max(start, annotation.end);

        if (start > plainText.size()) {
            break;
        }

        if (start > cursor) {
            html += plainText.mid(cursor, start - cursor).toHtmlEscaped();
        }

        if (end > start) {
            QString title = annotation.content.toHtmlEscaped();
            title.replace('"', "&quot;");
            html += QStringLiteral("<span style=\"background-color:%1;\" title=\"%2\">%3</span>")
                        .arg(annotation.color,
                             title,
                             plainText.mid(start, end - start).toHtmlEscaped());
        }

        cursor = std::max(cursor, end);
    }

    if (cursor < plainText.size()) {
        html += plainText.mid(cursor).toHtmlEscaped();
    }

    html.replace("\n", "<br>");
    return html;
}
