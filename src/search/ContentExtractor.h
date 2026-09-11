#pragma once

#include <QString>
#include <QVariantMap>
#include <QVariantList>

class ContentExtractor
{
public:
    struct Result
    {
        QString text;
        QString extractor;
        QString error;
        QVariantList passages;
    };

    Result extract(const QString& filePath,
                   const QString& mimeType,
                   const QString& extension) const;
    QString ensurePresentationPdf(const QString& filePath, QString* error = nullptr) const;
    static QVariantMap runtimeEnvironmentStatus();
    static QString normalizeTranscriptFromSrt(const QString& srtContent);
    static QString presentationCachePathForFile(const QString& filePath);

private:
    Result extractRaw(const QString& filePath, const QString& mimeType, const QString& extension) const;
    Result extractOfficeXml(const QString& filePath, const QString& extension) const;
    bool hasExecutable(const QString& program) const;
    bool isTextLike(const QString& mimeType, const QString& extension) const;
    bool isPresentationFileType(const QString& mimeType, const QString& extension) const;
    bool isVideoFileType(const QString& mimeType, const QString& extension) const;
    bool isAudioFileType(const QString& mimeType, const QString& extension) const;
    QString extractTextFile(const QString& filePath, QString* error) const;
    QString extractPdfText(const QString& filePath, QString* error, QVariantList* passages = nullptr) const;
    QString extractPdfWithTesseract(const QString& filePath, QString* error, QVariantList* passages = nullptr) const;
    QString extractWithTesseract(const QString& filePath, QString* error) const;
    QString extractMediaTranscript(const QString& filePath, bool fromVideo, QString* error) const;
};
