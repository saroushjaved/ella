
#include "library/AnnotationRepository.h"
#include "database/DatabaseManager.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>

namespace
{
QString nowIso()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

QString normalizeTargetType(const QString& targetType)
{
    const QString normalized = targetType.trimmed().toLower();
    if (normalized == QStringLiteral("text")) {
        return QStringLiteral("text");
    }
    if (normalized == QStringLiteral("pdf")) {
        return QStringLiteral("pdf");
    }
    if (normalized == QStringLiteral("image")) {
        return QStringLiteral("image");
    }
    if (normalized == QStringLiteral("video")) {
        return QStringLiteral("video");
    }
    if (normalized == QStringLiteral("audio")) {
        return QStringLiteral("audio");
    }
    if (normalized == QStringLiteral("presentation")) {
        return QStringLiteral("presentation");
    }
    return normalized;
}

QString normalizeAnnotationType(const QString& targetType, const QString& annotationType)
{
    const QString normalizedTarget = normalizeTargetType(targetType);
    const QString normalizedType = annotationType.trimmed().toLower();

    if (normalizedTarget == QStringLiteral("text")) {
        if (normalizedType == QStringLiteral("text-highlight")
            || normalizedType == QStringLiteral("highlight")
            || normalizedType == QStringLiteral("texthighlight")) {
            return QStringLiteral("text-highlight");
        }
        return QStringLiteral("text-highlight");
    }

    if (normalizedTarget == QStringLiteral("video") || normalizedTarget == QStringLiteral("audio")) {
        if (normalizedType == QStringLiteral("time-range")
            || normalizedType == QStringLiteral("range")
            || normalizedType == QStringLiteral("timestamp-range")) {
            return QStringLiteral("time-range");
        }
        return QStringLiteral("time-note");
    }

    if (normalizedType == QStringLiteral("pin")
        || normalizedType == QStringLiteral("pin-note")
        || normalizedType == QStringLiteral("note-pin")) {
        return QStringLiteral("pin-note");
    }

    if (normalizedType == QStringLiteral("rect-note")
        || normalizedType == QStringLiteral("rect")
        || normalizedType == QStringLiteral("note-rect")) {
        return QStringLiteral("rect-note");
    }

    if (normalizedType == QStringLiteral("area-highlight")
        || normalizedType == QStringLiteral("area")
        || normalizedType == QStringLiteral("highlight")) {
        return QStringLiteral("area-highlight");
    }

    return normalizedType;
}
}

QVariantList AnnotationRepository::getDocumentNotes(int fileId) const
{
    QVariantList notes;
    if (fileId < 0) {
        return notes;
    }

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(R"(
        SELECT id, file_id, title, body, created_at, updated_at
        FROM document_notes
        WHERE file_id = ?
        ORDER BY updated_at DESC, id DESC
    )");
    query.addBindValue(fileId);

    if (!query.exec()) {
        qWarning() << "Failed to fetch document notes:" << query.lastError().text();
        return notes;
    }

    while (query.next()) {
        QVariantMap note;
        note["id"] = query.value("id").toInt();
        note["fileId"] = query.value("file_id").toInt();
        note["title"] = query.value("title").toString();
        note["body"] = query.value("body").toString();
        note["createdAt"] = query.value("created_at").toString();
        note["updatedAt"] = query.value("updated_at").toString();
        notes.append(note);
    }

    return notes;
}

bool AnnotationRepository::addDocumentNote(int fileId, const QString& title, const QString& body)
{
    if (fileId < 0 || body.trimmed().isEmpty()) {
        return false;
    }

    const QString timestamp = nowIso();

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(R"(
        INSERT INTO document_notes (
            file_id, title, body, created_at, updated_at
        ) VALUES (?, ?, ?, ?, ?)
    )");
    query.addBindValue(fileId);
    query.addBindValue(title.trimmed());
    query.addBindValue(body.trimmed());
    query.addBindValue(timestamp);
    query.addBindValue(timestamp);

    if (!query.exec()) {
        qWarning() << "Failed to add document note:" << query.lastError().text();
        return false;
    }

    return true;
}

bool AnnotationRepository::updateDocumentNote(int noteId, const QString& title, const QString& body)
{
    if (noteId < 0 || body.trimmed().isEmpty()) {
        return false;
    }

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(R"(
        UPDATE document_notes
        SET title = ?,
            body = ?,
            updated_at = ?
        WHERE id = ?
    )");
    query.addBindValue(title.trimmed());
    query.addBindValue(body.trimmed());
    query.addBindValue(nowIso());
    query.addBindValue(noteId);

    if (!query.exec()) {
        qWarning() << "Failed to update document note:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool AnnotationRepository::deleteDocumentNote(int noteId)
{
    if (noteId < 0) {
        return false;
    }

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare("DELETE FROM document_notes WHERE id = ?");
    query.addBindValue(noteId);

    if (!query.exec()) {
        qWarning() << "Failed to delete document note:" << query.lastError().text();
        return false;
    }

    return true;
}

QVariantList AnnotationRepository::getAnnotations(int fileId, const QString& targetType) const
{
    QVariantList annotations;
    if (fileId < 0) {
        return annotations;
    }

    QSqlQuery query(DatabaseManager::instance().database());
    const QString normalizedTargetType = normalizeTargetType(targetType);

    if (normalizedTargetType.isEmpty()) {
        query.prepare(R"(
            SELECT id, file_id, target_type, annotation_type, page_number,
                   char_start, char_end, anchor_text,
                   x, y, width, height, color, content,
                   time_start_ms, time_end_ms,
                   created_at, updated_at
            FROM annotations
            WHERE file_id = ?
            ORDER BY COALESCE(time_start_ms, -1) ASC, page_number ASC, char_start ASC, id ASC
        )");
        query.addBindValue(fileId);
    } else {
        query.prepare(R"(
            SELECT id, file_id, target_type, annotation_type, page_number,
                   char_start, char_end, anchor_text,
                   x, y, width, height, color, content,
                   time_start_ms, time_end_ms,
                   created_at, updated_at
            FROM annotations
            WHERE file_id = ? AND target_type = ?
            ORDER BY COALESCE(time_start_ms, -1) ASC, page_number ASC, char_start ASC, id ASC
        )");
        query.addBindValue(fileId);
        query.addBindValue(normalizedTargetType);
    }

    if (!query.exec()) {
        qWarning() << "Failed to fetch annotations:" << query.lastError().text();
        return annotations;
    }

    while (query.next()) {
        QVariantMap item;
        const QString rowTargetType = normalizeTargetType(query.value("target_type").toString());
        const QString rowAnnotationType = normalizeAnnotationType(rowTargetType, query.value("annotation_type").toString());
        item["id"] = query.value("id").toInt();
        item["fileId"] = query.value("file_id").toInt();
        item["targetType"] = rowTargetType;
        item["annotationType"] = rowAnnotationType;
        item["pageNumber"] = query.value("page_number").isNull() ? -1 : query.value("page_number").toInt();
        item["charStart"] = query.value("char_start").isNull() ? -1 : query.value("char_start").toInt();
        item["charEnd"] = query.value("char_end").isNull() ? -1 : query.value("char_end").toInt();
        item["anchorText"] = query.value("anchor_text").toString();
        item["x"] = query.value("x").toDouble();
        item["y"] = query.value("y").toDouble();
        item["width"] = query.value("width").toDouble();
        item["height"] = query.value("height").toDouble();
        item["color"] = query.value("color").toString();
        item["content"] = query.value("content").toString();
        item["timeStartMs"] =
            query.value("time_start_ms").isNull() ? -1 : query.value("time_start_ms").toLongLong();
        item["timeEndMs"] =
            query.value("time_end_ms").isNull() ? -1 : query.value("time_end_ms").toLongLong();
        item["createdAt"] = query.value("created_at").toString();
        item["updatedAt"] = query.value("updated_at").toString();
        annotations.append(item);
    }

    return annotations;
}

bool AnnotationRepository::addAnnotation(int fileId,
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
    const QString normalizedTargetType = normalizeTargetType(targetType);
    const QString normalizedAnnotationType = normalizeAnnotationType(normalizedTargetType, annotationType);

    if (fileId < 0 || normalizedTargetType.isEmpty() || normalizedAnnotationType.isEmpty()) {
        return false;
    }

    if (normalizedTargetType == QStringLiteral("text") && (charStart < 0 || charEnd <= charStart)) {
        return false;
    }

    if ((normalizedTargetType == QStringLiteral("image")
         || normalizedTargetType == QStringLiteral("pdf")
         || normalizedTargetType == QStringLiteral("presentation"))
        && (normalizedAnnotationType == QStringLiteral("rect-note")
            || normalizedAnnotationType == QStringLiteral("area-highlight"))) {
        if (width <= 0.0 || height <= 0.0) {
            return false;
        }
    }

    if (normalizedTargetType == QStringLiteral("video") || normalizedTargetType == QStringLiteral("audio")) {
        if (normalizedAnnotationType == QStringLiteral("time-range")) {
            if (timeStartMs < 0 || timeEndMs <= timeStartMs) {
                return false;
            }
        } else {
            if (timeStartMs < 0) {
                return false;
            }
            if (timeEndMs < timeStartMs) {
                timeEndMs = timeStartMs;
            }
        }
    }

    const QString timestamp = nowIso();

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(R"(
        INSERT INTO annotations (
            file_id, target_type, annotation_type, page_number,
            char_start, char_end, anchor_text,
            x, y, width, height, color, content, time_start_ms, time_end_ms, created_at, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(fileId);
    query.addBindValue(normalizedTargetType);
    query.addBindValue(normalizedAnnotationType);
    if (pageNumber >= 0) {
        query.addBindValue(pageNumber);
    } else {
        query.addBindValue(QVariant());
    }
    if (charStart >= 0) {
        query.addBindValue(charStart);
    } else {
        query.addBindValue(QVariant());
    }
    if (charEnd >= 0) {
        query.addBindValue(charEnd);
    } else {
        query.addBindValue(QVariant());
    }
    query.addBindValue(anchorText);
    query.addBindValue(x);
    query.addBindValue(y);
    query.addBindValue(width);
    query.addBindValue(height);
    query.addBindValue(color.trimmed());
    query.addBindValue(content.trimmed());
    if (timeStartMs >= 0) {
        query.addBindValue(timeStartMs);
    } else {
        query.addBindValue(QVariant());
    }
    if (timeEndMs >= 0) {
        query.addBindValue(timeEndMs);
    } else {
        query.addBindValue(QVariant());
    }
    query.addBindValue(timestamp);
    query.addBindValue(timestamp);

    if (!query.exec()) {
        qWarning() << "Failed to add annotation:" << query.lastError().text();
        return false;
    }

    return true;
}

bool AnnotationRepository::updateAnnotation(int annotationId,
                                            const QString& color,
                                            const QString& content)
{
    if (annotationId < 0) {
        return false;
    }

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(R"(
        UPDATE annotations
        SET color = ?,
            content = ?,
            updated_at = ?
        WHERE id = ?
    )");
    query.addBindValue(color.trimmed());
    query.addBindValue(content.trimmed());
    query.addBindValue(nowIso());
    query.addBindValue(annotationId);

    if (!query.exec()) {
        qWarning() << "Failed to update annotation:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool AnnotationRepository::deleteAnnotation(int annotationId)
{
    if (annotationId < 0) {
        return false;
    }

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare("DELETE FROM annotations WHERE id = ?");
    query.addBindValue(annotationId);

    if (!query.exec()) {
        qWarning() << "Failed to delete annotation:" << query.lastError().text();
        return false;
    }

    return true;
}

QVariantList AnnotationRepository::getTextAnnotations(int fileId) const
{
    return getAnnotations(fileId, QStringLiteral("text"));
}
