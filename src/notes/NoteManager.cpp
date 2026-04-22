#include "notes/NoteManager.h"

#include "core/AppConfig.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTextDocument>
#include <QTextStream>

namespace
{
QString nowIso()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

QString safeFrontmatterValue(const QString& value)
{
    QString text = value;
    text.replace('\n', ' ');
    return text.trimmed();
}
}

NoteManager::NoteManager(QObject* parent)
    : QObject(parent)
{
}

QString NoteManager::notesDirectory() const
{
    return AppConfig::notesDirectory();
}

QString NoteManager::defaultNoteAuthor() const
{
    return AppConfig::defaultNoteAuthor();
}

QVariantList NoteManager::listNotes(const QString& searchText, const QString& metadataFilter) const
{
    QVariantList result;

    const QList<FileRecord> notes = m_repository.queryFiles(
        searchText,
        -1,
        QString(),
        QString(),
        QString(),
        QStringList() << QStringLiteral("active"),
        QStringList() << QStringLiteral("ellanote"),
        QStringList() << QStringLiteral("ella note"),
        QString(),
        QString(),
        QString(),
        QStringLiteral("modifiedAt"),
        false);

    const QString filter = metadataFilter.trimmed();

    for (const FileRecord& file : notes) {
        QFile rawFile(file.path);
        if (!rawFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QTextStream stream(&rawFile);
        stream.setEncoding(QStringConverter::Utf8);
        const NoteFrontmatter note = parseNoteFile(stream.readAll());
        rawFile.close();

        const QString resolvedTitle = note.title.trimmed().isEmpty() ? file.name : note.title.trimmed();
        const QString resolvedAuthor = note.author.trimmed().isEmpty() ? file.author : note.author.trimmed();
        const QString resolvedSource = note.source.trimmed().isEmpty() ? file.source : note.source.trimmed();
        const QString resolvedType = note.type.trimmed().isEmpty() ? file.documentType : note.type.trimmed();
        const QString resolvedDomain = note.technicalDomain.trimmed().isEmpty() ? file.technicalDomain : note.technicalDomain.trimmed();
        const QString resolvedSubject = note.subject.trimmed().isEmpty() ? file.subject : note.subject.trimmed();
        const QString resolvedSubtopic = note.subtopic.trimmed().isEmpty() ? file.subtopic : note.subtopic.trimmed();
        const QString resolvedLocation = note.location.trimmed().isEmpty() ? file.location : note.location.trimmed();

        if (!filter.isEmpty()) {
            const QString haystack = QStringLiteral("%1 %2 %3 %4 %5 %6 %7")
            .arg(resolvedTitle,
                 resolvedDomain,
                 resolvedSubject,
                 resolvedSubtopic,
                 resolvedLocation,
                 resolvedAuthor,
                 resolvedSource);
            if (!haystack.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
        }

        QVariantMap item;
        item["id"] = file.id;
        item["title"] = resolvedTitle;
        item["name"] = resolvedTitle;
        item["path"] = file.path;
        item["technicalDomain"] = resolvedDomain;
        item["subject"] = resolvedSubject;
        item["subtopic"] = resolvedSubtopic;
        item["location"] = resolvedLocation;
        item["source"] = resolvedSource;
        item["author"] = resolvedAuthor;
        item["documentType"] = resolvedType;
        item["createdAt"] = file.createdAt.isValid() ? file.createdAt.toString(Qt::ISODate) : QString();
        item["modifiedAt"] = file.modifiedAt.isValid() ? file.modifiedAt.toString(Qt::ISODate) : QString();
        result.append(item);
    }

    return result;
}

QVariantMap NoteManager::createNote(const QString& title,
                                    const QString& technicalDomain,
                                    const QString& subject,
                                    const QString& subtopic,
                                    const QString& location,
                                    const QString& initialHtml)
{
    const QString trimmedTitle = title.trimmed().isEmpty() ? QStringLiteral("Untitled Note") : title.trimmed();
    const QString filePath = generateUniqueNotePath(trimmedTitle);

    NoteFrontmatter note;
    note.title = trimmedTitle;
    note.author = AppConfig::defaultNoteAuthor();
    note.source = QStringLiteral("Ella Notebook");
    note.type = QStringLiteral("ella note");
    note.technicalDomain = technicalDomain.trimmed();
    note.subject = subject.trimmed();
    note.subtopic = subtopic.trimmed();
    note.location = location.trimmed();
    note.created = nowIso();

    QTextDocument document;
    if (!initialHtml.trimmed().isEmpty()) {
        document.setHtml(initialHtml);
        note.markdownBody = document.toMarkdown();
    }

    if (note.markdownBody.trimmed().isEmpty()) {
        note.markdownBody = QStringLiteral("# %1\n\n").arg(trimmedTitle);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return {};
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << buildNoteFileContent(note);
    file.close();

    if (!m_repository.addFile(filePath,
                              note.technicalDomain,
                              note.subject,
                              note.subtopic,
                              note.location,
                              note.source,
                              note.author,
                              note.type,
                              QString())) {
        return {};
    }

    const QVariantMap details = m_repository.getFileDetailsByPath(filePath);
    bool ok = false;
    const int parsedFileId = details.value("id").toInt(&ok);
    const int fileId = ok ? parsedFileId : -1;
    if (fileId < 0) {
        return {};
    }

    return loadNote(fileId);
}

QVariantMap NoteManager::loadNote(int fileId) const
{
    const QVariantMap details = m_repository.getFileDetails(fileId);
    if (details.isEmpty()) {
        return {};
    }

    QFile file(details.value("path").toString());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    const QString fileContent = stream.readAll();
    const NoteFrontmatter note = parseNoteFile(fileContent);

    return noteToVariant(fileId, details, note);
}

bool NoteManager::saveNote(int fileId,
                           const QString& title,
                           const QString& technicalDomain,
                           const QString& subject,
                           const QString& subtopic,
                           const QString& location,
                           const QString& editorHtml)
{
    const QVariantMap details = m_repository.getFileDetails(fileId);
    if (details.isEmpty()) {
        return false;
    }

    QFile readFile(details.value("path").toString());
    if (!readFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream readStream(&readFile);
    readStream.setEncoding(QStringConverter::Utf8);
    NoteFrontmatter current = parseNoteFile(readStream.readAll());
    readFile.close();

    QTextDocument document;
    document.setHtml(editorHtml);

    current.title = title.trimmed().isEmpty() ? QStringLiteral("Untitled Note") : title.trimmed();
    current.technicalDomain = technicalDomain.trimmed();
    current.subject = subject.trimmed();
    current.subtopic = subtopic.trimmed();
    current.location = location.trimmed();
    current.author = details.value("author").toString().trimmed().isEmpty()
                         ? AppConfig::defaultNoteAuthor()
                         : details.value("author").toString();
    current.source = QStringLiteral("Ella Notebook");
    current.type = QStringLiteral("ella note");
    if (current.created.trimmed().isEmpty()) {
        current.created = details.value("createdAt").toString().trimmed().isEmpty()
        ? nowIso()
        : details.value("createdAt").toString();
    }
    current.markdownBody = document.toMarkdown();

    QFile writeFile(details.value("path").toString());
    if (!writeFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    QTextStream writeStream(&writeFile);
    writeStream.setEncoding(QStringConverter::Utf8);
    writeStream << buildNoteFileContent(current);
    writeFile.close();

    return m_repository.updateFileMetadata(fileId,
                                           current.technicalDomain,
                                           current.subject,
                                           current.subtopic,
                                           current.location,
                                           current.source,
                                           current.author,
                                           current.type,
                                           QString());
}

QVariantList NoteManager::searchReferences(const QString& queryText, int limit) const
{
    QVariantList bibliographyLikeResults;
    const QVariantList raw = m_repository.searchReferenceTargets(queryText, limit);

    for (const QVariant& value : raw) {
        QVariantMap item = value.toMap();
        const QString path = item.value("path").toString();
        const QString documentType = item.value("documentType").toString();

        if (documentType.compare(QStringLiteral("ella note"), Qt::CaseInsensitive) == 0
            || QFileInfo(path).suffix().compare(QStringLiteral("ellanote"), Qt::CaseInsensitive) == 0) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream stream(&file);
                stream.setEncoding(QStringConverter::Utf8);
                const NoteFrontmatter note = parseNoteFile(stream.readAll());
                file.close();

                if (!note.title.trimmed().isEmpty()) {
                    item["title"] = note.title.trimmed();
                    item["name"] = note.title.trimmed();
                }
                if (!note.author.trimmed().isEmpty()) {
                    item["author"] = note.author.trimmed();
                }
                if (!note.source.trimmed().isEmpty()) {
                    item["source"] = note.source.trimmed();
                }
                if (!note.type.trimmed().isEmpty()) {
                    item["documentType"] = note.type.trimmed();
                }
                if (!note.technicalDomain.trimmed().isEmpty()) {
                    item["technicalDomain"] = note.technicalDomain.trimmed();
                }
                if (!note.subject.trimmed().isEmpty()) {
                    item["subject"] = note.subject.trimmed();
                }
                if (!note.subtopic.trimmed().isEmpty()) {
                    item["subtopic"] = note.subtopic.trimmed();
                }
                if (!note.location.trimmed().isEmpty()) {
                    item["location"] = note.location.trimmed();
                }
            }
        }

        item["displayText"] = QStringLiteral("%1 — %2 — %3")
                                  .arg(item.value("author").toString().trimmed().isEmpty() ? QStringLiteral("Unknown") : item.value("author").toString(),
                                       item.value("title").toString().trimmed().isEmpty() ? item.value("name").toString() : item.value("title").toString(),
                                       item.value("source").toString().trimmed().isEmpty() ? QStringLiteral("Unknown Source") : item.value("source").toString());
        bibliographyLikeResults.append(item);
    }

    return bibliographyLikeResults;
}

QVariantList NoteManager::bibliographyForContent(const QString& textOrMarkdown) const
{
    QVariantList bibliography;
    const QRegularExpression rx(QStringLiteral("@file_(\\d+)"));
    QRegularExpressionMatchIterator it = rx.globalMatch(textOrMarkdown);
    QSet<int> seen;
    int number = 1;

    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const int fileId = match.captured(1).toInt();
        if (fileId < 0 || seen.contains(fileId)) {
            continue;
        }
        seen.insert(fileId);

        QVariantMap file = m_repository.getFileDetails(fileId);
        if (file.isEmpty()) {
            continue;
        }

        const QString path = file.value("path").toString();
        const QString documentType = file.value("documentType").toString();
        if (documentType.compare(QStringLiteral("ella note"), Qt::CaseInsensitive) == 0
            || QFileInfo(path).suffix().compare(QStringLiteral("ellanote"), Qt::CaseInsensitive) == 0) {
            QFile noteFile(path);
            if (noteFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream stream(&noteFile);
                stream.setEncoding(QStringConverter::Utf8);
                const NoteFrontmatter note = parseNoteFile(stream.readAll());
                noteFile.close();

                if (!note.title.trimmed().isEmpty()) file["name"] = note.title.trimmed();
                if (!note.author.trimmed().isEmpty()) file["author"] = note.author.trimmed();
                if (!note.source.trimmed().isEmpty()) file["source"] = note.source.trimmed();
                if (!note.type.trimmed().isEmpty()) file["documentType"] = note.type.trimmed();
                if (!note.created.trimmed().isEmpty()) file["createdAt"] = note.created.trimmed();
            }
        }

        QVariantMap item;
        item["number"] = number++;
        item["fileId"] = fileId;
        item["author"] = file.value("author").toString();
        item["title"] = file.value("name").toString();
        item["source"] = file.value("source").toString();
        item["date"] = file.value("createdAt").toString().isEmpty()
                           ? file.value("indexedAt").toString()
                           : file.value("createdAt").toString();
        item["documentType"] = file.value("documentType").toString();
        item["displayText"] = QStringLiteral("[%1] %2 — %3 — %4 — %5")
                                  .arg(item.value("number").toInt())
                                  .arg(item.value("author").toString().isEmpty() ? QStringLiteral("Unknown") : item.value("author").toString(),
                                       item.value("title").toString(),
                                       item.value("source").toString().isEmpty() ? QStringLiteral("Unknown Source") : item.value("source").toString(),
                                       item.value("date").toString());
        bibliography.append(item);
    }

    return bibliography;
}

QString NoteManager::htmlToMarkdown(const QString& html) const
{
    QTextDocument document;
    document.setHtml(html);
    return document.toMarkdown();
}

QString NoteManager::markdownToHtml(const QString& markdown) const
{
    QTextDocument document;
    document.setMarkdown(markdown);
    return document.toHtml();
}

NoteManager::NoteFrontmatter NoteManager::parseNoteFile(const QString& fileContent) const
{
    NoteFrontmatter note;
    note.author = AppConfig::defaultNoteAuthor();
    note.source = QStringLiteral("Ella Notebook");
    note.type = QStringLiteral("ella note");

    const QString normalized = fileContent;
    if (!normalized.startsWith(QStringLiteral("---\n"))) {
        note.markdownBody = normalized;
        return note;
    }

    const int secondMarker = normalized.indexOf(QStringLiteral("\n---"), 4);
    if (secondMarker < 0) {
        note.markdownBody = normalized;
        return note;
    }

    const QString frontmatter = normalized.mid(4, secondMarker - 4);
    note.markdownBody = normalized.mid(secondMarker + 4).trimmed();

    const QStringList lines = frontmatter.split('\n');
    for (const QString& line : lines) {
        const int separator = line.indexOf(':');
        if (separator <= 0) {
            continue;
        }

        const QString key = line.left(separator).trimmed();
        const QString value = line.mid(separator + 1).trimmed();

        if (key == QStringLiteral("title")) note.title = value;
        else if (key == QStringLiteral("author")) note.author = value;
        else if (key == QStringLiteral("source")) note.source = value;
        else if (key == QStringLiteral("type")) note.type = value;
        else if (key == QStringLiteral("domain")) note.technicalDomain = value;
        else if (key == QStringLiteral("subject")) note.subject = value;
        else if (key == QStringLiteral("subtopic")) note.subtopic = value;
        else if (key == QStringLiteral("location")) note.location = value;
        else if (key == QStringLiteral("created")) note.created = value;
    }

    return note;
}

QString NoteManager::buildNoteFileContent(const NoteFrontmatter& note) const
{
    QString output;
    QTextStream stream(&output);
    stream << "---\n";
    stream << "title: " << safeFrontmatterValue(note.title) << "\n";
    stream << "author: " << safeFrontmatterValue(note.author) << "\n";
    stream << "source: " << safeFrontmatterValue(note.source) << "\n";
    stream << "type: " << safeFrontmatterValue(note.type) << "\n";
    stream << "domain: " << safeFrontmatterValue(note.technicalDomain) << "\n";
    stream << "subject: " << safeFrontmatterValue(note.subject) << "\n";
    stream << "subtopic: " << safeFrontmatterValue(note.subtopic) << "\n";
    stream << "location: " << safeFrontmatterValue(note.location) << "\n";
    stream << "created: " << safeFrontmatterValue(note.created) << "\n";
    stream << "---\n\n";
    stream << note.markdownBody.trimmed() << "\n";
    return output;
}

QString NoteManager::slugify(const QString& value) const
{
    QString slug = value.toLower();
    slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    slug.replace(QRegularExpression(QStringLiteral("-+")), QStringLiteral("-"));
    slug.remove(QRegularExpression(QStringLiteral("^-|-$")));
    return slug.isEmpty() ? QStringLiteral("note") : slug;
}

QString NoteManager::generateUniqueNotePath(const QString& title) const
{
    const QString baseName = slugify(title);
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    const QString directory = AppConfig::notesDirectory();
    QDir dir(directory);

    QString fileName = QStringLiteral("%1-%2.ellanote").arg(baseName, timestamp);
    QString absolute = dir.absoluteFilePath(fileName);

    int suffix = 1;
    while (QFileInfo::exists(absolute)) {
        fileName = QStringLiteral("%1-%2-%3.ellanote").arg(baseName, timestamp).arg(suffix++);
        absolute = dir.absoluteFilePath(fileName);
    }

    return absolute;
}

QVariantMap NoteManager::noteToVariant(int fileId, const QVariantMap& fileDetails, const NoteFrontmatter& note) const
{
    QTextDocument document;
    document.setMarkdown(note.markdownBody);

    QVariantMap result = fileDetails;
    const QString resolvedTitle = note.title.isEmpty() ? fileDetails.value("name").toString() : note.title;
    result["id"] = fileId;
    result["title"] = resolvedTitle;
    result["name"] = resolvedTitle;
    result["author"] = note.author;
    result["source"] = note.source;
    result["documentType"] = note.type;
    result["technicalDomain"] = note.technicalDomain;
    result["subject"] = note.subject;
    result["subtopic"] = note.subtopic;
    result["location"] = note.location;
    result["created"] = note.created;
    result["markdown"] = note.markdownBody;
    result["html"] = document.toHtml();
    result["bibliography"] = bibliographyForContent(note.markdownBody);
    return result;
}
