#include "sync/OAuthCallbackServer.h"

#include <QHostAddress>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

OAuthCallbackServer::OAuthCallbackServer(QObject* parent)
    : QObject(parent)
{
    connect(&m_server,
            &QTcpServer::newConnection,
            this,
            &OAuthCallbackServer::handleIncomingConnection);
}

bool OAuthCallbackServer::startListening(const QString& redirectUri)
{
    stopListening();

    const QUrl url(redirectUri.trimmed());
    if (!url.isValid() || url.scheme().toLower() != QStringLiteral("http")) {
        setLastError(QStringLiteral("OAuth redirect URI must be a valid http://localhost URL"));
        return false;
    }

    const QString host = url.host().trimmed().toLower();
    if (host != QStringLiteral("localhost")
        && host != QStringLiteral("127.0.0.1")
        && host != QStringLiteral("::1")) {
        setLastError(QStringLiteral("OAuth redirect host must be localhost"));
        return false;
    }

    const int portValue = url.port(53682);
    if (portValue <= 0 || portValue > 65535) {
        setLastError(QStringLiteral("OAuth redirect URI has an invalid port"));
        return false;
    }

    m_expectedPath = url.path().trimmed();
    if (m_expectedPath.isEmpty()) {
        m_expectedPath = QStringLiteral("/callback");
    }
    if (!m_expectedPath.startsWith('/')) {
        m_expectedPath.prepend('/');
    }

    if (!m_server.listen(QHostAddress::LocalHost, static_cast<quint16>(portValue))) {
        setLastError(QStringLiteral("Unable to listen on localhost:%1 (%2)")
                         .arg(portValue)
                         .arg(m_server.errorString()));
        return false;
    }

    setLastError(QString());
    emit listeningChanged();
    return true;
}

void OAuthCallbackServer::stopListening()
{
    if (m_server.isListening()) {
        m_server.close();
        emit listeningChanged();
    }
}

bool OAuthCallbackServer::listening() const
{
    return m_server.isListening();
}

QString OAuthCallbackServer::lastError() const
{
    return m_lastError;
}

void OAuthCallbackServer::setLastError(const QString& error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    emit lastErrorChanged();
}

void OAuthCallbackServer::handleIncomingConnection()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket* socket = m_server.nextPendingConnection();
        if (!socket) {
            continue;
        }

        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            const QByteArray request = socket->readAll();
            parseHttpRequestLine(request);

            const QByteArray response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Connection: close\r\n"
                "Cache-Control: no-store\r\n\r\n"
                "<html><body style='font-family:Segoe UI,sans-serif;padding:24px;'>"
                "<h3>Ella connected successfully.</h3>"
                "<p>You can close this tab and return to the app.</p>"
                "</body></html>";

            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });

        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void OAuthCallbackServer::parseHttpRequestLine(const QByteArray& rawRequest)
{
    const QList<QByteArray> lines = rawRequest.split('\n');
    if (lines.isEmpty()) {
        return;
    }

    const QByteArray firstLine = lines.first().trimmed();
    const QList<QByteArray> parts = firstLine.split(' ');
    if (parts.size() < 2) {
        return;
    }

    const QString method = QString::fromUtf8(parts.at(0)).trimmed().toUpper();
    if (method != QStringLiteral("GET")) {
        return;
    }

    const QString rawTarget = QString::fromUtf8(parts.at(1));
    if (rawTarget.isEmpty()) {
        return;
    }

    const QUrl callbackUrl(QStringLiteral("http://localhost") + rawTarget);
    if (callbackUrl.path() != m_expectedPath) {
        return;
    }

    const QUrlQuery query(callbackUrl);
    const QString code = query.queryItemValue(QStringLiteral("code"));
    const QString state = query.queryItemValue(QStringLiteral("state"));
    const QString error = query.queryItemValue(QStringLiteral("error"));
    const QString errorDescription = query.queryItemValue(QStringLiteral("error_description"));

    emit callbackReceived(code, state, error, errorDescription);
}
