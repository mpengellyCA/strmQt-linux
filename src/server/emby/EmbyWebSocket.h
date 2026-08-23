#pragma once

#include <QAbstractSocket>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

class QWebSocket;

namespace strmqt::emby {

// Emby's event socket: wss://<host>/embywebsocket?api_key=…&deviceId=…
//
// Verified against Emby 4.9.5.0 (2026-08-22), and the behaviour below is what
// the server actually does, not what the docs imply:
//
//  * The server closes an idle socket after ~30 s. It does NOT send
//    ForceKeepAlive first, and it does NOT answer an application-level
//    {"MessageType":"KeepAlive"} with one of its own. So the keep-alive is
//    write-only: send it well inside the timeout and never wait for a reply.
//  * WebSocket protocol ping/pong *is* answered (~1 ms round trip). That is the
//    only reliable liveness probe, so the watchdog rides on pong, not on
//    inbound application messages — a healthy socket can be silent for hours.
//  * An unparseable client message makes the server hang up immediately
//    (observed with a malformed SessionsStart). Only send what is known good.
//  * LibraryChanged carries plain string id arrays plus CollectionFolders and
//    IsEmpty, and is batched server-side (~60 s after the change).
//  * UserDataChanged carries the full user-data record per item, so a
//    played/favorite change can be patched into a model without a refetch.
//
// Remote control (Play / Playstate / GeneralCommand / Sessions) is milestone
// M12: those types are deliberately not handled here, but every message is
// re-emitted through messageReceived() so adding them later is a dispatch entry,
// not a rewrite.
//
// TLS errors are fatal (AGENTS.md): sslErrors is logged and never ignored.
class EmbyWebSocket : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)

public:
    // One entry of UserDataChanged.Data.UserDataList. Fields missing from the
    // wire keep their defaults (AGENTS.md: tolerant parsing).
    struct UserDataEntry
    {
        QString itemId;
        bool played = false;
        bool favorite = false;
        qint64 playbackPositionTicks = 0;
        int playCount = 0;
    };

    explicit EmbyWebSocket(QObject *parent = nullptr);
    ~EmbyWebSocket() override;

    // Connects, and keeps reconnecting until disconnectFromServer(). Calling it
    // again with a different token/device replaces the session (login → logout →
    // login must not leave the old socket running).
    void connectToServer(const QUrl &baseUrl, const QString &accessToken,
                         const QString &deviceId);
    void disconnectFromServer();

    bool isConnected() const { return m_connected; }

    // Number of connection attempts since the last successful open; 0 once
    // connected. Exposed so a test can assert the backoff does not spin.
    int reconnectAttempts() const { return m_attempts; }

    // http(s)://host/base → ws(s)://host/base/embywebsocket?api_key=…&deviceId=…
    static QUrl socketUrl(const QUrl &baseUrl, const QString &accessToken,
                          const QString &deviceId);

    // Tolerant parsers, public so unit tests can feed them server drift.
    static void parseLibraryChanged(const QJsonObject &data, QStringList *added,
                                    QStringList *removed, QStringList *updated);
    static QList<UserDataEntry> parseUserDataList(const QJsonObject &data);

    // Feeds one raw frame through the dispatch. Called by the socket; public so
    // tests can exercise parsing without a server.
    void handleTextMessage(const QString &text);

    // Test seams: shrink the timers so an integration test runs in milliseconds.
    void setBackoffForTests(int baseMs, int capMs);
    void setKeepAliveIntervalForTests(int intervalMs);

signals:
    void connectedChanged();
    void libraryChanged(const QStringList &added, const QStringList &removed,
                        const QStringList &updated);
    void userDataChanged(const QStringList &itemIds);
    // Richer companion to userDataChanged: the whole record, so a model can be
    // patched in place instead of refetched.
    void userDataEntriesChanged(const QList<strmqt::emby::EmbyWebSocket::UserDataEntry> &entries);
    // Escape hatch for every message, handled or not (M12 remote control).
    void messageReceived(const QString &messageType, const QJsonObject &data);
    // Emitted whenever a retry is armed; delayMs is the jittered backoff.
    void reconnectScheduled(int delayMs);

private:
    void openSocket();
    void scheduleReconnect();
    void teardownSocket();
    void setConnected(bool connected);
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError error);
    void sendKeepAlive();
    void applyKeepAliveInterval(int intervalMs);

    QWebSocket *m_socket = nullptr;
    QUrl m_baseUrl;
    QString m_accessToken;
    QString m_deviceId;
    bool m_wanted = false; // disconnectFromServer() clears it; nothing retries after
    bool m_connected = false;
    int m_attempts = 0;
    int m_backoffBaseMs = 1000;
    int m_backoffCapMs = 60000;
    int m_keepAliveMs = 10000; // server hangs up at ~30 s of silence
    bool m_awaitingPong = false;
    int m_missedPongs = 0;
    QTimer m_keepAlive;
    QTimer m_reconnect;
};

} // namespace strmqt::emby

Q_DECLARE_METATYPE(strmqt::emby::EmbyWebSocket::UserDataEntry)
