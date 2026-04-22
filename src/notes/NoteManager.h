#pragma once

#include "library/FileRepository.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class NoteManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString notesDirectory READ notesDirectory CONSTANT)
    Q_PROPERTY(QString defaultNoteAuthor READ defaultNoteAuthor CONSTANT)

public:
    explicit NoteManager(QObject* parent = nullptr);

    QString notesDirectory() const;
    QString defaultNoteAuthor() const;

    Q_INVOKABLE QVariantList listNotes(const QString& searchText = QString(),
                                       const QString& metadataFilter = QString()) const;

    Q_INVOKABLE QVariantMap createNote(const QString& title,
                                       const QString& technicalDomain,
                                       const QString& subject,
                                       const QString& subtopic,
                                       const QString& location,
                                       const QString& initialHtml = QString());

    Q_INVOKABLE QVariantMap loadNote(int fileId) const;

    Q_INVOKABLE bool saveNote(int fileId,
                              const QString& title,
                              const QString& technicalDomain,
                              const QString& subject,
                              const QString& subtopic,
                              const QString& location,
                              const QString& editorHtml);

    Q_INVOKABLE QVariantList searchReferences(const QString& queryText, int limit = 20) const;
    Q_INVOKABLE QVariantList bibliographyForContent(const QString& textOrMarkdown) const;
    Q_INVOKABLE QString htmlToMarkdown(const QString& html) const;
    Q_INVOKABLE QString markdownToHtml(const QString& markdown) const;

private:
    struct NoteFrontmatter
    {
        QString title;
        QString author;
        QString source;
        QString type;
        QString technicalDomain;
        QString subject;
        QString subtopic;
        QString location;
        QString created;
        QString markdownBody;
    };

    NoteFrontmatter parseNoteFile(const QString& fileContent) const;
    QString buildNoteFileContent(const NoteFrontmatter& note) const;
    QString slugify(const QString& value) const;
    QString generateUniqueNotePath(const QString& title) const;
    QVariantMap noteToVariant(int fileId, const QVariantMap& fileDetails, const NoteFrontmatter& note) const;

    FileRepository m_repository;
};
