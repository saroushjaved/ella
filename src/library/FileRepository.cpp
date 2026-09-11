#include "FileRepository.h"
#include "database/DatabaseManager.h"

#include <QDate>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>
#include <functional>

namespace
{
QDateTime selectedDateTimeForField(const FileRecord& file, const QString& dateField)
{
    if (dateField == "createdAt") {
        return file.createdAt;
    }

    if (dateField == "modifiedAt") {
        return file.modifiedAt;
    }

    return file.indexedAt;
}
}

bool FileRepository::addFile(const QString& filePath,
                             const QString& technicalDomain,
                             const QString& subject,
                             const QString& subtopic,
                             const QString& location,
                             const QString& source,
                             const QString& author,
                             const QString& documentType,
                             const QString& remarks)
{
    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        qWarning() << "Invalid file path:" << filePath;
        return false;
    }

    QMimeDatabase mimeDb;
    QSqlQuery query(database());

    query.prepare(R"(
        INSERT INTO files (
            id,
            path,
            name,
            extension,
            mime_type,
            size_bytes,
            created_at,
            modified_at,
            indexed_at,
            status,
            technical_domain,
            subject,
            subtopic,
            location,
            source,
            author,
            document_type,
            remarks
        )
        VALUES (
            (SELECT id FROM files WHERE path = :path_lookup),
            :path,
            :name,
            :extension,
            :mime,
            :size,
            :created,
            :modified,
            :indexed,
            0,
            :technical_domain,
            :subject,
            :subtopic,
            :location,
            :source,
            :author,
            :document_type,
            :remarks
        )
        ON CONFLICT(path) DO UPDATE SET
            name=excluded.name,extension=excluded.extension,mime_type=excluded.mime_type,
            size_bytes=excluded.size_bytes,created_at=excluded.created_at,modified_at=excluded.modified_at,
            status=0,technical_domain=excluded.technical_domain,subject=excluded.subject,
            subtopic=excluded.subtopic,location=excluded.location,source=excluded.source,
            author=excluded.author,document_type=excluded.document_type,remarks=excluded.remarks    )");

    query.bindValue(":path_lookup", info.absoluteFilePath());
    query.bindValue(":path", info.absoluteFilePath());
    query.bindValue(":name", info.fileName());
    query.bindValue(":extension", info.suffix());
    query.bindValue(":mime", mimeDb.mimeTypeForFile(info).name());
    query.bindValue(":size", info.size());
    query.bindValue(":created", info.birthTime().isValid() ? info.birthTime().toString(Qt::ISODate) : QString());
    query.bindValue(":modified", info.lastModified().isValid() ? info.lastModified().toString(Qt::ISODate) : QString());
    query.bindValue(":indexed", QDateTime::currentDateTime().toString(Qt::ISODate));
    query.bindValue(":technical_domain", technicalDomain.trimmed());
    query.bindValue(":subject", subject.trimmed());
    query.bindValue(":subtopic", subtopic.trimmed());
    query.bindValue(":location", location.trimmed());
    query.bindValue(":source", source.trimmed());
    query.bindValue(":author", author.trimmed());
    query.bindValue(":document_type", documentType.trimmed());
    query.bindValue(":remarks", remarks.trimmed());

    if (!query.exec()) {
        qWarning() << "Failed to insert/update file:" << query.lastError().text();
        return false;
    }

    return true;
}

bool FileRepository::updateFileMetadata(int fileId,
                                        const QString& technicalDomain,
                                        const QString& subject,
                                        const QString& subtopic,
                                        const QString& location,
                                        const QString& source,
                                        const QString& author,
                                        const QString& documentType,
                                        const QString& remarks)
{
    if (fileId < 0) {
        return false;
    }

    QSqlQuery query(database());
    query.prepare(R"(
        UPDATE files
        SET technical_domain = ?,
            subject = ?,
            subtopic = ?,
            location = ?,
            source = ?,
            author = ?,
            document_type = ?,
            remarks = ?
        WHERE id = ?
    )");

    query.addBindValue(technicalDomain.trimmed());
    query.addBindValue(subject.trimmed());
    query.addBindValue(subtopic.trimmed());
    query.addBindValue(location.trimmed());
    query.addBindValue(source.trimmed());
    query.addBindValue(author.trimmed());
    query.addBindValue(documentType.trimmed());
    query.addBindValue(remarks.trimmed());
    query.addBindValue(fileId);

    if (!query.exec()) {
        qWarning() << "Failed to update file metadata:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

QVariantMap FileRepository::runIntegrityScan()
{
    QVariantMap result;
    int activeCount = 0;
    int missingCount = 0;
    int updatedCount = 0;

    const QList<FileRecord> files = getAllFilesRaw();
    QMimeDatabase mimeDb;
    QSqlDatabase db = database();

    if (!db.transaction()) {
        qWarning() << "Failed to start transaction for integrity scan:" << db.lastError().text();
    }

    for (const FileRecord& file : files) {
        QFileInfo info(file.path);

        QSqlQuery q(db);

        if (!info.exists() || !info.isFile()) {
            q.prepare(R"(
                UPDATE files
                SET status = 1
                WHERE id = ?
            )");
            q.addBindValue(file.id);

            if (!q.exec()) {
                qWarning() << "Failed to mark file missing:" << q.lastError().text();
            } else {
                ++missingCount;
                ++updatedCount;
            }

            continue;
        }

        q.prepare(R"(
            UPDATE files
            SET name = ?,
                extension = ?,
                mime_type = ?,
                size_bytes = ?,
                created_at = ?,
                modified_at = ?,
                status = 0
            WHERE id = ?
        )");
        q.addBindValue(info.fileName());
        q.addBindValue(info.suffix());
        q.addBindValue(mimeDb.mimeTypeForFile(info).name());
        q.addBindValue(info.size());
        q.addBindValue(info.birthTime().isValid() ? info.birthTime().toString(Qt::ISODate) : QString());
        q.addBindValue(info.lastModified().isValid() ? info.lastModified().toString(Qt::ISODate) : QString());
        q.addBindValue(file.id);

        if (!q.exec()) {
            qWarning() << "Failed to refresh file info during scan:" << q.lastError().text();
        } else {
            ++activeCount;
            ++updatedCount;
        }
    }

    if (!db.commit()) {
        qWarning() << "Failed to commit integrity scan:" << db.lastError().text();
    }

    result["activeCount"] = activeCount;
    result["missingCount"] = missingCount;
    result["updatedCount"] = updatedCount;
    return result;
}

bool FileRepository::relinkFile(int fileId, const QString& newFilePath)
{
    if (fileId < 0) {
        return false;
    }

    QFileInfo info(newFilePath);
    if (!info.exists() || !info.isFile()) {
        qWarning() << "Invalid relink path:" << newFilePath;
        return false;
    }

    QSqlDatabase db = database();

    QSqlQuery duplicateCheck(db);
    duplicateCheck.prepare("SELECT id FROM files WHERE path = ? AND id <> ?");
    duplicateCheck.addBindValue(info.absoluteFilePath());
    duplicateCheck.addBindValue(fileId);

    if (!duplicateCheck.exec()) {
        qWarning() << "Failed to validate relink path:" << duplicateCheck.lastError().text();
        return false;
    }

    if (duplicateCheck.next()) {
        qWarning() << "Relink path already belongs to another file record";
        return false;
    }

    QMimeDatabase mimeDb;
    QSqlQuery query(db);
    query.prepare(R"(
        UPDATE files
        SET path = ?,
            name = ?,
            extension = ?,
            mime_type = ?,
            size_bytes = ?,
            created_at = ?,
            modified_at = ?,
            status = 0
        WHERE id = ?
    )");

    query.addBindValue(info.absoluteFilePath());
    query.addBindValue(info.fileName());
    query.addBindValue(info.suffix());
    query.addBindValue(mimeDb.mimeTypeForFile(info).name());
    query.addBindValue(info.size());
    query.addBindValue(info.birthTime().isValid() ? info.birthTime().toString(Qt::ISODate) : QString());
    query.addBindValue(info.lastModified().isValid() ? info.lastModified().toString(Qt::ISODate) : QString());
    query.addBindValue(fileId);

    if (!query.exec()) {
        qWarning() << "Failed to relink file:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool FileRepository::removeFile(int fileId)
{
    if (fileId < 0) {
        return false;
    }

    QSqlDatabase db = database();
    if (!db.transaction()) {
        qWarning() << "Failed to start remove file transaction:" << db.lastError().text();
        return false;
    }

    auto execDeleteByFileId = [&](const QString& sql, bool ignoreMissingTable) -> bool {
        QSqlQuery q(db);
        q.prepare(sql);
        q.addBindValue(fileId);
        if (!q.exec()) {
            const QString errorText = q.lastError().text();
            if (ignoreMissingTable && errorText.contains("no such table", Qt::CaseInsensitive)) {
                return true;
            }
            qWarning() << "Failed remove-file cleanup query:" << sql << errorText;
            return false;
        }
        return true;
    };

    const bool cleanupOk =
        execDeleteByFileId(QStringLiteral("DELETE FROM file_collections WHERE file_id = ?"), false) &&
        execDeleteByFileId(QStringLiteral("DELETE FROM document_notes WHERE file_id = ?"), false) &&
        execDeleteByFileId(QStringLiteral("DELETE FROM annotations WHERE file_id = ?"), false) &&
        execDeleteByFileId(QStringLiteral("DELETE FROM file_index_state WHERE file_id = ?"), false) &&
        execDeleteByFileId(QStringLiteral("DELETE FROM cloud_file_map WHERE file_id = ?"), false) &&
        execDeleteByFileId(QStringLiteral("DELETE FROM retrieval_events WHERE file_id = ?"), false) &&
        execDeleteByFileId(QStringLiteral("DELETE FROM file_content_fts WHERE file_id = ?"), true) &&
        execDeleteByFileId(QStringLiteral("DELETE FROM passage_fts WHERE file_id = ?"), true) &&
        execDeleteByFileId(QStringLiteral("DELETE FROM passages WHERE file_id = ?"), true) &&
        execDeleteByFileId(QStringLiteral("DELETE FROM favorites WHERE file_id = ?"), true) &&
        execDeleteByFileId(QStringLiteral("DELETE FROM file_tags WHERE file_id = ?"), true) &&
        execDeleteByFileId(QStringLiteral("DELETE FROM reading_positions WHERE file_id = ?"), true) &&
        execDeleteByFileId(QStringLiteral("DELETE FROM note_links WHERE ? IN (note_file_id, source_file_id)"), true);

    if (!cleanupOk) {
        db.rollback();
        return false;
    }

    QSqlQuery deleteFileQuery(db);
    deleteFileQuery.prepare(QStringLiteral("DELETE FROM files WHERE id = ?"));
    deleteFileQuery.addBindValue(fileId);
    if (!deleteFileQuery.exec()) {
        qWarning() << "Failed to remove file from files table:" << deleteFileQuery.lastError().text();
        db.rollback();
        return false;
    }

    if (deleteFileQuery.numRowsAffected() <= 0) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qWarning() << "Failed to commit remove file transaction:" << db.lastError().text();
        db.rollback();
        return false;
    }

    return true;
}

FileRecord FileRepository::fileFromQuery(const QSqlQuery& q) const
{
    FileRecord f;
    f.id = q.value("id").toInt();
    f.name = q.value("name").toString();
    f.path = q.value("path").toString();
    f.extension = q.value("extension").toString();
    f.mimeType = q.value("mime_type").toString();
    f.sizeBytes = q.value("size_bytes").toLongLong();
    f.createdAt = QDateTime::fromString(q.value("created_at").toString(), Qt::ISODate);
    f.modifiedAt = QDateTime::fromString(q.value("modified_at").toString(), Qt::ISODate);
    f.indexedAt = QDateTime::fromString(q.value("indexed_at").toString(), Qt::ISODate);
    f.status = static_cast<FileStatus>(q.value("status").toInt());

    f.technicalDomain = q.value("technical_domain").toString();
    f.subject = q.value("subject").toString();
    f.subtopic = q.value("subtopic").toString();
    f.location = q.value("location").toString();
    f.source = q.value("source").toString();
    f.author = q.value("author").toString();
    f.documentType = q.value("document_type").toString();
    f.remarks = q.value("remarks").toString();

    return f;
}

QList<FileRecord> FileRepository::getAllFilesRaw() const
{
    QList<FileRecord> list;
    QSqlQuery q(database());

    if (!q.exec("SELECT * FROM files WHERE removed_at IS NULL ORDER BY indexed_at DESC")) {
        qWarning() << "Failed to fetch files:" << q.lastError().text();
        return list;
    }

    while (q.next()) {
        list.append(fileFromQuery(q));
    }

    return list;
}

QList<QVariantMap> FileRepository::getAllCollectionsRaw() const
{
    QList<QVariantMap> list;
    QSqlQuery q(database());

    if (!q.exec("SELECT id, name, parent_collection_id FROM collections ORDER BY name COLLATE NOCASE ASC")) {
        qWarning() << "Failed to fetch collections:" << q.lastError().text();
        return list;
    }

    while (q.next()) {
        QVariantMap item;
        item["id"] = q.value("id").toInt();
        item["name"] = q.value("name").toString();
        item["parentCollectionId"] = q.value("parent_collection_id").isNull()
                                         ? -1
                                         : q.value("parent_collection_id").toInt();
        list.append(item);
    }

    return list;
}

QList<int> FileRepository::getCollectionSubtreeIds(int collectionId) const
{
    QList<int> ids;

    QSqlQuery q(database());
    q.prepare(R"(
        WITH RECURSIVE subtree(id) AS (
            SELECT id FROM collections WHERE id = ?
            UNION ALL
            SELECT c.id
            FROM collections c
            INNER JOIN subtree s ON c.parent_collection_id = s.id
        )
        SELECT id FROM subtree
    )");
    q.addBindValue(collectionId);

    if (!q.exec()) {
        qWarning() << "Failed to fetch collection subtree:" << q.lastError().text();
        return ids;
    }

    while (q.next()) {
        ids.append(q.value(0).toInt());
    }

    return ids;
}

bool FileRepository::fileMatchesSearch(const FileRecord& file, const QString& searchText) const
{
    const QString text = searchText.trimmed();
    if (text.isEmpty()) {
        return true;
    }

    const QStringList haystack = {
        file.name,
        file.path,
        file.extension,
        file.mimeType,
        file.technicalDomain,
        file.subject,
        file.subtopic,
        file.location,
        file.source,
        file.author,
        file.documentType,
        file.remarks
    };

    for (const QString& value : haystack) {
        if (value.contains(text, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

bool FileRepository::fileMatchesHierarchy(const FileRecord& file,
                                          const QString& technicalDomain,
                                          const QString& subject,
                                          const QString& subtopic) const
{
    if (!technicalDomain.trimmed().isEmpty() &&
        file.technicalDomain.compare(technicalDomain, Qt::CaseInsensitive) != 0) {
        return false;
    }

    if (!subject.trimmed().isEmpty() &&
        file.subject.compare(subject, Qt::CaseInsensitive) != 0) {
        return false;
    }

    if (!subtopic.trimmed().isEmpty() &&
        file.subtopic.compare(subtopic, Qt::CaseInsensitive) != 0) {
        return false;
    }

    return true;
}

bool FileRepository::fileMatchesAdvancedFilters(const FileRecord& file,
                                                const QStringList& statusFilters,
                                                const QStringList& extensionFilters,
                                                const QStringList& documentTypeFilters,
                                                const QString& dateField,
                                                const QString& dateFrom,
                                                const QString& dateTo) const
{
    if (!statusFilters.isEmpty()) {
        const bool isMissing = file.status == FileStatus::Missing;
        const bool isActive = file.status == FileStatus::Active;
        const bool isIndexed = isActive && file.indexedAt.isValid();
        const bool isInProgress = isActive && !file.indexedAt.isValid();

        bool statusMatch = false;
        for (const QString& rawToken : statusFilters) {
            const QString token = rawToken.trimmed().toLower();
            if (token == "failed" || token == "missing" || token == "error") {
                if (isMissing) {
                    statusMatch = true;
                    break;
                }
            } else if (token == "indexed") {
                if (isIndexed) {
                    statusMatch = true;
                    break;
                }
            } else if (token == "in_progress" || token == "inprogress" || token == "in progress") {
                if (isInProgress) {
                    statusMatch = true;
                    break;
                }
            } else if (token == "active") {
                if (isActive) {
                    statusMatch = true;
                    break;
                }
            }
        }

        if (!statusMatch) {
            return false;
        }
    }

    if (!extensionFilters.isEmpty()) {
        const QString fileExt = file.extension.trimmed().toLower();
        bool extensionMatch = false;
        for (QString requestedExt : extensionFilters) {
            requestedExt = requestedExt.trimmed().toLower();
            if (requestedExt.startsWith('.')) {
                requestedExt.remove(0, 1);
            }
            if (!requestedExt.isEmpty() && fileExt.compare(requestedExt, Qt::CaseInsensitive) == 0) {
                extensionMatch = true;
                break;
            }
        }

        if (!extensionMatch) {
            return false;
        }
    }

    if (!documentTypeFilters.isEmpty()) {
        bool documentTypeMatch = false;
        for (const QString& token : documentTypeFilters) {
            const QString normalized = token.trimmed();
            if (normalized.isEmpty()) {
                continue;
            }
            if (file.documentType.contains(normalized, Qt::CaseInsensitive)) {
                documentTypeMatch = true;
                break;
            }
        }
        if (!documentTypeMatch) {
            return false;
        }
    }

    const QString normalizedDateField = dateField.trimmed();
    const QString normalizedFrom = dateFrom.trimmed();
    const QString normalizedTo = dateTo.trimmed();

    if (!normalizedDateField.isEmpty() &&
        (!normalizedFrom.isEmpty() || !normalizedTo.isEmpty())) {
        const QDateTime actualDateTime = selectedDateTimeForField(file, normalizedDateField);
        if (!actualDateTime.isValid()) {
            return false;
        }

        const QDate actualDate = actualDateTime.date();

        if (!normalizedFrom.isEmpty()) {
            const QDate fromDate = QDate::fromString(normalizedFrom, "yyyy-MM-dd");
            if (fromDate.isValid() && actualDate < fromDate) {
                return false;
            }
        }

        if (!normalizedTo.isEmpty()) {
            const QDate toDate = QDate::fromString(normalizedTo, "yyyy-MM-dd");
            if (toDate.isValid() && actualDate > toDate) {
                return false;
            }
        }
    }

    return true;
}

QString FileRepository::fieldValue(const FileRecord& file, const QString& fieldName) const
{
    if (fieldName == "technical_domain") return file.technicalDomain;
    if (fieldName == "subject") return file.subject;
    if (fieldName == "subtopic") return file.subtopic;
    if (fieldName == "location") return file.location;
    if (fieldName == "source") return file.source;
    if (fieldName == "author") return file.author;
    if (fieldName == "document_type") return file.documentType;
    if (fieldName == "remarks") return file.remarks;
    if (fieldName == "name") return file.name;
    if (fieldName == "path") return file.path;
    return QString();
}

bool FileRepository::ruleMatchesFile(const FileRecord& file,
                                     const QString& fieldName,
                                     const QString& operatorType,
                                     const QString& value) const
{
    const QString actual = fieldValue(file, fieldName);
    const QString expected = value.trimmed();

    if (expected.isEmpty()) {
        return false;
    }

    if (operatorType == "exact") {
        return actual.compare(expected, Qt::CaseInsensitive) == 0;
    }

    if (operatorType == "contains") {
        return actual.contains(expected, Qt::CaseInsensitive);
    }

    return false;
}

QHash<int, QPair<double, QString>> FileRepository::queryContentMatches(const QString& searchText) const
{
    QHash<int, QPair<double, QString>> matches;

    const QString normalized = searchText.trimmed();
    if (normalized.isEmpty()) {
        return matches;
    }

    QString cleaned = normalized;
    cleaned.replace(QRegularExpression(QStringLiteral("[^\\w\\s]")), QStringLiteral(" "));
    const QStringList parts = cleaned.split(QRegularExpression(QStringLiteral("\\s+")),
                                            Qt::SkipEmptyParts);

    if (parts.isEmpty()) {
        return matches;
    }

    QStringList tokens;
    tokens.reserve(parts.size());
    for (const QString& part : parts) {
        QString token = part.trimmed().toLower();
        if (token.size() < 2) {
            continue;
        }
        token.remove('"');
        if (token.isEmpty()) {
            continue;
        }
        tokens.append(token + QStringLiteral("*"));
    }

    if (tokens.isEmpty()) {
        return matches;
    }

    const QString strictQuery = tokens.join(QStringLiteral(" AND "));
    const QString broadQuery = tokens.join(QStringLiteral(" OR "));

    auto executeFtsQuery = [&](const QString& ftsQuery, bool isBroadPass) {
        QSqlQuery query(database());
        query.prepare(QStringLiteral(R"(
            SELECT file_id,
                   snippet(file_content_fts, 1, '[', ']', ' ... ', 20) AS snippet_text,
                   bm25(file_content_fts) AS rank_value
            FROM file_content_fts
            WHERE file_content_fts MATCH ?
        )"));
        query.addBindValue(ftsQuery);

        if (!query.exec()) {
            qWarning() << "Failed to execute content search query:" << query.lastError().text();
            return;
        }

        while (query.next()) {
            const int fileId = query.value(0).toInt();
            const QString snippet = query.value(1).toString();
            double rankValue = query.value(2).toDouble();

            // Slightly de-prioritize broad OR-only matches.
            if (isBroadPass) {
                rankValue += 0.75;
            }

            if (!matches.contains(fileId) || rankValue < matches.value(fileId).first) {
                matches.insert(fileId, qMakePair(rankValue, snippet));
            }
        }
    };

    executeFtsQuery(strictQuery, false);
    if (tokens.size() > 1) {
        executeFtsQuery(broadQuery, true);
    }

    return matches;
}

QList<FileRecord> FileRepository::queryFiles(const QString& searchText,
    int collectionId, const QString& technicalDomain, const QString& subject,
    const QString& subtopic, const QStringList& statusFilters,
    const QStringList& extensionFilters, const QStringList& documentTypeFilters,
    const QString& dateField, const QString& dateFrom, const QString& dateTo,
    const QString& sortField, bool sortAscending, int limit, int offset,
    int* totalCount, bool favoritesOnly, const QString& folder, const QString& tag) const
{
    QList<FileRecord> result;
    QVariantMap parameters;
    auto bind = [&](const QVariant& value) {
        const QString key = QStringLiteral(":p%1").arg(parameters.size());
        parameters.insert(key, value); return key;
    };
    auto escapedLike = [](QString value) {
        value.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_");
        return value;
    };
    QStringList where{QStringLiteral("f.removed_at IS NULL")};
    auto equal = [&](const QString& column, const QString& value) {
        if (!value.trimmed().isEmpty()) where << column + " = " + bind(value.trimmed()) + " COLLATE NOCASE";
    };
    equal("f.technical_domain", technicalDomain); equal("f.subject", subject); equal("f.subtopic", subtopic);
    auto inList = [&](const QString& column, const QStringList& values) {
        if (values.isEmpty()) return;
        QStringList placeholders;
        for (const QString& value : values) placeholders << bind(value.toLower());
        where << "LOWER(" + column + ") IN (" + placeholders.join(',') + ")";
    };
    inList("f.status", statusFilters); inList("f.extension", extensionFilters); inList("f.document_type", documentTypeFilters);
    const QHash<QString, QString> dates{{"createdAt","created_at"},{"modifiedAt","modified_at"},{"indexedAt","indexed_at"}};
    const QString dateColumn = dates.value(dateField, "indexed_at");
    if (QDate::fromString(dateFrom, Qt::ISODate).isValid()) where << "date(f." + dateColumn + ") >= " + bind(dateFrom);
    if (QDate::fromString(dateTo, Qt::ISODate).isValid()) where << "date(f." + dateColumn + ") <= " + bind(dateTo);
    if (favoritesOnly) where << "EXISTS(SELECT 1 FROM favorites fav WHERE fav.file_id=f.id)";
    if (!tag.trimmed().isEmpty()) where << "EXISTS(SELECT 1 FROM file_tags t WHERE t.file_id=f.id AND t.tag=" + bind(tag.trimmed()) + " COLLATE NOCASE)";
    if (!folder.trimmed().isEmpty()) {
        QString normalized = QDir::fromNativeSeparators(QFileInfo(folder).absoluteFilePath());
        if (!normalized.endsWith('/')) normalized += '/';
        where << "REPLACE(f.path,'\\','/') LIKE " + bind(escapedLike(normalized) + "%") + " ESCAPE '\\'";
    }
    if (collectionId >= 0) {
        const QList<int> ids = getCollectionSubtreeIds(collectionId);
        QStringList placeholders;
        for (int id : ids) placeholders << bind(id);
        if (placeholders.isEmpty()) where << "0";
        else {
            const QString list = placeholders.join(',');
            QStringList alternatives{"EXISTS(SELECT 1 FROM file_collections fc WHERE fc.file_id=f.id AND fc.collection_id IN (" + list + "))"};
            QSqlQuery rules(database());
            rules.prepare("SELECT collection_id,field_name,operator_type,value FROM collection_rules WHERE collection_id IN (" + list + ")");
            for (const QString& key : placeholders) rules.bindValue(key, parameters.value(key));
            const QSet<QString> allowed{"technical_domain","subject","subtopic","location","source","author","document_type","remarks","name","path"};
            QHash<int,QStringList> grouped;
            if (rules.exec()) while (rules.next()) {
                const int id = rules.value(0).toInt();
                const QString field = rules.value(1).toString(), op = rules.value(2).toString(), value = rules.value(3).toString().trimmed();
                if (!allowed.contains(field) || value.isEmpty() || (op != "exact" && op != "contains")) { grouped[id] << "0"; continue; }
                grouped[id] << (op == "exact" ? "f." + field + " = " + bind(value) + " COLLATE NOCASE"
                    : "f." + field + " LIKE " + bind("%" + escapedLike(value) + "%") + " ESCAPE '\\'");
            }
            for (auto it=grouped.cbegin();it!=grouped.cend();++it) alternatives << "(" + it.value().join(" AND ") + ")";
            where << "(" + alternatives.join(" OR ") + ")";
        }
    }
    QString cte, join, snippet = "''", score = "0", reason = "''";
    QString passageId="NULL", passageOrdinal="NULL", anchorType="NULL", pageNumber="NULL", charStart="NULL", charEnd="NULL", timeStartMs="NULL", locator="NULL";
    const QString trimmed = searchText.trimmed();
    if (!trimmed.isEmpty()) {
        QStringList ftsTokens, metadataTerms;
        QRegularExpression tokenPattern(QStringLiteral("\"([^\"]+)\"|(\\S+)"));
        auto tokens = tokenPattern.globalMatch(trimmed);
        while (tokens.hasNext()) {
            const auto token = tokens.next();
            QString word = token.captured(1).isEmpty() ? token.captured(2) : token.captured(1);
            const bool phrase = !token.captured(1).isEmpty();
            QString escaped = word; escaped.replace('"', "\"\"");
            ftsTokens << "\"" + escaped + "\"" + (phrase ? "" : "*");
            const QString match = bind("%" + escapedLike(word) + "%");
            metadataTerms << "(COALESCE(f.name,'')||' '||COALESCE(f.path,'')||' '||COALESCE(f.subject,'')||' '||COALESCE(f.subtopic,'')||' '||COALESCE(f.technical_domain,'')||' '||COALESCE(f.author,'')||' '||COALESCE(f.remarks,'')||' '||COALESCE(f.source,'')) LIKE " + match + " ESCAPE '\\'";
        }
        if (!ftsTokens.isEmpty()) {
            cte = "WITH raw_hits AS MATERIALIZED (SELECT CAST(passage_id AS INTEGER) AS pid,CAST(file_id AS INTEGER) AS fid,snippet(passage_fts,2,'[',']',' … ',24) AS excerpt,bm25(passage_fts) AS ranking FROM passage_fts WHERE passage_fts MATCH " + bind(ftsTokens.join(" AND ")) + "), ranked_hits AS MATERIALIZED (SELECT *,ROW_NUMBER() OVER(PARTITION BY fid ORDER BY ranking,pid) AS passage_rank FROM raw_hits), hits AS MATERIALIZED (SELECT r.fid,r.pid,r.excerpt,r.ranking,p.ordinal,p.anchor_type,p.page_number,p.char_start,p.char_end,p.time_start_ms,p.locator FROM ranked_hits r JOIN passages p ON p.id=r.pid WHERE r.passage_rank=1) ";
            join = " LEFT JOIN hits ON hits.fid=f.id ";
            where << "((" + metadataTerms.join(" AND ") + ") OR hits.fid IS NOT NULL)";
            snippet = "COALESCE(hits.excerpt, f.path)";
            score = "CASE WHEN hits.fid IS NOT NULL THEN 120 - hits.ranking ELSE 0 END + CASE WHEN f.name LIKE " + bind("%" + escapedLike(QString(trimmed).remove('"')) + "%") + " ESCAPE '\\' THEN 40 ELSE 0 END";
            reason = "CASE WHEN hits.fid IS NOT NULL THEN 'Matched passage' || CASE WHEN COALESCE(hits.locator,'')<>'' THEN ' · '||hits.locator ELSE '' END ELSE 'Matched in filename or metadata' END";
            passageId="hits.pid";passageOrdinal="hits.ordinal";anchorType="hits.anchor_type";pageNumber="hits.page_number";charStart="hits.char_start";charEnd="hits.char_end";timeStartMs="hits.time_start_ms";locator="hits.locator";
        }
    }
    const QString from = " FROM files f " + join + " WHERE " + where.join(" AND ");
    const QHash<QString,QString> sortColumns{{"name","name"},{"path","path"},{"sizeBytes","size_bytes"},{"modifiedAt","modified_at"},{"createdAt","created_at"},{"indexedAt","indexed_at"}};
    QString sql = cte + "SELECT f.*, " + snippet + " AS search_snippet," + score + " AS search_score," + reason + " AS search_reason," + passageId + " AS search_passage_id," + passageOrdinal + " AS search_passage_ordinal," + anchorType + " AS search_anchor_type," + pageNumber + " AS search_page_number," + charStart + " AS search_char_start," + charEnd + " AS search_char_end," + timeStartMs + " AS search_time_start_ms," + locator + " AS search_locator" + from;
    sql += " ORDER BY " + (trimmed.isEmpty() ? QString() : QStringLiteral("search_score DESC, ")) + "f." + sortColumns.value(sortField,"indexed_at") + (sortAscending ? " ASC" : " DESC") + ",f.id ASC";
    if (limit >= 0) sql += " LIMIT " + QString::number(limit) + " OFFSET " + QString::number(qMax(0,offset));
    QSqlQuery query(database()); query.prepare(sql);
    for (auto it=parameters.cbegin();it!=parameters.cend();++it) query.bindValue(it.key(),it.value());
    if (!query.exec()) { qWarning() << "Library search failed:" << query.lastError().text(); if(totalCount)*totalCount=0; return result; }
    while(query.next()) { FileRecord file=fileFromQuery(query); file.searchSnippet=query.value("search_snippet").toString(); file.searchScore=query.value("search_score").toDouble(); file.searchMatchReason=query.value("search_reason").toString(); if(!query.value("search_passage_id").isNull())file.searchAnchor={{"passageId",query.value("search_passage_id")},{"passageOrdinal",query.value("search_passage_ordinal")},{"anchorType",query.value("search_anchor_type")},{"pageNumber",query.value("search_page_number")},{"charStart",query.value("search_char_start")},{"charEnd",query.value("search_char_end")},{"timeStartMs",query.value("search_time_start_ms")},{"locator",query.value("search_locator")},{"quote",file.searchSnippet}}; result << file; }
    if (totalCount) {
        QSqlQuery count(database()); count.prepare(cte + "SELECT COUNT(*)" + from);
        // Bind only placeholders present in this statement; score-only parameters are absent.
        const QString countSql = count.lastQuery();
        for(auto it=parameters.cbegin();it!=parameters.cend();++it) {
            QRegularExpression parameter(QRegularExpression::escape(it.key()) + "(?![0-9])");
            if(countSql.contains(parameter)) count.bindValue(it.key(),it.value());
        }
        *totalCount = count.exec() && count.next() ? count.value(0).toInt() : result.size();
    }
    return result;
}
QVariantMap FileRepository::getFileDetails(int fileId) const
{
    QVariantMap m;

    QSqlQuery q(database());
    q.prepare("SELECT * FROM files WHERE id = ?");
    q.addBindValue(fileId);

    if (!q.exec()) {
        qWarning() << "Failed to get file details:" << q.lastError().text();
        return m;
    }

    if (q.next()) {
        const int statusValue = q.value("status").toInt();

        m["id"] = q.value("id");
        m["name"] = q.value("name");
        m["path"] = q.value("path");
        m["extension"] = q.value("extension");
        m["mimeType"] = q.value("mime_type");
        m["sizeBytes"] = q.value("size_bytes");
        m["indexedAt"] = q.value("indexed_at");
        m["createdAt"] = q.value("created_at");
        m["modifiedAt"] = q.value("modified_at");

        m["statusValue"] = statusValue;
        m["statusText"] = (statusValue == 1 ? "Missing" : "Active");

        m["technicalDomain"] = q.value("technical_domain");
        m["subject"] = q.value("subject");
        m["subtopic"] = q.value("subtopic");
        m["location"] = q.value("location");
        m["source"] = q.value("source");
        m["author"] = q.value("author");
        m["documentType"] = q.value("document_type");
        m["remarks"] = q.value("remarks");

        m["collections"] = getFileCollections(fileId);
        m["collectionItems"] = getFileCollectionAssignments(fileId);
    }

    return m;
}

bool FileRepository::fileById(int fileId, FileRecord* result) const
{
    if (!result || fileId < 0) return false;
    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT * FROM files WHERE id=? AND removed_at IS NULL"));
    query.addBindValue(fileId);
    if (!query.exec() || !query.next()) return false;
    *result = fileFromQuery(query);
    return true;
}



QVariantMap FileRepository::getFileDetailsByPath(const QString& absolutePath) const
{
    QVariantMap m;

    const QString normalizedPath = QFileInfo(absolutePath).absoluteFilePath();
    if (normalizedPath.isEmpty()) {
        return m;
    }

    QSqlQuery q(database());
    q.prepare("SELECT id FROM files WHERE path = ?");
    q.addBindValue(normalizedPath);

    if (!q.exec()) {
        qWarning() << "Failed to get file details by path:" << q.lastError().text();
        return m;
    }

    if (!q.next()) {
        return m;
    }

    return getFileDetails(q.value(0).toInt());
}

QVariantList FileRepository::searchReferenceTargets(const QString& queryText, int limit) const
{
    QVariantList result;
    QList<FileRecord> allFiles = getAllFilesRaw();

    const QString query = queryText.trimmed();
    QList<FileRecord> matches;
    matches.reserve(allFiles.size());

    for (const FileRecord& file : allFiles) {
        if (query.isEmpty() || fileMatchesSearch(file, query)) {
            matches.append(file);
        }
    }

    std::sort(matches.begin(), matches.end(), [&](const FileRecord& a, const FileRecord& b) {
        auto score = [&](const FileRecord& file) {
            int value = 0;
            if (!query.isEmpty()) {
                if (file.name.contains(query, Qt::CaseInsensitive)) value += 20;
                if (file.documentType.contains(query, Qt::CaseInsensitive)) value += 12;
                if (file.author.contains(query, Qt::CaseInsensitive)) value += 8;
                if (file.subject.contains(query, Qt::CaseInsensitive)) value += 8;
                if (file.subtopic.contains(query, Qt::CaseInsensitive)) value += 6;
                if (file.technicalDomain.contains(query, Qt::CaseInsensitive)) value += 6;
            }
            if (file.documentType.compare(QStringLiteral("ella note"), Qt::CaseInsensitive) == 0) value += 4;
            return value;
        };

        const int aScore = score(a);
        const int bScore = score(b);
        if (aScore != bScore) {
            return aScore > bScore;
        }
        return a.indexedAt > b.indexedAt;
    });

    const int capped = std::max(1, limit);
    for (int i = 0; i < matches.size() && i < capped; ++i) {
        const FileRecord& file = matches.at(i);
        QVariantMap item;
        item["id"] = file.id;
        item["title"] = file.name;
        item["name"] = file.name;
        item["path"] = file.path;
        item["author"] = file.author;
        item["source"] = file.source;
        item["documentType"] = file.documentType;
        item["technicalDomain"] = file.technicalDomain;
        item["subject"] = file.subject;
        item["subtopic"] = file.subtopic;
        item["location"] = file.location;
        item["createdAt"] = file.createdAt.isValid() ? file.createdAt.toString(Qt::ISODate) : QString();
        item["modifiedAt"] = file.modifiedAt.isValid() ? file.modifiedAt.toString(Qt::ISODate) : QString();
        item["indexedAt"] = file.indexedAt.isValid() ? file.indexedAt.toString(Qt::ISODate) : QString();
        item["displayText"] = QStringLiteral("%1 — %2 — %3")
                                  .arg(file.author.trimmed().isEmpty() ? QStringLiteral("Unknown") : file.author,
                                       file.name,
                                       file.source.trimmed().isEmpty() ? QStringLiteral("Unknown Source") : file.source);
        result.append(item);
    }

    return result;
}

bool FileRepository::addCollection(const QString& name, int parentCollectionId)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    QSqlQuery q(database());
    q.prepare(R"(
        INSERT OR IGNORE INTO collections (name, parent_collection_id)
        VALUES (?, ?)
    )");
    q.addBindValue(trimmed);
    if (parentCollectionId >= 0) {
        q.addBindValue(parentCollectionId);
    } else {
        q.addBindValue(QVariant());
    }

    if (!q.exec()) {
        qWarning() << "Failed to add collection:" << q.lastError().text();
        return false;
    }

    return true;
}

bool FileRepository::renameCollection(int collectionId, const QString& newName)
{
    if (collectionId < 0 || newName.trimmed().isEmpty()) {
        return false;
    }

    QSqlQuery q(database());
    q.prepare(R"(
        UPDATE collections
        SET name = ?
        WHERE id = ?
    )");
    q.addBindValue(newName.trimmed());
    q.addBindValue(collectionId);

    if (!q.exec()) {
        qWarning() << "Failed to rename collection:" << q.lastError().text();
        return false;
    }

    return q.numRowsAffected() > 0;
}

bool FileRepository::deleteCollection(int collectionId)
{
    if (collectionId < 0) {
        return false;
    }

    const QList<int> subtreeIds = getCollectionSubtreeIds(collectionId);
    if (subtreeIds.isEmpty()) {
        return false;
    }

    QSqlDatabase db = database();
    if (!db.transaction()) {
        qWarning() << "Failed to start delete collection transaction:" << db.lastError().text();
        return false;
    }

    QString placeholders;
    for (int i = 0; i < subtreeIds.size(); ++i) {
        if (i > 0) placeholders += ",";
        placeholders += "?";
    }

    auto execDelete = [&](const QString& sql) -> bool {
        QSqlQuery q(db);
        q.prepare(sql);
        for (int id : subtreeIds) {
            q.addBindValue(id);
        }

        if (!q.exec()) {
            qWarning() << "Delete collection operation failed:" << q.lastError().text();
            return false;
        }
        return true;
    };

    const bool ok =
        execDelete(QString("DELETE FROM file_collections WHERE collection_id IN (%1)").arg(placeholders)) &&
        execDelete(QString("DELETE FROM collection_rules WHERE collection_id IN (%1)").arg(placeholders)) &&
        execDelete(QString("DELETE FROM collections WHERE id IN (%1)").arg(placeholders));

    if (!ok) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qWarning() << "Failed to commit collection delete:" << db.lastError().text();
        db.rollback();
        return false;
    }

    return true;
}

QVariantList FileRepository::flattenCollections(const QList<QVariantMap>& collections) const
{
    QHash<int, QList<QVariantMap>> childrenByParent;
    QList<QVariantMap> roots;

    for (const QVariantMap& item : collections) {
        const int parentId = item.value("parentCollectionId").toInt();
        if (parentId < 0) {
            roots.append(item);
        } else {
            childrenByParent[parentId].append(item);
        }
    }

    auto sorter = [](const QVariantMap& a, const QVariantMap& b) {
        return a.value("name").toString().toLower() < b.value("name").toString().toLower();
    };

    std::sort(roots.begin(), roots.end(), sorter);
    for (auto it = childrenByParent.begin(); it != childrenByParent.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(), sorter);
    }

    QVariantList flat;

    std::function<void(const QVariantMap&, int, const QString&)> visit =
        [&](const QVariantMap& node, int depth, const QString& parentPath) {
            const QString name = node.value("name").toString();
            const QString fullPath = parentPath.isEmpty() ? name : (parentPath + " / " + name);

            QVariantMap item = node;
            item["depth"] = depth;
            item["displayName"] = QString(depth * 4, ' ') + name;
            item["fullPath"] = fullPath;
            flat.append(item);

            const int id = node.value("id").toInt();
            const QList<QVariantMap> children = childrenByParent.value(id);
            for (const QVariantMap& child : children) {
                visit(child, depth + 1, fullPath);
            }
        };

    for (const QVariantMap& root : roots) {
        visit(root, 0, "");
    }

    return flat;
}

QVariantList FileRepository::getCollectionTreeFlat() const
{
    return flattenCollections(getAllCollectionsRaw());
}

QVariantList FileRepository::getCollectionPickerOptions() const
{
    return getCollectionTreeFlat();
}

bool FileRepository::assignFileToCollection(int fileId, int collectionId)
{
    QSqlQuery q(database());
    q.prepare(R"(
        INSERT OR IGNORE INTO file_collections (file_id, collection_id)
        VALUES (?, ?)
    )");
    q.addBindValue(fileId);
    q.addBindValue(collectionId);

    if (!q.exec()) {
        qWarning() << "Failed to assign collection:" << q.lastError().text();
        return false;
    }

    return true;
}

bool FileRepository::removeFileFromCollection(int fileId, int collectionId)
{
    if (fileId < 0 || collectionId < 0) {
        return false;
    }

    QSqlQuery q(database());
    q.prepare(R"(
        DELETE FROM file_collections
        WHERE file_id = ? AND collection_id = ?
    )");
    q.addBindValue(fileId);
    q.addBindValue(collectionId);

    if (!q.exec()) {
        qWarning() << "Failed to remove file from collection:" << q.lastError().text();
        return false;
    }

    return true;
}

QStringList FileRepository::getFileCollections(int fileId) const
{
    QStringList list;

    QSqlQuery q(database());
    q.prepare(R"(
        SELECT c.name
        FROM collections c
        INNER JOIN file_collections fc ON c.id = fc.collection_id
        WHERE fc.file_id = ?
        ORDER BY c.name COLLATE NOCASE ASC
    )");
    q.addBindValue(fileId);

    if (!q.exec()) {
        qWarning() << "Failed to fetch file collections:" << q.lastError().text();
        return list;
    }

    while (q.next()) {
        list.append(q.value(0).toString());
    }

    return list;
}

QVariantList FileRepository::getFileCollectionAssignments(int fileId) const
{
    QVariantList list;

    QSqlQuery q(database());
    q.prepare(R"(
        WITH RECURSIVE collection_paths AS (
            SELECT
                id,
                name,
                parent_collection_id,
                name AS full_path
            FROM collections
            WHERE parent_collection_id IS NULL

            UNION ALL

            SELECT
                c.id,
                c.name,
                c.parent_collection_id,
                cp.full_path || ' / ' || c.name AS full_path
            FROM collections c
            INNER JOIN collection_paths cp ON c.parent_collection_id = cp.id
        )
        SELECT cp.id, cp.name, cp.full_path
        FROM file_collections fc
        INNER JOIN collection_paths cp ON cp.id = fc.collection_id
        WHERE fc.file_id = ?
        ORDER BY cp.full_path COLLATE NOCASE ASC
    )");
    q.addBindValue(fileId);

    if (!q.exec()) {
        qWarning() << "Failed to fetch file collection assignments:" << q.lastError().text();
        return list;
    }

    while (q.next()) {
        QVariantMap item;
        item["id"] = q.value(0).toInt();
        item["name"] = q.value(1).toString();
        item["displayName"] = q.value(2).toString();
        list.append(item);
    }

    return list;
}

bool FileRepository::addCollectionRule(int collectionId,
                                       const QString& fieldName,
                                       const QString& operatorType,
                                       const QString& value)
{
    if (collectionId < 0 || fieldName.trimmed().isEmpty() ||
        operatorType.trimmed().isEmpty() || value.trimmed().isEmpty()) {
        return false;
    }

    QSqlQuery q(database());
    q.prepare(R"(
        INSERT INTO collection_rules (collection_id, field_name, operator_type, value)
        VALUES (?, ?, ?, ?)
    )");
    q.addBindValue(collectionId);
    q.addBindValue(fieldName.trimmed());
    q.addBindValue(operatorType.trimmed());
    q.addBindValue(value.trimmed());

    if (!q.exec()) {
        qWarning() << "Failed to add collection rule:" << q.lastError().text();
        return false;
    }

    return true;
}

bool FileRepository::deleteCollectionRule(int ruleId)
{
    if (ruleId < 0) {
        return false;
    }

    QSqlQuery q(database());
    q.prepare("DELETE FROM collection_rules WHERE id = ?");
    q.addBindValue(ruleId);

    if (!q.exec()) {
        qWarning() << "Failed to delete collection rule:" << q.lastError().text();
        return false;
    }

    return true;
}

QVariantList FileRepository::getCollectionRules(int collectionId) const
{
    QVariantList rules;

    if (collectionId < 0) {
        return rules;
    }

    QSqlQuery q(database());
    q.prepare(R"(
        SELECT id, field_name, operator_type, value
        FROM collection_rules
        WHERE collection_id = ?
        ORDER BY id DESC
    )");
    q.addBindValue(collectionId);

    if (!q.exec()) {
        qWarning() << "Failed to fetch collection rules:" << q.lastError().text();
        return rules;
    }

    while (q.next()) {
        QVariantMap item;
        item["id"] = q.value("id").toInt();
        item["fieldName"] = q.value("field_name").toString();
        item["operatorType"] = q.value("operator_type").toString();
        item["value"] = q.value("value").toString();
        item["displayText"] = QString("%1 %2 \"%3\"")
                                  .arg(q.value("field_name").toString(),
                                       q.value("operator_type").toString(),
                                       q.value("value").toString());
        rules.append(item);
    }

    return rules;
}

QVariantList FileRepository::getHierarchyTreeFlat() const
{
    const QList<FileRecord> files = getAllFilesRaw();

    QMap<QString, QMap<QString, QSet<QString>>> tree;
    for (const FileRecord& file : files) {
        const QString domain = file.technicalDomain.trimmed();
        if (domain.isEmpty()) {
            continue;
        }

        const QString subject = file.subject.trimmed();
        const QString subtopic = file.subtopic.trimmed();

        if (!subject.isEmpty()) {
            tree[domain][subject].insert(subtopic);
        } else {
            tree[domain];
        }
    }

    QVariantList flat;

    for (auto domainIt = tree.constBegin(); domainIt != tree.constEnd(); ++domainIt) {
        QVariantMap domainNode;
        domainNode["depth"] = 0;
        domainNode["displayName"] = domainIt.key();
        domainNode["label"] = domainIt.key();
        domainNode["technicalDomain"] = domainIt.key();
        domainNode["subject"] = "";
        domainNode["subtopic"] = "";
        flat.append(domainNode);

        const QMap<QString, QSet<QString>>& subjects = domainIt.value();
        for (auto subjectIt = subjects.constBegin(); subjectIt != subjects.constEnd(); ++subjectIt) {
            if (subjectIt.key().trimmed().isEmpty()) {
                continue;
            }

            QVariantMap subjectNode;
            subjectNode["depth"] = 1;
            subjectNode["displayName"] = QString(4, ' ') + subjectIt.key();
            subjectNode["label"] = subjectIt.key();
            subjectNode["technicalDomain"] = domainIt.key();
            subjectNode["subject"] = subjectIt.key();
            subjectNode["subtopic"] = "";
            flat.append(subjectNode);

            QStringList subtopics = subjectIt.value().values();
            subtopics.removeAll("");
            std::sort(subtopics.begin(), subtopics.end(), [](const QString& a, const QString& b) {
                return a.toLower() < b.toLower();
            });

            for (const QString& subtopic : subtopics) {
                QVariantMap subtopicNode;
                subtopicNode["depth"] = 2;
                subtopicNode["displayName"] = QString(8, ' ') + subtopic;
                subtopicNode["label"] = subtopic;
                subtopicNode["technicalDomain"] = domainIt.key();
                subtopicNode["subject"] = subjectIt.key();
                subtopicNode["subtopic"] = subtopic;
                flat.append(subtopicNode);
            }
        }
    }

    return flat;
}
