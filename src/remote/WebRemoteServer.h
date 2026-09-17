#pragma once

#include "core/Result.h"

#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSslConfiguration>
#include <QString>
#include <QTimer>
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
struct MediaItem;

namespace emby {
class EmbyClient;
}

// The phone companion (ARCHITECTURE.md §6): a small HTTPS server that serves the
// single-page remote in src/remote/web and a JSON API over the same controllers
// the desktop UI uses. It holds no playback or library logic of its own — every
// verb lands on PlayerController, ItemActions or EmbyClient, exactly as a click
// in the desktop UI would.
//
// Live state is pushed over Server-Sent Events: `status` whenever anything the
// Now Playing screen shows changes (coalesced), plus a slow tick while playing
// so a phone that slept catches up; `queue` when the queue changes.
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

    // What the desktop is showing ("browse", "player", "music", ...), so the
    // phone's controller can offer the buttons that mean something right now.
    void setInteractionContext(const QString &context);

    // Key names /api/navigate accepts; anything else is refused with 400.
    static QStringList navigationKeys();
    // The InputMap action a navigation key stands for ("menu" → "nav.contextMenu"),
    // empty for anything not in navigationKeys(). The remote names intents, not
    // keys, so a user who rebinds Back still has a working Back on the phone.
    static QString actionForNavigationKey(const QString &key);

signals:
    void runningChanged(bool running);
    void connectedClientsChanged(int count);
    // "home" | "search" | "settings" | "back"
    void navigationRequested(const QString &destination);
    // An InputMap action id: a navigation key resolved through
    // actionForNavigationKey(), or "player.toggleOsd". Application invokes it
    // by id (InputMap::trigger) — nothing is turned into a key here.
    void actionRequested(const QString &actionId);

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
    bool dispatchApi(QSslSocket *socket, const HttpRequest &req);

    // Route handlers
    bool handleStaticFile(QSslSocket *socket, const QString &path);
    void handleApiHome(QSslSocket *socket);
    void handleApiLibraries(QSslSocket *socket);
    void handleApiItems(QSslSocket *socket, const HttpRequest &req);
    void handleApiArtists(QSslSocket *socket, const HttpRequest &req);
    void handleApiItemDetails(QSslSocket *socket, const QString &itemId);
    void handleApiSeasons(QSslSocket *socket, const QString &seriesId);
    void handleApiEpisodes(QSslSocket *socket, const QString &seriesId, const HttpRequest &req);
    void handleApiPlaylistItems(QSslSocket *socket, const QString &playlistId);
    void handleApiSearch(QSslSocket *socket, const QString &query);
    void handleApiImage(QSslSocket *socket, const QString &itemId, const QString &imageType,
                        const HttpRequest &req);
    void handleApiPlay(QSslSocket *socket, const QJsonObject &body);
    void handleApiUserData(QSslSocket *socket, const QString &itemId, const QString &field,
                           const QJsonObject &body);
    void handleApiPlayback(QSslSocket *socket, const QJsonObject &body);
    void handleApiQueueAction(QSslSocket *socket, const QJsonObject &body);
    void handleApiVolume(QSslSocket *socket, const QJsonObject &body);
    void handleApiStream(QSslSocket *socket, const QJsonObject &body);
    void handleApiQuality(QSslSocket *socket, const QJsonObject &body);
    void handleApiSubtitleStyle(QSslSocket *socket, const QJsonObject &body);
    void handleApiNavigate(QSslSocket *socket, const QJsonObject &body);
    void handleApiEvents(QSslSocket *socket);
    void handleApiAuthPin(QSslSocket *socket, const QJsonObject &body);

    bool isAuthorized(const HttpRequest &req) const;
    void sendResponse(QSslSocket *socket, int statusCode, const QByteArray &contentType,
                      const QByteArray &body, const QHash<QByteArray, QByteArray> &extraHeaders = {});
    void sendJson(QSslSocket *socket, int statusCode, const QJsonObject &json);
    void sendJsonArray(QSslSocket *socket, int statusCode, const QJsonArray &json);
    void sendOk(QSslSocket *socket);
    void sendError(QSslSocket *socket, int statusCode, const QString &message);
    void broadcastSse(const QString &eventName, const QByteArray &data);
    void scheduleStatus();

    QJsonObject currentStatusJson() const;
    QJsonArray currentQueueJson() const;

    static QJsonObject itemJson(const MediaItem &item);
    static QJsonObject pageJson(const QList<MediaItem> &items, int total, int startIndex);

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
    QString m_interactionContext;

    // Many player signals fire together (a new item changes title, duration,
    // sources and tracks in one pass); one status event carries all of them.
    QTimer m_statusCoalesce;
    // Position is extrapolated on the phone; this keeps the extrapolation honest.
    QTimer m_statusTick;
    // Proxies and phone radios drop an event stream that says nothing for long.
    QTimer m_keepAlive;
};

} // namespace strmqt
