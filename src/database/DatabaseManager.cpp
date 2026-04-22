
#include "DatabaseManager.h"
#include "core/AppConfig.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

DatabaseManager& DatabaseManager::instance()
{
    static DatabaseManager instance;
    return instance;
}

bool DatabaseManager::initialize()
{
    m_lastError.clear();

    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase existing = QSqlDatabase::database(m_connectionName);
        if (existing.isOpen()) {
            return true;
        }
        existing.close();
        existing = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    if (!openConnection()) {
        if (!recoverCorruptDatabase(QStringLiteral("open_failed")) || !openConnection()) {
            if (m_lastError.isEmpty()) {
                m_lastError = QStringLiteral("Failed to open database");
            }
            qCritical() << "DB open failed:" << m_lastError;
            return false;
        }
    }

    if (!createTables()) {
        m_lastError = QStringLiteral("Failed to create or migrate database tables");
        qCritical() << m_lastError;
        closeConnection();
        if (!recoverCorruptDatabase(QStringLiteral("schema_migration_failed")) || !openConnection()
            || !createTables()) {
            if (m_lastError.isEmpty()) {
                m_lastError = QStringLiteral("Database recovery failed after schema migration error");
            }
            qCritical() << m_lastError;
            return false;
        }
    }

    if (!validateIntegrity()) {
        qCritical() << "Database integrity check failed:" << m_lastError;
        closeConnection();
        if (!recoverCorruptDatabase(QStringLiteral("integrity_check_failed")) || !openConnection()
            || !createTables() || !validateIntegrity()) {
            if (m_lastError.isEmpty()) {
                m_lastError = QStringLiteral("Database recovery failed after integrity check error");
            }
            qCritical() << m_lastError;
            return false;
        }
    }

    if (!pruneCorruptedIndexRows()) {
        qWarning() << "Index state cleanup failed during startup:" << m_lastError;
    }

    return true;
}

QSqlDatabase DatabaseManager::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

QString DatabaseManager::lastError() const
{
    return m_lastError;
}

bool DatabaseManager::openConnection()
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(AppConfig::databasePath());

    if (!db.open()) {
        m_lastError = db.lastError().text();
        return false;
    }

    return true;
}

void DatabaseManager::closeConnection()
{
    if (!QSqlDatabase::contains(m_connectionName)) {
        return;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool DatabaseManager::validateIntegrity()
{
    QSqlQuery query(database());
    if (!query.exec(QStringLiteral("PRAGMA quick_check"))) {
        m_lastError = query.lastError().text();
        return false;
    }

    if (!query.next()) {
        m_lastError = QStringLiteral("No response from PRAGMA quick_check");
        return false;
    }

    const QString quickCheckResult = query.value(0).toString().trimmed().toLower();
    if (quickCheckResult != QStringLiteral("ok")) {
        m_lastError = QStringLiteral("quick_check failed: %1").arg(quickCheckResult);
        return false;
    }

    return true;
}

bool DatabaseManager::pruneCorruptedIndexRows()
{
    auto exec = [&](const QString& sql) -> bool {
        QSqlQuery query(database());
        if (!query.exec(sql)) {
            m_lastError = query.lastError().text();
            return false;
        }
        return true;
    };

    const bool ok =
        exec(QStringLiteral("DELETE FROM file_index_state WHERE file_id NOT IN (SELECT id FROM files)")) &&
        exec(QStringLiteral("DELETE FROM file_content_fts WHERE file_id NOT IN (SELECT id FROM files)")) &&
        exec(QStringLiteral("DELETE FROM annotations WHERE file_id NOT IN (SELECT id FROM files)")) &&
        exec(QStringLiteral("DELETE FROM document_notes WHERE file_id NOT IN (SELECT id FROM files)"));

    return ok;
}

bool DatabaseManager::recoverCorruptDatabase(const QString& reason)
{
    closeConnection();

    const QString dbPath = AppConfig::databasePath();
    QFileInfo info(dbPath);
    if (!info.exists()) {
        return true;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString backupPath = QStringLiteral("%1.corrupt-%2-%3")
                                   .arg(dbPath, reason, timestamp);

    if (QFile::exists(backupPath)) {
        QFile::remove(backupPath);
    }

    if (!QFile::rename(dbPath, backupPath)) {
        if (!QFile::copy(dbPath, backupPath)) {
            m_lastError = QStringLiteral("Failed to backup corrupted database file");
            return false;
        }
        QFile::remove(dbPath);
    }

    qWarning() << "Database recovered by moving previous file to:" << backupPath
               << "| Rebuild search index is recommended.";
    return true;
}

bool DatabaseManager::createTables()
{
    QSqlQuery query(database());

    const QString createFilesTable = R"(
        CREATE TABLE IF NOT EXISTS files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT NOT NULL UNIQUE,
            name TEXT NOT NULL,
            extension TEXT,
            mime_type TEXT,
            size_bytes INTEGER DEFAULT 0,
            created_at TEXT,
            modified_at TEXT,
            indexed_at TEXT,
            status INTEGER DEFAULT 0,
            technical_domain TEXT,
            subject TEXT,
            subtopic TEXT,
            location TEXT,
            source TEXT,
            author TEXT,
            document_type TEXT,
            remarks TEXT
        )
    )";

    if (!query.exec(createFilesTable)) {
        qCritical() << "Failed to create files table:" << query.lastError().text();
        return false;
    }

    auto ensureTableColumn = [&](const QString& tableName,
                                 const QString& columnName,
                                 const QString& definition) -> bool {
        QSqlQuery alterQuery(database());
        const QString sql = QString("ALTER TABLE %1 ADD COLUMN %2 %3")
                                .arg(tableName, columnName, definition);

        if (!alterQuery.exec(sql)) {
            const QString err = alterQuery.lastError().text();
            if (!err.contains("duplicate column name", Qt::CaseInsensitive)) {
                qCritical() << "Failed to add column" << columnName
                            << "to table" << tableName << ":" << err;
                return false;
            }
        }

        return true;
    };

    if (!ensureTableColumn("files", "technical_domain", "TEXT")) return false;
    if (!ensureTableColumn("files", "subject", "TEXT")) return false;
    if (!ensureTableColumn("files", "subtopic", "TEXT")) return false;
    if (!ensureTableColumn("files", "location", "TEXT")) return false;
    if (!ensureTableColumn("files", "source", "TEXT")) return false;
    if (!ensureTableColumn("files", "author", "TEXT")) return false;
    if (!ensureTableColumn("files", "document_type", "TEXT")) return false;
    if (!ensureTableColumn("files", "remarks", "TEXT")) return false;

    const QString createCollectionsTable = R"(
        CREATE TABLE IF NOT EXISTS collections (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            parent_collection_id INTEGER,
            UNIQUE(name, parent_collection_id)
        )
    )";

    if (!query.exec(createCollectionsTable)) {
        qCritical() << "Failed to create collections table:" << query.lastError().text();
        return false;
    }

    if (!ensureTableColumn("collections", "parent_collection_id", "INTEGER")) return false;

    const QString createFileCollectionsTable = R"(
        CREATE TABLE IF NOT EXISTS file_collections (
            file_id INTEGER NOT NULL,
            collection_id INTEGER NOT NULL,
            PRIMARY KEY (file_id, collection_id)
        )
    )";

    if (!query.exec(createFileCollectionsTable)) {
        qCritical() << "Failed to create file_collections table:" << query.lastError().text();
        return false;
    }

    const QString createCollectionRulesTable = R"(
        CREATE TABLE IF NOT EXISTS collection_rules (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            collection_id INTEGER NOT NULL,
            field_name TEXT NOT NULL,
            operator_type TEXT NOT NULL,
            value TEXT NOT NULL
        )
    )";

    if (!query.exec(createCollectionRulesTable)) {
        qCritical() << "Failed to create collection_rules table:" << query.lastError().text();
        return false;
    }

    const QString createDocumentNotesTable = R"(
        CREATE TABLE IF NOT EXISTS document_notes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_id INTEGER NOT NULL,
            title TEXT,
            body TEXT NOT NULL,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    )";

    if (!query.exec(createDocumentNotesTable)) {
        qCritical() << "Failed to create document_notes table:" << query.lastError().text();
        return false;
    }

    const QString createAnnotationsTable = R"(
        CREATE TABLE IF NOT EXISTS annotations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_id INTEGER NOT NULL,
            target_type TEXT NOT NULL,
            annotation_type TEXT NOT NULL,
            page_number INTEGER,
            char_start INTEGER,
            char_end INTEGER,
            anchor_text TEXT,
            x REAL DEFAULT 0,
            y REAL DEFAULT 0,
            width REAL DEFAULT 0,
            height REAL DEFAULT 0,
            color TEXT,
            content TEXT,
            time_start_ms INTEGER,
            time_end_ms INTEGER,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    )";

    if (!query.exec(createAnnotationsTable)) {
        qCritical() << "Failed to create annotations table:" << query.lastError().text();
        return false;
    }

    if (!ensureTableColumn("annotations", "time_start_ms", "INTEGER")) return false;
    if (!ensureTableColumn("annotations", "time_end_ms", "INTEGER")) return false;

    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_document_notes_file_id ON document_notes(file_id)")) {
        qCritical() << "Failed to create idx_document_notes_file_id:" << query.lastError().text();
        return false;
    }

    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_annotations_file_target ON annotations(file_id, target_type)")) {
        qCritical() << "Failed to create idx_annotations_file_target:" << query.lastError().text();
        return false;
    }

    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_annotations_file_page ON annotations(file_id, page_number)")) {
        qCritical() << "Failed to create idx_annotations_file_page:" << query.lastError().text();
        return false;
    }

    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_annotations_file_target_time ON annotations(file_id, target_type, time_start_ms)")) {
        qCritical() << "Failed to create idx_annotations_file_target_time:" << query.lastError().text();
        return false;
    }

    const QString createFileIndexStateTable = R"(
        CREATE TABLE IF NOT EXISTS file_index_state (
            file_id INTEGER PRIMARY KEY,
            file_path TEXT,
            file_size_bytes INTEGER DEFAULT 0,
            file_modified_at TEXT,
            content_hash TEXT,
            indexed_at TEXT,
            status TEXT,
            last_error TEXT,
            extractor TEXT
        )
    )";

    if (!query.exec(createFileIndexStateTable)) {
        qCritical() << "Failed to create file_index_state table:" << query.lastError().text();
        return false;
    }

    if (!query.exec(
            "CREATE INDEX IF NOT EXISTS idx_file_index_state_status ON file_index_state(status)")) {
        qCritical() << "Failed to create idx_file_index_state_status:" << query.lastError().text();
        return false;
    }

    const QString createContentFtsTable = R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS file_content_fts
        USING fts5(file_id UNINDEXED, content_text)
    )";

    if (!query.exec(createContentFtsTable)) {
        qWarning() << "Failed to create file_content_fts table (content search will be metadata-only):"
                   << query.lastError().text();
    }

    const QString createCloudConnectionsTable = R"(
        CREATE TABLE IF NOT EXISTS cloud_connections (
            provider TEXT PRIMARY KEY,
            account_email TEXT,
            access_token_enc TEXT NOT NULL,
            refresh_token_enc TEXT,
            expires_at TEXT,
            connected_at TEXT,
            updated_at TEXT
        )
    )";

    if (!query.exec(createCloudConnectionsTable)) {
        qCritical() << "Failed to create cloud_connections table:" << query.lastError().text();
        return false;
    }

    const QString createCloudFileMapTable = R"(
        CREATE TABLE IF NOT EXISTS cloud_file_map (
            file_id INTEGER NOT NULL,
            provider TEXT NOT NULL,
            cloud_item_id TEXT,
            cloud_path TEXT,
            content_hash TEXT,
            last_synced_at TEXT,
            PRIMARY KEY (file_id, provider)
        )
    )";

    if (!query.exec(createCloudFileMapTable)) {
        qCritical() << "Failed to create cloud_file_map table:" << query.lastError().text();
        return false;
    }

    const QString createSyncJobsTable = R"(
        CREATE TABLE IF NOT EXISTS sync_jobs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            provider TEXT NOT NULL,
            job_type TEXT NOT NULL,
            payload_json TEXT,
            status TEXT NOT NULL,
            retry_count INTEGER DEFAULT 0,
            next_retry_at TEXT,
            last_error TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    )";

    if (!query.exec(createSyncJobsTable)) {
        qCritical() << "Failed to create sync_jobs table:" << query.lastError().text();
        return false;
    }

    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_sync_jobs_status_retry ON sync_jobs(status, next_retry_at)")) {
        qCritical() << "Failed to create idx_sync_jobs_status_retry:" << query.lastError().text();
        return false;
    }

    const QString createRetrievalEventsTable = R"(
        CREATE TABLE IF NOT EXISTS retrieval_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            event_type TEXT NOT NULL,
            query_text TEXT,
            file_id INTEGER,
            latency_ms INTEGER,
            useful_state TEXT,
            metadata_json TEXT,
            created_at TEXT NOT NULL
        )
    )";

    if (!query.exec(createRetrievalEventsTable)) {
        qCritical() << "Failed to create retrieval_events table:" << query.lastError().text();
        return false;
    }

    if (!query.exec(
            "CREATE INDEX IF NOT EXISTS idx_retrieval_events_type_time ON retrieval_events(event_type, created_at)")) {
        qCritical() << "Failed to create idx_retrieval_events_type_time:" << query.lastError().text();
        return false;
    }

    return true;
}
