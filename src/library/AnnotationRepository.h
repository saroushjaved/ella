
#pragma once

#include <QVariantList>
#include <QString>
#include <QtGlobal>

class AnnotationRepository
{
public:
    QVariantList getDocumentNotes(int fileId) const;
    bool addDocumentNote(int fileId, const QString& title, const QString& body);
    bool updateDocumentNote(int noteId, const QString& title, const QString& body);
    bool deleteDocumentNote(int noteId);

    QVariantList getAnnotations(int fileId, const QString& targetType) const;

    bool addAnnotation(int fileId,
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
                       qint64 timeEndMs);

    bool updateAnnotation(int annotationId,
                          const QString& color,
                          const QString& content);

    bool deleteAnnotation(int annotationId);

    QVariantList getTextAnnotations(int fileId) const;
};
