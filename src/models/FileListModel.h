#pragma once

#include "library/AnnotationRepository.h"
#include "library/FileRecord.h"
#include "library/FileRepository.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>
#include <QTimer>
#include <QThreadPool>
#include <atomic>
#include <memory>

class IndexingService;
class CloudSyncService;

class FileListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool cloudSyncExperimental READ cloudSyncExperimental CONSTANT)
    Q_PROPERTY(bool searching READ searching NOTIFY searchStateChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY searchStateChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY searchStateChanged)

public:
    enum FileRoles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        ExtensionRole,
        MimeTypeRole,
        SizeRole,
        IndexedAtRole,
        StatusRole,
        SourceRole,
        DocumentTypeRole,
        SearchSnippetRole,
        SearchMatchReasonRole,
        SearchScoreRole,
        SearchAnchorRole
    };

    explicit FileListModel(QObject* parent = nullptr);

    void setIndexingService(IndexingService* indexingService);
    void setCloudSyncService(CloudSyncService* cloudSyncService);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void refreshCurrentView();
    Q_INVOKABLE void search(const QString& text);
    bool searching() const { return m_searching; }
    bool hasMore() const { return m_files.size() < m_totalCount; }
    int totalCount() const { return m_totalCount; }
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void applySemanticResults(const QString& query, const QVariantList& results);
    Q_INVOKABLE void setLibraryFilters(bool favoritesOnly, const QString& folder, const QString& tag);
    Q_INVOKABLE bool openFileById(int fileId) const;
    Q_INVOKABLE bool openContainingFolderById(int fileId) const;
    Q_INVOKABLE QString fileUrlById(int fileId) const;
    Q_INVOKABLE QString readTextFileById(int fileId) const;
    Q_INVOKABLE QVariantMap presentationPdfPreviewById(int fileId) const;
    Q_INVOKABLE bool assignCollectionById(int fileId, int collectionId);
    Q_INVOKABLE bool removeFileFromCollectionById(int fileId, int collectionId);
    Q_INVOKABLE bool relinkFileById(int fileId, const QString& path);

    Q_INVOKABLE void setAdvancedFilters(int statusValue,
                                        const QString& extension,
                                        const QString& documentType,
                                        const QString& dateField,
                                        const QString& dateFrom,
                                        const QString& dateTo);
    Q_INVOKABLE void setAdvancedFiltersV2(const QVariantList& statusFilters,
                                          const QVariantList& extensionFilters,
                                          const QVariantList& documentTypeFilters,
                                          const QString& dateField,
                                          const QString& dateFrom,
                                          const QString& dateTo);

    Q_INVOKABLE void clearAdvancedFilters();

    Q_INVOKABLE void setSort(const QString& fieldName, bool ascending);

    Q_INVOKABLE bool addFile(const QString& filePath,
                             const QString& technicalDomain,
                             const QString& subject,
                             const QString& subtopic,
                             const QString& location,
                             const QString& source,
                             const QString& author,
                             const QString& documentType,
                             const QString& remarks);
    Q_INVOKABLE QVariantMap importFiles(const QVariantList& filePaths);
    Q_INVOKABLE QVariantMap importFolder(const QString& folderPath);
    Q_INVOKABLE QVariantMap importStatus() const;
    Q_INVOKABLE QVariantList recentRetrievalQueries(int limit = 5) const;
    Q_INVOKABLE QVariantList recentOpenedSources(int limit = 5) const;
    Q_INVOKABLE bool trackRetrievalEvent(const QString& eventType,
                                         const QString& queryText,
                                         int fileId,
                                         int latencyMs,
                                         const QString& usefulState,
                                         const QString& metadataJson);

    Q_INVOKABLE QVariantMap get(int index) const;
    Q_INVOKABLE QVariantMap getDetails(int index) const;
    Q_INVOKABLE QVariantMap getDetailsById(int fileId) const;
    Q_INVOKABLE int indexOfFileId(int fileId) const;

    Q_INVOKABLE bool updateFileMetadata(int fileIndex,
                                        const QString& technicalDomain,
                                        const QString& subject,
                                        const QString& subtopic,
                                        const QString& location,
                                        const QString& source,
                                        const QString& author,
                                        const QString& documentType,
                                        const QString& remarks);

    Q_INVOKABLE QVariantMap runIntegrityScan();
    Q_INVOKABLE bool relinkFile(int fileIndex, const QString& newFilePath);
    Q_INVOKABLE bool removeFile(int fileIndex);

    Q_INVOKABLE bool addCollection(const QString& name, int parentCollectionId);
    Q_INVOKABLE bool renameCollection(int collectionId, const QString& newName);
    Q_INVOKABLE bool deleteCollection(int collectionId);

    Q_INVOKABLE QVariantList getCollectionTree() const;
    Q_INVOKABLE QVariantList getCollectionPickerOptions() const;
    Q_INVOKABLE bool assignCollection(int fileIndex, int collectionId);
    Q_INVOKABLE bool removeFileFromCollection(int fileIndex, int collectionId);

    Q_INVOKABLE bool addCollectionRule(int collectionId,
                                       const QString& fieldName,
                                       const QString& operatorType,
                                       const QString& value);
    Q_INVOKABLE bool deleteCollectionRule(int ruleId);
    Q_INVOKABLE QVariantList getCollectionRules(int collectionId) const;

    Q_INVOKABLE QVariantList getHierarchyTree() const;

    Q_INVOKABLE void filterByCollection(int collectionId);
    Q_INVOKABLE void clearCollectionFilter();
    Q_INVOKABLE int currentCollectionId() const;

    Q_INVOKABLE void filterByHierarchy(const QString& technicalDomain,
                                       const QString& subject,
                                       const QString& subtopic);
    Q_INVOKABLE void clearHierarchyFilter();

    Q_INVOKABLE bool openFile(int fileIndex) const;
    Q_INVOKABLE bool openContainingFolder(int fileIndex) const;

    Q_INVOKABLE QString readTextFile(int fileIndex) const;
    Q_INVOKABLE QString fileUrl(int fileIndex) const;
    Q_INVOKABLE QString presentationPdfUrl(int fileIndex) const;
    Q_INVOKABLE QVariantMap presentationPdfPreview(int fileIndex) const;

    Q_INVOKABLE bool isEllaNote(int fileIndex) const;
    Q_INVOKABLE bool isEllaNoteFileId(int fileId) const;
    Q_INVOKABLE QVariantList searchReferenceTargets(const QString& queryText, int limit = 20) const;

    Q_INVOKABLE QVariantList getDocumentNotes(int fileId) const;
    Q_INVOKABLE bool addDocumentNote(int fileId, const QString& title, const QString& body);
    Q_INVOKABLE bool updateDocumentNote(int noteId, const QString& title, const QString& body);
    Q_INVOKABLE bool deleteDocumentNote(int noteId);

    Q_INVOKABLE QVariantList getAnnotations(int fileId, const QString& targetType) const;
    Q_INVOKABLE bool addAnnotation(int fileId,
                                   const QString& targetType,
                                   const QString& annotationType,
                                   int pageNumber,
                                   int charStart,
                                   int charEnd,
                                   const QString& anchorText,
                                   double x,
                                   double y,
                                   double width,
                                   double height,
                                   const QString& color,
                                   const QString& content,
                                   qint64 timeStartMs = -1,
                                   qint64 timeEndMs = -1);
    Q_INVOKABLE bool updateAnnotation(int annotationId, const QString& color, const QString& content);
    Q_INVOKABLE bool deleteAnnotation(int annotationId);

    Q_INVOKABLE QString buildAnnotatedTextHtml(int fileId, const QString& plainText) const;
    Q_INVOKABLE QVariantMap indexStatus() const;
    Q_INVOKABLE QVariantMap searchHealth() const;
    Q_INVOKABLE void rebuildSearchIndex();
    Q_INVOKABLE QVariantMap exportDiagnosticsBundle() const;
    Q_INVOKABLE QVariantMap releaseMetadata() const;
    Q_INVOKABLE bool cloudSyncExperimental() const;
    Q_INVOKABLE bool shouldShowBetaScopeNotice() const;
    Q_INVOKABLE void markBetaScopeNoticeSeen();

signals:
    void indexStatusChanged();
    void searchStateChanged();

private:
    void refreshFiles();
    void requestSearchPage(bool append);
    void fuseSemanticResults();
    QList<FileRecord> allFilesForBackgroundJobs() const;
    bool findFileRecordById(int fileId, FileRecord* outFile) const;
    void notifyFileChanged(int fileId, bool forceReindex, bool syncCatalog);
    void notifyCatalogChanged();
    bool trackRetrievalEventInternal(const QString& eventType,
                                     const QString& queryText,
                                     int fileId,
                                     int latencyMs,
                                     const QString& usefulState,
                                     const QString& metadataJson) const;
    QVariantMap defaultMetadataForImport(const QString& absoluteFilePath) const;

    QList<FileRecord> m_files;
    FileRepository m_repository;
    AnnotationRepository m_annotationRepository;
    IndexingService* m_indexingService = nullptr;
    CloudSyncService* m_cloudSyncService = nullptr;

    QString m_searchText;
    QString m_semanticQuery;
    QVariantList m_semanticResults;
    int m_keywordLoadedCount = 0;
    int m_currentCollectionId = -1;

    QString m_currentTechnicalDomain;
    QString m_currentSubject;
    QString m_currentSubtopic;

    QStringList m_statusFilters;
    QStringList m_extensionFilters;
    QStringList m_documentTypeFilters;
    QString m_dateFieldFilter;
    QString m_dateFromFilter;
    QString m_dateToFilter;

    QString m_sortField = "indexedAt";
    bool m_sortAscending = false;

    QVariantMap m_importStatus;
    QString m_lastTrackedQuery;
    QString m_pendingTtfrQuery;
    QElapsedTimer m_queryTimer;
    QTimer m_searchDebounce;
    QThreadPool m_searchPool;
    std::shared_ptr<std::atomic<quint64>> m_searchToken=std::make_shared<std::atomic<quint64>>(0);
    quint64 m_searchGeneration = 0;
    bool m_searching = false;
    int m_totalCount = 0;
    bool m_favoritesOnly = false;
    QString m_folderFilter;
    QString m_tagFilter;
};
