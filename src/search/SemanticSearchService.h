#pragma once

#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QVariantList>

class QProcess;

class SemanticSearchService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
public:
    explicit SemanticSearchService(QObject* parent = nullptr);
    ~SemanticSearchService() override;
    bool available() const;
    bool busy() const { return m_active.id != 0 || !m_queue.isEmpty(); }
    QString status() const { return m_status; }
    Q_INVOKABLE quint64 search(const QString& query, int limit = 40);
    Q_INVOKABLE quint64 relatedPassages(int fileId, int passageOrdinal, int limit = 20);
    Q_INVOKABLE bool rebuildIndex();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void refresh();
signals:
    void stateChanged();
    void resultsReady(quint64 requestId, const QString& query, const QVariantList& results, const QString& error);
    void indexFinished(bool ok, const QString& message);
private:
    struct Request { quint64 id = 0; QString query; QByteArray payload; bool rebuild = false; };
    QString helperPath() const;
    bool ensureProcess();
    quint64 enqueue(const QString& query, const QVariantMap& command, bool rebuild = false);
    void sendNext();
    void consumeOutput();
    void failAll(const QString& error);
    QPointer<QProcess> m_process;
    QQueue<Request> m_queue;
    Request m_active;
    QByteArray m_stdout;
    QString m_status = QStringLiteral("Optional semantic component is not configured.");
    quint64 m_nextId = 1;
};
