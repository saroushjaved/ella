#include "SemanticSearchService.h"
#include "core/AppConfig.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

SemanticSearchService::SemanticSearchService(QObject* parent) : QObject(parent) {}
SemanticSearchService::~SemanticSearchService()
{
    if (m_process) { m_process->disconnect(this); m_process->terminate(); if (!m_process->waitForFinished(1500)) m_process->kill(); }
}
QString SemanticSearchService::helperPath() const { return qEnvironmentVariable("ELLA_SEMANTIC_HELPER_PATH").trimmed(); }
bool SemanticSearchService::available() const { return QFileInfo(helperPath()).isFile(); }
void SemanticSearchService::refresh()
{
    m_status = available() ? QStringLiteral("Search by meaning is ready.") : QStringLiteral("Install or choose the semantic component in Settings.");
    emit stateChanged();
}
bool SemanticSearchService::ensureProcess()
{
    if (m_process && m_process->state() != QProcess::NotRunning) return true;
    const QString executable = helperPath();
    if (!QFileInfo(executable).isFile()) { m_status = QStringLiteral("The optional semantic component is unavailable."); emit stateChanged(); return false; }
    auto* process = new QProcess(this); m_process = process;
    process->setProgram(executable);
    process->setArguments({QStringLiteral("--database"), AppConfig::databasePath(), QStringLiteral("--index-dir"), QDir(AppConfig::cacheDirectory()).filePath(QStringLiteral("semantic"))});
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, &QProcess::started, this, &SemanticSearchService::sendNext);
    connect(process, &QProcess::readyReadStandardOutput, this, &SemanticSearchService::consumeOutput);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
        const QString details = QString::fromUtf8(process->readAllStandardError()).trimmed();
        failAll(details.isEmpty() ? QStringLiteral("Semantic helper stopped.") : details);
        if (m_process == process) m_process = nullptr;
        process->deleteLater();
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process](int, QProcess::ExitStatus) {
        if (m_active.id || !m_queue.isEmpty()) failAll(QStringLiteral("Semantic helper exited before completing the request."));
        if (m_process == process) m_process = nullptr;
        process->deleteLater();
    });
    m_status = QStringLiteral("Starting search-by-meaning component…"); emit stateChanged(); process->start(); return true;
}
quint64 SemanticSearchService::enqueue(const QString& query, const QVariantMap& values, bool rebuild)
{
    if (m_queue.size() + (m_active.id ? 1 : 0) >= 4) { m_status = QStringLiteral("Meaning-search queue is full. Wait for the current search to finish."); emit stateChanged(); return 0; }
    Request request; request.id = m_nextId++; request.query = query; request.rebuild = rebuild;
    QVariantMap command = values; command[QStringLiteral("requestId")] = QVariant::fromValue<qulonglong>(request.id);
    request.payload = QJsonDocument::fromVariant(command).toJson(QJsonDocument::Compact) + '\n'; m_queue.enqueue(request);
    if (!ensureProcess()) { m_queue.removeLast(); return 0; }
    sendNext(); emit stateChanged(); return request.id;
}
quint64 SemanticSearchService::search(const QString& query, int limit)
{
    const QString trimmed = query.trimmed(); if (trimmed.isEmpty()) return 0;
    return enqueue(trimmed, {{QStringLiteral("command"), QStringLiteral("search")}, {QStringLiteral("query"), trimmed}, {QStringLiteral("limit"), qBound(1, limit, 100)}});
}
quint64 SemanticSearchService::relatedPassages(int fileId, int passageOrdinal, int limit)
{
    if (fileId < 0 || passageOrdinal < 0) return 0;
    return enqueue({}, {{QStringLiteral("command"), QStringLiteral("related")}, {QStringLiteral("fileId"), fileId}, {QStringLiteral("passageOrdinal"), passageOrdinal}, {QStringLiteral("limit"), qBound(1, limit, 100)}});
}
bool SemanticSearchService::rebuildIndex() { return enqueue({}, {{QStringLiteral("command"), QStringLiteral("rebuild")}}, true) != 0; }
void SemanticSearchService::sendNext()
{
    if (!m_process || m_process->state() != QProcess::Running || m_active.id || m_queue.isEmpty()) return;
    m_active = m_queue.dequeue(); m_status = m_active.rebuild ? QStringLiteral("Building semantic passage index…") : QStringLiteral("Searching passages by meaning…");
    if (m_process->write(m_active.payload) != m_active.payload.size()) { failAll(QStringLiteral("Could not send work to the semantic helper.")); return; }
    emit stateChanged();
}
void SemanticSearchService::consumeOutput()
{
    if (!m_process) return; m_stdout += m_process->readAllStandardOutput();
    while (true) {
        const qsizetype newline = m_stdout.indexOf('\n'); if (newline < 0) break;
        const QByteArray line = m_stdout.left(newline).trimmed(); m_stdout.remove(0, newline + 1); if (line.isEmpty()) continue;
        QJsonParseError parse; const QJsonObject response = QJsonDocument::fromJson(line, &parse).object();
        if (parse.error != QJsonParseError::NoError || response.value("requestId").toVariant().toULongLong() != m_active.id) { failAll(QStringLiteral("Semantic helper returned an invalid or out-of-order response.")); return; }
        const QString error = response.value("error").toString(); const QVariantList results = response.value("results").toArray().toVariantList(); const Request completed = m_active; m_active = {};
        m_status = error.isEmpty() ? QStringLiteral("Search by meaning is ready.") : error;
        if (completed.rebuild) emit indexFinished(error.isEmpty(), error.isEmpty() ? response.value("message").toString() : error); else emit resultsReady(completed.id, completed.query, results, error);
        emit stateChanged(); sendNext();
    }
}
void SemanticSearchService::failAll(const QString& value)
{
    const QString error = value.isEmpty() ? QStringLiteral("Semantic helper is unavailable.") : value.left(500);
    if (m_active.id) { if (m_active.rebuild) emit indexFinished(false, error); else emit resultsReady(m_active.id, m_active.query, {}, error); }
    while (!m_queue.isEmpty()) { const Request request = m_queue.dequeue(); if (request.rebuild) emit indexFinished(false, error); else emit resultsReady(request.id, request.query, {}, error); }
    m_active = {}; m_status = error; emit stateChanged();
}
void SemanticSearchService::cancel() { failAll(QStringLiteral("Meaning search cancelled.")); if (m_process) m_process->kill(); }
