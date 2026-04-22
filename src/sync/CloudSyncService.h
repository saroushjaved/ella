#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class CloudSyncService : public QObject
{
    Q_OBJECT

public:
    explicit CloudSyncService(QObject* parent = nullptr);

    Q_INVOKABLE bool connectProvider(const QString& provider,
                                     const QString& accountEmail,
                                     const QString& accessToken,
                                     const QString& refreshToken,
                                     const QString& expiresAtIso);
    Q_INVOKABLE bool disconnectProvider(const QString& provider);
    Q_INVOKABLE QVariantMap beginOAuthConnect(const QString& provider, const QString& redirectUri);
    Q_INVOKABLE bool completeOAuthConnect(const QString& provider,
                                          const QString& authorizationCode,
                                          const QString& redirectUri,
                                          const QString& codeVerifier);
    Q_INVOKABLE QVariantMap oauthConfigurationStatus(const QString& provider) const;

    Q_INVOKABLE void syncNow();
    Q_INVOKABLE QVariantMap status() const;
    Q_INVOKABLE QVariantList providerStatuses() const;

    void enqueueFileUpload(int fileId, const QString& reason);
    void enqueueFileDelete(int fileId, const QString& reason);
    void enqueueCatalogSync(const QString& reason);

signals:
    void statusChanged();

private:
    QString normalizeProvider(const QString& provider) const;
    QVariantList connectedProviders() const;

    bool enqueueJob(const QString& provider,
                    const QString& jobType,
                    const QVariantMap& payload);
    void processNextJob();
    bool processJobRecord(const QVariantMap& jobRecord, QString* errorMessage);

    bool processFileUploadJob(const QString& provider,
                              const QVariantMap& payload,
                              QString* errorMessage);
    bool processFileDeleteJob(const QString& provider,
                              const QVariantMap& payload,
                              QString* errorMessage);
    bool processCatalogSyncJob(const QString& provider,
                               const QVariantMap& payload,
                               QString* errorMessage);

    QString computeCloudRelativePath(const QVariantMap& fileDetails) const;
    QVariantList exportFilesCatalog() const;
    QVariantList exportCollectionsCatalog() const;
    QVariantList exportHierarchyCatalog() const;
    QVariantList exportAnnotationsCatalog() const;

    bool m_processing = false;
    QString m_lastError;
    QString m_lastSyncAt;
    QTimer m_workerTimer;
};
