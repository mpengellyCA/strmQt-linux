#pragma once

#include "core/Result.h"

#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSslConfiguration>
#include <QString>
#include <QUrl>

class QSslServer;
class QSslSocket;
class QNetworkAccessManager;

namespace strmqt {

class Settings;
class PlayerController;
class ItemActions;
class HomeController;
class SessionController;

namespace emby {
class EmbyClient;
}

class WebRemoteServer : public QObject
{
    Q_OBJECT

public:
    explicit WebRemoteServer(Settings *settings,
                             PlayerController *player,
                             ItemActions *actions,
                             HomeController *home,
                             SessionController *session,
                             emby::EmbyClient *client,
                             QObject *parent = nullptr);
    ~WebRemoteServer() override;

    bool start();
    void stop();
    bool isRunning() const;
    quint16 port() const;
    int connectedClientsCount() const;

    // Forces regenerating self-signed certs and restarts server
    bool regenerateCertificate();

signals:
    void runningChanged(bool running);
    void connectedClientsChanged(int count);
    void navigationRequested(const QString &destination);
    void keyNavigationRequested(const QString &key);
    void osdToggleRequested();

private slots:
    void onStartedEncryptionHandshake(QSslSocket *socket);
    void onClientDisconnected();
    void broadcastPlayerStatus();
    void broadcastQueue();

private:
    struct HttpRequest {
        QString method;
        QString path;
        QUrl url;
        QHash<QByteArray, QByteArray> headers;
        QByteArray body;
    };

    void handleReadyRead(QSslSocket *socket, QByteArray &buffer);
    void dispatchRequest(QSslSocket *socket, const HttpRequest &req);

    // Route handlers
    void handleStaticFile(QSslSocket *socket, const QString &path);
    void handleApiStatus(QSslSocket *socket);
    void handleApiQueue(QSslSocket *socket);
    void handleApiHome(QSslSocket *socket);
    void handleApiLibraries(QSslSocket *socket);
    void handleApiLibraryItems(QSslSocket *socket, const HttpRequest &req);
    void handleApiItemDetails(QSslSocket *socket, const QString &itemId);
    void handleApiSearch(QSslSocket *socket, const QString &query);
    void handleApiImage(QSslSocket *socket, const QString &itemId, const QString &imageType);
    void handleApiPlay(QSslSocket *socket, const QJsonObject &body);
    void handleApiPlayback(QSslSocket *socket, const QJsonObject &body);
    void handleApiVolume(QSslSocket *socket, const QJsonObject &body);
    void handleApiStream(QSslSocket *socket, const QJsonObject &body);
    void handleApiNavigate(QSslSocket *socket, const QJsonObject &body);
    void handleApiEvents(QSslSocket *socket);
    void handleApiAuthPin(QSslSocket *socket, const QJsonObject &body);

    bool isAuthorized(const HttpRequest &req) const;
    void sendResponse(QSslSocket *socket, int statusCode, const QByteArray &contentType,
                      const QByteArray &body, const QHash<QByteArray, QByteArray> &extraHeaders = {});
    void sendJson(QSslSocket *socket, int statusCode, const QJsonObject &json);
    void broadcastSse(const QString &eventName, const QByteArray &data);

    QJsonObject currentStatusJson() const;
    QJsonArray currentQueueJson() const;

    Settings *m_settings;
    PlayerController *m_player;
    ItemActions *m_actions;
    HomeController *m_home;
    SessionController *m_session;
    emby::EmbyClient *m_client;

    QSslServer *m_server = nullptr;
    QList<QPointer<QSslSocket>> m_sseClients;
    QSet<QString> m_authorizedTokens;
    QNetworkAccessManager *m_imageNam = nullptr;
    int m_failedPinAttempts = 0;
    qint64 m_lastFailedPinTimeMs = 0;
};

} // namespace strmqt
