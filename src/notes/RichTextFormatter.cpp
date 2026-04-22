#include "RichTextFormatter.h"

#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QTextBlockFormat>
#include <QtGui/QTextCharFormat>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtGui/QTextListFormat>
#include <QtQuick/QQuickTextDocument>
#include <QtCore/QtGlobal>

RichTextFormatter::RichTextFormatter(QObject *parent)
    : QObject(parent)
{
}

QTextDocument *RichTextFormatter::resolveDocument(QObject *quickTextDocument) const
{
    auto *wrapper = qobject_cast<QQuickTextDocument *>(quickTextDocument);
    if (!wrapper) {
        return nullptr;
    }
    return wrapper->textDocument();
}

bool RichTextFormatter::mergeCharFormat(QObject *quickTextDocument,
                                        int selectionStart,
                                        int selectionEnd,
                                        const QTextCharFormat &format) const
{
    QTextDocument *document = resolveDocument(quickTextDocument);
    if (!document) {
        return false;
    }

    QTextCursor cursor(document);
    if (selectionStart >= 0 && selectionEnd > selectionStart) {
        cursor.setPosition(selectionStart);
        cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    } else {
        cursor.select(QTextCursor::WordUnderCursor);
    }

    cursor.mergeCharFormat(format);
    return true;
}

bool RichTextFormatter::toggleBold(QObject *quickTextDocument, int selectionStart, int selectionEnd)
{
    QTextDocument *document = resolveDocument(quickTextDocument);
    if (!document) {
        return false;
    }

    QTextCursor cursor(document);
    if (selectionStart >= 0 && selectionEnd > selectionStart) {
        cursor.setPosition(selectionStart);
        cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(selectionStart >= 0 ? selectionStart : 0);
        cursor.select(QTextCursor::WordUnderCursor);
    }

    QTextCharFormat fmt;
    const bool makeBold = cursor.charFormat().fontWeight() != QFont::Bold;
    fmt.setFontWeight(makeBold ? QFont::Bold : QFont::Normal);
    cursor.mergeCharFormat(fmt);
    return true;
}

bool RichTextFormatter::toggleItalic(QObject *quickTextDocument, int selectionStart, int selectionEnd)
{
    QTextDocument *document = resolveDocument(quickTextDocument);
    if (!document) {
        return false;
    }

    QTextCursor cursor(document);
    if (selectionStart >= 0 && selectionEnd > selectionStart) {
        cursor.setPosition(selectionStart);
        cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(selectionStart >= 0 ? selectionStart : 0);
        cursor.select(QTextCursor::WordUnderCursor);
    }

    QTextCharFormat fmt;
    fmt.setFontItalic(!cursor.charFormat().fontItalic());
    cursor.mergeCharFormat(fmt);
    return true;
}

bool RichTextFormatter::toggleHighlight(QObject *quickTextDocument, int selectionStart, int selectionEnd)
{
    QTextDocument *document = resolveDocument(quickTextDocument);
    if (!document) {
        return false;
    }

    QTextCursor cursor(document);
    if (selectionStart >= 0 && selectionEnd > selectionStart) {
        cursor.setPosition(selectionStart);
        cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(selectionStart >= 0 ? selectionStart : 0);
        cursor.select(QTextCursor::WordUnderCursor);
    }

    QTextCharFormat fmt;
    const QColor current = cursor.charFormat().background().color();
    if (current.isValid() && current == QColor(QStringLiteral("#fff59d"))) {
        fmt.clearBackground();
    } else {
        fmt.setBackground(QColor(QStringLiteral("#fff59d")));
    }
    cursor.mergeCharFormat(fmt);
    return true;
}

bool RichTextFormatter::applyHeading(QObject *quickTextDocument, int cursorPosition, int level)
{
    QTextDocument *document = resolveDocument(quickTextDocument);
    if (!document) {
        return false;
    }

    QTextCursor cursor(document);
    cursor.setPosition(qMax(0, cursorPosition));
    cursor.select(QTextCursor::BlockUnderCursor);

    QTextBlockFormat blockFormat = cursor.blockFormat();
    blockFormat.setHeadingLevel(qBound(0, level, 6));
    cursor.setBlockFormat(blockFormat);

    QTextCharFormat charFormat = cursor.charFormat();
    if (level <= 0) {
        charFormat.setFontPointSize(12.0);
        charFormat.setFontWeight(QFont::Normal);
    } else {
        static const qreal sizes[] = {12.0, 24.0, 20.0, 18.0, 16.0, 14.0, 13.0};
        charFormat.setFontPointSize(sizes[qBound(0, level, 6)]);
        charFormat.setFontWeight(QFont::Bold);
    }

    cursor.mergeCharFormat(charFormat);
    return true;
}

bool RichTextFormatter::applyBulletList(QObject *quickTextDocument, int cursorPosition)
{
    QTextDocument *document = resolveDocument(quickTextDocument);
    if (!document) {
        return false;
    }

    QTextCursor cursor(document);
    cursor.setPosition(qMax(0, cursorPosition));

    QTextListFormat fmt;
    fmt.setStyle(QTextListFormat::ListDisc);
    cursor.createList(fmt);
    return true;
}

bool RichTextFormatter::applyNumberedList(QObject *quickTextDocument, int cursorPosition)
{
    QTextDocument *document = resolveDocument(quickTextDocument);
    if (!document) {
        return false;
    }

    QTextCursor cursor(document);
    cursor.setPosition(qMax(0, cursorPosition));

    QTextListFormat fmt;
    fmt.setStyle(QTextListFormat::ListDecimal);
    cursor.createList(fmt);
    return true;
}

bool RichTextFormatter::toggleBlockQuote(QObject *quickTextDocument, int cursorPosition)
{
    QTextDocument *document = resolveDocument(quickTextDocument);
    if (!document) {
        return false;
    }

    QTextCursor cursor(document);
    cursor.setPosition(qMax(0, cursorPosition));
    cursor.select(QTextCursor::BlockUnderCursor);

    QTextBlockFormat fmt = cursor.blockFormat();
    if (fmt.leftMargin() > 0.0) {
        fmt.setLeftMargin(0.0);
        fmt.setBackground(Qt::transparent);
    } else {
        fmt.setLeftMargin(24.0);
        fmt.setBackground(QColor(QStringLiteral("#f6f8fc")));
    }

    cursor.setBlockFormat(fmt);
    return true;
}

bool RichTextFormatter::toggleCodeBlock(QObject *quickTextDocument, int cursorPosition)
{
    QTextDocument *document = resolveDocument(quickTextDocument);
    if (!document) {
        return false;
    }

    QTextCursor cursor(document);
    cursor.setPosition(qMax(0, cursorPosition));
    cursor.select(QTextCursor::BlockUnderCursor);

    QTextBlockFormat blockFormat = cursor.blockFormat();
    QTextCharFormat charFormat = cursor.charFormat();

    const bool enableCode = !charFormat.fontFixedPitch();
    if (enableCode) {
        blockFormat.setBackground(QColor(QStringLiteral("#f6f8fc")));
        blockFormat.setLeftMargin(16.0);
        charFormat.setFontFamilies({QStringLiteral("monospace")});
        charFormat.setFontFixedPitch(true);
    } else {
        blockFormat.setBackground(Qt::transparent);
        blockFormat.setLeftMargin(0.0);
        charFormat.setFontFixedPitch(false);
    }

    cursor.setBlockFormat(blockFormat);
    cursor.mergeCharFormat(charFormat);
    return true;
}

QString RichTextFormatter::richTextToMarkdown(const QString &richText) const
{
    return richText;
}

QString RichTextFormatter::markdownToRichText(const QString &markdown) const
{
    return markdown;
}