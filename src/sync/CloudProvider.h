#pragma once

#include <QVariantMap>
#include <QJsonDocument>
#include <QString>
#include <QStringList>

#include <memory>

class CloudProvider
{
public:
    virtual ~CloudProvider() = default;

    virtual QString providerId() const = 0;
    virtual QString displayName() const = 0;
    virtual QString clientIdEnvVar() const = 0;
    virtual QString clientSecretEnvVar() const = 0;

    QVariantMap beginOAuth(const QString& redirectUri) const;
    QVariantMap completeOAuth(const QString& authorizationCode,
                              const QString& redirectUri,
                              const QString& codeVerifier) const;
    QVariantMap refreshAccessToken(const QString& refreshToken) const;

    virtual bool validateAccessToken(const QString& accessToken,
                                     QString* accountEmail,
                                     QString* errorMessage) const = 0;
    virtual bool upsertFileContent(const QString& accessToken,
                                   const QString& cloudPath,
                                   const QByteArray& contentBytes,
                                   const QString& contentType,
                                   QString* cloudItemId,
                                   QString* errorMessage) const = 0;
    virtual bool trashItem(const QString& accessToken,
                           const QString& cloudItemId,
                           const QString& cloudPath,
                           QString* errorMessage) const = 0;

protected:
    virtual QString authorizationEndpoint() const = 0;
    virtual QString tokenEndpoint() const = 0;
    virtual QStringList scopeList() const = 0;
    virtual void amendAuthorizationQuery(class QUrlQuery* query) const;
    virtual void amendTokenExchangeQuery(class QUrlQuery* query) const;
};

std::unique_ptr<CloudProvider> createCloudProvider(const QString& providerId);
