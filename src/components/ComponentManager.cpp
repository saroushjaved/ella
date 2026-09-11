#include "ComponentManager.h"
#include "core/AppConfig.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>

namespace {
QString setting(const QString& id) { return QStringLiteral("components/local/") + id; }
// This script is owned by the application, never supplied by a downloaded package.
// No downloaded script or executable is run during installation.
const char extractScript[] = R"PS(
param([string]$Archive,[string]$Destination,[long]$Limit)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = [IO.Path]::GetFullPath($Destination).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
[IO.Directory]::CreateDirectory($root) | Out-Null
$zip = [IO.Compression.ZipFile]::OpenRead($Archive)
try {
  if ($zip.Entries.Count -gt 100000) { throw 'Archive contains too many files' }
  $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
  [long]$total = 0
  foreach ($entry in $zip.Entries) {
    $name = $entry.FullName.Replace('\','/').TrimEnd('/')
    if (-not $name -or $name.StartsWith('/') -or $name.Contains(':')) { throw 'Unsafe archive path' }
    foreach ($part in $name.Split('/')) {
      if (-not $part -or $part -eq '.' -or $part -eq '..' -or $part -match '[<>"|?*\x00-\x1f]' -or $part -match '[. ]$' -or $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)') { throw 'Unsafe archive filename' }
    }
    $target = [IO.Path]::GetFullPath([IO.Path]::Combine($root,$name))
    if (-not $target.StartsWith($root,[StringComparison]::OrdinalIgnoreCase) -or -not $seen.Add($target)) { throw 'Duplicate or escaping archive path' }
    $attrs = $entry.ExternalAttributes
    if ((($attrs -shr 16) -band 0xF000) -eq 0xA000 -or ($attrs -band 0x400) -ne 0) { throw 'Archive links are prohibited' }
    if ($entry.Length -lt 0 -or $entry.Length -gt $Limit -or $total -gt ($Limit - $entry.Length)) { throw 'Archive exceeds declared installed size' }
    $total += $entry.Length
  }
  foreach ($entry in $zip.Entries) {
    $target = [IO.Path]::GetFullPath([IO.Path]::Combine($root,$entry.FullName.Replace('\','/')))
    if ($entry.FullName.EndsWith('/') -or $entry.FullName.EndsWith('\')) { [IO.Directory]::CreateDirectory($target) | Out-Null; continue }
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    $inputStream = $entry.Open()
    $outputStream = [IO.File]::Open($target,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
    try {
      $buffer = New-Object byte[] 65536
      [long]$written = 0
      while (($count = $inputStream.Read($buffer,0,$buffer.Length)) -gt 0) {
        $written += $count
        if ($written -gt $entry.Length) { throw 'Archive expanded beyond declared entry size' }
        $outputStream.Write($buffer,0,$count)
      }
      if ($written -ne $entry.Length) { throw 'Truncated archive entry' }
    } finally { $inputStream.Dispose(); $outputStream.Dispose() }
  }
} finally { $zip.Dispose() }
)PS";
}

ComponentManager::ComponentManager(QObject* parent) : QObject(parent), m_network(new QNetworkAccessManager(this))
{
    const auto add = [this](const QString& id, const QString& name, const QString& description,
                           const QString& environment, const QString& executable) {
        Entry entry; entry.id = id; entry.name = name; entry.description = description;
        entry.environment = environment; entry.executable = executable;
        m_entries.insert(id, entry); m_order.append(id);
    };
    add("ocr", "OCR", "Search text inside images and scanned PDFs.", "ELLA_TESSERACT_PATH", "tesseract");
    add("office", "Office rendering", "Render Office documents faithfully with LibreOffice.", "ELLA_LIBREOFFICE_PATH", "soffice");
    add("ffmpeg", "Media conversion", "Prepare audio and video for local transcription.", "ELLA_FFMPEG_PATH", "ffmpeg");
    add("transcription", "Local transcription", "Transcribe speech with whisper.cpp; requires media conversion and a speech model.", "ELLA_WHISPER_PATH", "whisper-cli");
    add("speech-model", "Speech model", "Whisper model weights used for local transcription.", "ELLA_WHISPER_MODEL_PATH", "");
    add("semantic", "Search by meaning", "CPU-only E5 embeddings and related passages through the optional native helper.", "ELLA_SEMANTIC_HELPER_PATH", "ella-semantic");
    // Complete interrupted atomic swaps without deleting a user's configured tools.
    for (const QString& id : m_order) {
        const QString current = root() + '/' + id;
        const QString previous = current + ".previous";
        if (!QFileInfo::exists(current) && QFileInfo::exists(previous)) QDir().rename(previous, current);
        if (QFileInfo::exists(current) && QFileInfo::exists(previous)) removeOwnedDirectory(previous);
        removeOwnedDirectory(root() + '/' + id + ".install");
    }
    const QString manifest = root() + "/catalogue.json";
    if (QFileInfo::exists(manifest)) loadManifest(QUrl::fromLocalFile(manifest));
    publishEnvironment();
}

ComponentManager::~ComponentManager()
{
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); }
    if (m_extract) { m_extract->disconnect(this); m_extract->kill(); m_extract->waitForFinished(3000); }
}

QString ComponentManager::root() const
{
    const QString path = AppConfig::appDataDirectory() + "/components";
    QDir().mkpath(path);
    return QDir(path).absolutePath();
}

bool ComponentManager::isSafeRelativePath(const QString& path)
{
    if (path.isEmpty() || path.contains('\\') || QDir::isAbsolutePath(path)) return false;
    static const QRegularExpression unsafe(QStringLiteral("[<>:\"|?*\\x00-\\x1f]"));
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])($|\\.)"), QRegularExpression::CaseInsensitiveOption);
    for (const QString& part : path.split('/')) {
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') || part.endsWith(' ')
            || part.contains(unsafe) || reserved.match(part).hasMatch()) return false;
    }
    return true;
}

QString ComponentManager::configuredPath(const Entry& entry) const
{
    QSettings settings;
    const QString userPath = settings.value(setting(entry.id)).toString();
    if (QFileInfo(userPath).isFile()) return QFileInfo(userPath).absoluteFilePath();
    QFile metadata(root() + '/' + entry.id + "/.ella-component.json");
    if (metadata.open(QIODevice::ReadOnly)) {
        const QString relative = QJsonDocument::fromJson(metadata.readAll()).object().value("entrypoint").toString();
        const QString installed = root() + '/' + entry.id + '/' + relative;
        if (isSafeRelativePath(relative) && QFileInfo(installed).isFile()) return installed;
    }
    const QString environment = qEnvironmentVariable(entry.environment.toUtf8().constData());
    if (QFileInfo(environment).isFile()) return environment;
    return entry.executable.isEmpty() ? QString() : QStandardPaths::findExecutable(entry.executable);
}

QVariantList ComponentManager::components() const
{
    QVariantList result;
    for (const QString& id : m_order) {
        const Entry entry = m_entries.value(id);
        const QString path = configuredPath(entry);
        const bool installed = QFileInfo::exists(root() + '/' + id + "/.ella-component.json");
        result.append(QVariantMap{{"id", id}, {"name", entry.name}, {"description", entry.description},
            {"available", !entry.url.isEmpty()}, {"installed", installed}, {"configured", !path.isEmpty()},
            {"path", path}, {"version", entry.version}, {"downloadBytes", entry.downloadBytes},
            {"installedBytes", entry.installedBytes}, {"status", path.isEmpty()
                ? (entry.url.isEmpty() ? "No verified download published; choose an existing local tool." : "Available to install")
                : (installed ? "Installed" : "Existing local tool configured")}});
    }
    return result;
}

bool ComponentManager::loadManifest(const QUrl& localFile)
{
    if (m_busy) return false;
    if (!localFile.isLocalFile()) return fail("Choose a local component catalogue JSON file.");
    QFile file(localFile.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return fail("Cannot read component catalogue (maximum 1 MB).");
    const QByteArray bytes = file.readAll();
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || document.object().value("schemaVersion").toInt() != 1
        || !document.object().value("components").isArray()) return fail("Unsupported component catalogue format.");
    auto entries = m_entries;
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        it->url = QUrl(); it->hash.clear(); it->entrypoint.clear(); it->version.clear();
        it->downloadBytes = 0; it->installedBytes = 0;
    }
    QSet<QString> ids;
    for (const QJsonValue& value : document.object().value("components").toArray()) {
        const QJsonObject object = value.toObject();
        const QString id = object.value("id").toString();
        if (!entries.contains(id) || ids.contains(id)) return fail("Catalogue contains an unknown or duplicate component.");
        ids.insert(id);
        Entry& entry = entries[id];
        entry.url = QUrl(object.value("url").toString());
        entry.hash = object.value("sha256").toString().toLower();
        entry.entrypoint = object.value("entrypoint").toString();
        entry.version = object.value("version").toString();
        entry.downloadBytes = object.value("downloadBytes").toInteger();
        entry.installedBytes = object.value("installedBytes").toInteger();
        if (entry.url.scheme() != "https" || entry.url.host().isEmpty() || !entry.url.userInfo().isEmpty()
            || !QRegularExpression("^[0-9a-f]{64}$").match(entry.hash).hasMatch() || entry.version.isEmpty()
            || !isSafeRelativePath(entry.entrypoint) || entry.entrypoint == ".ella-component.json"
            || entry.downloadBytes <= 0 || entry.downloadBytes > 4LL * 1024 * 1024 * 1024
            || entry.installedBytes <= 0 || entry.installedBytes > 8LL * 1024 * 1024 * 1024)
            return fail("Each component requires a pinned HTTPS ZIP, SHA-256, version, safe entrypoint and exact size limits.");
    }
    QSaveFile saved(root() + "/catalogue.json");
    if (!saved.open(QIODevice::WriteOnly) || saved.write(bytes) != bytes.size() || !saved.commit())
        return fail("Cannot save component catalogue.");
    m_entries = entries; m_error.clear(); emit componentsChanged(); emit stateChanged(); return true;
}

bool ComponentManager::useLocalPath(const QString& id, const QUrl& localFile)
{
    if (m_busy || !m_entries.contains(id)) return false;
    if (!localFile.isLocalFile() || !QFileInfo(localFile.toLocalFile()).isFile())
        return fail("Choose an existing local executable or model file.");
    const QString path = QFileInfo(localFile.toLocalFile()).absoluteFilePath();
    const QString extension = QFileInfo(path).suffix().toLower();
    if (id != "speech-model" && extension != "exe" && extension != "com")
        return fail("Choose the Windows executable for this component.");
    if (id == "speech-model" && extension != "bin") return fail("Choose a whisper.cpp .bin model.");
    QSettings settings; settings.setValue(setting(id), path); settings.sync();
    if (settings.status() != QSettings::NoError) return fail("Could not save the local tool setting.");
    publishEnvironment(); m_error.clear(); emit componentsChanged(); emit stateChanged(); return true;
}

void ComponentManager::publishEnvironment()
{
    for (const Entry& entry : m_entries) {
        const QString path = configuredPath(entry);
        if (!path.isEmpty()) qputenv(entry.environment.toUtf8().constData(), path.toUtf8());
    }
}

void ComponentManager::refresh() { publishEnvironment(); emit componentsChanged(); }

bool ComponentManager::removeOwnedDirectory(const QString& path)
{
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString base = QDir::cleanPath(root()) + '/';
    if (!clean.startsWith(base, Qt::CaseInsensitive) || clean.mid(base.size()).contains('/')
        || QFileInfo(clean).isSymLink()) return false;
    if (!QFileInfo::exists(clean)) return true;
    return QDir(clean).removeRecursively();
}

bool ComponentManager::fail(const QString& error)
{
    m_error = error; m_activity = "Action needed"; m_busy = false;
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr; }
    if (m_extract) { m_extract->disconnect(this); m_extract->kill(); m_extract->deleteLater(); m_extract = nullptr; }
    if (m_download) { m_download->close(); m_download.reset(); }
    if (!m_workDir.isEmpty()) removeOwnedDirectory(m_workDir);
    m_workDir.clear(); emit stateChanged(); emit componentsChanged(); return false;
}

void ComponentManager::install(const QString& id)
{
    if (m_busy || !m_entries.contains(id)) return;
    m_active = m_entries.value(id);
    if (m_active.url.isEmpty()) { fail("No verified download is published for this component. Choose an existing local tool or load an audited catalogue."); return; }
    const QStorageInfo volume(root());
    if (volume.isValid() && volume.bytesAvailable() < m_active.downloadBytes + m_active.installedBytes + 64 * 1024 * 1024) {
        fail("There is not enough free disk space to install this component safely."); return;
    }
    m_workDir = root() + '/' + id + ".install";
    if (!removeOwnedDirectory(m_workDir) || !QDir().mkpath(m_workDir)) { fail("Cannot prepare component installation directory."); return; }
    m_download = std::make_unique<QFile>(m_workDir + "/component.zip");
    if (!m_download->open(QIODevice::WriteOnly | QIODevice::NewOnly)) { fail("Cannot create component download."); return; }
    m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_received = 0; m_progress = 0; m_busy = true; m_error.clear(); m_activity = "Downloading " + m_active.name;
    QNetworkRequest request(m_active.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);
    m_reply = m_network->get(request);
    m_reply->setReadBufferSize(256 * 1024);
    connect(m_reply, &QNetworkReply::readyRead, this, &ComponentManager::drainDownload);
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        if (!m_reply) return;
        drainDownload();
        if (!m_reply) return;
        if (m_reply->error() != QNetworkReply::NoError) { fail("Download failed: " + m_reply->errorString()); return; }
        if (m_received != m_active.downloadBytes || m_hash->result().toHex() != m_active.hash.toLatin1()) {
            fail("Component verification failed: download size or SHA-256 does not match the catalogue."); return;
        }
        m_reply->deleteLater(); m_reply = nullptr;
        if (!m_download->flush()) { fail("Could not finish writing the component download."); return; }
        m_download->close(); m_download.reset(); extractDownload();
    });
    emit stateChanged();
}

void ComponentManager::drainDownload()
{
    if (!m_reply || !m_download) return;
    const QByteArray bytes = m_reply->readAll();
    if (m_received + bytes.size() > m_active.downloadBytes) { fail("Component download exceeds the catalogue size limit."); return; }
    if (m_download->write(bytes) != bytes.size()) { fail("Unable to write component download; check free disk space."); return; }
    m_hash->addData(bytes); m_received += bytes.size();
    m_progress = double(m_received) / double(m_active.downloadBytes) * 0.85; emit stateChanged();
}

void ComponentManager::extractDownload()
{
    QSaveFile script(m_workDir + "/extract.ps1");
    if (!script.open(QIODevice::WriteOnly) || script.write(extractScript) != qint64(sizeof(extractScript) - 1) || !script.commit()) {
        fail("Could not prepare the archive validator."); return;
    }
    QString powershell = QStandardPaths::findExecutable("powershell.exe");
    if (powershell.isEmpty()) { fail("Windows PowerShell is required to validate and extract component ZIP files."); return; }
    m_activity = "Verifying and extracting " + m_active.name; emit stateChanged();
    m_extract = new QProcess(this);
    connect(m_extract, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) fail("The archive validator could not start.");
    });
    connect(m_extract, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
        if (!m_extract) return;
        const QString detail = QString::fromLocal8Bit(m_extract->readAllStandardError()).left(1000);
        m_extract->deleteLater(); m_extract = nullptr;
        if (code != 0 || status != QProcess::NormalExit) { fail("Archive validation failed. " + detail); return; }
        finishInstallation();
    });
    m_extract->start(powershell, {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
        m_workDir + "/extract.ps1", "-Archive", m_workDir + "/component.zip", "-Destination",
        m_workDir + "/payload", "-Limit", QString::number(m_active.installedBytes)});
}

void ComponentManager::finishInstallation()
{
    const QString payload = m_workDir + "/payload";
    if (!QFileInfo(payload + '/' + m_active.entrypoint).isFile()) { fail("The package does not contain its declared entrypoint."); return; }
    QSaveFile metadata(payload + "/.ella-component.json");
    const QByteArray bytes = QJsonDocument(QJsonObject{{"id", m_active.id}, {"version", m_active.version},
        {"sha256", m_active.hash}, {"entrypoint", m_active.entrypoint}}).toJson();
    if (!metadata.open(QIODevice::WriteOnly) || metadata.write(bytes) != bytes.size() || !metadata.commit()) {
        fail("Could not save component installation metadata."); return;
    }
    const QString current = root() + '/' + m_active.id;
    const QString previous = current + ".previous";
    if (!removeOwnedDirectory(previous)) { fail("Could not remove the previous component backup."); return; }
    const bool replacing = QFileInfo::exists(current);
    if (replacing && !QDir().rename(current, previous)) { fail("Component is in use. Close its active work and retry."); return; }
    if (!QDir().rename(payload, current)) {
        if (replacing) QDir().rename(previous, current);
        fail("Could not activate the component; the previous version was preserved."); return;
    }
    QSettings settings; settings.remove(setting(m_active.id)); settings.sync();
    removeOwnedDirectory(previous); removeOwnedDirectory(m_workDir); m_workDir.clear();
    m_busy = false; m_progress = 1; m_activity = m_active.name + " installed";
    publishEnvironment(); emit stateChanged(); emit componentsChanged(); emit componentInstalled(m_active.id);
}

void ComponentManager::cancel()
{
    if (!m_busy) return;
    fail("Installation cancelled; existing components were preserved.");
}

bool ComponentManager::remove(const QString& id)
{
    if (m_busy || !m_entries.contains(id)) return false;
    if (!removeOwnedDirectory(root() + '/' + id)) return fail("Could not remove component; it may still be in use.");
    QSettings settings; settings.remove(setting(id)); settings.sync();
    qunsetenv(m_entries.value(id).environment.toUtf8().constData());
    m_error.clear(); m_activity = m_entries.value(id).name + " removed";
    emit componentsChanged(); emit stateChanged(); return true;
}
