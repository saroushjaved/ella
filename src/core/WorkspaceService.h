#pragma once
#include <QObject>
#include <QFutureWatcher>
#include <QVariantMap>
#include <QVariantList>
#include <functional>

class WorkspaceService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    explicit WorkspaceService(QObject* parent = nullptr);
    ~WorkspaceService() override;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    Q_INVOKABLE QVariantMap backupLibrary(const QString& destination);
    Q_INVOKABLE QVariantMap restoreLibrary(const QString& backupFile);
    Q_INVOKABLE QVariantMap exportKnowledge(const QString& destination);
    Q_INVOKABLE QVariantMap storageStatus() const;
    Q_INVOKABLE QVariantMap clearDerivedCaches();
    Q_INVOKABLE bool clearHistory();
    Q_INVOKABLE QVariantMap createSourceNote(int fileId, const QString& title, const QString& quote, const QVariantMap& anchor);
    Q_INVOKABLE QVariantList backlinks(int fileId) const;
    Q_INVOKABLE bool openReleasePage() const;
    static bool applyPendingRestore(QString* error);
signals:
    void busyChanged();
    void statusChanged();
    void operationFinished(const QVariantMap& result);
    void notesChanged();
private:
    QVariantMap launch(const QString& label, std::function<QVariantMap()> operation);
    bool m_busy = false;
    QString m_status;
    QFutureWatcher<QVariantMap> m_worker;
};
