#include "search/ContentExtractor.h"
#include "core/AppConfig.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPdfDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

namespace
{
constexpr qint64 kMaxTextBytes = 1024 * 1024 * 8;
constexpr int kMaxPdfTextPages = 400;
constexpr int kMaxPdfOcrPages = 40;
constexpr qreal kOcrRenderDpi = 150.0;
constexpr int kWhisperTimeoutMs = 10 * 60 * 1000;
constexpr int kFfmpegTimeoutMs = 5 * 60 * 1000;
constexpr int kLibreOfficeTimeoutMs = 3 * 60 * 1000;

bool runProcess(const QString& programPath,
                const QStringList& arguments,
                int timeoutMs,
                QByteArray* stdOut,
                QByteArray* stdErr,
                QString* error);

QString normalizeExtractedText(const QString& text)
{
    QString normalized = text;
    normalized.replace("\r\n", "\n");
    normalized.replace('\r', '\n');
    normalized.replace(QRegularExpression(QStringLiteral("[\\t\\f\\v]+")), QStringLiteral(" "));
    normalized.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    return normalized.trimmed();
}

QString cacheKeyForFile(const QFileInfo& info)
{
    const QByteArray input = info.absoluteFilePath().toUtf8()
                             + '|'
                             + QByteArray::number(info.size())
                             + '|'
                             + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(input, QCryptographicHash::Sha1).toHex());
}

QString resolveFromEnv(const QString& envName)
{
    const QString envValue = qEnvironmentVariable(envName.toUtf8().constData()).trimmed();
    if (envValue.isEmpty()) {
        return QString();
    }

    const QFileInfo info(envValue);
    if (!info.exists() || !info.isFile()) {
        return QString();
    }

    return info.absoluteFilePath();
}

QString resolveExecutableFromEnv(const QString& envName, const QStringList& relativeCandidates)
{
    const QString envValue = qEnvironmentVariable(envName.toUtf8().constData()).trimmed();
    if (envValue.isEmpty()) {
        return QString();
    }

    const QFileInfo envInfo(envValue);
    if (envInfo.exists() && envInfo.isFile()) {
        return envInfo.absoluteFilePath();
    }

    if (envInfo.exists() && envInfo.isDir()) {
        const QDir root(envInfo.absoluteFilePath());
        for (const QString& relPath : relativeCandidates) {
            const QFileInfo candidate(root.filePath(relPath));
            if (candidate.exists() && candidate.isFile()) {
                return candidate.absoluteFilePath();
            }
        }
    }

    return QString();
}

QString resolveFromAppDirCandidates(const QStringList& relativeCandidates)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString& relPath : relativeCandidates) {
        const QFileInfo info(appDir + '/' + relPath);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return QString();
}

QString firstExistingFilePath(const QStringList& absoluteCandidates)
{
    for (const QString& candidatePath : absoluteCandidates) {
        const QFileInfo info(candidatePath);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return QString();
}

QString escapePowerShellSingleQuoted(const QString& value)
{
    QString escaped = value;
    escaped.replace('\'', QStringLiteral("''"));
    return QStringLiteral("'") + escaped + QStringLiteral("'");
}

QString bundledTesseractProgramPath()
{
    return resolveFromAppDirCandidates({
        QStringLiteral("tools/tesseract/tesseract.exe"),
        QStringLiteral("tesseract/tesseract.exe")
    });
}

QString resolveTesseractProgramPath()
{
    const QString envOverride = resolveFromEnv(QStringLiteral("ELLA_TESSERACT_PATH"));
    if (!envOverride.isEmpty()) {
        return envOverride;
    }

    const QString bundledPath = bundledTesseractProgramPath();
    if (!bundledPath.isEmpty()) {
        return bundledPath;
    }

    return QStandardPaths::findExecutable(QStringLiteral("tesseract"));
}

QString resolveFfmpegProgramPath()
{
    const QString envOverride = resolveExecutableFromEnv(
        QStringLiteral("ELLA_FFMPEG_PATH"),
        {
            QStringLiteral("ffmpeg.exe"),
            QStringLiteral("bin/ffmpeg.exe")
        });
    if (!envOverride.isEmpty()) {
        return envOverride;
    }

    const QString bundledPath = resolveFromAppDirCandidates({
        QStringLiteral("tools/ffmpeg/ffmpeg.exe"),
        QStringLiteral("ffmpeg/ffmpeg.exe")
    });
    if (!bundledPath.isEmpty()) {
        return bundledPath;
    }

    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

QString resolveWhisperProgramPath()
{
    const QString envOverride = resolveExecutableFromEnv(
        QStringLiteral("ELLA_WHISPER_PATH"),
        {
            QStringLiteral("whisper-cli.exe"),
            QStringLiteral("main.exe"),
            QStringLiteral("whisper.exe"),
            QStringLiteral("bin/whisper-cli.exe"),
            QStringLiteral("bin/main.exe"),
            QStringLiteral("bin/whisper.exe")
        });
    if (!envOverride.isEmpty()) {
        return envOverride;
    }

    const QString bundledPath = resolveFromAppDirCandidates({
        QStringLiteral("tools/whisper/whisper-cli.exe"),
        QStringLiteral("tools/whisper/main.exe"),
        QStringLiteral("whisper/whisper-cli.exe"),
        QStringLiteral("whisper/main.exe")
    });
    if (!bundledPath.isEmpty()) {
        return bundledPath;
    }

    QString found = QStandardPaths::findExecutable(QStringLiteral("whisper-cli"));
    if (!found.isEmpty()) {
        return found;
    }

    found = QStandardPaths::findExecutable(QStringLiteral("main"));
    if (!found.isEmpty()) {
        return found;
    }

    found = QStandardPaths::findExecutable(QStringLiteral("whisper"));
    return found;
}

QString resolveWhisperModelPath()
{
    const QString envOverride = resolveFromEnv(QStringLiteral("ELLA_WHISPER_MODEL_PATH"));
    if (!envOverride.isEmpty()) {
        return envOverride;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/tools/whisper/models/ggml-base.en.bin"),
        appDir + QStringLiteral("/tools/whisper/ggml-base.en.bin"),
        appDir + QStringLiteral("/whisper/models/ggml-base.en.bin"),
        appDir + QStringLiteral("/whisper/ggml-base.en.bin")
    };

    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }

    const QStringList modelDirs = {
        appDir + QStringLiteral("/tools/whisper/models"),
        appDir + QStringLiteral("/whisper/models")
    };
    for (const QString& dirPath : modelDirs) {
        QDir dir(dirPath);
        const QFileInfoList modelFiles =
            dir.entryInfoList({QStringLiteral("*.bin")}, QDir::Files, QDir::Name);
        if (!modelFiles.isEmpty()) {
            return modelFiles.first().absoluteFilePath();
        }
    }

    return QString();
}

QString resolveLibreOfficeProgramPath()
{
    const QString envOverride = resolveExecutableFromEnv(
        QStringLiteral("ELLA_LIBREOFFICE_PATH"),
        {
            QStringLiteral("program/soffice.exe"),
            QStringLiteral("program/soffice.com"),
            QStringLiteral("soffice.exe"),
            QStringLiteral("soffice.com")
        });
    if (!envOverride.isEmpty()) {
        return envOverride;
    }

    const QString bundledPath = resolveFromAppDirCandidates({
        QStringLiteral("tools/libreoffice/program/soffice.exe"),
        QStringLiteral("tools/libreoffice/program/soffice.com"),
        QStringLiteral("libreoffice/program/soffice.exe"),
        QStringLiteral("libreoffice/program/soffice.com")
    });
    if (!bundledPath.isEmpty()) {
        return bundledPath;
    }

    const QString programFiles = qEnvironmentVariable("ProgramFiles").trimmed();
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)").trimmed();
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA").trimmed();
    const QString userProfile = qEnvironmentVariable("USERPROFILE").trimmed();

    QStringList commonCandidates;
    if (!programFiles.isEmpty()) {
        commonCandidates << (QDir(programFiles).filePath(QStringLiteral("LibreOffice/program/soffice.com")))
                         << (QDir(programFiles).filePath(QStringLiteral("LibreOffice/program/soffice.exe")));
    } else {
        commonCandidates << QStringLiteral("C:/Program Files/LibreOffice/program/soffice.com")
                         << QStringLiteral("C:/Program Files/LibreOffice/program/soffice.exe");
    }

    if (!programFilesX86.isEmpty()) {
        commonCandidates << (QDir(programFilesX86).filePath(QStringLiteral("LibreOffice/program/soffice.com")))
                         << (QDir(programFilesX86).filePath(QStringLiteral("LibreOffice/program/soffice.exe")));
    } else {
        commonCandidates << QStringLiteral("C:/Program Files (x86)/LibreOffice/program/soffice.com")
                         << QStringLiteral("C:/Program Files (x86)/LibreOffice/program/soffice.exe");
    }

    if (!localAppData.isEmpty()) {
        commonCandidates << (QDir(localAppData).filePath(QStringLiteral("Programs/LibreOffice/program/soffice.com")))
                         << (QDir(localAppData).filePath(QStringLiteral("Programs/LibreOffice/program/soffice.exe")));
    }

    if (!userProfile.isEmpty()) {
        commonCandidates << (QDir(userProfile).filePath(QStringLiteral("scoop/apps/libreoffice/current/program/soffice.com")))
                         << (QDir(userProfile).filePath(QStringLiteral("scoop/apps/libreoffice/current/program/soffice.exe")));
    }

    const QString commonPath = firstExistingFilePath(commonCandidates);
    if (!commonPath.isEmpty()) {
        return commonPath;
    }

    QString found = QStandardPaths::findExecutable(QStringLiteral("soffice.com"));
    if (!found.isEmpty()) {
        return found;
    }

    found = QStandardPaths::findExecutable(QStringLiteral("soffice.exe"));
    if (!found.isEmpty()) {
        return found;
    }

    return QStandardPaths::findExecutable(QStringLiteral("soffice"));
}

QString resolvePowerShellProgramPath()
{
    QString found = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    if (!found.isEmpty()) {
        return found;
    }

    found = firstExistingFilePath({
        QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe")
    });
    if (!found.isEmpty()) {
        return found;
    }

    return QStandardPaths::findExecutable(QStringLiteral("pwsh.exe"));
}

QString resolvePowerPointProgramPath()
{
    const QString envOverride = resolveExecutableFromEnv(
        QStringLiteral("ELLA_POWERPOINT_PATH"),
        {
            QStringLiteral("POWERPNT.EXE"),
            QStringLiteral("powerpnt.exe"),
            QStringLiteral("root/Office16/POWERPNT.EXE"),
            QStringLiteral("root/Office15/POWERPNT.EXE"),
            QStringLiteral("Office16/POWERPNT.EXE"),
            QStringLiteral("Office15/POWERPNT.EXE"),
            QStringLiteral("Office14/POWERPNT.EXE")
        });
    if (!envOverride.isEmpty()) {
        return envOverride;
    }

    const QString programFiles = qEnvironmentVariable("ProgramFiles").trimmed();
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)").trimmed();
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA").trimmed();

    QStringList candidates;
    if (!programFiles.isEmpty()) {
        const QDir root(programFiles);
        candidates << root.filePath(QStringLiteral("Microsoft Office/root/Office16/POWERPNT.EXE"))
                   << root.filePath(QStringLiteral("Microsoft Office/Office16/POWERPNT.EXE"))
                   << root.filePath(QStringLiteral("Microsoft Office/Office15/POWERPNT.EXE"))
                   << root.filePath(QStringLiteral("Microsoft Office/Office14/POWERPNT.EXE"));
    }

    if (!programFilesX86.isEmpty()) {
        const QDir root(programFilesX86);
        candidates << root.filePath(QStringLiteral("Microsoft Office/root/Office16/POWERPNT.EXE"))
                   << root.filePath(QStringLiteral("Microsoft Office/Office16/POWERPNT.EXE"))
                   << root.filePath(QStringLiteral("Microsoft Office/Office15/POWERPNT.EXE"))
                   << root.filePath(QStringLiteral("Microsoft Office/Office14/POWERPNT.EXE"));
    }

    if (!localAppData.isEmpty()) {
        const QDir root(localAppData);
        candidates << root.filePath(QStringLiteral("Microsoft/WindowsApps/powerpnt.exe"));
    }

    return firstExistingFilePath(candidates);
}

bool convertPresentationWithPowerPoint(const QString& sourceFilePath,
                                       const QString& outputDirectoryPath,
                                       QString* error)
{
    if (error) {
        error->clear();
    }

    const QString powershellPath = resolvePowerShellProgramPath();
    if (powershellPath.isEmpty()) {
        if (error) {
            *error = QStringLiteral("PowerShell runtime is unavailable for PowerPoint conversion");
        }
        return false;
    }

    const QString script = QStringLiteral(
        "$ErrorActionPreference='Stop';"
        "$src=%1;"
        "$outDir=%2;"
        "$base=[System.IO.Path]::GetFileNameWithoutExtension($src);"
        "$pdf=[System.IO.Path]::Combine($outDir, $base + '.pdf');"
        "$pp=$null; $pres=$null;"
        "try {"
        "  $pp=New-Object -ComObject PowerPoint.Application;"
        "  $pp.Visible=0;"
        "  $pres=$pp.Presentations.Open($src, $true, $false, $false);"
        "  $pres.SaveAs($pdf, 32);"
        "  $pres.Close();"
        "  $pp.Quit();"
        "  [Console]::Out.Write($pdf);"
        "} catch {"
        "  try { if ($pres -ne $null) { $pres.Close() } } catch {};"
        "  try { if ($pp -ne $null) { $pp.Quit() } } catch {};"
        "  throw"
        "}")
            .arg(escapePowerShellSingleQuoted(sourceFilePath),
                 escapePowerShellSingleQuoted(outputDirectoryPath));

    QByteArray outBytes;
    QByteArray errBytes;
    QString processError;
    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"),
        QStringLiteral("Bypass"),
        QStringLiteral("-Command"),
        script
    };

    if (!runProcess(powershellPath, args, kLibreOfficeTimeoutMs, &outBytes, &errBytes, &processError)) {
        if (error) {
            *error = QStringLiteral("PowerPoint conversion failed: %1").arg(processError);
        }
        return false;
    }

    return true;
}

QString resolveTessdataRoot(const QString& tesseractProgramPath)
{
    if (tesseractProgramPath.isEmpty()) {
        return QString();
    }

    const QDir programDir = QFileInfo(tesseractProgramPath).dir();
    if (programDir.exists(QStringLiteral("tessdata"))) {
        return programDir.absoluteFilePath(QStringLiteral("tessdata"));
    }

    return QString();
}

bool runProcess(const QString& programPath,
                const QStringList& arguments,
                int timeoutMs,
                QByteArray* stdOut,
                QByteArray* stdErr,
                QString* error)
{
    QProcess process;
    process.start(programPath, arguments);

    if (!process.waitForStarted(5000)) {
        if (error) {
            *error = QStringLiteral("Unable to start process: %1").arg(programPath);
        }
        return false;
    }

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(3000);
        if (error) {
            *error = QStringLiteral("Process timed out: %1").arg(QFileInfo(programPath).fileName());
        }
        return false;
    }

    const QByteArray outBytes = process.readAllStandardOutput();
    const QByteArray errBytes = process.readAllStandardError();
    if (stdOut) {
        *stdOut = outBytes;
    }
    if (stdErr) {
        *stdErr = errBytes;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            const QString stderrText = QString::fromUtf8(errBytes).trimmed();
            *error = QStringLiteral("%1 failed%2")
                         .arg(QFileInfo(programPath).fileName(),
                              stderrText.isEmpty() ? QString() : QStringLiteral(": ") + stderrText);
        }
        return false;
    }

    return true;
}

QString parseSrtToTimestampedTranscript(const QString& srtContent)
{
    const QRegularExpression timeRange(
        QStringLiteral("^(\\d{2}):(\\d{2}):(\\d{2})[,\\.]\\d{3}\\s+-->"));

    QString currentTag;
    QStringList blockLines;
    QStringList outputLines;

    auto flushBlock = [&]() {
        const QString content = normalizeExtractedText(blockLines.join(' '));
        blockLines.clear();
        if (content.isEmpty()) {
            currentTag.clear();
            return;
        }

        if (!currentTag.isEmpty()) {
            outputLines.append(currentTag + QStringLiteral(" ") + content);
        } else {
            outputLines.append(content);
        }

        currentTag.clear();
    };

    const QStringList lines = srtContent.split('\n');
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            flushBlock();
            continue;
        }

        const QRegularExpressionMatch match = timeRange.match(line);
        if (match.hasMatch()) {
            currentTag = QStringLiteral("[%1:%2:%3]")
                             .arg(match.captured(1), match.captured(2), match.captured(3));
            continue;
        }

        bool isIndexLine = false;
        line.toInt(&isIndexLine);
        if (isIndexLine && blockLines.isEmpty()) {
            continue;
        }

        blockLines.append(line);
    }

    flushBlock();
    return normalizeExtractedText(outputLines.join('\n'));
}

QString readTextFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

bool writeTextFile(const QString& path, const QString& text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    file.write(text.toUtf8());
    file.close();
    return true;
}
}

ContentExtractor::Result ContentExtractor::extract(const QString& filePath,
                                                   const QString& mimeType,
                                                   const QString& extension) const
{
    Result result;

    const QString normalizedMime = mimeType.trimmed().toLower();
    const QString normalizedExt = extension.trimmed().toLower();

    if (isTextLike(normalizedMime, normalizedExt)) {
        result.text = extractTextFile(filePath, &result.error);
        result.extractor = QStringLiteral("text");
        return result;
    }

    if (isPresentationFileType(normalizedMime, normalizedExt)) {
        QString conversionError;
        const QString pdfPath = ensurePresentationPdf(filePath, &conversionError);
        if (pdfPath.isEmpty()) {
            result.extractor = QStringLiteral("ppt-pdf");
            result.error = conversionError.isEmpty()
                               ? QStringLiteral("Unable to convert presentation to PDF")
                               : conversionError;
            return result;
        }

        result.text = extractPdfText(pdfPath, &result.error);
        result.extractor = QStringLiteral("ppt-pdf");

        if (result.text.trimmed().isEmpty()) {
            QString ocrError;
            const QString ocrText = extractPdfWithTesseract(pdfPath, &ocrError);
            if (!ocrText.trimmed().isEmpty()) {
                result.text = ocrText;
                result.extractor = QStringLiteral("ppt-pdf-ocr");
                result.error.clear();
            } else if (!ocrError.trimmed().isEmpty()) {
                result.error = ocrError;
            }
        }

        return result;
    }

    if (normalizedMime == QStringLiteral("application/pdf") || normalizedExt == QStringLiteral("pdf")) {
        result.text = extractPdfText(filePath, &result.error);
        result.extractor = QStringLiteral("pdf");

        if (result.text.trimmed().isEmpty()) {
            QString ocrError;
            const QString ocrText = extractPdfWithTesseract(filePath, &ocrError);
            if (!ocrText.trimmed().isEmpty()) {
                result.text = ocrText;
                result.extractor = QStringLiteral("pdf-ocr");
                result.error.clear();
            } else if (!ocrError.trimmed().isEmpty()) {
                result.error = ocrError;
            }
        }

        return result;
    }

    if (normalizedMime.startsWith(QStringLiteral("image/"))) {
        result.text = extractWithTesseract(filePath, &result.error);
        result.extractor = QStringLiteral("image-ocr");
        return result;
    }

    if (isVideoFileType(normalizedMime, normalizedExt)) {
        result.text = extractMediaTranscript(filePath, true, &result.error);
        result.extractor = QStringLiteral("video-whisper");
        return result;
    }

    if (isAudioFileType(normalizedMime, normalizedExt)) {
        result.text = extractMediaTranscript(filePath, false, &result.error);
        result.extractor = QStringLiteral("audio-whisper");
        return result;
    }

    result.extractor = QStringLiteral("none");
    result.error = QStringLiteral("No extractor available for this file type");
    return result;
}

QString ContentExtractor::normalizeTranscriptFromSrt(const QString& srtContent)
{
    return parseSrtToTimestampedTranscript(srtContent);
}

QString ContentExtractor::presentationCachePathForFile(const QString& filePath)
{
    const QFileInfo sourceInfo(filePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        return QString();
    }

    return QDir(AppConfig::presentationCacheDirectory())
        .filePath(cacheKeyForFile(sourceInfo) + QStringLiteral(".pdf"));
}

bool ContentExtractor::hasExecutable(const QString& program) const
{
    const QString trimmed = program.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    const QFileInfo fileInfo(trimmed);
    if (fileInfo.isAbsolute() || trimmed.contains('/') || trimmed.contains('\\')) {
        return fileInfo.exists() && fileInfo.isFile();
    }

    return !QStandardPaths::findExecutable(trimmed).isEmpty();
}

bool ContentExtractor::isTextLike(const QString& mimeType, const QString& extension) const
{
    if (mimeType.startsWith(QStringLiteral("text/"))) {
        return true;
    }

    static const QStringList textExtensions = {
        QStringLiteral("txt"),
        QStringLiteral("md"),
        QStringLiteral("markdown"),
        QStringLiteral("csv"),
        QStringLiteral("json"),
        QStringLiteral("xml"),
        QStringLiteral("yaml"),
        QStringLiteral("yml"),
        QStringLiteral("qml"),
        QStringLiteral("cpp"),
        QStringLiteral("cc"),
        QStringLiteral("c"),
        QStringLiteral("h"),
        QStringLiteral("hpp"),
        QStringLiteral("js"),
        QStringLiteral("ts"),
        QStringLiteral("py"),
        QStringLiteral("java"),
        QStringLiteral("go"),
        QStringLiteral("rs"),
        QStringLiteral("ini"),
        QStringLiteral("cfg"),
        QStringLiteral("conf"),
        QStringLiteral("log"),
        QStringLiteral("sql"),
        QStringLiteral("ellanote")
    };

    return textExtensions.contains(extension);
}

bool ContentExtractor::isPresentationFileType(const QString& mimeType, const QString& extension) const
{
    if (mimeType == QStringLiteral("application/vnd.ms-powerpoint")
        || mimeType == QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.presentation")
        || mimeType == QStringLiteral("application/vnd.oasis.opendocument.presentation")) {
        return true;
    }

    static const QStringList presentationExtensions = {
        QStringLiteral("ppt"),
        QStringLiteral("pptx"),
        QStringLiteral("odp")
    };
    return presentationExtensions.contains(extension);
}

bool ContentExtractor::isVideoFileType(const QString& mimeType, const QString& extension) const
{
    if (mimeType.startsWith(QStringLiteral("video/"))) {
        return true;
    }

    static const QStringList videoExtensions = {
        QStringLiteral("mp4"),
        QStringLiteral("mkv"),
        QStringLiteral("mov"),
        QStringLiteral("avi"),
        QStringLiteral("wmv"),
        QStringLiteral("webm"),
        QStringLiteral("m4v")
    };
    return videoExtensions.contains(extension);
}

bool ContentExtractor::isAudioFileType(const QString& mimeType, const QString& extension) const
{
    if (mimeType.startsWith(QStringLiteral("audio/"))) {
        return true;
    }

    static const QStringList audioExtensions = {
        QStringLiteral("mp3"),
        QStringLiteral("wav"),
        QStringLiteral("m4a"),
        QStringLiteral("aac"),
        QStringLiteral("flac"),
        QStringLiteral("ogg"),
        QStringLiteral("opus"),
        QStringLiteral("wma")
    };
    return audioExtensions.contains(extension);
}

QString ContentExtractor::extractTextFile(const QString& filePath, QString* error) const
{
    if (error) {
        error->clear();
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Unable to open text file for reading");
        }
        return QString();
    }

    QByteArray data = file.read(kMaxTextBytes + 1);
    const bool truncated = data.size() > kMaxTextBytes;
    if (truncated) {
        data.chop(1);
    }

    QString text = QString::fromUtf8(data);
    if (text.isEmpty() && !data.isEmpty()) {
        text = QString::fromLocal8Bit(data);
    }

    if (truncated) {
        text += QStringLiteral("\n\n[Truncated for indexing at 8 MB]");
    }

    return normalizeExtractedText(text);
}

QString ContentExtractor::extractPdfText(const QString& filePath, QString* error) const
{
    if (error) {
        error->clear();
    }

    QPdfDocument document;
    const QPdfDocument::Error loadError = document.load(filePath);
    if (loadError != QPdfDocument::Error::None) {
        if (error) {
            *error = QStringLiteral("Unable to read PDF text (error code %1)")
                         .arg(static_cast<int>(loadError));
        }
        return QString();
    }

    if (document.status() != QPdfDocument::Status::Ready || document.pageCount() <= 0) {
        if (error) {
            *error = QStringLiteral("PDF is not ready for text extraction");
        }
        return QString();
    }

    const int totalPages = document.pageCount();
    const int pagesToRead = qMin(totalPages, kMaxPdfTextPages);
    QStringList pageTexts;
    pageTexts.reserve(pagesToRead);

    for (int page = 0; page < pagesToRead; ++page) {
        const QString pageText = normalizeExtractedText(document.getAllText(page).text());
        if (!pageText.isEmpty()) {
            pageTexts.append(pageText);
        }
    }

    if (totalPages > pagesToRead) {
        pageTexts.append(
            QStringLiteral("[PDF text extraction truncated to first %1 pages]").arg(pagesToRead));
    }

    if (pageTexts.isEmpty()) {
        if (error) {
            *error = QStringLiteral("No selectable text found in PDF");
        }
        return QString();
    }

    return normalizeExtractedText(pageTexts.join(QStringLiteral("\n\n")));
}

QString ContentExtractor::extractPdfWithTesseract(const QString& filePath, QString* error) const
{
    if (error) {
        error->clear();
    }

    QPdfDocument document;
    const QPdfDocument::Error loadError = document.load(filePath);
    if (loadError != QPdfDocument::Error::None) {
        if (error) {
            *error = QStringLiteral("Unable to load PDF for OCR (error code %1)")
                         .arg(static_cast<int>(loadError));
        }
        return QString();
    }

    if (document.status() != QPdfDocument::Status::Ready || document.pageCount() <= 0) {
        if (error) {
            *error = QStringLiteral("PDF is not ready for OCR");
        }
        return QString();
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (error) {
            *error = QStringLiteral("Unable to create temporary directory for PDF OCR");
        }
        return QString();
    }

    const int totalPages = document.pageCount();
    const int pagesToRead = qMin(totalPages, kMaxPdfOcrPages);
    QStringList pageTexts;
    pageTexts.reserve(pagesToRead);
    QString firstError;

    for (int page = 0; page < pagesToRead; ++page) {
        const QSizeF pageSizePt = document.pagePointSize(page);
        if (!pageSizePt.isValid() || pageSizePt.isEmpty()) {
            continue;
        }

        const QSize imageSize(
            qMax(800, static_cast<int>((pageSizePt.width() / 72.0) * kOcrRenderDpi)),
            qMax(800, static_cast<int>((pageSizePt.height() / 72.0) * kOcrRenderDpi)));

        const QImage renderedPage = document.render(page, imageSize);
        if (renderedPage.isNull()) {
            continue;
        }

        const QString imagePath =
            tempDir.filePath(QStringLiteral("page_%1.png").arg(page + 1, 4, 10, QChar('0')));
        if (!renderedPage.save(imagePath, "PNG")) {
            continue;
        }

        QString pageError;
        const QString pageText = extractWithTesseract(imagePath, &pageError);
        if (!pageText.trimmed().isEmpty()) {
            pageTexts.append(pageText);
        } else if (firstError.isEmpty() && !pageError.trimmed().isEmpty()) {
            firstError = pageError;
        }
    }

    if (totalPages > pagesToRead) {
        pageTexts.append(
            QStringLiteral("[PDF OCR truncated to first %1 pages]").arg(pagesToRead));
    }

    if (pageTexts.isEmpty()) {
        if (error) {
            *error = firstError.isEmpty()
                         ? QStringLiteral("OCR did not return text for this PDF")
                         : firstError;
        }
        return QString();
    }

    return normalizeExtractedText(pageTexts.join(QStringLiteral("\n\n")));
}

QString ContentExtractor::ensurePresentationPdf(const QString& filePath, QString* error) const
{
    if (error) {
        error->clear();
    }

    const QFileInfo sourceInfo(filePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        if (error) {
            *error = QStringLiteral("Presentation file does not exist");
        }
        return QString();
    }

    const QString cachePath = presentationCachePathForFile(filePath);
    const QFileInfo cachedInfo(cachePath);
    if (cachedInfo.exists() && cachedInfo.isFile() && cachedInfo.size() > 0) {
        return cachedInfo.absoluteFilePath();
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (error) {
            *error = QStringLiteral("Unable to create temporary directory for PPT conversion");
        }
        return QString();
    }

    auto resolveConvertedInfo = [&]() -> QFileInfo {
        const QString expectedPath =
            tempDir.filePath(sourceInfo.completeBaseName() + QStringLiteral(".pdf"));
        QFileInfo expectedInfo(expectedPath);
        if (expectedInfo.exists() && expectedInfo.isFile()) {
            return expectedInfo;
        }

        const QDir outDir(tempDir.path());
        const QFileInfoList pdfFiles =
            outDir.entryInfoList({QStringLiteral("*.pdf")}, QDir::Files, QDir::Time);
        if (!pdfFiles.isEmpty()) {
            return pdfFiles.first();
        }

        return QFileInfo();
    };

    auto waitForConvertedInfo = [&]() -> QFileInfo {
        QFileInfo info = resolveConvertedInfo();
        if (info.exists() && info.isFile() && info.size() > 0) {
            return info;
        }

        const int retryCount = 100;
        for (int attempt = 0; attempt < retryCount; ++attempt) {
            QThread::msleep(200);
            info = resolveConvertedInfo();
            if (info.exists() && info.isFile() && info.size() > 0) {
                return info;
            }
        }

        return QFileInfo();
    };

    QFileInfo convertedInfo;
    QStringList conversionErrors;

    const QString libreOfficePath = resolveLibreOfficeProgramPath();
    if (!libreOfficePath.isEmpty() && hasExecutable(libreOfficePath)) {
        QByteArray outBytes;
        QByteArray errBytes;
        QString processError;
        const QStringList args = {
            QStringLiteral("--headless"),
            QStringLiteral("--nologo"),
            QStringLiteral("--nofirststartwizard"),
            QStringLiteral("--convert-to"),
            QStringLiteral("pdf"),
            QStringLiteral("--outdir"),
            tempDir.path(),
            sourceInfo.absoluteFilePath()
        };

        if (runProcess(libreOfficePath,
                       args,
                       kLibreOfficeTimeoutMs,
                       &outBytes,
                       &errBytes,
                       &processError)) {
            convertedInfo = waitForConvertedInfo();
            if (!(convertedInfo.exists() && convertedInfo.isFile() && convertedInfo.size() > 0)) {
                QString detail = QStringLiteral("LibreOffice completed but no PDF output was generated");
                const QString stdOutText = QString::fromUtf8(outBytes).trimmed();
                const QString stdErrText = QString::fromUtf8(errBytes).trimmed();
                if (!stdErrText.isEmpty()) {
                    detail += QStringLiteral(": ") + stdErrText;
                } else if (!stdOutText.isEmpty()) {
                    detail += QStringLiteral(": ") + stdOutText;
                }
                conversionErrors.append(detail);
            }
        } else {
            conversionErrors.append(QStringLiteral("LibreOffice conversion failed: %1").arg(processError));
        }
    } else {
        conversionErrors.append(
            QStringLiteral("LibreOffice is not available (set ELLA_LIBREOFFICE_PATH or bundle tools/libreoffice)"));
    }

    if (!(convertedInfo.exists() && convertedInfo.isFile() && convertedInfo.size() > 0)) {
        QString powerpointError;
        const bool powerpointOk = convertPresentationWithPowerPoint(
            sourceInfo.absoluteFilePath(),
            tempDir.path(),
            &powerpointError);
        if (powerpointOk) {
            convertedInfo = waitForConvertedInfo();
            if (!(convertedInfo.exists() && convertedInfo.isFile() && convertedInfo.size() > 0)) {
                conversionErrors.append(
                    QStringLiteral("PowerPoint conversion completed but no PDF output was generated"));
            }
        } else {
            conversionErrors.append(powerpointError);
        }
    }

    if (!(convertedInfo.exists() && convertedInfo.isFile() && convertedInfo.size() > 0)) {
        if (error) {
            *error = conversionErrors.isEmpty()
                         ? QStringLiteral("Presentation conversion failed")
                         : conversionErrors.join(QStringLiteral(" | "));
        }
        return QString();
    }

    QFile::remove(cachePath);
    if (!QFile::copy(convertedInfo.absoluteFilePath(), cachePath)) {
        if (error) {
            *error = QStringLiteral("Failed to store converted presentation PDF in cache");
        }
        return QString();
    }

    return QFileInfo(cachePath).absoluteFilePath();
}

QString ContentExtractor::extractMediaTranscript(const QString& filePath,
                                                 bool fromVideo,
                                                 QString* error) const
{
    if (error) {
        error->clear();
    }

    const QFileInfo sourceInfo(filePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        if (error) {
            *error = QStringLiteral("Media file does not exist");
        }
        return QString();
    }

    const QString transcriptPath =
        QDir(AppConfig::transcriptCacheDirectory()).filePath(cacheKeyForFile(sourceInfo) + ".txt");
    const QString cachedTranscript = readTextFile(transcriptPath);
    if (!cachedTranscript.trimmed().isEmpty()) {
        return normalizeExtractedText(cachedTranscript);
    }

    const QString ffmpegPath = resolveFfmpegProgramPath();
    if (ffmpegPath.isEmpty() || !hasExecutable(ffmpegPath)) {
        if (error) {
            *error = QStringLiteral(
                "ffmpeg is not available (set ELLA_FFMPEG_PATH or bundle tools/ffmpeg)");
        }
        return QString();
    }

    const QString whisperPath = resolveWhisperProgramPath();
    if (whisperPath.isEmpty() || !hasExecutable(whisperPath)) {
        if (error) {
            *error = QStringLiteral(
                "whisper executable is not available (set ELLA_WHISPER_PATH or bundle tools/whisper)");
        }
        return QString();
    }

    const QString modelPath = resolveWhisperModelPath();
    if (modelPath.isEmpty() || !hasExecutable(modelPath)) {
        if (error) {
            *error = QStringLiteral(
                "whisper model is missing (set ELLA_WHISPER_MODEL_PATH or bundle ggml-base.en.bin)");
        }
        return QString();
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (error) {
            *error = QStringLiteral("Unable to create temporary directory for transcription");
        }
        return QString();
    }

    const QString audioPath = tempDir.filePath(QStringLiteral("audio.wav"));
    QStringList ffmpegArgs = {
        QStringLiteral("-y"),
        QStringLiteral("-i"),
        sourceInfo.absoluteFilePath()
    };
    if (fromVideo) {
        ffmpegArgs.append(QStringLiteral("-vn"));
    }
    ffmpegArgs << QStringLiteral("-ac")
               << QStringLiteral("1")
               << QStringLiteral("-ar")
               << QStringLiteral("16000")
               << QStringLiteral("-f")
               << QStringLiteral("wav")
               << audioPath;

    QByteArray ffmpegOut;
    QByteArray ffmpegErr;
    QString ffmpegError;
    if (!runProcess(ffmpegPath, ffmpegArgs, kFfmpegTimeoutMs, &ffmpegOut, &ffmpegErr, &ffmpegError)) {
        if (error) {
            *error = ffmpegError;
        }
        return QString();
    }

    const QString whisperOutputBase = tempDir.filePath(QStringLiteral("transcript"));
    const QStringList whisperArgs = {
        QStringLiteral("-m"),
        modelPath,
        QStringLiteral("-f"),
        audioPath,
        QStringLiteral("-l"),
        QStringLiteral("en"),
        QStringLiteral("-otxt"),
        QStringLiteral("-osrt"),
        QStringLiteral("-of"),
        whisperOutputBase
    };

    QByteArray whisperOut;
    QByteArray whisperErr;
    QString whisperError;
    if (!runProcess(whisperPath,
                    whisperArgs,
                    kWhisperTimeoutMs,
                    &whisperOut,
                    &whisperErr,
                    &whisperError)) {
        if (error) {
            *error = whisperError;
        }
        return QString();
    }

    const QString srtPath = whisperOutputBase + QStringLiteral(".srt");
    const QString txtPath = whisperOutputBase + QStringLiteral(".txt");
    QString transcript = parseSrtToTimestampedTranscript(readTextFile(srtPath));
    if (transcript.trimmed().isEmpty()) {
        transcript = normalizeExtractedText(readTextFile(txtPath));
    }

    if (transcript.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("whisper did not return transcript text");
        }
        return QString();
    }

    writeTextFile(transcriptPath, transcript);
    return transcript;
}

QVariantMap ContentExtractor::runtimeEnvironmentStatus()
{
    QVariantMap info;

    const QString tesseractPath = resolveTesseractProgramPath();
    const QString tessdataRoot = resolveTessdataRoot(tesseractPath);
    QString ocrSource = QStringLiteral("missing");
    if (!tesseractPath.isEmpty()) {
        const QString envPath = resolveFromEnv(QStringLiteral("ELLA_TESSERACT_PATH"));
        if (!envPath.isEmpty()
            && QFileInfo(envPath).absoluteFilePath().compare(
                   QFileInfo(tesseractPath).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
            ocrSource = QStringLiteral("env");
        } else if (tesseractPath.contains(QStringLiteral("/tools/tesseract/"), Qt::CaseInsensitive)
                   || tesseractPath.contains(QStringLiteral("\\tools\\tesseract\\"), Qt::CaseInsensitive)) {
            ocrSource = QStringLiteral("bundled");
        } else {
            ocrSource = QStringLiteral("system");
        }
    }

    const bool tessdataReady = !tesseractPath.isEmpty()
                               && (!tessdataRoot.isEmpty() || ocrSource == QStringLiteral("system"));

    info[QStringLiteral("ocrAvailable")] = !tesseractPath.isEmpty();
    info[QStringLiteral("ocrPath")] = tesseractPath;
    info[QStringLiteral("tessdataPath")] = tessdataRoot;
    info[QStringLiteral("tessdataReady")] = tessdataReady;
    info[QStringLiteral("ocrSource")] = ocrSource;

    const QString ffmpegPath = resolveFfmpegProgramPath();
    const QString whisperPath = resolveWhisperProgramPath();
    const QString whisperModelPath = resolveWhisperModelPath();
    const QString libreOfficePath = resolveLibreOfficeProgramPath();
    const QString powerPointPath = resolvePowerPointProgramPath();
    const QString powerShellPath = resolvePowerShellProgramPath();

    info[QStringLiteral("ffmpegAvailable")] = !ffmpegPath.isEmpty();
    info[QStringLiteral("ffmpegPath")] = ffmpegPath;
    info[QStringLiteral("whisperAvailable")] = !whisperPath.isEmpty();
    info[QStringLiteral("whisperPath")] = whisperPath;
    info[QStringLiteral("whisperModelAvailable")] = !whisperModelPath.isEmpty();
    info[QStringLiteral("whisperModelPath")] = whisperModelPath;
    info[QStringLiteral("mediaTranscriptionReady")] =
        !ffmpegPath.isEmpty() && !whisperPath.isEmpty() && !whisperModelPath.isEmpty();
    info[QStringLiteral("libreOfficeAvailable")] = !libreOfficePath.isEmpty();
    info[QStringLiteral("libreOfficePath")] = libreOfficePath;
    info[QStringLiteral("powerPointAvailable")] = !powerPointPath.isEmpty();
    info[QStringLiteral("powerPointPath")] = powerPointPath;
    info[QStringLiteral("powerShellAvailable")] = !powerShellPath.isEmpty();
    info[QStringLiteral("powerShellPath")] = powerShellPath;
    info[QStringLiteral("pptConversionReady")] = !libreOfficePath.isEmpty()
                                                 || (!powerPointPath.isEmpty() && !powerShellPath.isEmpty());

    return info;
}

QString ContentExtractor::extractWithTesseract(const QString& filePath, QString* error) const
{
    if (error) {
        error->clear();
    }

    const QString programPath = resolveTesseractProgramPath();
    if (!hasExecutable(programPath)) {
        if (error) {
            *error = QStringLiteral(
                "tesseract is not available (bundle tools/tesseract or install it on PATH)");
        }
        return QString();
    }

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

    const QString tessdataRoot = resolveTessdataRoot(programPath);
    if (!tessdataRoot.isEmpty()) {
        environment.insert(QStringLiteral("TESSDATA_PREFIX"), tessdataRoot);
    }
    process.setProcessEnvironment(environment);

    QStringList arguments;
    arguments << filePath
              << QStringLiteral("stdout")
              << QStringLiteral("-l")
              << QStringLiteral("eng");

    process.start(programPath, arguments);
    if (!process.waitForStarted(3000)) {
        if (error) {
            *error = QStringLiteral("Unable to start tesseract OCR process");
        }
        return QString();
    }

    if (!process.waitForFinished(180000)) {
        process.kill();
        if (error) {
            *error = QStringLiteral("tesseract timed out");
        }
        return QString();
    }

    const QByteArray stdoutBytes = process.readAllStandardOutput();
    const QByteArray stderrBytes = process.readAllStandardError();

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            *error = QStringLiteral("tesseract failed: %1")
                         .arg(QString::fromUtf8(stderrBytes).trimmed());
        }
        return QString();
    }

    return normalizeExtractedText(QString::fromUtf8(stdoutBytes));
}
