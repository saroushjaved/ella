#include "sync/CloudSyncService.h"

#include "core/AppConfig.h"
#include "core/SecureStorage.h"
#include "database/DatabaseManager.h"
#include "sync/CloudProvider.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>

namespace
{
QString nowIso()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

struct ProviderConnection
{
    QString provider;
    QString accountEmail;
    QString accessToken;
    QString refreshToken;
    QString expiresAtIso;
};

QString sanitizePathSegment(const QString& value,
                            const QString& fallback = QStringLiteral("default"),
                            int maxLength = 64)
{
    QString normalized = value.trimmed();
    if (normalized.isEmpty()) {
        normalized = fallback;
    }
    normalized.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    normalized = normalized.left(maxLength);
    return normalized;
}

QString profileSegmentFromEmail(const QString& email)
{
    QString profile = email.trimmed();
    const int atIndex = profile.indexOf('@');
    if (atIndex > 0) {
        profile = profile.left(atIndex);
    }
    return sanitizePathSegment(profile, QStringLiteral("default-profile"), 48);
}

QString cloudRootPrefix(const QString& accountEmail)
{
    return QStringLiteral("ELLA/%1").arg(profileSegmentFromEmail(accountEmail));
}

bool loadConnectionRecord(const QString& provider,
                          ProviderConnection* connection,
                          QString* errorMessage)
{
    if (connection) {
        connection->provider.clear();
        connection->accountEmail.clear();
        connection->accessToken.clear();
        connection->refreshToken.clear();
        connection->expiresAtIso.clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    if (!connection) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid connection output pointer");
        }
        return false;
    }

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral(R"(
        SELECT provider, account_email, access_token_enc, refresh_token_enc, expires_at
        FROM cloud_connections
        WHERE provider = ?
        LIMIT 1
    )"));
    query.addBindValue(provider);
    if (!query.exec() || !query.next()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cloud provider is not connected");
        }
        return false;
    }

    const QString accessTokenEncrypted = query.value(2).toString();
    const QString refreshTokenEncrypted = query.value(3).toString();
    QString decryptError;

    const QString accessToken = SecureStorage::unprotect(accessTokenEncrypted, &decryptError).trimmed();
    if (accessToken.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to decrypt provider access token: %1").arg(decryptError);
        }
        return false;
    }

    QString refreshToken;
    if (!refreshTokenEncrypted.trimmed().isEmpty()) {
        refreshToken = SecureStorage::unprotect(refreshTokenEncrypted, &decryptError).trimmed();
        if (refreshToken.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unable to decrypt provider refresh token: %1").arg(decryptError);
            }
            return false;
        }
    }

    connection->provider = query.value(0).toString();
    connection->accountEmail = query.value(1).toString();
    connection->accessToken = accessToken;
    connection->refreshToken = refreshToken;
    connection->expiresAtIso = query.value(4).toString();
    return true;
}

bool persistConnectionTokens(const ProviderConnection& connection,
                             const QString& accessToken,
                             const QString& refreshToken,
                             const QString& expiresAtIso,
                             QString* errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    QString protectError;
    const QString accessTokenEncrypted = SecureStorage::protect(accessToken, &protectError);
    if (accessTokenEncrypted.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to protect refreshed access token: %1").arg(protectError);
        }
        return false;
    }

    QString refreshTokenEncrypted;
    if (!refreshToken.trimmed().isEmpty()) {
        refreshTokenEncrypted = SecureStorage::protect(refreshToken, &protectError);
        if (refreshTokenEncrypted.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unable to protect refreshed refresh token: %1").arg(protectError);
            }
            return false;
        }
    }

    QSqlQuery update(DatabaseManager::instance().database());
    update.prepare(QStringLiteral(R"(
        UPDATE cloud_connections
        SET access_token_enc = ?,
            refresh_token_enc = ?,
            expires_at = ?,
            updated_at = ?
        WHERE provider = ?
    )"));
    update.addBindValue(accessTokenEncrypted);
    update.addBindValue(refreshTokenEncrypted);
    update.addBindValue(expiresAtIso.trimmed());
    update.addBindValue(nowIso());
    update.addBindValue(connection.provider);
    if (!update.exec()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to store refreshed tokens: %1").arg(update.lastError().text());
        }
        return false;
    }

    return true;
}

bool ensureFreshAccessToken(ProviderConnection* connection, QString* errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    if (!connection) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid provider connection object");
        }
        return false;
    }

    QDateTime expiresAt = QDateTime::fromString(connection->expiresAtIso, Qt::ISODate);
    if (!expiresAt.isValid()) {
        return true;
    }

    const QDateTime now = QDateTime::currentDateTime();
    if (expiresAt > now.addSecs(120)) {
        return true;
    }

    if (connection->refreshToken.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Provider token expired and no refresh token is available");
        }
        return false;
    }

    const std::unique_ptr<CloudProvider> provider = createCloudProvider(connection->provider);
    if (!provider) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Provider implementation unavailable for token refresh");
        }
        return false;
    }

    const QVariantMap refreshResult = provider->refreshAccessToken(connection->refreshToken);
    if (!refreshResult.value(QStringLiteral("ok")).toBool()) {
        if (errorMessage) {
            *errorMessage = refreshResult.value(QStringLiteral("error")).toString();
        }
        return false;
    }

    const QString newAccessToken = refreshResult.value(QStringLiteral("accessToken")).toString().trimmed();
    const QString newRefreshToken =
        refreshResult.value(QStringLiteral("refreshToken")).toString().trimmed();
    const QString newExpiresAt = refreshResult.value(QStringLiteral("expiresAtIso")).toString().trimmed();

    if (newAccessToken.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Provider refresh did not return a usable access token");
        }
        return false;
    }

    if (!persistConnectionTokens(*connection,
                                 newAccessToken,
                                 newRefreshToken.isEmpty() ? connection->refreshToken : newRefreshToken,
                                 newExpiresAt,
                                 errorMessage)) {
        return false;
    }

    connection->accessToken = newAccessToken;
    if (!newRefreshToken.isEmpty()) {
        connection->refreshToken = newRefreshToken;
    }
    connection->expiresAtIso = newExpiresAt;
    return true;
}

bool writeMirrorFile(const QString& absolutePath,
                     const QByteArray& content,
                     QString* errorMessage)
{
    QDir folder = QFileInfo(absolutePath).dir();
    if (!folder.exists() && !folder.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to create cloud mirror folder");
        }
        return false;
    }

    QSaveFile outFile(absolutePath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to open mirror output file");
        }
        return false;
    }

    outFile.write(content);
    if (!outFile.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to save mirror output file");
        }
        return false;
    }

    return true;
}
}

CloudSyncService::CloudSyncService(QObject* parent)
    : QObject(parent)
{
    m_workerTimer.setInterval(1500);
    m_workerTimer.setSingleShot(false);
    connect(&m_workerTimer, &QTimer::timeout, this, &CloudSyncService::processNextJob);
    m_workerTimer.start();
}

bool CloudSyncService::connectProvider(const QString& provider,
                                       const QString& accountEmail,
                                       const QString& accessToken,
                                       const QString& refreshToken,
                                       const QString& expiresAtIso)
{
    const QString normalizedProvider = normalizeProvider(provider);
    if (normalizedProvider.isEmpty() || accessToken.trimmed().isEmpty()) {
        return false;
    }

    QString protectError;
    const QString protectedAccessToken = SecureStorage::protect(accessToken.trimmed(), &protectError);
    if (protectedAccessToken.isEmpty() && !accessToken.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("Unable to secure provider token: %1").arg(protectError);
        emit statusChanged();
        return false;
    }

    const QString protectedRefreshToken = refreshToken.trimmed().isEmpty()
                                              ? QString()
                                              : SecureStorage::protect(refreshToken.trimmed(), &protectError);

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral(R"(
        INSERT INTO cloud_connections(
            provider, account_email, access_token_enc, refresh_token_enc,
            expires_at, connected_at, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(provider) DO UPDATE SET
            account_email = excluded.account_email,
            access_token_enc = excluded.access_token_enc,
            refresh_token_enc = excluded.refresh_token_enc,
            expires_at = excluded.expires_at,
            connected_at = excluded.connected_at,
            updated_at = excluded.updated_at
    )"));
    query.addBindValue(normalizedProvider);
    query.addBindValue(accountEmail.trimmed());
    query.addBindValue(protectedAccessToken);
    query.addBindValue(protectedRefreshToken);
    query.addBindValue(expiresAtIso.trimmed());
    query.addBindValue(nowIso());
    query.addBindValue(nowIso());

    if (!query.exec()) {
        m_lastError = QStringLiteral("Failed to connect provider: %1").arg(query.lastError().text());
        emit statusChanged();
        return false;
    }

    QSqlQuery activeFilesQuery(DatabaseManager::instance().database());
    activeFilesQuery.prepare(QStringLiteral(
        "SELECT id FROM files WHERE status = 0 ORDER BY id ASC"));
    if (activeFilesQuery.exec()) {
        while (activeFilesQuery.next()) {
            const int fileId = activeFilesQuery.value(0).toInt();
            if (fileId < 0) {
                continue;
            }

            enqueueJob(normalizedProvider, QStringLiteral("file_upload"), {
                { QStringLiteral("fileId"), fileId },
                { QStringLiteral("reason"), QStringLiteral("provider_connected_backfill") }
            });
        }
    } else {
        m_lastError = QStringLiteral("Connected provider, but failed to enqueue file backfill: %1")
                          .arg(activeFilesQuery.lastError().text());
    }

    enqueueJob(normalizedProvider,
               QStringLiteral("catalog_sync"),
               { { QStringLiteral("reason"), QStringLiteral("provider_connected") } });
    emit statusChanged();
    return true;
}

bool CloudSyncService::disconnectProvider(const QString& provider)
{
    const QString normalizedProvider = normalizeProvider(provider);
    if (normalizedProvider.isEmpty()) {
        return false;
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.transaction()) {
        return false;
    }

    QSqlQuery removeConnection(db);
    removeConnection.prepare(QStringLiteral("DELETE FROM cloud_connections WHERE provider = ?"));
    removeConnection.addBindValue(normalizedProvider);
    if (!removeConnection.exec()) {
        db.rollback();
        m_lastError = removeConnection.lastError().text();
        emit statusChanged();
        return false;
    }

    QSqlQuery clearPending(db);
    clearPending.prepare(QStringLiteral(
        "DELETE FROM sync_jobs WHERE provider = ? AND status IN ('pending', 'failed')"));
    clearPending.addBindValue(normalizedProvider);
    if (!clearPending.exec()) {
        db.rollback();
        m_lastError = clearPending.lastError().text();
        emit statusChanged();
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        m_lastError = db.lastError().text();
        emit statusChanged();
        return false;
    }

    emit statusChanged();
    return true;
}

QVariantMap CloudSyncService::beginOAuthConnect(const QString& provider, const QString& redirectUri)
{
    QVariantMap result;
    result[QStringLiteral("ok")] = false;

    const QString normalizedProvider = normalizeProvider(provider);
    result[QStringLiteral("provider")] = normalizedProvider;

    if (normalizedProvider.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("Unsupported provider");
        return result;
    }

    const auto providerImpl = createCloudProvider(normalizedProvider);
    if (!providerImpl) {
        result[QStringLiteral("error")] = QStringLiteral("Provider integration is not available");
        return result;
    }

    const QString clientIdEnvVar = providerImpl->clientIdEnvVar();
    const QString clientSecretEnvVar = providerImpl->clientSecretEnvVar();
    const bool hasClientId = !qEnvironmentVariable(clientIdEnvVar.toUtf8().constData()).trimmed().isEmpty();
    const bool hasClientSecret =
        !qEnvironmentVariable(clientSecretEnvVar.toUtf8().constData()).trimmed().isEmpty();

    result[QStringLiteral("displayName")] = providerImpl->displayName();
    result[QStringLiteral("clientIdEnvVar")] = clientIdEnvVar;
    result[QStringLiteral("clientSecretEnvVar")] = clientSecretEnvVar;
    result[QStringLiteral("hasClientId")] = hasClientId;
    result[QStringLiteral("hasClientSecret")] = hasClientSecret;
    result[QStringLiteral("ready")] = hasClientId && hasClientSecret;

    if (!hasClientId || !hasClientSecret) {
        QStringList missing;
        if (!hasClientId) {
            missing.append(clientIdEnvVar);
        }
        if (!hasClientSecret) {
            missing.append(clientSecretEnvVar);
        }

        result[QStringLiteral("error")] =
            QStringLiteral("Missing OAuth environment variables: %1").arg(missing.join(QStringLiteral(", ")));
        return result;
    }

    const QVariantMap oauthResult = providerImpl->beginOAuth(redirectUri);
    for (auto it = oauthResult.constBegin(); it != oauthResult.constEnd(); ++it) {
        result[it.key()] = it.value();
    }

    if (!result.value(QStringLiteral("ok")).toBool()) {
        m_lastError = result.value(QStringLiteral("error")).toString();
        emit statusChanged();
    }

    return result;
}

bool CloudSyncService::completeOAuthConnect(const QString& provider,
                                            const QString& authorizationCode,
                                            const QString& redirectUri,
                                            const QString& codeVerifier)
{
    const QString normalizedProvider = normalizeProvider(provider);
    if (normalizedProvider.isEmpty()) {
        m_lastError = QStringLiteral("Unsupported provider");
        emit statusChanged();
        return false;
    }

    const auto providerImpl = createCloudProvider(normalizedProvider);
    if (!providerImpl) {
        m_lastError = QStringLiteral("Provider integration is not available");
        emit statusChanged();
        return false;
    }

    const QVariantMap tokenResult =
        providerImpl->completeOAuth(authorizationCode, redirectUri, codeVerifier);
    if (!tokenResult.value(QStringLiteral("ok")).toBool()) {
        m_lastError = tokenResult.value(QStringLiteral("error")).toString();
        emit statusChanged();
        return false;
    }

    const bool connected = connectProvider(normalizedProvider,
                                           tokenResult.value(QStringLiteral("accountEmail")).toString(),
                                           tokenResult.value(QStringLiteral("accessToken")).toString(),
                                           tokenResult.value(QStringLiteral("refreshToken")).toString(),
                                           tokenResult.value(QStringLiteral("expiresAtIso")).toString());

    if (connected) {
        const QString warning = tokenResult.value(QStringLiteral("warning")).toString().trimmed();
        if (!warning.isEmpty()) {
            m_lastError = warning;
        }
    }

    emit statusChanged();
    return connected;
}

QVariantMap CloudSyncService::oauthConfigurationStatus(const QString& provider) const
{
    QVariantMap result;
    result[QStringLiteral("ok")] = false;

    const QString normalizedProvider = normalizeProvider(provider);
    result[QStringLiteral("provider")] = normalizedProvider;

    if (normalizedProvider.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("Unsupported provider");
        return result;
    }

    const auto providerImpl = createCloudProvider(normalizedProvider);
    if (!providerImpl) {
        result[QStringLiteral("error")] = QStringLiteral("Provider integration is not available");
        return result;
    }

    const QString clientIdEnvVar = providerImpl->clientIdEnvVar();
    const QString clientSecretEnvVar = providerImpl->clientSecretEnvVar();
    const bool hasClientId = !qEnvironmentVariable(clientIdEnvVar.toUtf8().constData()).trimmed().isEmpty();
    const bool hasClientSecret =
        !qEnvironmentVariable(clientSecretEnvVar.toUtf8().constData()).trimmed().isEmpty();

    result[QStringLiteral("displayName")] = providerImpl->displayName();
    result[QStringLiteral("clientIdEnvVar")] = clientIdEnvVar;
    result[QStringLiteral("clientSecretEnvVar")] = clientSecretEnvVar;
    result[QStringLiteral("hasClientId")] = hasClientId;
    result[QStringLiteral("hasClientSecret")] = hasClientSecret;
    result[QStringLiteral("ready")] = hasClientId && hasClientSecret;

    if (hasClientId && hasClientSecret) {
        result[QStringLiteral("ok")] = true;
    } else {
        QStringList missing;
        if (!hasClientId) {
            missing.append(clientIdEnvVar);
        }
        if (!hasClientSecret) {
            missing.append(clientSecretEnvVar);
        }

        result[QStringLiteral("error")] =
            QStringLiteral("Missing OAuth environment variables: %1").arg(missing.join(QStringLiteral(", ")));
    }

    return result;
}

void CloudSyncService::syncNow()
{
    processNextJob();
}

QVariantMap CloudSyncService::status() const
{
    QVariantMap map;
    map[QStringLiteral("processing")] = m_processing;
    map[QStringLiteral("lastError")] = m_lastError;
    map[QStringLiteral("lastSyncAt")] = m_lastSyncAt;

    QSqlQuery pendingQuery(DatabaseManager::instance().database());
    pendingQuery.exec(QStringLiteral(
        "SELECT COUNT(*) FROM sync_jobs WHERE status IN ('pending', 'failed')"));
    map[QStringLiteral("pendingJobs")] = pendingQuery.next() ? pendingQuery.value(0).toInt() : 0;

    QSqlQuery failedQuery(DatabaseManager::instance().database());
    failedQuery.exec(QStringLiteral(
        "SELECT COUNT(*) FROM sync_jobs WHERE status = 'terminal_failed'"));
    map[QStringLiteral("terminalFailedJobs")] = failedQuery.next() ? failedQuery.value(0).toInt() : 0;

    QSqlQuery providerQuery(DatabaseManager::instance().database());
    providerQuery.exec(QStringLiteral("SELECT COUNT(*) FROM cloud_connections"));
    map[QStringLiteral("connectedProviders")] = providerQuery.next() ? providerQuery.value(0).toInt() : 0;

    return map;
}

QVariantList CloudSyncService::providerStatuses() const
{
    QVariantList list;

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral(R"(
        SELECT provider, account_email, expires_at, connected_at
        FROM cloud_connections
        ORDER BY provider ASC
    )"));

    if (!query.exec()) {
        return list;
    }

    while (query.next()) {
        QVariantMap item;
        const QString provider = query.value(0).toString();
        item[QStringLiteral("provider")] = provider;
        item[QStringLiteral("accountEmail")] = query.value(1).toString();
        item[QStringLiteral("expiresAt")] = query.value(2).toString();
        item[QStringLiteral("connectedAt")] = query.value(3).toString();

        QSqlQuery pendingQuery(DatabaseManager::instance().database());
        pendingQuery.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM sync_jobs WHERE provider = ? AND status IN ('pending', 'failed')"));
        pendingQuery.addBindValue(provider);
        pendingQuery.exec();
        item[QStringLiteral("pendingJobs")] = pendingQuery.next() ? pendingQuery.value(0).toInt() : 0;

        QSqlQuery mapQuery(DatabaseManager::instance().database());
        mapQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM cloud_file_map WHERE provider = ?"));
        mapQuery.addBindValue(provider);
        mapQuery.exec();
        item[QStringLiteral("mirroredFiles")] = mapQuery.next() ? mapQuery.value(0).toInt() : 0;

        list.append(item);
    }

    return list;
}

void CloudSyncService::enqueueFileUpload(int fileId, const QString& reason)
{
    if (fileId < 0) {
        return;
    }

    const QVariantList providers = connectedProviders();
    for (const QVariant& providerValue : providers) {
        enqueueJob(providerValue.toString(), QStringLiteral("file_upload"), {
            { QStringLiteral("fileId"), fileId },
            { QStringLiteral("reason"), reason }
        });
    }
}

void CloudSyncService::enqueueFileDelete(int fileId, const QString& reason)
{
    if (fileId < 0) {
        return;
    }

    const QVariantList providers = connectedProviders();
    for (const QVariant& providerValue : providers) {
        enqueueJob(providerValue.toString(), QStringLiteral("file_delete"), {
            { QStringLiteral("fileId"), fileId },
            { QStringLiteral("reason"), reason }
        });
    }
}

void CloudSyncService::enqueueCatalogSync(const QString& reason)
{
    const QVariantList providers = connectedProviders();
    for (const QVariant& providerValue : providers) {
        enqueueJob(providerValue.toString(), QStringLiteral("catalog_sync"), {
            { QStringLiteral("reason"), reason }
        });
    }
}

QString CloudSyncService::normalizeProvider(const QString& provider) const
{
    const QString value = provider.trimmed().toLower();
    if (value == QStringLiteral("google") || value == QStringLiteral("google_drive")) {
        return QStringLiteral("google_drive");
    }
    if (value == QStringLiteral("onedrive") || value == QStringLiteral("outlook")) {
        return QStringLiteral("onedrive");
    }
    return QString();
}

QVariantList CloudSyncService::connectedProviders() const
{
    QVariantList providers;

    QSqlQuery query(DatabaseManager::instance().database());
    if (!query.exec(QStringLiteral("SELECT provider FROM cloud_connections ORDER BY provider ASC"))) {
        return providers;
    }

    while (query.next()) {
        providers.append(query.value(0).toString());
    }

    return providers;
}

bool CloudSyncService::enqueueJob(const QString& provider,
                                  const QString& jobType,
                                  const QVariantMap& payload)
{
    const QString normalizedProvider = normalizeProvider(provider);
    if (normalizedProvider.isEmpty()) {
        return false;
    }

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral(R"(
        INSERT INTO sync_jobs(
            provider, job_type, payload_json, status,
            retry_count, next_retry_at, last_error, created_at, updated_at
        ) VALUES (?, ?, ?, 'pending', 0, '', '', ?, ?)
    )"));
    query.addBindValue(normalizedProvider);
    query.addBindValue(jobType.trimmed().toLower());
    query.addBindValue(QString::fromUtf8(QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact)));
    query.addBindValue(nowIso());
    query.addBindValue(nowIso());

    const bool ok = query.exec();
    if (!ok) {
        m_lastError = QStringLiteral("Failed to enqueue cloud sync job: %1").arg(query.lastError().text());
    }
    emit statusChanged();
    return ok;
}

void CloudSyncService::processNextJob()
{
    if (m_processing) {
        return;
    }

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral(R"(
        SELECT id, provider, job_type, payload_json, retry_count
        FROM sync_jobs
        WHERE status IN ('pending', 'failed')
          AND retry_count < 6
          AND (next_retry_at IS NULL OR next_retry_at = '' OR next_retry_at <= ?)
        ORDER BY created_at ASC, id ASC
        LIMIT 1
    )"));
    query.addBindValue(nowIso());

    if (!query.exec() || !query.next()) {
        return;
    }

    QVariantMap jobRecord;
    jobRecord[QStringLiteral("id")] = query.value(0).toInt();
    jobRecord[QStringLiteral("provider")] = query.value(1).toString();
    jobRecord[QStringLiteral("jobType")] = query.value(2).toString();
    jobRecord[QStringLiteral("payloadJson")] = query.value(3).toString();
    jobRecord[QStringLiteral("retryCount")] = query.value(4).toInt();

    const int jobId = jobRecord.value(QStringLiteral("id")).toInt();

    QSqlQuery markProcessing(DatabaseManager::instance().database());
    markProcessing.prepare(QStringLiteral(
        "UPDATE sync_jobs SET status = 'processing', updated_at = ? WHERE id = ?"));
    markProcessing.addBindValue(nowIso());
    markProcessing.addBindValue(jobId);
    markProcessing.exec();

    m_processing = true;
    emit statusChanged();

    QString errorMessage;
    const bool ok = processJobRecord(jobRecord, &errorMessage);

    if (ok) {
        QSqlQuery markDone(DatabaseManager::instance().database());
        markDone.prepare(QStringLiteral(
            "UPDATE sync_jobs SET status = 'done', last_error = '', updated_at = ? WHERE id = ?"));
        markDone.addBindValue(nowIso());
        markDone.addBindValue(jobId);
        markDone.exec();
        m_lastSyncAt = nowIso();
    } else {
        const int retryCount = jobRecord.value(QStringLiteral("retryCount")).toInt() + 1;
        const bool terminal = retryCount >= 6;
        const int backoffSeconds = terminal ? 0 : (1 << (retryCount - 1)) * 30;
        const QString nextRetry = terminal
                                      ? QString()
                                      : QDateTime::currentDateTime().addSecs(backoffSeconds).toString(Qt::ISODate);

        QSqlQuery markFailed(DatabaseManager::instance().database());
        markFailed.prepare(QStringLiteral(R"(
            UPDATE sync_jobs
            SET status = ?,
                retry_count = ?,
                next_retry_at = ?,
                last_error = ?,
                updated_at = ?
            WHERE id = ?
        )"));
        markFailed.addBindValue(terminal ? QStringLiteral("terminal_failed") : QStringLiteral("failed"));
        markFailed.addBindValue(retryCount);
        markFailed.addBindValue(nextRetry);
        markFailed.addBindValue(errorMessage.left(1000));
        markFailed.addBindValue(nowIso());
        markFailed.addBindValue(jobId);
        markFailed.exec();

        m_lastError = errorMessage;
    }

    m_processing = false;
    emit statusChanged();
}

bool CloudSyncService::processJobRecord(const QVariantMap& jobRecord, QString* errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    const QString provider = jobRecord.value(QStringLiteral("provider")).toString();
    const QString jobType = jobRecord.value(QStringLiteral("jobType")).toString();
    const QString payloadJson = jobRecord.value(QStringLiteral("payloadJson")).toString();
    const QVariantMap payload = QJsonDocument::fromJson(payloadJson.toUtf8()).object().toVariantMap();

    if (jobType == QStringLiteral("file_upload")) {
        return processFileUploadJob(provider, payload, errorMessage);
    }
    if (jobType == QStringLiteral("file_delete")) {
        return processFileDeleteJob(provider, payload, errorMessage);
    }
    if (jobType == QStringLiteral("catalog_sync")) {
        return processCatalogSyncJob(provider, payload, errorMessage);
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Unknown cloud sync job type: %1").arg(jobType);
    }
    return false;
}

bool CloudSyncService::processFileUploadJob(const QString& provider,
                                            const QVariantMap& payload,
                                            QString* errorMessage)
{
    const int fileId = payload.value(QStringLiteral("fileId")).toInt();
    if (fileId < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid file id for upload job");
        }
        return false;
    }

    const QString normalizedProvider = normalizeProvider(provider);
    if (normalizedProvider.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unsupported cloud provider");
        }
        return false;
    }

    ProviderConnection connection;
    if (!loadConnectionRecord(normalizedProvider, &connection, errorMessage)) {
        return false;
    }

    if (!ensureFreshAccessToken(&connection, errorMessage)) {
        return false;
    }

    const std::unique_ptr<CloudProvider> providerImpl = createCloudProvider(normalizedProvider);
    if (!providerImpl) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cloud provider implementation unavailable");
        }
        return false;
    }

    QSqlQuery fileQuery(DatabaseManager::instance().database());
    fileQuery.prepare(QStringLiteral(R"(
        SELECT id, path, name, mime_type, technical_domain, subject, subtopic
        FROM files
        WHERE id = ?
    )"));
    fileQuery.addBindValue(fileId);
    if (!fileQuery.exec() || !fileQuery.next()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("File does not exist in local index");
        }
        return false;
    }

    QVariantMap fileDetails;
    fileDetails[QStringLiteral("id")] = fileQuery.value(0).toInt();
    fileDetails[QStringLiteral("path")] = fileQuery.value(1).toString();
    fileDetails[QStringLiteral("name")] = fileQuery.value(2).toString();
    fileDetails[QStringLiteral("mimeType")] = fileQuery.value(3).toString();
    fileDetails[QStringLiteral("technicalDomain")] = fileQuery.value(4).toString();
    fileDetails[QStringLiteral("subject")] = fileQuery.value(5).toString();
    fileDetails[QStringLiteral("subtopic")] = fileQuery.value(6).toString();

    const QString localPath = fileDetails.value(QStringLiteral("path")).toString();
    QFileInfo localInfo(localPath);
    if (!localInfo.exists() || !localInfo.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Linked local file is missing");
        }
        return false;
    }

    QFile localFile(localPath);
    if (!localFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to read local file for sync");
        }
        return false;
    }
    const QByteArray fileContent = localFile.readAll();
    localFile.close();

    QString contentType = fileDetails.value(QStringLiteral("mimeType")).toString().trimmed();
    if (contentType.isEmpty()) {
        contentType = QMimeDatabase().mimeTypeForFile(localInfo).name();
    }
    if (contentType.isEmpty()) {
        contentType = QStringLiteral("application/octet-stream");
    }

    const QString relativeCloudPath = computeCloudRelativePath(fileDetails);
    const QString fullCloudPath =
        cloudRootPrefix(connection.accountEmail) + QStringLiteral("/library/") + relativeCloudPath;

    const QString mirrorAbsolutePath = AppConfig::appDataDirectory()
                                       + QStringLiteral("/cloud_mirror/")
                                       + normalizedProvider
                                       + QStringLiteral("/")
                                       + fullCloudPath;
    if (!writeMirrorFile(mirrorAbsolutePath, fileContent, errorMessage)) {
        return false;
    }

    QString cloudItemId;
    if (!providerImpl->upsertFileContent(connection.accessToken,
                                         fullCloudPath,
                                         fileContent,
                                         contentType,
                                         &cloudItemId,
                                         errorMessage)) {
        return false;
    }

    if (fileContent.isEmpty() && localInfo.size() > 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to read local file bytes");
        }
        return false;
    }

    const QString contentHash = QString::fromLatin1(
        QCryptographicHash::hash(fileContent, QCryptographicHash::Sha256).toHex());

    QSqlQuery upsertMap(DatabaseManager::instance().database());
    upsertMap.prepare(QStringLiteral(R"(
        INSERT INTO cloud_file_map(
            file_id, provider, cloud_item_id, cloud_path, content_hash, last_synced_at
        ) VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(file_id, provider) DO UPDATE SET
            cloud_item_id = excluded.cloud_item_id,
            cloud_path = excluded.cloud_path,
            content_hash = excluded.content_hash,
            last_synced_at = excluded.last_synced_at
    )"));
    upsertMap.addBindValue(fileId);
    upsertMap.addBindValue(normalizedProvider);
    upsertMap.addBindValue(cloudItemId);
    upsertMap.addBindValue(fullCloudPath);
    upsertMap.addBindValue(contentHash);
    upsertMap.addBindValue(nowIso());

    if (!upsertMap.exec()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to update cloud file mapping: %1")
                                .arg(upsertMap.lastError().text());
        }
        return false;
    }

    return true;
}

bool CloudSyncService::processFileDeleteJob(const QString& provider,
                                            const QVariantMap& payload,
                                            QString* errorMessage)
{
    const int fileId = payload.value(QStringLiteral("fileId")).toInt();
    if (fileId < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid file id for delete job");
        }
        return false;
    }

    const QString normalizedProvider = normalizeProvider(provider);
    if (normalizedProvider.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unsupported cloud provider");
        }
        return false;
    }

    QSqlQuery mapQuery(DatabaseManager::instance().database());
    mapQuery.prepare(QStringLiteral(
        "SELECT cloud_item_id, cloud_path FROM cloud_file_map WHERE file_id = ? AND provider = ?"));
    mapQuery.addBindValue(fileId);
    mapQuery.addBindValue(normalizedProvider);

    QString cloudItemId;
    QString cloudPath;
    if (mapQuery.exec() && mapQuery.next()) {
        cloudItemId = mapQuery.value(0).toString();
        cloudPath = mapQuery.value(1).toString();
    }

    const QRegularExpression legacySyntheticId(
        QStringLiteral("^%1_\\d+$").arg(QRegularExpression::escape(normalizedProvider)));
    if (legacySyntheticId.match(cloudItemId.trimmed()).hasMatch()) {
        cloudItemId.clear();
    }

    if (!cloudPath.trimmed().isEmpty()) {
        const QString mirrorAbsolute = AppConfig::appDataDirectory()
                                       + QStringLiteral("/cloud_mirror/")
                                       + normalizedProvider
                                       + QStringLiteral("/")
                                       + cloudPath;
        QFile::remove(mirrorAbsolute);
    }

    ProviderConnection connection;
    const bool hasConnection = loadConnectionRecord(normalizedProvider, &connection, nullptr);
    if (hasConnection) {
        if (!ensureFreshAccessToken(&connection, errorMessage)) {
            return false;
        }

        const std::unique_ptr<CloudProvider> providerImpl = createCloudProvider(normalizedProvider);
        if (!providerImpl) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Cloud provider implementation unavailable");
            }
            return false;
        }

        if (!providerImpl->trashItem(connection.accessToken, cloudItemId, cloudPath, errorMessage)) {
            return false;
        }
    }

    QSqlQuery removeMap(DatabaseManager::instance().database());
    removeMap.prepare(QStringLiteral(
        "DELETE FROM cloud_file_map WHERE file_id = ? AND provider = ?"));
    removeMap.addBindValue(fileId);
    removeMap.addBindValue(normalizedProvider);
    if (!removeMap.exec()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to delete cloud mapping");
        }
        return false;
    }

    return true;
}

bool CloudSyncService::processCatalogSyncJob(const QString& provider,
                                             const QVariantMap& payload,
                                             QString* errorMessage)
{
    Q_UNUSED(payload);

    const QString normalizedProvider = normalizeProvider(provider);
    if (normalizedProvider.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unsupported cloud provider");
        }
        return false;
    }

    ProviderConnection connection;
    if (!loadConnectionRecord(normalizedProvider, &connection, errorMessage)) {
        return false;
    }

    if (!ensureFreshAccessToken(&connection, errorMessage)) {
        return false;
    }

    const std::unique_ptr<CloudProvider> providerImpl = createCloudProvider(normalizedProvider);
    if (!providerImpl) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cloud provider implementation unavailable");
        }
        return false;
    }

    const QString rootPrefix = cloudRootPrefix(connection.accountEmail);
    const QString mirrorCatalogRoot = AppConfig::appDataDirectory()
                                      + QStringLiteral("/cloud_mirror/")
                                      + normalizedProvider
                                      + QStringLiteral("/")
                                      + rootPrefix
                                      + QStringLiteral("/catalog");

    QDir rootDir(mirrorCatalogRoot);
    if (!rootDir.exists() && !rootDir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to create local catalog mirror folder");
        }
        return false;
    }

    auto writeCatalog = [&](const QString& fileName, const QVariantList& data) -> bool {
        const QByteArray jsonBytes =
            QJsonDocument(QJsonArray::fromVariantList(data)).toJson(QJsonDocument::Indented);
        const QString localPath = mirrorCatalogRoot + QStringLiteral("/") + fileName;
        if (!writeMirrorFile(localPath, jsonBytes, errorMessage)) {
            return false;
        }

        const QString remotePath = rootPrefix + QStringLiteral("/catalog/") + fileName;
        return providerImpl->upsertFileContent(connection.accessToken,
                                               remotePath,
                                               jsonBytes,
                                               QStringLiteral("application/json"),
                                               nullptr,
                                               errorMessage);
    };

    return writeCatalog(QStringLiteral("files.json"), exportFilesCatalog())
           && writeCatalog(QStringLiteral("collections.json"), exportCollectionsCatalog())
           && writeCatalog(QStringLiteral("hierarchy.json"), exportHierarchyCatalog())
           && writeCatalog(QStringLiteral("annotations.json"), exportAnnotationsCatalog());
}

QString CloudSyncService::computeCloudRelativePath(const QVariantMap& fileDetails) const
{
    auto normalizeSegment = [](const QString& value, const QString& fallback) {
        QString normalized = value.trimmed();
        if (normalized.isEmpty()) {
            normalized = fallback;
        }
        normalized.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
        normalized = normalized.left(64);
        return normalized;
    };

    const QString domain = normalizeSegment(fileDetails.value(QStringLiteral("technicalDomain")).toString(),
                                            QStringLiteral("Uncategorized"));
    const QString subject = normalizeSegment(fileDetails.value(QStringLiteral("subject")).toString(),
                                             QStringLiteral("General"));
    const QString subtopic = normalizeSegment(fileDetails.value(QStringLiteral("subtopic")).toString(),
                                              QStringLiteral("Misc"));
    const QString name = normalizeSegment(fileDetails.value(QStringLiteral("name")).toString(),
                                          QStringLiteral("file"));

    const int fileId = fileDetails.value(QStringLiteral("id")).toInt();
    return QStringLiteral("%1/%2/%3/%4__%5")
        .arg(domain, subject, subtopic, QString::number(fileId), name);
}

QVariantList CloudSyncService::exportFilesCatalog() const
{
    QVariantList list;
    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral(R"(
        SELECT id, path, name, extension, mime_type, size_bytes, indexed_at, status,
               technical_domain, subject, subtopic, location, source, author, document_type, remarks
        FROM files
        ORDER BY indexed_at DESC, id DESC
    )"));

    if (!query.exec()) {
        return list;
    }

    while (query.next()) {
        QVariantMap item;
        item[QStringLiteral("id")] = query.value(0).toInt();
        item[QStringLiteral("path")] = query.value(1).toString();
        item[QStringLiteral("name")] = query.value(2).toString();
        item[QStringLiteral("extension")] = query.value(3).toString();
        item[QStringLiteral("mimeType")] = query.value(4).toString();
        item[QStringLiteral("sizeBytes")] = query.value(5).toLongLong();
        item[QStringLiteral("indexedAt")] = query.value(6).toString();
        item[QStringLiteral("status")] = query.value(7).toInt();
        item[QStringLiteral("technicalDomain")] = query.value(8).toString();
        item[QStringLiteral("subject")] = query.value(9).toString();
        item[QStringLiteral("subtopic")] = query.value(10).toString();
        item[QStringLiteral("location")] = query.value(11).toString();
        item[QStringLiteral("source")] = query.value(12).toString();
        item[QStringLiteral("author")] = query.value(13).toString();
        item[QStringLiteral("documentType")] = query.value(14).toString();
        item[QStringLiteral("remarks")] = query.value(15).toString();
        list.append(item);
    }

    return list;
}

QVariantList CloudSyncService::exportCollectionsCatalog() const
{
    QVariantList list;

    QSqlQuery collectionsQuery(DatabaseManager::instance().database());
    collectionsQuery.prepare(QStringLiteral(
        "SELECT id, name, parent_collection_id FROM collections ORDER BY id ASC"));
    if (collectionsQuery.exec()) {
        while (collectionsQuery.next()) {
            QVariantMap item;
            item[QStringLiteral("kind")] = QStringLiteral("collection");
            item[QStringLiteral("id")] = collectionsQuery.value(0).toInt();
            item[QStringLiteral("name")] = collectionsQuery.value(1).toString();
            item[QStringLiteral("parentCollectionId")] = collectionsQuery.value(2);
            list.append(item);
        }
    }

    QSqlQuery assignmentsQuery(DatabaseManager::instance().database());
    assignmentsQuery.prepare(QStringLiteral(
        "SELECT file_id, collection_id FROM file_collections ORDER BY collection_id ASC, file_id ASC"));
    if (assignmentsQuery.exec()) {
        while (assignmentsQuery.next()) {
            QVariantMap item;
            item[QStringLiteral("kind")] = QStringLiteral("assignment");
            item[QStringLiteral("fileId")] = assignmentsQuery.value(0).toInt();
            item[QStringLiteral("collectionId")] = assignmentsQuery.value(1).toInt();
            list.append(item);
        }
    }

    QSqlQuery rulesQuery(DatabaseManager::instance().database());
    rulesQuery.prepare(QStringLiteral(
        "SELECT id, collection_id, field_name, operator_type, value FROM collection_rules ORDER BY id ASC"));
    if (rulesQuery.exec()) {
        while (rulesQuery.next()) {
            QVariantMap item;
            item[QStringLiteral("kind")] = QStringLiteral("rule");
            item[QStringLiteral("id")] = rulesQuery.value(0).toInt();
            item[QStringLiteral("collectionId")] = rulesQuery.value(1).toInt();
            item[QStringLiteral("fieldName")] = rulesQuery.value(2).toString();
            item[QStringLiteral("operatorType")] = rulesQuery.value(3).toString();
            item[QStringLiteral("value")] = rulesQuery.value(4).toString();
            list.append(item);
        }
    }

    return list;
}

QVariantList CloudSyncService::exportHierarchyCatalog() const
{
    QVariantList list;

    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral(R"(
        SELECT DISTINCT technical_domain, subject, subtopic
        FROM files
        WHERE IFNULL(technical_domain, '') <> ''
        ORDER BY technical_domain COLLATE NOCASE, subject COLLATE NOCASE, subtopic COLLATE NOCASE
    )"));

    if (!query.exec()) {
        return list;
    }

    while (query.next()) {
        QVariantMap item;
        item[QStringLiteral("technicalDomain")] = query.value(0).toString();
        item[QStringLiteral("subject")] = query.value(1).toString();
        item[QStringLiteral("subtopic")] = query.value(2).toString();
        list.append(item);
    }

    return list;
}

QVariantList CloudSyncService::exportAnnotationsCatalog() const
{
    QVariantList list;

    QSqlQuery annotationQuery(DatabaseManager::instance().database());
    annotationQuery.prepare(QStringLiteral(R"(
        SELECT id, file_id, target_type, annotation_type, page_number,
               char_start, char_end, anchor_text, x, y, width, height,
               color, content, time_start_ms, time_end_ms, created_at, updated_at
        FROM annotations
        ORDER BY file_id ASC, id ASC
    )"));
    if (annotationQuery.exec()) {
        while (annotationQuery.next()) {
            QVariantMap item;
            item[QStringLiteral("kind")] = QStringLiteral("annotation");
            item[QStringLiteral("id")] = annotationQuery.value(0).toInt();
            item[QStringLiteral("fileId")] = annotationQuery.value(1).toInt();
            item[QStringLiteral("targetType")] = annotationQuery.value(2).toString();
            item[QStringLiteral("annotationType")] = annotationQuery.value(3).toString();
            item[QStringLiteral("pageNumber")] = annotationQuery.value(4);
            item[QStringLiteral("charStart")] = annotationQuery.value(5);
            item[QStringLiteral("charEnd")] = annotationQuery.value(6);
            item[QStringLiteral("anchorText")] = annotationQuery.value(7).toString();
            item[QStringLiteral("x")] = annotationQuery.value(8).toDouble();
            item[QStringLiteral("y")] = annotationQuery.value(9).toDouble();
            item[QStringLiteral("width")] = annotationQuery.value(10).toDouble();
            item[QStringLiteral("height")] = annotationQuery.value(11).toDouble();
            item[QStringLiteral("color")] = annotationQuery.value(12).toString();
            item[QStringLiteral("content")] = annotationQuery.value(13).toString();
            item[QStringLiteral("timeStartMs")] = annotationQuery.value(14);
            item[QStringLiteral("timeEndMs")] = annotationQuery.value(15);
            item[QStringLiteral("createdAt")] = annotationQuery.value(16).toString();
            item[QStringLiteral("updatedAt")] = annotationQuery.value(17).toString();
            list.append(item);
        }
    }

    QSqlQuery notesQuery(DatabaseManager::instance().database());
    notesQuery.prepare(QStringLiteral(R"(
        SELECT id, file_id, title, body, created_at, updated_at
        FROM document_notes
        ORDER BY file_id ASC, id ASC
    )"));
    if (notesQuery.exec()) {
        while (notesQuery.next()) {
            QVariantMap item;
            item[QStringLiteral("kind")] = QStringLiteral("document_note");
            item[QStringLiteral("id")] = notesQuery.value(0).toInt();
            item[QStringLiteral("fileId")] = notesQuery.value(1).toInt();
            item[QStringLiteral("title")] = notesQuery.value(2).toString();
            item[QStringLiteral("body")] = notesQuery.value(3).toString();
            item[QStringLiteral("createdAt")] = notesQuery.value(4).toString();
            item[QStringLiteral("updatedAt")] = notesQuery.value(5).toString();
            list.append(item);
        }
    }

    return list;
}
