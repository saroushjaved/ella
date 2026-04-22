#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

class QTextDocument;
class QTextCharFormat;

class RichTextFormatter final : public QObject
{
    Q_OBJECT

public:
    explicit RichTextFormatter(QObject *parent = nullptr);

    Q_INVOKABLE bool toggleBold(QObject *quickTextDocument, int selectionStart, int selectionEnd);
    Q_INVOKABLE bool toggleItalic(QObject *quickTextDocument, int selectionStart, int selectionEnd);
    Q_INVOKABLE bool applyHeading(QObject *quickTextDocument, int cursorPosition, int level);
    Q_INVOKABLE bool applyBulletList(QObject *quickTextDocument, int cursorPosition);
    Q_INVOKABLE bool applyNumberedList(QObject *quickTextDocument, int cursorPosition);
    Q_INVOKABLE bool toggleBlockQuote(QObject *quickTextDocument, int cursorPosition);
    Q_INVOKABLE bool toggleCodeBlock(QObject *quickTextDocument, int cursorPosition);
    Q_INVOKABLE bool toggleHighlight(QObject *quickTextDocument, int selectionStart, int selectionEnd);

    Q_INVOKABLE QString richTextToMarkdown(const QString &richText) const;
    Q_INVOKABLE QString markdownToRichText(const QString &markdown) const;

private:
    QTextDocument *resolveDocument(QObject *quickTextDocument) const;
    bool mergeCharFormat(QObject *quickTextDocument,
                         int selectionStart,
                         int selectionEnd,
                         const QTextCharFormat &format) const;
};