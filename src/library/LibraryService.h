#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QFileSystemWatcher>
#include <QTimer>
#include <atomic>
#include <memory>

class IndexingService;

class LibraryService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap activity READ activity NOTIFY activityChanged)
public:
    explicit LibraryService(QObject* parent=nullptr);
    ~LibraryService() override;
    void setIndexingService(IndexingService* service) { m_indexing=service; }
    QVariantMap activity() const { return m_activity; }
    Q_INVOKABLE QVariantList watchedFolders() const;
    Q_INVOKABLE bool removeWatchedFolder(int rootId);
    Q_INVOKABLE void previewImport(const QVariantList& paths, const QStringList& exclusions);
    Q_INVOKABLE void startImport(const QVariantList& paths, const QStringList& exclusions, bool watchFolders);
    Q_INVOKABLE void scanNow();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QVariantList favorites() const;
    Q_INVOKABLE bool isFavorite(int fileId) const;
    Q_INVOKABLE bool setFavorite(int fileId, bool favorite);
    Q_INVOKABLE QStringList tags(int fileId) const;
    Q_INVOKABLE bool setTags(int fileId, const QStringList& tags);
    Q_INVOKABLE QVariantList savedSearches() const;
    Q_INVOKABLE bool saveSearch(const QString& name, const QString& query);
    Q_INVOKABLE bool deleteSavedSearch(int id);
    Q_INVOKABLE QVariantMap readingPosition(int fileId) const;
    Q_INVOKABLE bool saveReadingPosition(int fileId, const QVariantMap& anchor);
    Q_INVOKABLE QVariantList duplicates() const;
    Q_INVOKABLE QVariantList missingSources() const;
    Q_INVOKABLE bool relinkFile(int fileId, const QString& path);
    Q_INVOKABLE bool removeFile(int fileId);
    Q_INVOKABLE bool undoRemoval();
    Q_INVOKABLE bool clearHistory();
signals:
    void changed();
    void filesChanged();
    void activityChanged();
    void previewReady(const QVariantMap& preview);
private:
    void runJob(int id, const QVariantMap& payload);
    void rebuildWatches();
    void restoreJobs();
    void updateJobStatus(const QString& status);
    QVariantMap m_activity;
    int m_jobId=-1;
    int m_removedId=-1;
    std::shared_ptr<std::atomic_int> m_control;
    QFileSystemWatcher m_watcher;
    QTimer m_reconcile;
    QTimer m_watchDebounce;
    QTimer m_progressPoll;
    IndexingService* m_indexing=nullptr;
};
