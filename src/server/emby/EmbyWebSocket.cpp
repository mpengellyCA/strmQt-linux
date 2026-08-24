#include "EmbyWebSocket.h"

#include "core/Log.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QSslError>
#include <QUrlQuery>
#include <QWebSocket>

namespace strmqt::emby {

namespace {

const auto kKeepAliveFrame = QStringLiteral("{\"MessageType\":\"KeepAlive\"}");
const auto kPingPayload = QByteArrayLiteral("strmqt");
constexpr quint64 kMaxIncomingMessageBytes = 1024 * 1024;

// An id array on the wire is normally ["123","456"], but Emby has shipped
// object entries for some of these fields across versions. Take whatever looks
// like an id and drop the rest rather than failing the whole message.
QStringList idList(const QJsonValue &value)
{
    QStringList ids;
    if (!value.isArray())
        return ids;
    const QJsonArray array = value.toArray();
    ids.reserve(array.size());
    for (const QJsonValue &entry : array) {
        QString id;
        if (entry.isString())
            id = entry.toString();
        else if (entry.isDouble())
            id = QString::number(static_cast<qint64>(entry.toDouble()));
        else if (entry.isObject()) {
            const QJsonObject object = entry.toObject();
            id = object.value(QStringLiteral("Id")).toString();
            if (id.isEmpty())
                id = object.value(QStringLiteral("ItemId")).toString();
        }
        if (!id.isEmpty())
            ids.append(id);
    }
    return ids;
}

} // namespace

EmbyWebSocket::EmbyWebSocket(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<EmbyWebSocket::UserDataEntry>();
    qRegisterMetaType<QList<EmbyWebSocket::UserDataEntry>>();

    m_keepAlive.setInterval(m_keepAliveMs);
    connect(&m_keepAlive, &QTimer::timeout, this, &EmbyWebSocket::sendKeepAlive);

    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, [this] {
        if (m_wanted)
            openSocket();
    });

    m_handshake.setSingleShot(true);
    connect(&m_handshake, &QTimer::timeout, this, [this] {
        if (!m_socket || m_socket->state() == QAbstractSocket::ConnectedState)
            return;
        qCWarning(logServer) << "websocket: handshake timed out";
        teardownSocket();
        scheduleReconnect();
    });
}

EmbyWebSocket::~EmbyWebSocket()
{
    m_wanted = false;
    teardownSocket();
}

QUrl EmbyWebSocket::socketUrl(const QUrl &baseUrl, const QString &accessToken,
                              const QString &deviceId)
{
    QUrl url = baseUrl;
    // Scheme comes from the base URL: http → ws, https → wss. Anything else (a
    // hand-typed "strm.example.com" that parsed schemeless) is treated as plain.
    url.setScheme(baseUrl.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0
                      ? QStringLiteral("wss")
                      : QStringLiteral("ws"));

    QString path = baseUrl.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path + QStringLiteral("/embywebsocket"));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("api_key"), accessToken);
    query.addQueryItem(QStringLiteral("deviceId"), deviceId);
    url.setQuery(query);
    return url;
}

void EmbyWebSocket::connectToServer(const QUrl &baseUrl, const QString &accessToken,
                                    const QString &deviceId)
{
    if (accessToken.isEmpty() || !baseUrl.isValid()) {
        qCWarning(logServer) << "websocket: refusing to connect without a session";
        disconnectFromServer();
        return;
    }

    const bool sameSession = m_wanted && baseUrl == m_baseUrl && accessToken == m_accessToken &&
                             deviceId == m_deviceId;
    if (sameSession) {
        const bool viableSocket = m_socket && m_socket->state() != QAbstractSocket::UnconnectedState;
        if (viableSocket || m_reconnect.isActive())
            return;
        openSocket();
        return;
    }

    // A new token means the old socket is authenticated as somebody else.
    teardownSocket();
    m_baseUrl = baseUrl;
    m_accessToken = accessToken;
    m_deviceId = deviceId;
    m_wanted = true;
    m_attempts = 0;
    openSocket();
}

void EmbyWebSocket::disconnectFromServer()
{
    m_wanted = false;
    m_reconnect.stop();
    m_attempts = 0;
    teardownSocket();
}

void EmbyWebSocket::setBackoffForTests(int baseMs, int capMs)
{
    m_backoffBaseMs = qMax(1, baseMs);
    m_backoffCapMs = qMax(m_backoffBaseMs, capMs);
}

void EmbyWebSocket::setKeepAliveIntervalForTests(int intervalMs)
{
    applyKeepAliveInterval(intervalMs);
}

void EmbyWebSocket::setHandshakeTimeoutForTests(int timeoutMs)
{
    m_handshakeTimeoutMs = qMax(1, timeoutMs);
}

void EmbyWebSocket::applyKeepAliveInterval(int intervalMs)
{
    m_keepAliveMs = qMax(1, intervalMs);
    m_keepAlive.setInterval(m_keepAliveMs);
    if (m_keepAlive.isActive())
        m_keepAlive.start();
}

void EmbyWebSocket::teardownSocket()
{
    m_handshake.stop();
    m_keepAlive.stop();
    m_awaitingPong = false;
    m_missedPongs = 0;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    setConnected(false);
}

void EmbyWebSocket::openSocket()
{
    if (!m_wanted)
        return;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
    }

    ++m_attempts;
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_socket->setMaxAllowedIncomingFrameSize(kMaxIncomingMessageBytes);
    m_socket->setMaxAllowedIncomingMessageSize(kMaxIncomingMessageBytes);
    connect(m_socket, &QWebSocket::connected, this, &EmbyWebSocket::onConnected);
    connect(m_socket, &QWebSocket::disconnected, this, &EmbyWebSocket::onDisconnected);
    connect(m_socket, &QWebSocket::errorOccurred, this, &EmbyWebSocket::onError);
    connect(m_socket, &QWebSocket::textMessageReceived, this,
            &EmbyWebSocket::handleTextMessage);
    connect(m_socket, &QWebSocket::pong, this, [this](quint64, const QByteArray &) {
        m_awaitingPong = false;
        m_missedPongs = 0;
    });
    // AGENTS.md: TLS errors are fatal. Log and let the handshake fail — there is
    // deliberately no ignoreSslErrors() call anywhere in this file.
    connect(m_socket, &QWebSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        for (const QSslError &error : errors)
            qCCritical(logServer) << "websocket TLS error:" << error.errorString();
    });

    const QUrl url = socketUrl(m_baseUrl, m_accessToken, m_deviceId);
    // Never log the query: it carries the access token.
    qCInfo(logServer) << "websocket: connecting to" << url.toString(QUrl::RemoveQuery)
                      << "(attempt" << m_attempts << ")";
    m_handshake.start(m_handshakeTimeoutMs);
    m_socket->open(url);
}

void EmbyWebSocket::onConnected()
{
    m_handshake.stop();
    m_attempts = 0;
    m_awaitingPong = false;
    m_missedPongs = 0;
    m_keepAlive.start();
    qCInfo(logServer) << "websocket: connected";
    setConnected(true);
}

void EmbyWebSocket::onDisconnected()
{
    const bool wasConnected = m_connected;
    m_handshake.stop();
    m_keepAlive.stop();
    setConnected(false);
    if (wasConnected)
        qCInfo(logServer) << "websocket: disconnected";
    scheduleReconnect();
}

void EmbyWebSocket::onError(QAbstractSocket::SocketError error)
{
    qCWarning(logServer) << "websocket error:" << error
                         << (m_socket ? m_socket->errorString() : QString());
    m_handshake.stop();
    m_keepAlive.stop();
    setConnected(false);
    scheduleReconnect();
}

void EmbyWebSocket::scheduleReconnect()
{
    if (!m_wanted || m_reconnect.isActive())
        return;

    // Capped exponential backoff with jitter, the same discipline as
    // PlayerController::recoverMidStream(): never spin on a server that is down.
    const int exponent = qMin(m_attempts > 0 ? m_attempts - 1 : 0, 20);
    const qint64 raw = static_cast<qint64>(m_backoffBaseMs) << exponent;
    const int capped = static_cast<int>(qMin<qint64>(raw, m_backoffCapMs));
    // ±25 % jitter so a server restart does not bring every client back at once.
    const int spread = qMax(1, capped / 4);
    const int delay = qMax(1, capped - spread + static_cast<int>(
                                                    QRandomGenerator::global()->bounded(2 * spread)));

    qCInfo(logServer) << "websocket: reconnecting in" << delay << "ms (attempt" << m_attempts
                      << ")";
    m_reconnect.start(delay);
    emit reconnectScheduled(delay);
}

void EmbyWebSocket::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    emit connectedChanged();
}

void EmbyWebSocket::sendKeepAlive()
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    // The server never answers the application-level KeepAlive, so a missing
    // reply proves nothing. Liveness rides on the WebSocket ping/pong, which
    // Emby does answer; two silent probes in a row means the socket is dead
    // even though the OS still thinks the connection is up.
    if (m_awaitingPong && ++m_missedPongs >= 2) {
        qCWarning(logServer) << "websocket: no pong in" << (m_keepAliveMs * m_missedPongs)
                             << "ms, treating socket as dead";
        m_keepAlive.stop();
        if (m_socket)
            m_socket->abort(); // → disconnected → scheduleReconnect()
        return;
    }

    m_socket->sendTextMessage(kKeepAliveFrame);
    m_awaitingPong = true;
    m_socket->ping(kPingPayload);
}

void EmbyWebSocket::parseLibraryChanged(const QJsonObject &data, QStringList *added,
                                        QStringList *removed, QStringList *updated)
{
    // FoldersAddedTo / FoldersRemovedFrom name the *parent* folders touched, so
    // they belong with the added/removed sets: a rail keyed on a library id has
    // to refresh when its folder gains children.
    if (added) {
        *added = idList(data.value(QStringLiteral("ItemsAdded")));
        *added += idList(data.value(QStringLiteral("FoldersAddedTo")));
        added->removeDuplicates();
    }
    if (removed) {
        *removed = idList(data.value(QStringLiteral("ItemsRemoved")));
        *removed += idList(data.value(QStringLiteral("FoldersRemovedFrom")));
        removed->removeDuplicates();
    }
    if (updated) {
        *updated = idList(data.value(QStringLiteral("ItemsUpdated")));
        // Observed on Emby 4.9.5: CollectionFolders rides along on the same
        // message and names libraries whose contents moved.
        *updated += idList(data.value(QStringLiteral("CollectionFolders")));
        updated->removeDuplicates();
    }
}

QList<EmbyWebSocket::UserDataEntry> EmbyWebSocket::parseUserDataList(const QJsonObject &data)
{
    QList<UserDataEntry> entries;
    const QJsonValue list = data.value(QStringLiteral("UserDataList"));
    if (!list.isArray())
        return entries;

    const QJsonArray array = list.toArray();
    entries.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        UserDataEntry entry;
        entry.itemId = object.value(QStringLiteral("ItemId")).toString();
        if (entry.itemId.isEmpty()) // an entry without an id is unusable
            continue;
        entry.played = object.value(QStringLiteral("Played")).toBool(false);
        entry.favorite = object.value(QStringLiteral("IsFavorite")).toBool(false);
        entry.playbackPositionTicks = static_cast<qint64>(
            object.value(QStringLiteral("PlaybackPositionTicks")).toDouble(0));
        entry.playCount = object.value(QStringLiteral("PlayCount")).toInt(0);
        entries.append(entry);
    }
    return entries;
}

void EmbyWebSocket::handleTextMessage(const QString &text)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(logServer) << "websocket: unparseable frame dropped:" << error.errorString();
        return;
    }

    const QJsonObject root = document.object();
    const QString type = root.value(QStringLiteral("MessageType")).toString();
    if (type.isEmpty()) {
        qCWarning(logServer) << "websocket: frame without MessageType dropped";
        return;
    }
    // Data is an object for the types below, but a bare string or number for
    // others (ForceKeepAlive sends a number of seconds). toObject() defaults to
    // {} for those, which is exactly what the handlers want.
    const QJsonValue dataValue = root.value(QStringLiteral("Data"));
    const QJsonObject data = dataValue.toObject();

    if (type == QLatin1String("LibraryChanged")) {
        QStringList added;
        QStringList removed;
        QStringList updated;
        parseLibraryChanged(data, &added, &removed, &updated);
        if (added.isEmpty() && removed.isEmpty() && updated.isEmpty())
            qCDebug(logServer) << "websocket: empty LibraryChanged";
        emit libraryChanged(added, removed, updated);
    } else if (type == QLatin1String("UserDataChanged")) {
        const QList<UserDataEntry> entries = parseUserDataList(data);
        QStringList ids;
        ids.reserve(entries.size());
        for (const UserDataEntry &entry : entries)
            ids.append(entry.itemId);
        if (!entries.isEmpty()) {
            emit userDataEntriesChanged(entries);
            emit userDataChanged(ids);
        }
    } else if (type == QLatin1String("ForceKeepAlive")) {
        // Data is the server's timeout in seconds. Halve it and keep a floor so
        // a nonsense value cannot turn the keep-alive into a busy loop.
        const int timeoutSeconds = static_cast<int>(dataValue.toDouble(0));
        if (timeoutSeconds > 0)
            applyKeepAliveInterval(qMax(1000, timeoutSeconds * 1000 / 2));
    } else if (type == QLatin1String("KeepAlive")) {
        // Nothing to do: liveness is the ping/pong, not this.
    } else {
        // Play / Playstate / GeneralCommand / Sessions / RefreshProgress …
        // Remote control is M12; until then the escape hatch below is the seam.
        qCDebug(logServer) << "websocket: unhandled message type" << type;
    }

    emit messageReceived(type, data);
}

} // namespace strmqt::emby
