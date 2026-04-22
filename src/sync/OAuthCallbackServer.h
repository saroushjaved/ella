#pragma once

#include <QObject>
#include <QTcpServer>

class OAuthCallbackServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit OAuthCallbackServer(QObject* parent = nullptr);

    Q_INVOKABLE bool startListening(const QString& redirectUri);
    Q_INVOKABLE void stopListening();

    bool listening() const;
    QString lastError() const;

signals:
    void callbackReceived(const QString& code,
                          const QString& state,
                          const QString& error,
                          const QString& errorDescription);
    void listeningChanged();
    void lastErrorChanged();

private:
    void setLastError(const QString& error);
    void handleIncomingConnection();
    void parseHttpRequestLine(const QByteArray& rawRequest);

    QTcpServer m_server;
    QString m_expectedPath = QStringLiteral("/callback");
    QString m_lastError;
};
