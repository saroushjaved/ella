#pragma once

#include <QObject>
#include <QVariantList>
#include <QUrl>
#include <QHash>
#include <QPointer>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QFile;
class QCryptographicHash;

// Optional tools are installed only after an explicit user action. The core never
// fetches a catalogue, executable or model at startup.
class ComponentManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList components READ components NOTIFY componentsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString activity READ activity NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
public:
    explicit ComponentManager(QObject* parent = nullptr);
    ~ComponentManager() override;
    QVariantList components() const;
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString activity() const { return m_activity; }
    QString lastError() const { return m_error; }
    Q_INVOKABLE bool loadManifest(const QUrl& localFile);
    Q_INVOKABLE bool useLocalPath(const QString& id, const QUrl& localFile);
    Q_INVOKABLE void install(const QString& id);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool remove(const QString& id);
    Q_INVOKABLE void refresh();
    static bool isSafeRelativePath(const QString& path);
signals:
    void componentsChanged();
    void stateChanged();
    void componentInstalled(const QString& id);
private:
    struct Entry {
        QString id, name, description, environment, executable, version, hash, entrypoint;
        QUrl url;
        qint64 downloadBytes = 0, installedBytes = 0;
    };
    QString root() const;
    QString configuredPath(const Entry& entry) const;
    bool fail(const QString& error);
    bool removeOwnedDirectory(const QString& path);
    void drainDownload();
    void extractDownload();
    void finishInstallation();
    void publishEnvironment();
    QHash<QString, Entry> m_entries;
    QStringList m_order;
    QNetworkAccessManager* m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QPointer<QProcess> m_extract;
    std::unique_ptr<QFile> m_download;
    std::unique_ptr<QCryptographicHash> m_hash;
    Entry m_active;
    QString m_workDir, m_activity, m_error;
    bool m_busy = false;
    double m_progress = 0;
    qint64 m_received = 0;
};
