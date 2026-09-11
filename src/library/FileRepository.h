#pragma once

#include "library/FileRecord.h"

#include <QList>
#include <QHash>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QSqlQuery>
#include <QSqlDatabase>

class FileRepository
{
public:
    explicit FileRepository(const QString& connectionName = QStringLiteral("secondbrain_connection"))
        : m_connectionName(connectionName) {}
    bool addFile(const QString& filePath,
                 const QString& technicalDomain,
                 const QString& subject,
                 const QString& subtopic,
                 const QString& location,
                 const QString& source,
                 const QString& author,
                 const QString& documentType,
                 const QString& remarks);

    QList<FileRecord> queryFiles(const QString& searchText,
                                 int collectionId,
                                 const QString& technicalDomain,
                                 const QString& subject,
                                 const QString& subtopic,
                                 const QStringList& statusFilters,
                                 const QStringList& extensionFilters,
                                 const QStringList& documentTypeFilters,
                                 const QString& dateField,
                                 const QString& dateFrom,
                                 const QString& dateTo,
                                 const QString& sortField,
                                 bool sortAscending,
                                 int limit = -1, int offset = 0, int* totalCount = nullptr,
                                 bool favoritesOnly = false, const QString& folder = {},
                                 const QString& tag = {}) const;

    QVariantMap getFileDetails(int fileId) const;
    bool fileById(int fileId, FileRecord* result) const;
    QVariantMap getFileDetailsByPath(const QString& absolutePath) const;

    QVariantList searchReferenceTargets(const QString& queryText, int limit = 20) const;

    bool updateFileMetadata(int fileId,
                            const QString& technicalDomain,
                            const QString& subject,
                            const QString& subtopic,
                            const QString& location,
                            const QString& source,
                            const QString& author,
                            const QString& documentType,
                            const QString& remarks);

    QVariantMap runIntegrityScan();
    bool relinkFile(int fileId, const QString& newFilePath);
    bool removeFile(int fileId);

    bool addCollection(const QString& name, int parentCollectionId = -1);
    bool renameCollection(int collectionId, const QString& newName);
    bool deleteCollection(int collectionId);

    QVariantList getCollectionTreeFlat() const;
    QVariantList getCollectionPickerOptions() const;

    bool assignFileToCollection(int fileId, int collectionId);
    bool removeFileFromCollection(int fileId, int collectionId);

    QStringList getFileCollections(int fileId) const;
    QVariantList getFileCollectionAssignments(int fileId) const;

    bool addCollectionRule(int collectionId,
                           const QString& fieldName,
                           const QString& operatorType,
                           const QString& value);
    bool deleteCollectionRule(int ruleId);
    QVariantList getCollectionRules(int collectionId) const;

    QVariantList getHierarchyTreeFlat() const;

private:
    QSqlDatabase database() const { return QSqlDatabase::database(m_connectionName); }
    QString m_connectionName;
    FileRecord fileFromQuery(const QSqlQuery& q) const;
    QList<FileRecord> getAllFilesRaw() const;
    QList<QVariantMap> getAllCollectionsRaw() const;
    QList<int> getCollectionSubtreeIds(int collectionId) const;
    QVariantList flattenCollections(const QList<QVariantMap>& collections) const;

    bool fileMatchesSearch(const FileRecord& file, const QString& searchText) const;
    bool fileMatchesHierarchy(const FileRecord& file,
                              const QString& technicalDomain,
                              const QString& subject,
                              const QString& subtopic) const;

    bool fileMatchesAdvancedFilters(const FileRecord& file,
                                    const QStringList& statusFilters,
                                    const QStringList& extensionFilters,
                                    const QStringList& documentTypeFilters,
                                    const QString& dateField,
                                    const QString& dateFrom,
                                    const QString& dateTo) const;

    QHash<int, QPair<double, QString>> queryContentMatches(const QString& searchText) const;

    QString fieldValue(const FileRecord& file, const QString& fieldName) const;
    bool ruleMatchesFile(const FileRecord& file,
                         const QString& fieldName,
                         const QString& operatorType,
                         const QString& value) const;
};
