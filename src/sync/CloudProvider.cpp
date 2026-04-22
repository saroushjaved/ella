#include "sync/CloudProvider.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace
{
QString base64UrlEncode(const QByteArray& data)
{
    return QString::fromLatin1(
        data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString randomUrlSafeToken(int byteCount)
{
    QByteArray randomBytes;
    randomBytes.resize(byteCount);
    for (int i = 0; i < byteCount; ++i) {
        randomBytes[i] = static_cast<char>(QRandomGenerator::global()->bounded(0, 256));
    }
    return base64UrlEncode(randomBytes);
}

bool executeRequest(QNetworkReply* reply,
                    QByteArray* responseBytes,
                    QString* errorMessage,
                    int timeoutMs = 30000)
{
    if (!reply) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No network reply object was created");
        }
        return false;
    }

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        reply->abort();
        loop.quit();
    });

    timeoutTimer.start(timeoutMs);
    loop.exec();
    timeoutTimer.stop();

    const QByteArray body = reply->readAll();
    if (responseBytes) {
        *responseBytes = body;
    }

    const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int httpStatus = statusCode.isValid() ? statusCode.toInt() : 0;
    const bool okStatus = httpStatus >= 200 && httpStatus < 300;

    if (timedOut) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Network request timed out");
        }
        reply->deleteLater();
        return false;
    }

    if (reply->error() != QNetworkReply::NoError || !okStatus) {
        if (errorMessage) {
            QString detail = QString::fromUtf8(body).trimmed();
            if (detail.isEmpty()) {
                detail = reply->errorString();
            }
            *errorMessage = QStringLiteral("HTTP %1: %2").arg(httpStatus).arg(detail);
        }
        reply->deleteLater();
        return false;
    }

    reply->deleteLater();
    return true;
}

bool executeJsonGet(const QUrl& url,
                    const QString& accessToken,
                    QJsonObject* object,
                    QString* errorMessage)
{
    if (object) {
        object->empty();
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QByteArray responseBytes;
    QNetworkReply* reply = manager.get(request);
    if (!executeRequest(reply, &responseBytes, errorMessage)) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(responseBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("Unable to parse provider JSON response: %1").arg(parseError.errorString());
        }
        return false;
    }

    if (object) {
        *object = json.object();
    }
    return true;
}

bool executeJsonRequest(const QString& method,
                        const QUrl& url,
                        const QString& accessToken,
                        const QByteArray& bodyBytes,
                        const QString& contentType,
                        QJsonObject* object,
                        QString* errorMessage)
{
    if (object) {
        object->empty();
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());
    if (!contentType.trimmed().isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, contentType.trimmed());
    }

    QNetworkReply* reply = nullptr;
    const QString normalizedMethod = method.trimmed().toUpper();
    if (normalizedMethod == QStringLiteral("GET")) {
        reply = manager.get(request);
    } else if (normalizedMethod == QStringLiteral("POST")) {
        reply = manager.post(request, bodyBytes);
    } else if (normalizedMethod == QStringLiteral("PUT")) {
        reply = manager.put(request, bodyBytes);
    } else if (normalizedMethod == QStringLiteral("PATCH")) {
        reply = manager.sendCustomRequest(request, "PATCH", bodyBytes);
    } else if (normalizedMethod == QStringLiteral("DELETE")) {
        reply = manager.deleteResource(request);
    } else {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unsupported HTTP method: %1").arg(normalizedMethod);
        }
        return false;
    }

    QByteArray responseBytes;
    if (!executeRequest(reply, &responseBytes, errorMessage, 45000)) {
        return false;
    }

    if (!object) {
        return true;
    }

    if (responseBytes.trimmed().isEmpty()) {
        object->empty();
        return true;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(responseBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to parse JSON response: %1").arg(parseError.errorString());
        }
        return false;
    }

    *object = json.object();
    return true;
}

QString urlEncodePathPreservingSlash(const QString& path)
{
    QString normalized = path;
    while (normalized.startsWith('/')) {
        normalized.remove(0, 1);
    }
    while (normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return QString::fromLatin1(QUrl::toPercentEncoding(normalized, "/"));
}

QString sanitizeCloudPath(const QString& cloudPath)
{
    QString normalized = cloudPath.trimmed();
    normalized.replace('\\', '/');
    while (normalized.startsWith('/')) {
        normalized.remove(0, 1);
    }
    while (normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized;
}

QStringList splitCloudPath(const QString& cloudPath)
{
    const QString normalized = sanitizeCloudPath(cloudPath);
    if (normalized.isEmpty()) {
        return {};
    }
    return normalized.split('/', Qt::SkipEmptyParts);
}

QString escapeGoogleQueryLiteral(const QString& value)
{
    QString escaped = value;
    escaped.replace("\\", "\\\\");
    escaped.replace("'", "\\'");
    return escaped;
}

bool googleFindByNameInParent(const QString& accessToken,
                              const QString& parentId,
                              const QString& itemName,
                              const QString& mimeTypeFilter,
                              QString* itemId,
                              QString* errorMessage)
{
    if (itemId) {
        itemId->clear();
    }

    QUrl queryUrl(QStringLiteral("https://www.googleapis.com/drive/v3/files"));
    QUrlQuery query;

    QString condition = QStringLiteral("trashed = false and '%1' in parents and name = '%2'")
                            .arg(escapeGoogleQueryLiteral(parentId),
                                 escapeGoogleQueryLiteral(itemName));
    if (!mimeTypeFilter.trimmed().isEmpty()) {
        condition += QStringLiteral(" and mimeType = '%1'")
                         .arg(escapeGoogleQueryLiteral(mimeTypeFilter.trimmed()));
    }

    query.addQueryItem(QStringLiteral("q"), condition);
    query.addQueryItem(QStringLiteral("spaces"), QStringLiteral("drive"));
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("files(id,name,mimeType)"));
    queryUrl.setQuery(query);

    QJsonObject response;
    if (!executeJsonGet(queryUrl, accessToken, &response, errorMessage)) {
        return false;
    }

    const QJsonArray files = response.value(QStringLiteral("files")).toArray();
    if (files.isEmpty()) {
        return true;
    }

    const QString foundId = files.at(0).toObject().value(QStringLiteral("id")).toString().trimmed();
    if (itemId) {
        *itemId = foundId;
    }
    return true;
}

bool googleCreateFolder(const QString& accessToken,
                        const QString& parentId,
                        const QString& folderName,
                        QString* folderId,
                        QString* errorMessage)
{
    if (folderId) {
        folderId->clear();
    }

    QJsonObject metadata;
    metadata.insert(QStringLiteral("name"), folderName);
    metadata.insert(QStringLiteral("mimeType"), QStringLiteral("application/vnd.google-apps.folder"));
    metadata.insert(QStringLiteral("parents"), QJsonArray { parentId });

    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id"));
    url.setQuery(query);

    QJsonObject response;
    if (!executeJsonRequest(QStringLiteral("POST"),
                            url,
                            accessToken,
                            QJsonDocument(metadata).toJson(QJsonDocument::Compact),
                            QStringLiteral("application/json"),
                            &response,
                            errorMessage)) {
        return false;
    }

    const QString createdId = response.value(QStringLiteral("id")).toString().trimmed();
    if (createdId.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Google Drive folder creation did not return an id");
        }
        return false;
    }

    if (folderId) {
        *folderId = createdId;
    }
    return true;
}

bool googleEnsureFolderChain(const QString& accessToken,
                             const QStringList& folderSegments,
                             QString* folderId,
                             QString* errorMessage)
{
    if (folderId) {
        folderId->clear();
    }

    QString currentParentId = QStringLiteral("root");
    for (const QString& segment : folderSegments) {
        const QString folderName = segment.trimmed();
        if (folderName.isEmpty()) {
            continue;
        }

        QString existingId;
        if (!googleFindByNameInParent(accessToken,
                                      currentParentId,
                                      folderName,
                                      QStringLiteral("application/vnd.google-apps.folder"),
                                      &existingId,
                                      errorMessage)) {
            return false;
        }

        if (existingId.isEmpty()) {
            if (!googleCreateFolder(accessToken, currentParentId, folderName, &existingId, errorMessage)) {
                return false;
            }
        }

        currentParentId = existingId;
    }

    if (folderId) {
        *folderId = currentParentId;
    }
    return true;
}

bool googleCreateFileMultipart(const QString& accessToken,
                               const QString& parentId,
                               const QString& fileName,
                               const QByteArray& contentBytes,
                               const QString& contentType,
                               QString* fileId,
                               QString* errorMessage)
{
    if (fileId) {
        fileId->clear();
    }

    QJsonObject metadata;
    metadata.insert(QStringLiteral("name"), fileName);
    metadata.insert(QStringLiteral("parents"), QJsonArray { parentId });

    const QString boundary = QStringLiteral("ella_%1").arg(randomUrlSafeToken(12));
    QByteArray body;
    body.append("--").append(boundary.toUtf8()).append("\r\n");
    body.append("Content-Type: application/json; charset=UTF-8\r\n\r\n");
    body.append(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    body.append("\r\n");
    body.append("--").append(boundary.toUtf8()).append("\r\n");
    body.append("Content-Type: ").append(contentType.toUtf8()).append("\r\n\r\n");
    body.append(contentBytes);
    body.append("\r\n");
    body.append("--").append(boundary.toUtf8()).append("--\r\n");

    QUrl url(QStringLiteral("https://www.googleapis.com/upload/drive/v3/files"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("uploadType"), QStringLiteral("multipart"));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id"));
    url.setQuery(query);

    QJsonObject response;
    if (!executeJsonRequest(QStringLiteral("POST"),
                            url,
                            accessToken,
                            body,
                            QStringLiteral("multipart/related; boundary=%1").arg(boundary),
                            &response,
                            errorMessage)) {
        return false;
    }

    const QString createdId = response.value(QStringLiteral("id")).toString().trimmed();
    if (createdId.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Google Drive upload did not return a file id");
        }
        return false;
    }

    if (fileId) {
        *fileId = createdId;
    }
    return true;
}

bool googleUpdateFileContent(const QString& accessToken,
                             const QString& fileId,
                             const QByteArray& contentBytes,
                             const QString& contentType,
                             QString* errorMessage)
{
    QUrl url(QStringLiteral("https://www.googleapis.com/upload/drive/v3/files/%1").arg(fileId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("uploadType"), QStringLiteral("media"));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id"));
    url.setQuery(query);

    return executeJsonRequest(QStringLiteral("PATCH"),
                              url,
                              accessToken,
                              contentBytes,
                              contentType,
                              nullptr,
                              errorMessage);
}

bool googleTrashById(const QString& accessToken, const QString& fileId, QString* errorMessage)
{
    if (fileId.trimmed().isEmpty()) {
        return true;
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(fileId.trimmed()));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id"));
    url.setQuery(query);

    QJsonObject payload;
    payload.insert(QStringLiteral("trashed"), true);

    return executeJsonRequest(QStringLiteral("PATCH"),
                              url,
                              accessToken,
                              QJsonDocument(payload).toJson(QJsonDocument::Compact),
                              QStringLiteral("application/json"),
                              nullptr,
                              errorMessage);
}

bool googleResolveFileIdByPath(const QString& accessToken,
                               const QString& cloudPath,
                               QString* fileId,
                               QString* errorMessage)
{
    if (fileId) {
        fileId->clear();
    }

    const QStringList segments = splitCloudPath(cloudPath);
    if (segments.isEmpty()) {
        return true;
    }

    QString parentId = QStringLiteral("root");
    for (int i = 0; i < segments.size(); ++i) {
        const QString segment = segments.at(i);
        const bool isLast = (i == segments.size() - 1);
        QString foundId;

        if (!googleFindByNameInParent(accessToken,
                                      parentId,
                                      segment,
                                      isLast ? QString() : QStringLiteral("application/vnd.google-apps.folder"),
                                      &foundId,
                                      errorMessage)) {
            return false;
        }

        if (foundId.isEmpty()) {
            return true;
        }

        parentId = foundId;
    }

    if (fileId) {
        *fileId = parentId;
    }
    return true;
}

class GoogleDriveProvider final : public CloudProvider
{
public:
    QString providerId() const override { return QStringLiteral("google_drive"); }
    QString displayName() const override { return QStringLiteral("Google Drive"); }
    QString clientIdEnvVar() const override { return QStringLiteral("ELLA_GOOGLE_CLIENT_ID"); }
    QString clientSecretEnvVar() const override { return QStringLiteral("ELLA_GOOGLE_CLIENT_SECRET"); }

    bool validateAccessToken(const QString& accessToken,
                             QString* accountEmail,
                             QString* errorMessage) const override
    {
        if (accountEmail) {
            accountEmail->clear();
        }

        QJsonObject profile;
        if (!executeJsonGet(QUrl(QStringLiteral("https://www.googleapis.com/oauth2/v2/userinfo")),
                            accessToken,
                            &profile,
                            errorMessage)) {
            return false;
        }

        const QString email = profile.value(QStringLiteral("email")).toString().trimmed();
        if (email.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Google user profile did not include email");
            }
            return false;
        }

        if (accountEmail) {
            *accountEmail = email;
        }
        return true;
    }

    bool upsertFileContent(const QString& accessToken,
                           const QString& cloudPath,
                           const QByteArray& contentBytes,
                           const QString& contentType,
                           QString* cloudItemId,
                           QString* errorMessage) const override
    {
        if (cloudItemId) {
            cloudItemId->clear();
        }

        const QStringList pathSegments = splitCloudPath(cloudPath);
        if (pathSegments.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Google Drive cloud path is empty");
            }
            return false;
        }

        const QString fileName = pathSegments.last();
        QStringList folderSegments = pathSegments;
        folderSegments.removeLast();

        QString parentFolderId;
        if (!googleEnsureFolderChain(accessToken, folderSegments, &parentFolderId, errorMessage)) {
            return false;
        }

        QString existingFileId;
        if (!googleFindByNameInParent(accessToken,
                                      parentFolderId,
                                      fileName,
                                      QString(),
                                      &existingFileId,
                                      errorMessage)) {
            return false;
        }

        const QString normalizedContentType = contentType.trimmed().isEmpty()
                                                  ? QStringLiteral("application/octet-stream")
                                                  : contentType.trimmed();

        if (!existingFileId.isEmpty()) {
            if (!googleUpdateFileContent(accessToken,
                                         existingFileId,
                                         contentBytes,
                                         normalizedContentType,
                                         errorMessage)) {
                return false;
            }
            if (cloudItemId) {
                *cloudItemId = existingFileId;
            }
            return true;
        }

        return googleCreateFileMultipart(accessToken,
                                         parentFolderId,
                                         fileName,
                                         contentBytes,
                                         normalizedContentType,
                                         cloudItemId,
                                         errorMessage);
    }

    bool trashItem(const QString& accessToken,
                   const QString& cloudItemId,
                   const QString& cloudPath,
                   QString* errorMessage) const override
    {
        const QString explicitId = cloudItemId.trimmed();
        if (!explicitId.isEmpty()) {
            return googleTrashById(accessToken, explicitId, errorMessage);
        }

        QString resolvedId;
        if (!googleResolveFileIdByPath(accessToken, cloudPath, &resolvedId, errorMessage)) {
            return false;
        }
        if (resolvedId.isEmpty()) {
            return true;
        }
        return googleTrashById(accessToken, resolvedId, errorMessage);
    }

protected:
    QString authorizationEndpoint() const override
    {
        return QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth");
    }

    QString tokenEndpoint() const override
    {
        return QStringLiteral("https://oauth2.googleapis.com/token");
    }

    QStringList scopeList() const override
    {
        return {
            QStringLiteral("openid"),
            QStringLiteral("email"),
            QStringLiteral("profile"),
            QStringLiteral("https://www.googleapis.com/auth/drive.file"),
            QStringLiteral("https://www.googleapis.com/auth/drive.metadata.readonly")
        };
    }

    void amendAuthorizationQuery(QUrlQuery* query) const override
    {
        if (!query) {
            return;
        }
        query->addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
        query->addQueryItem(QStringLiteral("include_granted_scopes"), QStringLiteral("true"));
        query->addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
    }
};

class OneDriveProvider final : public CloudProvider
{
public:
    QString providerId() const override { return QStringLiteral("onedrive"); }
    QString displayName() const override { return QStringLiteral("OneDrive"); }
    QString clientIdEnvVar() const override { return QStringLiteral("ELLA_ONEDRIVE_CLIENT_ID"); }
    QString clientSecretEnvVar() const override { return QStringLiteral("ELLA_ONEDRIVE_CLIENT_SECRET"); }

    bool validateAccessToken(const QString& accessToken,
                             QString* accountEmail,
                             QString* errorMessage) const override
    {
        if (accountEmail) {
            accountEmail->clear();
        }

        QJsonObject me;
        if (!executeJsonGet(QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me")),
                            accessToken,
                            &me,
                            errorMessage)) {
            return false;
        }

        QString email = me.value(QStringLiteral("mail")).toString().trimmed();
        if (email.isEmpty()) {
            email = me.value(QStringLiteral("userPrincipalName")).toString().trimmed();
        }

        if (email.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Microsoft profile did not include an email");
            }
            return false;
        }

        if (accountEmail) {
            *accountEmail = email;
        }
        return true;
    }

    bool upsertFileContent(const QString& accessToken,
                           const QString& cloudPath,
                           const QByteArray& contentBytes,
                           const QString& contentType,
                           QString* cloudItemId,
                           QString* errorMessage) const override
    {
        if (cloudItemId) {
            cloudItemId->clear();
        }

        const QString normalizedPath = sanitizeCloudPath(cloudPath);
        if (normalizedPath.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("OneDrive cloud path is empty");
            }
            return false;
        }

        const QString encodedPath = urlEncodePathPreservingSlash(normalizedPath);
        QUrl url(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/root:/%1:/content")
                     .arg(encodedPath));

        QJsonObject response;
        if (!executeJsonRequest(QStringLiteral("PUT"),
                                url,
                                accessToken,
                                contentBytes,
                                contentType.trimmed().isEmpty()
                                    ? QStringLiteral("application/octet-stream")
                                    : contentType.trimmed(),
                                &response,
                                errorMessage)) {
            return false;
        }

        if (cloudItemId) {
            *cloudItemId = response.value(QStringLiteral("id")).toString().trimmed();
        }

        return true;
    }

    bool trashItem(const QString& accessToken,
                   const QString& cloudItemId,
                   const QString& cloudPath,
                   QString* errorMessage) const override
    {
        QUrl url;
        const QString explicitId = cloudItemId.trimmed();
        if (!explicitId.isEmpty()) {
            url = QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1")
                           .arg(QString::fromLatin1(QUrl::toPercentEncoding(explicitId))));
        } else {
            const QString normalizedPath = sanitizeCloudPath(cloudPath);
            if (normalizedPath.isEmpty()) {
                return true;
            }
            url = QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/root:/%1")
                           .arg(urlEncodePathPreservingSlash(normalizedPath)));
        }

        return executeJsonRequest(QStringLiteral("DELETE"),
                                  url,
                                  accessToken,
                                  QByteArray(),
                                  QString(),
                                  nullptr,
                                  errorMessage);
    }

protected:
    QString authorizationEndpoint() const override
    {
        return QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/authorize");
    }

    QString tokenEndpoint() const override
    {
        return QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/token");
    }

    QStringList scopeList() const override
    {
        return {
            QStringLiteral("offline_access"),
            QStringLiteral("User.Read"),
            QStringLiteral("Files.ReadWrite.All")
        };
    }

    void amendTokenExchangeQuery(QUrlQuery* query) const override
    {
        if (!query) {
            return;
        }
        query->addQueryItem(QStringLiteral("scope"), scopeList().join(QStringLiteral(" ")));
    }
};
}

QVariantMap CloudProvider::beginOAuth(const QString& redirectUri) const
{
    QVariantMap result;
    result[QStringLiteral("ok")] = false;
    result[QStringLiteral("provider")] = providerId();
    result[QStringLiteral("displayName")] = displayName();

    const QString trimmedRedirectUri = redirectUri.trimmed();
    if (trimmedRedirectUri.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("Redirect URI is required");
        return result;
    }

    const QString clientId = qEnvironmentVariable(clientIdEnvVar().toUtf8().constData()).trimmed();
    if (clientId.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("Missing OAuth client id environment variable: %1")
                                              .arg(clientIdEnvVar());
        return result;
    }

    const QString codeVerifier = randomUrlSafeToken(48);
    const QString state = randomUrlSafeToken(24);
    const QString codeChallenge = base64UrlEncode(
        QCryptographicHash::hash(codeVerifier.toUtf8(), QCryptographicHash::Sha256));

    QUrl url(authorizationEndpoint());
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), clientId);
    query.addQueryItem(QStringLiteral("redirect_uri"), trimmedRedirectUri);
    query.addQueryItem(QStringLiteral("scope"), scopeList().join(QStringLiteral(" ")));
    query.addQueryItem(QStringLiteral("state"), state);
    query.addQueryItem(QStringLiteral("code_challenge"), codeChallenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    amendAuthorizationQuery(&query);

    url.setQuery(query);

    result[QStringLiteral("ok")] = true;
    result[QStringLiteral("authUrl")] = url.toString();
    result[QStringLiteral("redirectUri")] = trimmedRedirectUri;
    result[QStringLiteral("codeVerifier")] = codeVerifier;
    result[QStringLiteral("state")] = state;
    return result;
}

QVariantMap CloudProvider::completeOAuth(const QString& authorizationCode,
                                         const QString& redirectUri,
                                         const QString& codeVerifier) const
{
    QVariantMap result;
    result[QStringLiteral("ok")] = false;
    result[QStringLiteral("provider")] = providerId();
    result[QStringLiteral("displayName")] = displayName();

    const QString code = authorizationCode.trimmed();
    const QString redirect = redirectUri.trimmed();
    const QString verifier = codeVerifier.trimmed();

    if (code.isEmpty() || redirect.isEmpty() || verifier.isEmpty()) {
        result[QStringLiteral("error")] =
            QStringLiteral("Authorization code, redirect URI, and code verifier are required");
        return result;
    }

    const QString clientId = qEnvironmentVariable(clientIdEnvVar().toUtf8().constData()).trimmed();
    const QString clientSecret =
        qEnvironmentVariable(clientSecretEnvVar().toUtf8().constData()).trimmed();

    if (clientId.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("Missing OAuth client id: %1")
                                              .arg(clientIdEnvVar());
        return result;
    }

    if (clientSecret.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("Missing OAuth client secret: %1")
                                              .arg(clientSecretEnvVar());
        return result;
    }

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    query.addQueryItem(QStringLiteral("code"), code);
    query.addQueryItem(QStringLiteral("redirect_uri"), redirect);
    query.addQueryItem(QStringLiteral("client_id"), clientId);
    query.addQueryItem(QStringLiteral("client_secret"), clientSecret);
    query.addQueryItem(QStringLiteral("code_verifier"), verifier);
    amendTokenExchangeQuery(&query);

    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(tokenEndpoint())};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QByteArray responseBytes;
    QNetworkReply* reply = manager.post(request, query.toString(QUrl::FullyEncoded).toUtf8());
    QString requestError;
    if (!executeRequest(reply, &responseBytes, &requestError)) {
        result[QStringLiteral("error")] = QStringLiteral("Token exchange failed: %1").arg(requestError);
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(responseBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        result[QStringLiteral("error")] =
            QStringLiteral("Unable to parse OAuth token response: %1").arg(parseError.errorString());
        return result;
    }

    const QJsonObject token = json.object();
    const QString accessToken = token.value(QStringLiteral("access_token")).toString().trimmed();
    const QString refreshToken = token.value(QStringLiteral("refresh_token")).toString().trimmed();
    const int expiresInSeconds = token.value(QStringLiteral("expires_in")).toInt(3600);

    if (accessToken.isEmpty()) {
        result[QStringLiteral("error")] =
            QStringLiteral("OAuth token response did not include an access token");
        return result;
    }

    QString accountEmail;
    QString validationError;
    const bool validated = validateAccessToken(accessToken, &accountEmail, &validationError);

    result[QStringLiteral("ok")] = true;
    result[QStringLiteral("accessToken")] = accessToken;
    result[QStringLiteral("refreshToken")] = refreshToken;
    result[QStringLiteral("expiresAtIso")] =
        QDateTime::currentDateTime().addSecs(expiresInSeconds).toString(Qt::ISODate);
    result[QStringLiteral("accountEmail")] = accountEmail;
    result[QStringLiteral("validated")] = validated;
    if (!validated && !validationError.isEmpty()) {
        result[QStringLiteral("warning")] = validationError;
    }

    return result;
}

QVariantMap CloudProvider::refreshAccessToken(const QString& refreshToken) const
{
    QVariantMap result;
    result[QStringLiteral("ok")] = false;
    result[QStringLiteral("provider")] = providerId();
    result[QStringLiteral("displayName")] = displayName();

    const QString tokenValue = refreshToken.trimmed();
    if (tokenValue.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("Refresh token is empty");
        return result;
    }

    const QString clientId = qEnvironmentVariable(clientIdEnvVar().toUtf8().constData()).trimmed();
    const QString clientSecret =
        qEnvironmentVariable(clientSecretEnvVar().toUtf8().constData()).trimmed();

    if (clientId.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("Missing OAuth client id: %1")
                                              .arg(clientIdEnvVar());
        return result;
    }

    if (clientSecret.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("Missing OAuth client secret: %1")
                                              .arg(clientSecretEnvVar());
        return result;
    }

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    query.addQueryItem(QStringLiteral("refresh_token"), tokenValue);
    query.addQueryItem(QStringLiteral("client_id"), clientId);
    query.addQueryItem(QStringLiteral("client_secret"), clientSecret);
    amendTokenExchangeQuery(&query);

    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(tokenEndpoint())};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QByteArray responseBytes;
    QNetworkReply* reply = manager.post(request, query.toString(QUrl::FullyEncoded).toUtf8());
    QString requestError;
    if (!executeRequest(reply, &responseBytes, &requestError)) {
        result[QStringLiteral("error")] = QStringLiteral("Token refresh failed: %1").arg(requestError);
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(responseBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        result[QStringLiteral("error")] =
            QStringLiteral("Unable to parse OAuth refresh response: %1").arg(parseError.errorString());
        return result;
    }

    const QJsonObject token = json.object();
    const QString accessToken = token.value(QStringLiteral("access_token")).toString().trimmed();
    const QString newRefreshToken = token.value(QStringLiteral("refresh_token")).toString().trimmed();
    const int expiresInSeconds = token.value(QStringLiteral("expires_in")).toInt(3600);

    if (accessToken.isEmpty()) {
        result[QStringLiteral("error")] =
            QStringLiteral("OAuth refresh response did not include an access token");
        return result;
    }

    result[QStringLiteral("ok")] = true;
    result[QStringLiteral("accessToken")] = accessToken;
    result[QStringLiteral("refreshToken")] = newRefreshToken.isEmpty() ? tokenValue : newRefreshToken;
    result[QStringLiteral("expiresAtIso")] =
        QDateTime::currentDateTime().addSecs(expiresInSeconds).toString(Qt::ISODate);
    return result;
}

void CloudProvider::amendAuthorizationQuery(QUrlQuery* query) const
{
    Q_UNUSED(query);
}

void CloudProvider::amendTokenExchangeQuery(QUrlQuery* query) const
{
    Q_UNUSED(query);
}

std::unique_ptr<CloudProvider> createCloudProvider(const QString& providerId)
{
    const QString normalized = providerId.trimmed().toLower();
    if (normalized == QStringLiteral("google_drive")) {
        return std::make_unique<GoogleDriveProvider>();
    }
    if (normalized == QStringLiteral("onedrive")) {
        return std::make_unique<OneDriveProvider>();
    }
    return nullptr;
}
