#pragma once

#include "core/Result.h"

#include <QElapsedTimer>
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

#include <functional>
#include <optional>

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
    // Closes the listener AND every open connection (keep-alive requests and
    // event streams), and drops a pairing code that was showing. Paired
    // devices stay paired: they connect again once it starts.
    void stop();
    bool isRunning() const;
    quint16 port() const;
    // The address the listener is bound to; null while stopped.
    QHostAddress boundAddress() const;
    // Why the last start() failed; empty after a successful start.
    QString errorString() const { return m_error; }
    int connectedClientsCount() const;

    // Forces regenerating self-signed certs and restarts server
    bool regenerateCertificate();

    // ── Pairing ─────────────────────────────────────────────────────────────
    // A phone or a StrmLight pairs once, with a one-time four-digit code, and
    // stays paired until it is removed (ARCHITECTURE.md §6). There is one code
    // at a time, of one of two kinds:
    //
    //   shown      made here and shown on the TV, because a phone asked for one
    //              (POST /api/auth/request); the phone sends it back.
    //   StrmLight  made by a StrmLight and shown on its own screen; someone
    //              types it into Settings or a paired phone, and the StrmLight,
    //              which asks with it every 2 s, is let in on its next try. Only
    //              a request carrying the X-StrmLight header can use it.
    //
    // A code is good for kPairingCodeSeconds, for one device, and for
    // kMaxCodeMisses wrong guesses; on top of that the per-peer lockout and the
    // shared allowance (handleApiAuthPin) apply to every guess.
    static constexpr int kPairingCodeSeconds = 120;
    static constexpr int kMaxCodeMisses = 5;

    // Shows a fresh code on the TV, or keeps the one showing. False while a
    // StrmLight code is expected: that one keeps the slot until it is used,
    // cancelled or runs out.
    bool showPairingCode();
    // Expects the code a StrmLight is showing, in place of whatever code was
    // current. False for anything but four digits.
    bool expectStrmLightCode(const QString &code);
    // Drops the current code. A shown code cancelled at the TV also keeps
    // phones from asking for another for a while.
    void cancelPairing();
    // The code to put on the TV; empty unless a shown code is current.
    QString pairingCode() const;
    bool awaitingStrmLight() const;

    // What Settings keeps about each paired device, without its hash.
    QVariantList pairedDevices() const;
    void removePairedDevice(const QString &id);
    void removeAllPairedDevices();

    // What the desktop is showing ("browse", "player", "music", ...), so the
    // phone's controller can offer the buttons that mean something right now.
    void setInteractionContext(const QString &context);
    // Whether the full player is the page on screen. With playback active this
    // is what tells the phone "full" from "mini" (status app.playerView), so a
    // view toggle can say which way it will go.
    void setPlayerOnTop(bool onTop);
    // The InputMap catalogue: every id /api/action accepts. The phone invokes
    // an action by the same id a key or a pad button resolves to, so an action
    // added to the catalogue exists for the phone without a table edit here.
    void setActions(const QStringList &actionIds);

    // Key names /api/navigate accepts; anything else is refused with 400.
    static QStringList navigationKeys();
    // The InputMap action a navigation key stands for ("menu" → "nav.contextMenu"),
    // empty for anything not in navigationKeys(). The remote names intents, not
    // keys, so a user who rebinds Back still has a working Back on the phone.
    static QString actionForNavigationKey(const QString &key);

    // Per-connection timeouts: a request must complete within requestMs of its
    // first byte, and a connection idle between requests closes after idleMs.
    // Defaults 20 s and 60 s; tests shorten them. Applies to timers armed later.
    void setTimeoutsForTests(int requestMs, int idleMs);
    // Replaces NetworkAddressHelper::resolveBindAddress for start().
    void setBindAddressResolverForTests(std::function<QHostAddress(const QString &mode)> resolver);
    // Replaces the monotonic clock the PIN lockout runs on (milliseconds).
    void setClockForTests(std::function<qint64()> nowMs);
    // Replaces the random source of shown codes.
    void setCodeGeneratorForTests(std::function<QString()> generate);

    // What failed PINs are counted against: the address for IPv4 (an
    // IPv4-mapped IPv6 address is its IPv4 form), the /64 for IPv6 from
    // elsewhere — one host commonly holds a whole /64, so anything narrower is
    // a fresh allowance per address it cares to use. On this computer's own
    // link (a link-local peer, or one in the /64 of `local`, the address the
    // connection arrived on) it is the address again: there the /64 is the
    // whole LAN, and one guesser would lock out every phone on it. Rotating
    // addresses there buys nothing, because peers that have not given the PIN
    // also share one allowance (handleApiAuthPin).
    static QString pinPeerKey(const QHostAddress &peer, const QHostAddress &local = {});

signals:
    void runningChanged(bool running);
    void connectedClientsChanged(int count);
    // "home" | "search" | "settings" | "back"
    void navigationRequested(const QString &destination);
    // An InputMap action id: named outright by /api/action, or a legacy
    // navigation key resolved through actionForNavigationKey(). Application
    // invokes it by id (InputMap::trigger) — nothing is turned into a key here.
    void actionRequested(const QString &actionId);
    // A code appeared, was used, cancelled or ran out.
    void pairingChanged();
    // `kind` is "phone" or "strmlight".
    void devicePaired(const QString &name, const QString &kind);

private slots:
    void onStartedEncryptionHandshake(QSslSocket *socket);
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

    void onEncrypted(QSslSocket *socket);
    void handleReadyRead(QSslSocket *socket, QByteArray &buffer);
    // Each connection has one single-shot timer. While a request is pending
    // (first byte seen, not yet complete) it runs the request timeout; between
    // requests, the idle timeout. On expiry the connection is aborted.
    void restartTimeout(QSslSocket *socket, bool pending);
    bool requestPending(QSslSocket *socket) const;
    // For long-lived responses (the SSE event stream): stops and removes the
    // socket's timer, so neither timeout applies to it again.
    void exemptFromIdleTimeout(QSslSocket *socket);
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
    void handleApiAction(QSslSocket *socket, const QJsonObject &body);
    void handleApiEvents(QSslSocket *socket, const HttpRequest &req);
    void handleApiAuthPin(QSslSocket *socket, const HttpRequest &req);
    void handleApiAuthRequest(QSslSocket *socket);
    void handleApiPairStrmLight(QSslSocket *socket, const QJsonObject &body);

    bool isAuthorized(const HttpRequest &req) const;
    bool isPairedToken(const QString &token) const;
    // Rebuilds the token-hash index from Settings, and closes the event stream
    // of any device that is no longer there.
    void reloadDevices();
    // Records a new device and returns its token, which is never stored.
    QString pairDevice(const QString &name, bool strmLight);
    void setPendingCode(std::optional<QString> code, bool strmLight);
    void sendResponse(QSslSocket *socket, int statusCode, const QByteArray &contentType,
                      const QByteArray &body, const QHash<QByteArray, QByteArray> &extraHeaders = {});
    void sendJson(QSslSocket *socket, int statusCode, const QJsonObject &json);
    void sendJsonArray(QSslSocket *socket, int statusCode, const QJsonArray &json);
    void sendOk(QSslSocket *socket);
    void sendError(QSslSocket *socket, int statusCode, const QString &message);
    // Writes one chunk to an event stream, under the same backlog cap as
    // sendResponse: a phone that stopped reading has its stream aborted
    // rather than buffered without bound. False when the stream is gone.
    bool writeSse(QSslSocket *socket, const QByteArray &chunk);
    // Writes `chunk` to every open stream and forgets the ones that are gone.
    void writeToAllStreams(const QByteArray &chunk);
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
    QList<QPointer<QSslSocket>> m_clients;    // every open connection
    QList<QPointer<QSslSocket>> m_sseClients; // open /api/events streams
    QHash<QString, QString> m_deviceByHash;   // SHA-256 of a token → device id, from Settings
    struct PendingCode {
        QString code;
        bool strmLight = false;
        int misses = 0;
        // Each peer's last wrong guess at this code: a StrmLight polls the
        // same code every 2 s, and asking again is not guessing again.
        QHash<QString, QString> lastGuess;
    };
    std::optional<PendingCode> m_code;
    QTimer m_codeExpiry;
    // After a shown code is cancelled at the TV, phones may not ask for
    // another until then (on m_clock).
    qint64 m_requestsRefusedUntil = 0;
    std::function<QString()> m_codeGenerator;
    QNetworkAccessManager *m_imageNam = nullptr;
    struct PinFailures {
        int misses = 0;    // in a row, since the last right PIN
        qint64 lockMs = 0; // the current lockout; doubles per miss past the fifth
        qint64 lockedUntil = 0;
        qint64 lastMiss = 0;
    };
    QHash<QString, PinFailures> m_pinFailures; // by pinPeerKey()
    // Misses by peers that have not shown the PIN, across all of them; one is
    // forgiven per kPinGlobalForgiveMs, counted from m_pinGlobalSince.
    int m_pinGlobalMisses = 0;
    qint64 m_pinGlobalSince = 0;
    // Peers (by pinPeerKey()) that have paired a device: the limit shared by
    // strangers does not lock them out.
    QSet<QString> m_pinKnownPeers;
    QElapsedTimer m_pinClock;
    std::function<qint64()> m_clock;
    std::function<QHostAddress(const QString &)> m_bindResolver;
    QString m_error;
    QString m_interactionContext;
    bool m_playerOnTop = false;
    QSet<QString> m_actionIds;
    int m_requestTimeoutMs = 20000;
    int m_idleTimeoutMs = 60000;

    // Many player signals fire together (a new item changes title, duration,
    // sources and tracks in one pass); one status event carries all of them.
    QTimer m_statusCoalesce;
    // Position is extrapolated on the phone; this keeps the extrapolation honest.
    QTimer m_statusTick;
    // Proxies and phone radios drop an event stream that says nothing for long.
    QTimer m_keepAlive;
};

} // namespace strmqt
