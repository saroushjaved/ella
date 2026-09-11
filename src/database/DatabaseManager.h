#pragma once

#include <QSqlDatabase>
#include <QString>

class DatabaseManager
{
public:
    static DatabaseManager& instance();

    bool initialize();
    QSqlDatabase database() const;
    QString lastError() const;

private:
    DatabaseManager() = default;
    bool openConnection();
    void closeConnection();
    bool validateIntegrity();
    bool pruneCorruptedIndexRows();
    bool createTables();
    bool migrate();

    QString m_connectionName = "secondbrain_connection";
    QString m_lastError;
};
