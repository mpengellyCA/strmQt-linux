#include "WebRemoteServer.h"

#include "NetworkAddressHelper.h"
#include "TlsCertificateGenerator.h"
#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/HomeController.h"
#include "app/controllers/PlayerController.h"
#include "app/controllers/SessionController.h"
#include "app/models/MediaItemModel.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "playback/PlayerBackend.h"
#include "server/dto/ItemDetails.h"
#include "server/dto/ItemsQuery.h"
#include "server/dto/Library.h"
#include "server/emby/EmbyClient.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSslServer>
#include <QSslSocket>
#include <QUrlQuery>
#include <QUuid>

#include <memory>
#include <utility>

namespace strmqt {

namespace {

// A request line plus headers larger than this is not a browser talking to us.
constexpr qsizetype kMaxHeaderBytes = 64 * 1024;
// Most JSON bodies the remote sends are a handful of fields; the largest is a
// "play this list" carrying up to 300 rows the phone already holds.
constexpr qsizetype kMaxBodyBytes = 1024 * 1024;

constexpr int kStatusCoalesceMs = 120;
constexpr int kStatusTickMs = 5000;
constexpr int kKeepAliveMs = 20000;

// Backpressure: no further request on a connection is parsed while this much
// output is still unsent, and a connection whose backlog keeps growing past
// the hard cap is dropped. Neither the client's input nor our output can grow
// without bound for a client that never reads.
constexpr qint64 kWritePauseBytes = 256 * 1024;
constexpr qint64 kWriteAbortBytes = 4 * 1024 * 1024;
// Room for one largest request; QSslSocket stops reading beyond it.
constexpr qint64 kReadBufferBytes = kMaxHeaderBytes + kMaxBodyBytes + 16 * 1024;
// Past this many open connections, a new one is closed as soon as it is up.
// (Handshakes still in progress are capped by QTcpServer's pending limit and
// QSslServer's handshake timeout.)
constexpr qsizetype kMaxConnections = 32;
constexpr char kTimeoutTimerName[] = "remoteTimeout";
// Set on a connection once it is an /api/events stream: from then on it only
// receives, and anything the client sends on it is discarded unparsed.
constexpr char kEventStreamProperty[] = "remoteEventStream";
// The bearer token an event stream was opened with: one stream per token.
constexpr char kEventTokenProperty[] = "remoteEventToken";

// Unsent output on a TLS socket: plaintext Qt has not yet encrypted, plus
// ciphertext the kernel has not yet taken. bytesToWrite() alone drops to zero
// as soon as Qt encrypts, however much is still queued.
qint64 writeBacklog(const QSslSocket *socket)
{
    return socket->bytesToWrite() + socket->encryptedBytesToWrite();
}

// Per peer address: the fifth miss in a row locks that address out for 2 s;
// each further miss once the lockout has passed doubles it, up to 5 min.
// Every try while locked out is 429, even the right PIN. Nothing is shared
// between addresses, and an address idle for 10 min is forgotten.
constexpr int kMaxFailedPins = 5;
constexpr qint64 kPinLockoutMs = 2000;
constexpr qint64 kPinMaxLockoutMs = 5 * 60 * 1000;
constexpr qint64 kPinFailureIdleMs = 10 * 60 * 1000;

// Fixed-length XOR over the expected PIN. The time taken depends on the PIN's
// length only, never on how many leading digits a guess got right.
bool constantTimeEquals(const QByteArray &given, const QByteArray &expected)
{
    const QByteArray padded = given.leftJustified(expected.size(), '\0', true);
    quint8 diff = given.size() == expected.size() ? 0 : 1;
    for (qsizetype i = 0; i < expected.size(); ++i)
        diff |= quint8(padded.at(i)) ^ quint8(expected.at(i));
    return diff == 0;
}

bool hasControlCharacter(const QString &text)
{
    for (const QChar c : text) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7F)
            return true;
    }
    return false;
}

QByteArray mimeTypeForPath(const QString &path)
{
    if (path.endsWith(QLatin1String(".html"))) return "text/html; charset=utf-8";
    if (path.endsWith(QLatin1String(".css"))) return "text/css; charset=utf-8";
    if (path.endsWith(QLatin1String(".js"))) return "text/javascript; charset=utf-8";
    if (path.endsWith(QLatin1String(".json"))) return "application/json";
    if (path.endsWith(QLatin1String(".svg"))) return "image/svg+xml";
    if (path.endsWith(QLatin1String(".png"))) return "image/png";
    if (path.endsWith(QLatin1String(".jpg")) || path.endsWith(QLatin1String(".jpeg"))) return "image/jpeg";
    if (path.endsWith(QLatin1String(".ttf"))) return "font/ttf";
    return "application/octet-stream";
}

// Every pattern here is anchored with anchoredPattern() (\A...\z): `$` would
// also match before a trailing newline.
bool isSafeId(const QString &value)
{
    static const QRegularExpression re(
        QRegularExpression::anchoredPattern(QStringLiteral("[A-Za-z0-9_-]{1,128}")));
    return re.match(value).hasMatch();
}

// An Emby item id as the server issues them: decimal on Emby, 32 hex digits
// (bare or as a dashed GUID) on Jellyfin and for some views.
bool isEmbyItemId(const QString &value)
{
    static const QRegularExpression re(QRegularExpression::anchoredPattern(QStringLiteral(
        "[0-9]{1,20}|[0-9A-Fa-f]{32}"
        "|[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}")));
    return re.match(value).hasMatch();
}

// Emby's ImageType names; the image proxy asks for no other.
bool isImageType(const QString &value)
{
    static const QSet<QString> types{
        QStringLiteral("Primary"), QStringLiteral("Art"),   QStringLiteral("Backdrop"),
        QStringLiteral("Banner"),  QStringLiteral("Logo"),  QStringLiteral("Thumb"),
        QStringLiteral("Disc"),    QStringLiteral("Box"),   QStringLiteral("Screenshot"),
        QStringLiteral("Menu"),    QStringLiteral("Chapter"), QStringLiteral("BoxRear"),
        QStringLiteral("Profile")};
    return types.contains(value);
}

// Only these are safe to hand back as an <img> source: no SVG (it can carry
// script), no other document type that a browser might render or execute.
bool isAllowedImageMime(const QString &mime)
{
    static const QSet<QString> allowed{
        QStringLiteral("image/jpeg"), QStringLiteral("image/png"), QStringLiteral("image/webp"),
        QStringLiteral("image/gif"), QStringLiteral("image/avif")};
    return allowed.contains(mime);
}

QStringList splitList(const QString &value)
{
    QStringList out;
    const QStringList parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            out.append(trimmed);
    }
    return out;
}

QString queryValue(const QUrlQuery &q, const char *name)
{
    return q.queryItemValue(QString::fromLatin1(name), QUrl::FullyDecoded);
}

QJsonObject imageRefJson(const MediaItem::ImageRef &ref)
{
    if (!ref.isValid())
        return {};
    QJsonObject o;
    o[QStringLiteral("itemId")] = ref.itemId;
    o[QStringLiteral("type")] = ref.imageType;
    o[QStringLiteral("tag")] = ref.tag;
    return o;
}

// The desktop's four accents (Theme.qml). The phone follows whichever is set so
// it reads as the same product.
QString accentHex(const QString &name)
{
    if (name == QLatin1String("emby")) return QStringLiteral("#52B54B");
    if (name == QLatin1String("jellyfin")) return QStringLiteral("#AA5CC3");
    if (name == QLatin1String("breeze")) return QStringLiteral("#3DAEE9");
    return QStringLiteral("#F0A02A");
}

QString repeatModeName(PlayQueue::RepeatMode mode)
{
    switch (mode) {
    case PlayQueue::RepeatAll: return QStringLiteral("all");
    case PlayQueue::RepeatOne: return QStringLiteral("one");
    default: return QStringLiteral("off");
    }
}

// The item map ItemActions and PlayQueue take, built from a server item. Same
// role names as MediaItemModel::get(), so the verbs see exactly what a desktop
// card would have handed them.
QVariantMap itemMap(const MediaItem &item)
{
    QVariantMap map;
    const auto &roles = MediaItemModel::mediaRoleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), MediaItemModel::dataForItem(item, it.key()));
    // Not model roles, but resolve() reads them: they let an episode started
    // from the phone continue its series.
    map.insert(QStringLiteral("seriesId"), item.seriesId);
    map.insert(QStringLiteral("seasonId"), item.seasonId);
    return map;
}

// Minimal map for rows the phone already holds (an episode list it is playing
// from). Enough for the queue to label and start each row.
QVariantMap itemMapFromClient(const QJsonObject &o)
{
    QVariantMap map;
    map.insert(QStringLiteral("itemId"), o.value(QLatin1String("id")).toString());
    map.insert(QStringLiteral("name"), o.value(QLatin1String("name")).toString());
    map.insert(QStringLiteral("type"), o.value(QLatin1String("type")).toString());
    map.insert(QStringLiteral("seriesName"), o.value(QLatin1String("seriesName")).toString());
    map.insert(QStringLiteral("seriesId"), o.value(QLatin1String("seriesId")).toString());
    map.insert(QStringLiteral("seasonId"), o.value(QLatin1String("seasonId")).toString());
    map.insert(QStringLiteral("indexNumber"), o.value(QLatin1String("indexNumber")).toInt(-1));
    map.insert(QStringLiteral("parentIndexNumber"),
               o.value(QLatin1String("parentIndexNumber")).toInt(-1));
    map.insert(QStringLiteral("runtimeMs"), o.value(QLatin1String("runtimeMs")).toVariant());
    map.insert(QStringLiteral("album"), o.value(QLatin1String("album")).toString());
    map.insert(QStringLiteral("albumId"), o.value(QLatin1String("albumId")).toString());
    return map;
}

ItemsQuery itemsQueryFrom(const QUrlQuery &q)
{
    ItemsQuery query;
    query.parentId = queryValue(q, "parentId");
    query.searchTerm = queryValue(q, "search");
    query.sortBy = queryValue(q, "sortBy");
    if (query.sortBy.isEmpty())
        query.sortBy = QStringLiteral("SortName");
    query.sortDescending = queryValue(q, "sortOrder") == QLatin1String("Descending");
    query.includeItemTypes = splitList(queryValue(q, "types"));
    query.filters = splitList(queryValue(q, "filters"));
    query.genreIds = splitList(queryValue(q, "genreIds"));
    query.personIds = splitList(queryValue(q, "personIds"));
    query.studioIds = splitList(queryValue(q, "studioIds"));
    query.artistIds = splitList(queryValue(q, "artistIds"));
    query.albumArtistIds = splitList(queryValue(q, "albumArtistIds"));
    // A letter jump as a name range (ItemsQuery): NameStartsWith times out on
    // large artist lists, and "#" has no prefix form at all.
    const QString letter = queryValue(q, "letter").toUpper();
    if (letter == QLatin1String("#")) {
        query.nameLessThan = QStringLiteral("A");
    } else if (letter.size() == 1 && letter.at(0) >= QLatin1Char('A')
               && letter.at(0) <= QLatin1Char('Z')) {
        query.nameStartsWithOrGreater = letter;
        if (letter.at(0) != QLatin1Char('Z'))
            query.nameLessThan = QString(QChar(letter.at(0).unicode() + 1));
    }
    query.recursive = queryValue(q, "recursive") == QLatin1String("true");
    query.startIndex = qMax(0, queryValue(q, "startIndex").toInt());
    const QString limit = queryValue(q, "limit");
    query.limit = limit.isEmpty() ? 60 : qBound(1, limit.toInt(), 200);
    query.fields = {QStringLiteral("Overview"), QStringLiteral("PremiereDate")};
    return query;
}

ItemsQuery itemsQueryFrom(const QJsonObject &o)
{
    QUrlQuery q;
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QJsonValue v = it.value();
        q.addQueryItem(it.key(), v.isBool() ? (v.toBool() ? QStringLiteral("true")
                                                          : QStringLiteral("false"))
                                            : v.toVariant().toString());
    }
    return itemsQueryFrom(q);
}

} // namespace

WebRemoteServer::WebRemoteServer(Settings *settings,
                                 PlayerController *player,
                                 ItemActions *actions,
                                 HomeController *home,
                                 SessionController *session,
                                 emby::EmbyClient *client,
                                 QObject *parent)
    : QObject(parent),
      m_settings(settings),
      m_player(player),
      m_actions(actions),
      m_home(home),
      m_session(session),
      m_client(client),
      m_server(new QSslServer(this)),
      m_imageNam(new QNetworkAccessManager(this))
{
    m_pinClock.start();
    connect(m_server, &QSslServer::startedEncryptionHandshake, this, &WebRemoteServer::onStartedEncryptionHandshake);
    connect(m_server, &QSslServer::sslErrors, this, [](QSslSocket *, const QList<QSslError> &errors) {
        qCWarning(logApp) << "webremote server sslErrors:" << errors;
    });
    connect(m_server, &QSslServer::handshakeInterruptedOnError, this, [](QSslSocket *, const QSslError &error) {
        qCWarning(logApp) << "webremote server handshakeInterruptedOnError:" << error;
    });
    connect(m_server, &QSslServer::errorOccurred, this, [](QSslSocket *, QAbstractSocket::SocketError error) {
        qCWarning(logApp) << "webremote server errorOccurred:" << error;
    });

    m_statusCoalesce.setSingleShot(true);
    m_statusCoalesce.setInterval(kStatusCoalesceMs);
    connect(&m_statusCoalesce, &QTimer::timeout, this, &WebRemoteServer::broadcastPlayerStatus);

    m_statusTick.setInterval(kStatusTickMs);
    connect(&m_statusTick, &QTimer::timeout, this, [this] {
        if (m_player && m_player->active() && !m_player->paused() && !m_sseClients.isEmpty())
            broadcastPlayerStatus();
    });

    m_keepAlive.setInterval(kKeepAliveMs);
    connect(&m_keepAlive, &QTimer::timeout, this, [this] {
        // An SSE comment line: ignored by EventSource, but it is traffic.
        writeToAllStreams(": keep-alive\n\n");
    });

    // Player signal bindings for live status push
    if (m_player) {
        const auto schedule = [this] { scheduleStatus(); };
        connect(m_player, &PlayerController::activeChanged, this, schedule);
        connect(m_player, &PlayerController::pausedChanged, this, schedule);
        connect(m_player, &PlayerController::busyChanged, this, schedule);
        connect(m_player, &PlayerController::bufferingChanged, this, schedule);
        connect(m_player, &PlayerController::isAudioChanged, this, schedule);
        connect(m_player, &PlayerController::titleChanged, this, schedule);
        connect(m_player, &PlayerController::durationChanged, this, schedule);
        connect(m_player, &PlayerController::streamMethodChanged, this, schedule);
        connect(m_player, &PlayerController::errorMessageChanged, this, schedule);
        connect(m_player, &PlayerController::volumeChanged, this, schedule);
        connect(m_player, &PlayerController::mutedChanged, this, schedule);
        connect(m_player, &PlayerController::playbackSpeedChanged, this, schedule);
        connect(m_player, &PlayerController::audioDelayChanged, this, schedule);
        connect(m_player, &PlayerController::subtitleDelayChanged, this, schedule);
        connect(m_player, &PlayerController::sourcesChanged, this, schedule);
        connect(m_player, &PlayerController::sourceIndexChanged, this, schedule);
        connect(m_player, &PlayerController::upNextChanged, this, schedule);
        connect(m_player, &PlayerController::chaptersChanged, this, schedule);
        connect(m_player, &PlayerController::currentChapterChanged, this, schedule);
        connect(m_player, &PlayerController::seeked, this, schedule);
        connect(m_player, &PlayerController::queueStateChanged, this, [this] {
            scheduleStatus();
            broadcastQueue();
        });
        if (auto *backend = qobject_cast<PlayerBackend *>(m_player->backendObject()))
            connect(backend, &PlayerBackend::tracksChanged, this, schedule);
        if (PlayQueue *queue = m_player->queue()) {
            connect(queue, &PlayQueue::queueChanged, this, &WebRemoteServer::broadcastQueue);
            connect(queue, &PlayQueue::currentChanged, this, &WebRemoteServer::broadcastQueue);
            connect(queue, &PlayQueue::shuffledChanged, this, schedule);
            connect(queue, &PlayQueue::repeatModeChanged, this, schedule);
        }
    }
    if (m_settings) {
        // A new PIN signs out every phone that signed in with the old one.
        connect(m_settings, &Settings::webRemotePinChanged, this, [this] {
            m_authorizedTokens.clear();
            // An open event stream was authorised with the old PIN: close it
            // too. Swapped out first: abort() emits disconnected synchronously,
            // and that handler edits m_sseClients.
            const QList<QPointer<QSslSocket>> streams = std::exchange(m_sseClients, {});
            for (const QPointer<QSslSocket> &socket : streams) {
                if (socket)
                    socket->abort();
            }
            if (!streams.isEmpty())
                emit connectedClientsChanged(0);
        });
        const auto schedule = [this] { scheduleStatus(); };
        connect(m_settings, &Settings::maxBitrateKbpsChanged, this, schedule);
        connect(m_settings, &Settings::playbackModeChanged, this, schedule);
        connect(m_settings, &Settings::subtitleStyleChanged, this, schedule);
        connect(m_settings, &Settings::themeAccentChanged, this, schedule);
    }
}

WebRemoteServer::~WebRemoteServer()
{
    stop();
}

bool WebRemoteServer::start()
{
    if (m_server->isListening())
        return true;
    m_error.clear();

    // Ensure self-signed certificate exists with SANs
    const QStringList sanAddresses = NetworkAddressHelper::allHostAddressesForSan();
    if (!TlsCertificateGenerator::ensureCertificate(sanAddresses)) {
        m_error = QStringLiteral("Could not create the TLS certificate.");
        qCWarning(logApp) << "webremote: failed to ensure TLS certificate";
        return false;
    }

    QSslCertificate cert;
    QSslKey key;
    if (!TlsCertificateGenerator::load(cert, key)) {
        m_error = QStringLiteral("Could not load the TLS certificate.");
        qCWarning(logApp) << "webremote: failed to load TLS certificate and key";
        return false;
    }

    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    config.setLocalCertificate(cert);
    config.setPrivateKey(key);
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    m_server->setSslConfiguration(config);

    const int port = m_settings ? m_settings->webRemotePort() : 8337;
    const QString mode = m_settings ? m_settings->webRemoteBindMode() : QStringLiteral("all");
    const QHostAddress bindAddr = m_bindResolver ? m_bindResolver(mode)
                                                 : NetworkAddressHelper::resolveBindAddress(mode);

    if (!m_server->listen(bindAddr, static_cast<quint16>(port))) {
        m_error = QStringLiteral("Could not listen on port %1: %2")
                      .arg(port)
                      .arg(m_server->errorString());
        qCWarning(logApp) << "webremote: failed to listen on" << bindAddr.toString() << port
                         << ":" << m_server->errorString();
        emit runningChanged(false);
        return false;
    }

    m_statusTick.start();
    m_keepAlive.start();
    qCInfo(logApp) << "webremote: listening on" << bindAddr.toString() << port;
    emit runningChanged(true);
    return true;
}

void WebRemoteServer::stop()
{
    const bool wasListening = m_server->isListening();
    if (wasListening)
        m_server->close();
    m_statusTick.stop();
    m_keepAlive.stop();
    m_statusCoalesce.stop();
    // Every phone signs in again after a stop: a restart, a logout or a
    // server switch.
    m_authorizedTokens.clear();
    m_sseClients.clear();
    // Every open connection goes too: a keep-alive socket or event stream
    // must not outlive the server it was opened on. A copy, because abort()
    // emits disconnected synchronously and its handler edits m_clients.
    const QList<QPointer<QSslSocket>> clients = std::exchange(m_clients, {});
    for (const QPointer<QSslSocket> &socket : clients) {
        if (socket)
            socket->abort();
    }
    emit connectedClientsChanged(0);

    if (wasListening) {
        emit runningChanged(false);
        qCInfo(logApp) << "webremote: stopped";
    }
}

bool WebRemoteServer::isRunning() const
{
    return m_server && m_server->isListening();
}

quint16 WebRemoteServer::port() const
{
    return m_server ? m_server->serverPort() : 0;
}

QHostAddress WebRemoteServer::boundAddress() const
{
    return m_server->isListening() ? m_server->serverAddress() : QHostAddress();
}

void WebRemoteServer::setTimeoutsForTests(int requestMs, int idleMs)
{
    m_requestTimeoutMs = requestMs;
    m_idleTimeoutMs = idleMs;
}

void WebRemoteServer::setBindAddressResolverForTests(
    std::function<QHostAddress(const QString &)> resolver)
{
    m_bindResolver = std::move(resolver);
}

void WebRemoteServer::setClockForTests(std::function<qint64()> nowMs)
{
    m_clock = std::move(nowMs);
}

int WebRemoteServer::connectedClientsCount() const
{
    return m_sseClients.size();
}

bool WebRemoteServer::regenerateCertificate()
{
    const bool wasRunning = isRunning();
    if (wasRunning)
        stop();

    const QStringList sanAddresses = NetworkAddressHelper::allHostAddressesForSan();
    const bool ok = TlsCertificateGenerator::ensureCertificate(sanAddresses, true);

    if (wasRunning && ok)
        start();

    return ok;
}

void WebRemoteServer::setInteractionContext(const QString &context)
{
    if (m_interactionContext == context)
        return;
    m_interactionContext = context;
    scheduleStatus();
}

namespace {

struct NavigationKey {
    const char *key;
    const char *action;
};

constexpr NavigationKey kNavigationKeys[] = {
    {"up", "nav.up"},
    {"down", "nav.down"},
    {"left", "nav.left"},
    {"right", "nav.right"},
    {"select", "nav.select"},
    {"back", "nav.back"},
    {"menu", "nav.contextMenu"},
    {"pageUp", "nav.pageUp"},
    {"pageDown", "nav.pageDown"},
    {"prevTab", "nav.previousTab"},
    {"nextTab", "nav.nextTab"},
    {"toggleMenu", "app.toggleMenu"},
    {"fullscreen", "app.fullscreen"},
};

} // namespace

QStringList WebRemoteServer::navigationKeys()
{
    QStringList keys;
    for (const NavigationKey &entry : kNavigationKeys)
        keys.append(QString::fromLatin1(entry.key));
    return keys;
}

QString WebRemoteServer::actionForNavigationKey(const QString &key)
{
    for (const NavigationKey &entry : kNavigationKeys) {
        if (key == QLatin1String(entry.key))
            return QString::fromLatin1(entry.action);
    }
    return {};
}

void WebRemoteServer::onStartedEncryptionHandshake(QSslSocket *socket)
{
    // QSslServer's own `encrypted` handler (connected before this signal)
    // queues the socket as pending; ours runs after it and dequeues it.
    connect(socket, &QSslSocket::encrypted, this, [this, socket] { onEncrypted(socket); });
    connect(socket, &QAbstractSocket::disconnected, socket, &QObject::deleteLater);
}

void WebRemoteServer::onEncrypted(QSslSocket *socket)
{
    m_server->nextPendingConnection(); // keep QTcpServer's pending queue empty
    // A handshake that finishes after stop() must not become a live connection.
    if (!m_server->isListening()) {
        socket->abort();
        return;
    }
    m_clients.removeIf([](const QPointer<QSslSocket> &client) { return client.isNull(); });
    if (m_clients.size() >= kMaxConnections) {
        qCWarning(logApp) << "webremote: connection limit reached; closing a new connection";
        socket->abort();
        return;
    }
    m_clients.append(socket);
    socket->setReadBufferSize(kReadBufferBytes);
    auto *timer = new QTimer(socket);
    timer->setObjectName(QLatin1String(kTimeoutTimerName));
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, socket, [socket] {
        qCDebug(logApp) << "webremote: closing a slow or idle connection";
        socket->abort();
    });
    restartTimeout(socket, false);

    auto buffer = std::make_shared<QByteArray>();
    connect(socket, &QSslSocket::readyRead, this, [this, socket, buffer] {
        handleReadyRead(socket, *buffer);
    });
    // Output drained: a download in progress is not idle, and parsing paused
    // by backpressure picks up where it stopped.
    connect(socket, &QSslSocket::encryptedBytesWritten, this, [this, socket, buffer] {
        if (!requestPending(socket))
            restartTimeout(socket, false);
        if (writeBacklog(socket) <= kWritePauseBytes
            && (!buffer->isEmpty() || socket->bytesAvailable() > 0))
            handleReadyRead(socket, *buffer);
    });
    connect(socket, &QAbstractSocket::disconnected, this, [this, socket] {
        m_clients.removeAll(socket);
        if (m_sseClients.removeAll(socket) > 0)
            emit connectedClientsChanged(connectedClientsCount());
    });
    if (socket->bytesAvailable() > 0)
        handleReadyRead(socket, *buffer);
}

void WebRemoteServer::restartTimeout(QSslSocket *socket, bool pending)
{
    auto *timer = socket->findChild<QTimer *>(QLatin1String(kTimeoutTimerName),
                                              Qt::FindDirectChildrenOnly);
    if (!timer)
        return; // exempt
    timer->setProperty("requestPending", pending);
    timer->start(pending ? m_requestTimeoutMs : m_idleTimeoutMs);
}

bool WebRemoteServer::requestPending(QSslSocket *socket) const
{
    const auto *timer = socket->findChild<QTimer *>(QLatin1String(kTimeoutTimerName),
                                                    Qt::FindDirectChildrenOnly);
    return timer && timer->property("requestPending").toBool();
}

void WebRemoteServer::exemptFromIdleTimeout(QSslSocket *socket)
{
    auto *timer = socket->findChild<QTimer *>(QLatin1String(kTimeoutTimerName),
                                              Qt::FindDirectChildrenOnly);
    if (!timer)
        return;
    timer->stop();
    timer->setObjectName(QString()); // no longer found, so never restarted
    timer->deleteLater();
}

void WebRemoteServer::handleReadyRead(QSslSocket *socket, QByteArray &buffer)
{
    // An event stream takes no further requests: a response written into it
    // would corrupt the stream.
    if (socket->property(kEventStreamProperty).toBool()) {
        buffer.clear();
        socket->readAll();
        return;
    }
    // Backpressure: leave further input unread (QSslSocket's read buffer is
    // bounded) until encryptedBytesWritten shows the client is reading.
    if (writeBacklog(socket) > kWritePauseBytes)
        return;
    buffer.append(socket->readAll());
    // The request clock starts at a request's first byte and is not extended
    // by later ones.
    if (!buffer.isEmpty() && !requestPending(socket))
        restartTimeout(socket, true);

    while (!buffer.isEmpty() && socket->state() == QAbstractSocket::ConnectedState
           && writeBacklog(socket) <= kWritePauseBytes) {
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0 || headerEnd > kMaxHeaderBytes) {
            if (headerEnd > kMaxHeaderBytes || buffer.size() > kMaxHeaderBytes) {
                sendResponse(socket, 431, "text/plain", "Request headers too large");
                buffer.clear();
                socket->disconnectFromHost();
            }
            return; // Headers incomplete
        }

        const QByteArray headerBlock = buffer.left(headerEnd);
        const QList<QByteArray> lines = headerBlock.split('\n');

        HttpRequest req;
        qsizetype contentLength = 0;
        bool badLength = false;
        bool seenLength = false;
        bool transferEncoding = false;

        const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
        req.method = QString::fromLatin1(requestLine.value(0)).toUpper();
        req.url = QUrl(QString::fromLatin1(requestLine.value(1)), QUrl::TolerantMode);
        req.path = req.url.path();

        for (qsizetype i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines[i].trimmed();
            const qsizetype colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            const QByteArray key = line.left(colon).trimmed().toLower();
            const QByteArray val = line.mid(colon + 1).trimmed();
            req.headers.insert(key, val);
            if (key == "content-length") {
                bool ok = false;
                const qlonglong n = val.toLongLong(&ok);
                // Two lengths are ambiguous framing (request smuggling): refused.
                badLength = badLength || seenLength || !ok || n < 0;
                seenLength = true;
                contentLength =
                    badLength ? 0 : static_cast<qsizetype>(qMin<qlonglong>(n, kMaxBodyBytes + 1));
            } else if (key == "transfer-encoding") {
                transferEncoding = true;
            }
        }

        // Only Content-Length framing is understood; guessing at chunked
        // bodies would desynchronise the connection.
        if (transferEncoding) {
            sendResponse(socket, 501, "text/plain", "Transfer-Encoding not supported");
            buffer.clear();
            socket->disconnectFromHost();
            return;
        }
        if (badLength) {
            sendResponse(socket, 400, "text/plain", "Bad Content-Length");
            buffer.clear();
            socket->disconnectFromHost();
            return;
        }
        // Refused on the header alone: a body over the cap is never buffered.
        if (contentLength > kMaxBodyBytes) {
            sendResponse(socket, 413, "text/plain", "Request body too large");
            buffer.clear();
            socket->disconnectFromHost();
            return;
        }

        const qsizetype bodyStart = headerEnd + 4;
        if (buffer.size() - bodyStart < contentLength)
            return; // Body incomplete, wait for more data

        req.body = buffer.mid(bodyStart, contentLength);
        buffer.remove(0, bodyStart + contentLength);
        // Idle until the next request; a pipelined one already buffered starts
        // its own request clock now.
        restartTimeout(socket, !buffer.isEmpty());
        dispatchRequest(socket, req);
        // That request opened an event stream: whatever was pipelined behind
        // it is dropped, not answered into the stream.
        if (socket->property(kEventStreamProperty).toBool()) {
            buffer.clear();
            return;
        }
    }
}

void WebRemoteServer::dispatchRequest(QSslSocket *socket, const HttpRequest &req)
{
    if (!m_server->isListening()) { // stopped while this request was in flight
        socket->abort();
        return;
    }
    // A decoded %0A, %00 or %7F has no business in a path, and a newline could
    // otherwise slip past a pattern or into a log line.
    if (hasControlCharacter(req.path)) {
        sendResponse(socket, 400, "text/plain", "Bad Request");
        return;
    }
    qCDebug(logApp) << "webremote:" << req.method << req.path;

    // The page and its API share one origin, so there is no cross-origin
    // client to serve. No CORS headers are sent: a page elsewhere in the
    // phone's browser must not be able to drive the TV.
    if (req.method == QLatin1String("OPTIONS")) {
        sendResponse(socket, 204, "text/plain", "");
        return;
    }

    if (req.method == QLatin1String("GET") && !req.path.startsWith(QLatin1String("/api/"))) {
        if (!handleStaticFile(socket, req.path))
            sendResponse(socket, 404, "text/plain", "Not Found");
        return;
    }

    if (!req.path.startsWith(QLatin1String("/api/"))) {
        sendResponse(socket, 404, "text/plain", "Not Found");
        return;
    }

    // A JSON body is required on every POST. Besides being what the page sends,
    // it forces a preflight for any cross-origin form or fetch, which fails.
    if (req.method == QLatin1String("POST")
        && !req.headers.value("content-type").startsWith("application/json")) {
        sendError(socket, 415, QStringLiteral("Expected application/json"));
        return;
    }

    // PIN Authentication Check
    if (req.path == QLatin1String("/api/auth/pin")) {
        if (req.method == QLatin1String("GET")) {
            QJsonObject res;
            res[QStringLiteral("required")] = m_settings ? m_settings->webRemoteRequirePin() : false;
            res[QStringLiteral("authorized")] =
                !(m_settings && m_settings->webRemoteRequirePin()) || isAuthorized(req);
            sendJson(socket, 200, res);
            return;
        }
        if (req.method == QLatin1String("POST")) {
            handleApiAuthPin(socket, QJsonDocument::fromJson(req.body).object());
            return;
        }
    }

    if (m_settings && m_settings->webRemoteRequirePin() && !isAuthorized(req)) {
        QJsonObject err;
        err[QStringLiteral("error")] = QStringLiteral("Unauthorized");
        err[QStringLiteral("requirePin")] = true;
        sendJson(socket, 401, err);
        return;
    }

    if (!dispatchApi(socket, req))
        sendError(socket, 404, QStringLiteral("Unknown endpoint"));
}

bool WebRemoteServer::dispatchApi(QSslSocket *socket, const HttpRequest &req)
{
    const bool get = req.method == QLatin1String("GET");
    const bool post = req.method == QLatin1String("POST");
    const QString &path = req.path;
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts); // "api", ...
    const QJsonObject body = post ? QJsonDocument::fromJson(req.body).object() : QJsonObject();

    if (get) {
        if (path == QLatin1String("/api/status")) {
            sendJson(socket, 200, currentStatusJson());
            return true;
        }
        if (path == QLatin1String("/api/queue")) {
            sendJsonArray(socket, 200, currentQueueJson());
            return true;
        }
        if (path == QLatin1String("/api/events")) {
            handleApiEvents(socket, req);
            return true;
        }
        if (path == QLatin1String("/api/home")) {
            handleApiHome(socket);
            return true;
        }
        if (path == QLatin1String("/api/libraries")) {
            handleApiLibraries(socket);
            return true;
        }
        if (path == QLatin1String("/api/items")) {
            handleApiItems(socket, req);
            return true;
        }
        if (path == QLatin1String("/api/artists")) {
            handleApiArtists(socket, req);
            return true;
        }
        if (path == QLatin1String("/api/search")) {
            handleApiSearch(socket, QUrlQuery(req.url).queryItemValue(QStringLiteral("q"),
                                                                        QUrl::FullyDecoded));
            return true;
        }
        // Back-compat for the old page's grid: /api/library/{id}/items
        if (parts.size() == 4 && parts.at(1) == QLatin1String("library")
            && parts.at(3) == QLatin1String("items") && isSafeId(parts.at(2))) {
            HttpRequest scoped = req;
            QUrlQuery q(req.url);
            q.removeAllQueryItems(QStringLiteral("parentId"));
            q.addQueryItem(QStringLiteral("parentId"), parts.at(2));
            scoped.url.setQuery(q);
            handleApiItems(socket, scoped);
            return true;
        }
        if (parts.size() >= 3 && parts.at(1) == QLatin1String("item") && isSafeId(parts.at(2))) {
            const QString id = parts.at(2);
            if (parts.size() == 3) {
                handleApiItemDetails(socket, id);
                return true;
            }
            if (parts.size() == 4 && parts.at(3) == QLatin1String("seasons")) {
                handleApiSeasons(socket, id);
                return true;
            }
            if (parts.size() == 4 && parts.at(3) == QLatin1String("episodes")) {
                handleApiEpisodes(socket, id, req);
                return true;
            }
            if (parts.size() == 4 && parts.at(3) == QLatin1String("playlist")) {
                handleApiPlaylistItems(socket, id);
                return true;
            }
        }
        // /api/image/{id}/{type}
        if (parts.size() == 4 && parts.at(1) == QLatin1String("image")) {
            handleApiImage(socket, parts.at(2), parts.at(3), req);
            return true;
        }
        return false;
    }

    if (!post)
        return false;

    if (path == QLatin1String("/api/play")) {
        handleApiPlay(socket, body);
        return true;
    }
    if (path == QLatin1String("/api/playback")) {
        handleApiPlayback(socket, body);
        return true;
    }
    if (path == QLatin1String("/api/queue")) {
        handleApiQueueAction(socket, body);
        return true;
    }
    if (path == QLatin1String("/api/volume")) {
        handleApiVolume(socket, body);
        return true;
    }
    if (path == QLatin1String("/api/stream")) {
        handleApiStream(socket, body);
        return true;
    }
    if (path == QLatin1String("/api/quality")) {
        handleApiQuality(socket, body);
        return true;
    }
    if (path == QLatin1String("/api/subtitles/style")) {
        handleApiSubtitleStyle(socket, body);
        return true;
    }
    if (path == QLatin1String("/api/navigate")) {
        handleApiNavigate(socket, body);
        return true;
    }
    // /api/item/{id}/favorite and /api/item/{id}/played
    if (parts.size() == 4 && parts.at(1) == QLatin1String("item") && isSafeId(parts.at(2))
        && (parts.at(3) == QLatin1String("favorite") || parts.at(3) == QLatin1String("played"))) {
        handleApiUserData(socket, parts.at(2), parts.at(3), body);
        return true;
    }
    return false;
}

bool WebRemoteServer::handleStaticFile(QSslSocket *socket, const QString &path)
{
    // Fonts are the desktop's own (Theme.qml), served from the same resources so
    // the phone renders in the product's type rather than the handset's default.
    static const QHash<QString, QString> fonts{
        {QStringLiteral("/fonts/archivo.ttf"), QStringLiteral(":/fonts/Archivo[wdth,wght].ttf")},
        {QStringLiteral("/fonts/public-sans.ttf"), QStringLiteral(":/fonts/PublicSans[wght].ttf")},
        {QStringLiteral("/fonts/plex-mono.ttf"), QStringLiteral(":/fonts/IBMPlexMono-Medium.ttf")},
    };

    QString resPath;
    bool immutable = false;
    if (path == QLatin1String("/") || path == QLatin1String("/index.html")) {
        resPath = QStringLiteral(":/webremote/index.html");
    } else if (fonts.contains(path)) {
        resPath = fonts.value(path);
        immutable = true;
    } else {
        // Flat names only: no directory can be named, so nothing outside the
        // remote's own resource prefix is reachable.
        static const QRegularExpression safe(QRegularExpression::anchoredPattern(
            QStringLiteral("/([a-z0-9][a-z0-9-]*\\.(?:js|css|svg|json|png))")));
        const QRegularExpressionMatch match = safe.match(path);
        if (!match.hasMatch())
            return false;
        resPath = QStringLiteral(":/webremote/") + match.captured(1);
    }

    QFile file(resPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();

    QHash<QByteArray, QByteArray> headers;
    // The page ships inside the binary, so an upgrade changes it under the same
    // URL. Revalidate every load rather than run last version's script.
    headers["Cache-Control"] = immutable ? "public, max-age=604800" : "no-cache";
    sendResponse(socket, 200, mimeTypeForPath(resPath), data, headers);
    return true;
}

bool WebRemoteServer::isAuthorized(const HttpRequest &req) const
{
    const QByteArray auth = req.headers.value("authorization");
    if (auth.startsWith("Bearer ")) {
        const QString token = QString::fromUtf8(auth.mid(7)).trimmed();
        if (m_authorizedTokens.contains(token))
            return true;
    }
    // EventSource and <img> cannot set headers, and both only GET. Anything
    // that changes state needs the header: a token in a URL leaks through
    // history and logs far more easily.
    if (req.method != QLatin1String("GET"))
        return false;
    const QString queryToken = QUrlQuery(req.url).queryItemValue(QStringLiteral("token"));
    return !queryToken.isEmpty() && m_authorizedTokens.contains(queryToken);
}

void WebRemoteServer::handleApiAuthPin(QSslSocket *socket, const QJsonObject &body)
{
    const qint64 now = m_clock ? m_clock() : m_pinClock.elapsed();
    m_pinFailures.removeIf([now](const QHash<QString, PinFailures>::iterator it) {
        return now >= it->lockedUntil && now - it->lastMiss > kPinFailureIdleMs;
    });
    // The same client over IPv4 and IPv4-mapped IPv6 is one address.
    QHostAddress peer = socket->peerAddress();
    bool isV4 = false;
    const quint32 v4 = peer.toIPv4Address(&isV4);
    if (isV4)
        peer = QHostAddress(v4);
    const QString peerKey = peer.toString();

    const auto found = m_pinFailures.constFind(peerKey);
    if (found != m_pinFailures.cend() && now < found->lockedUntil) {
        QJsonObject err;
        err[QStringLiteral("error")] = QStringLiteral("Too many failed attempts. Please wait.");
        sendJson(socket, 429, err);
        return;
    }

    const QString pin = body.value(QLatin1String("pin")).toString();
    const QString expected = m_settings ? m_settings->webRemotePin() : QString();
    if (!pin.isEmpty() && !expected.isEmpty()
        && constantTimeEquals(pin.toUtf8(), expected.toUtf8())) {
        m_pinFailures.remove(peerKey);
        const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_authorizedTokens.insert(token);
        QJsonObject resp;
        resp[QStringLiteral("token")] = token;
        sendJson(socket, 200, resp);
        return;
    }
    PinFailures &failures = m_pinFailures[peerKey];
    ++failures.misses;
    failures.lastMiss = now;
    if (failures.misses >= kMaxFailedPins) {
        failures.lockMs = failures.lockMs == 0 ? kPinLockoutMs
                                               : std::min(failures.lockMs * 2, kPinMaxLockoutMs);
        failures.lockedUntil = now + failures.lockMs;
    }
    QJsonObject err;
    err[QStringLiteral("error")] = QStringLiteral("Invalid PIN");
    sendJson(socket, 403, err);
}

// ── Serialisation ────────────────────────────────────────────────────────────

QJsonObject WebRemoteServer::itemJson(const MediaItem &it)
{
    QJsonObject o;
    o[QStringLiteral("id")] = it.id;
    o[QStringLiteral("name")] = it.name;
    o[QStringLiteral("type")] = it.type;
    const auto putString = [&o](const char *key, const QString &value) {
        if (!value.isEmpty())
            o[QLatin1String(key)] = value;
    };
    putString("seriesId", it.seriesId);
    putString("seriesName", it.seriesName);
    putString("seasonId", it.seasonId);
    putString("seasonName", it.seasonName);
    putString("officialRating", it.officialRating);
    putString("premiereDate", it.premiereDate);
    putString("endDate", it.endDate);
    putString("status", it.status);
    putString("albumArtist", it.albumArtist);
    putString("album", it.album);
    putString("albumId", it.albumId);
    putString("playlistItemId", it.playlistItemId);
    if (it.indexNumber >= 0)
        o[QStringLiteral("indexNumber")] = it.indexNumber;
    if (it.parentIndexNumber >= 0)
        o[QStringLiteral("parentIndexNumber")] = it.parentIndexNumber;
    if (it.productionYear > 0)
        o[QStringLiteral("year")] = it.productionYear;
    if (it.communityRating > 0)
        o[QStringLiteral("communityRating")] = it.communityRating;
    if (it.runtimeTicks > 0)
        o[QStringLiteral("runtimeMs")] = it.runtimeMs();
    if (it.playbackPositionTicks > 0)
        o[QStringLiteral("positionMs")] = it.positionMs();
    if (it.playedPercentage > 0)
        o[QStringLiteral("playedPercentage")] = it.playedPercentage;
    if (it.childCount > 0)
        o[QStringLiteral("childCount")] = it.childCount;
    if (it.unplayedItemCount > 0)
        o[QStringLiteral("unplayedCount")] = it.unplayedItemCount;
    o[QStringLiteral("played")] = it.played;
    o[QStringLiteral("favorite")] = it.favorite;
    o[QStringLiteral("resumable")] = it.isResumable();
    if (!it.artists.isEmpty())
        o[QStringLiteral("artists")] = QJsonArray::fromStringList(it.artists);
    if (!it.artistIds.isEmpty())
        o[QStringLiteral("artistIds")] = QJsonArray::fromStringList(it.artistIds);

    QJsonObject images;
    const QJsonObject cover = imageRefJson(it.coverSource());
    if (!cover.isEmpty())
        images[QStringLiteral("cover")] = cover;
    const QJsonObject thumb = imageRefJson(it.thumbSource());
    if (!thumb.isEmpty())
        images[QStringLiteral("thumb")] = thumb;
    if (!it.backdropImageTags.isEmpty()) {
        images[QStringLiteral("backdrop")] =
            imageRefJson({it.id, QStringLiteral("Backdrop"), it.backdropImageTags.first()});
    } else if (!it.parentBackdropImageTag.isEmpty() && !it.parentBackdropItemId.isEmpty()) {
        images[QStringLiteral("backdrop")] = imageRefJson(
            {it.parentBackdropItemId, QStringLiteral("Backdrop"), it.parentBackdropImageTag});
    }
    // An episode's own Primary is a still; the phone wants the series poster
    // for a portrait slot.
    if (!it.seriesId.isEmpty()) {
        QJsonObject series;
        series[QStringLiteral("itemId")] = it.seriesId;
        series[QStringLiteral("type")] = QStringLiteral("Primary");
        images[QStringLiteral("seriesPoster")] = series;
    }
    o[QStringLiteral("images")] = images;
    return o;
}

QJsonObject WebRemoteServer::pageJson(const QList<MediaItem> &items, int total, int startIndex)
{
    QJsonArray arr;
    for (const MediaItem &item : items)
        arr.append(itemJson(item));
    QJsonObject o;
    o[QStringLiteral("items")] = arr;
    // /Persons-class endpoints report 0 while returning rows (ARCHITECTURE.md);
    // never report fewer than were delivered.
    o[QStringLiteral("total")] = qMax(total, startIndex + static_cast<int>(items.size()));
    o[QStringLiteral("startIndex")] = startIndex;
    return o;
}

QJsonObject WebRemoteServer::currentStatusJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("version")] = QStringLiteral(STRMQT_VERSION);

    QJsonObject app;
    app[QStringLiteral("context")] = m_interactionContext;
    app[QStringLiteral("accent")] =
        accentHex(m_settings ? m_settings->themeAccent() : QString());
    app[QStringLiteral("signedIn")] = m_client && m_client->hasSession();
    obj[QStringLiteral("app")] = app;

    if (m_settings) {
        QJsonObject quality;
        quality[QStringLiteral("maxBitrateKbps")] = m_settings->maxBitrateKbps();
        quality[QStringLiteral("playbackMode")] = m_settings->playbackMode();
        obj[QStringLiteral("quality")] = quality;

        QJsonObject style;
        style[QStringLiteral("scale")] = m_settings->subtitleScale();
        style[QStringLiteral("color")] = m_settings->subtitleColor();
        style[QStringLiteral("background")] = m_settings->subtitleBackground();
        style[QStringLiteral("position")] = m_settings->subtitlePosition();
        obj[QStringLiteral("subtitleStyle")] = style;
    }

    QJsonObject playback;
    if (!m_player) {
        playback[QStringLiteral("active")] = false;
        obj[QStringLiteral("playback")] = playback;
        return obj;
    }

    playback[QStringLiteral("active")] = m_player->active();
    playback[QStringLiteral("paused")] = m_player->paused();
    playback[QStringLiteral("busy")] = m_player->busy();
    playback[QStringLiteral("buffering")] = m_player->buffering();
    playback[QStringLiteral("isAudio")] = m_player->isAudio();
    playback[QStringLiteral("positionMs")] = m_player->positionMs();
    playback[QStringLiteral("durationMs")] = m_player->durationMs();
    playback[QStringLiteral("bufferedEndMs")] = m_player->bufferedEndMs();
    playback[QStringLiteral("title")] = m_player->title();
    playback[QStringLiteral("streamMethod")] = m_player->streamMethod();
    playback[QStringLiteral("errorMessage")] = m_player->errorMessage();
    playback[QStringLiteral("volume")] = m_player->volume();
    playback[QStringLiteral("maxVolume")] = PlayerController::maxVolume();
    playback[QStringLiteral("muted")] = m_player->muted();
    playback[QStringLiteral("speed")] = m_player->playbackSpeed();
    playback[QStringLiteral("audioDelayMs")] = m_player->audioDelayMs();
    playback[QStringLiteral("subtitleDelayMs")] = m_player->subtitleDelayMs();

    if (PlayQueue *queue = m_player->queue()) {
        if (queue->currentIndex() >= 0)
            playback[QStringLiteral("item")] = itemJson(queue->current());
        QJsonObject q;
        q[QStringLiteral("index")] = queue->currentIndex();
        q[QStringLiteral("count")] = queue->rowCount();
        q[QStringLiteral("shuffled")] = queue->shuffled();
        q[QStringLiteral("repeat")] = repeatModeName(queue->repeatMode());
        q[QStringLiteral("hasNext")] = m_player->hasNext();
        q[QStringLiteral("hasPrevious")] = m_player->hasPrevious();
        q[QStringLiteral("contextLabel")] = queue->contextLabel();
        playback[QStringLiteral("queue")] = q;
    }

    QJsonObject upNext;
    upNext[QStringLiteral("visible")] = m_player->upNextVisible();
    upNext[QStringLiteral("seconds")] = m_player->upNextSecondsRemaining();
    const QVariantMap next = m_player->nextItem();
    if (!next.isEmpty()) {
        QJsonObject n;
        n[QStringLiteral("id")] = next.value(QStringLiteral("itemId")).toString();
        n[QStringLiteral("name")] = next.value(QStringLiteral("label")).toString().isEmpty()
                                        ? next.value(QStringLiteral("name")).toString()
                                        : next.value(QStringLiteral("label")).toString();
        upNext[QStringLiteral("item")] = n;
    }
    playback[QStringLiteral("upNext")] = upNext;

    QJsonArray chapters;
    for (const QVariant &chapter : m_player->chapters()) {
        const QVariantMap m = chapter.toMap();
        QJsonObject c;
        c[QStringLiteral("name")] = m.value(QStringLiteral("name")).toString();
        c[QStringLiteral("startMs")] = m.value(QStringLiteral("startMs")).toLongLong();
        chapters.append(c);
    }
    playback[QStringLiteral("chapters")] = chapters;
    playback[QStringLiteral("currentChapter")] = m_player->currentChapter();

    // Versions. The stream lists inside each source are the server's view and
    // are dropped here: the phone picks tracks from the engine's lists below.
    QJsonArray sources;
    for (const QVariant &source : m_player->sources()) {
        QVariantMap m = source.toMap();
        m.remove(QStringLiteral("audioStreams"));
        m.remove(QStringLiteral("subtitleStreams"));
        sources.append(QJsonObject::fromVariantMap(m));
    }
    playback[QStringLiteral("sources")] = sources;
    playback[QStringLiteral("sourceIndex")] = m_player->sourceIndex();
    QVariantMap current = m_player->currentSource();
    current.remove(QStringLiteral("audioStreams"));
    current.remove(QStringLiteral("subtitleStreams"));
    playback[QStringLiteral("currentSource")] = QJsonObject::fromVariantMap(current);

    // Tracks as the ENGINE numbers them — the only ids setAudioTrack() and
    // setSubtitleTrack() accept, and the only lists that include external files.
    if (auto *be = qobject_cast<PlayerBackend *>(m_player->backendObject())) {
        playback[QStringLiteral("audioTracks")] = QJsonArray::fromVariantList(be->audioTracks());
        playback[QStringLiteral("subtitleTracks")] =
            QJsonArray::fromVariantList(be->subtitleTracks());
        playback[QStringLiteral("currentAudioTrackId")] = be->currentAudioTrackId();
        playback[QStringLiteral("currentSubtitleTrackId")] = be->currentSubtitleTrackId();
    }

    obj[QStringLiteral("playback")] = playback;
    return obj;
}

QJsonArray WebRemoteServer::currentQueueJson() const
{
    QJsonArray arr;
    if (!m_player || !m_player->queue())
        return arr;

    PlayQueue *queue = m_player->queue();
    for (int i = 0; i < queue->rowCount(); ++i) {
        const QVariantMap row = queue->itemAt(i);
        QJsonObject o = itemJson(PlayQueue::itemFromVariant(row));
        o[QStringLiteral("label")] = row.value(QStringLiteral("label")).toString();
        o[QStringLiteral("current")] = i == queue->currentIndex();
        arr.append(o);
    }
    return arr;
}

// ── Browse ───────────────────────────────────────────────────────────────────

void WebRemoteServer::handleApiHome(QSslSocket *socket)
{
    if (!m_client || !m_client->hasSession()) {
        sendError(socket, 503, QStringLiteral("StrmQt is not signed in to a server"));
        return;
    }

    // Four kinds of request fan out; the reply goes when the last one lands.
    struct Pending {
        QJsonObject result;
        QList<Library> libraries;
        QHash<QString, QJsonArray> latest;
        int outstanding = 0;
    };
    auto pending = std::make_shared<Pending>();
    QPointer<QSslSocket> safeSocket(socket);

    const auto finishOne = [this, pending, safeSocket] {
        if (--pending->outstanding > 0)
            return;
        QJsonArray latest;
        for (const Library &lib : std::as_const(pending->libraries)) {
            const QJsonArray items = pending->latest.value(lib.id);
            if (items.isEmpty())
                continue;
            QJsonObject rail;
            rail[QStringLiteral("libraryId")] = lib.id;
            rail[QStringLiteral("name")] = lib.name;
            rail[QStringLiteral("collectionType")] = lib.collectionType;
            rail[QStringLiteral("items")] = items;
            latest.append(rail);
        }
        pending->result[QStringLiteral("latest")] = latest;
        if (safeSocket)
            sendJson(safeSocket, 200, pending->result);
    };

    pending->outstanding = 3;
    m_client->resumeItems(20).then(this, [pending, finishOne](const Result<ItemsPage> &res) {
        pending->result[QStringLiteral("resume")] =
            res.ok() ? pageJson(res.value.items, res.value.totalRecordCount, 0)
                                 .value(QStringLiteral("items"))
                     : QJsonArray();
        finishOne();
    });
    m_client->nextUp(20).then(this, [pending, finishOne](const Result<ItemsPage> &res) {
        pending->result[QStringLiteral("nextUp")] =
            res.ok() ? pageJson(res.value.items, res.value.totalRecordCount, 0)
                                 .value(QStringLiteral("items"))
                     : QJsonArray();
        finishOne();
    });
    m_client->userViews().then(this, [this, pending, finishOne](const Result<QList<Library>> &res) {
        if (res.ok()) {
            for (const Library &lib : res.value) {
                // Latest is meaningless for curated sets.
                if (lib.collectionType == QLatin1String("playlists")
                    || lib.collectionType == QLatin1String("boxsets"))
                    continue;
                pending->libraries.append(lib);
                ++pending->outstanding;
                const QString id = lib.id;
                m_client->latestItems(id, 16).then(
                    this, [pending, finishOne, id](const Result<QList<MediaItem>> &latest) {
                        if (latest.ok()) {
                            QJsonArray arr;
                            for (const MediaItem &item : latest.value)
                                arr.append(itemJson(item));
                            pending->latest.insert(id, arr);
                        }
                        finishOne();
                    });
            }
        }
        finishOne();
    });
}

void WebRemoteServer::handleApiLibraries(QSslSocket *socket)
{
    if (!m_client || !m_client->hasSession()) {
        sendError(socket, 503, QStringLiteral("StrmQt is not signed in to a server"));
        return;
    }

    QPointer<QSslSocket> safeSocket(socket);
    m_client->userViews().then(this, [this, safeSocket](const Result<QList<Library>> &res) {
        if (!safeSocket)
            return;
        if (!res.ok()) {
            sendError(safeSocket, 502, QStringLiteral("Could not load libraries: %1").arg(res.error));
            return;
        }
        QJsonArray arr;
        for (const Library &lib : res.value) {
            QJsonObject o;
            o[QStringLiteral("id")] = lib.id;
            o[QStringLiteral("name")] = lib.name;
            o[QStringLiteral("type")] = lib.collectionType;
            if (!lib.primaryImageTag.isEmpty())
                o[QStringLiteral("image")] = imageRefJson(
                    {lib.id, QStringLiteral("Primary"), lib.primaryImageTag});
            arr.append(o);
        }
        sendJsonArray(safeSocket, 200, arr);
    });
}

void WebRemoteServer::handleApiItems(QSslSocket *socket, const HttpRequest &req)
{
    if (!m_client || !m_client->hasSession()) {
        sendError(socket, 503, QStringLiteral("StrmQt is not signed in to a server"));
        return;
    }

    const ItemsQuery query = itemsQueryFrom(QUrlQuery(req.url));
    QPointer<QSslSocket> safeSocket(socket);
    m_client->items(query).then(this, [this, safeSocket, query](const Result<ItemsPage> &res) {
        if (!safeSocket)
            return;
        if (!res.ok()) {
            sendError(safeSocket, 502, QStringLiteral("Could not load items: %1").arg(res.error));
            return;
        }
        sendJson(safeSocket, 200,
                 pageJson(res.value.items, res.value.totalRecordCount, query.startIndex));
    });
}

void WebRemoteServer::handleApiArtists(QSslSocket *socket, const HttpRequest &req)
{
    if (!m_client || !m_client->hasSession()) {
        sendError(socket, 503, QStringLiteral("StrmQt is not signed in to a server"));
        return;
    }

    const ItemsQuery query = itemsQueryFrom(QUrlQuery(req.url));
    QPointer<QSslSocket> safeSocket(socket);
    m_client->albumArtists(query).then(this, [this, safeSocket, query](const Result<ItemsPage> &res) {
        if (!safeSocket)
            return;
        if (!res.ok()) {
            sendError(safeSocket, 502, QStringLiteral("Could not load artists: %1").arg(res.error));
            return;
        }
        sendJson(safeSocket, 200,
                 pageJson(res.value.items, res.value.totalRecordCount, query.startIndex));
    });
}

void WebRemoteServer::handleApiItemDetails(QSslSocket *socket, const QString &itemId)
{
    if (!m_client || !m_client->hasSession()) {
        sendError(socket, 503, QStringLiteral("StrmQt is not signed in to a server"));
        return;
    }

    QPointer<QSslSocket> safeSocket(socket);
    m_client->itemDetails(itemId).then(this, [this, safeSocket](const Result<ItemDetails> &res) {
        if (!safeSocket)
            return;
        if (!res.ok()) {
            sendError(safeSocket, 404, QStringLiteral("Item not found"));
            return;
        }
        const ItemDetails &d = res.value;
        QJsonObject o = itemJson(d.item);
        o[QStringLiteral("overview")] = d.item.overview;
        o[QStringLiteral("tagline")] = d.tagline;
        if (d.criticRating > 0)
            o[QStringLiteral("criticRating")] = d.criticRating;

        QJsonArray genres;
        for (const NamedId &g : d.genreItems)
            genres.append(QJsonObject::fromVariantMap(g.toVariantMap()));
        o[QStringLiteral("genres")] = genres;

        QJsonArray studios;
        for (const NamedId &s : d.studios)
            studios.append(QJsonObject::fromVariantMap(s.toVariantMap()));
        o[QStringLiteral("studios")] = studios;

        QJsonArray people;
        for (const Person &p : d.people) {
            if (people.size() >= 24)
                break;
            people.append(QJsonObject::fromVariantMap(p.toVariantMap()));
        }
        o[QStringLiteral("people")] = people;

        QJsonArray sources;
        for (qsizetype i = 0; i < d.mediaSources.size(); ++i) {
            const MediaSource &src = d.mediaSources.at(i);
            QJsonObject s;
            s[QStringLiteral("index")] = static_cast<int>(i);
            s[QStringLiteral("name")] = src.displayName();
            s[QStringLiteral("resolution")] = src.resolutionLabel();
            s[QStringLiteral("container")] = src.container;
            s[QStringLiteral("bitrate")] = src.bitrate;
            s[QStringLiteral("size")] = src.size;
            s[QStringLiteral("isHdr")] = src.isHdr();
            s[QStringLiteral("audioCount")] = static_cast<int>(src.audioStreams().size());
            s[QStringLiteral("subtitleCount")] = static_cast<int>(src.subtitleStreams().size());
            sources.append(s);
        }
        o[QStringLiteral("mediaSources")] = sources;
        o[QStringLiteral("chapterCount")] = static_cast<int>(d.chapters.size());
        o[QStringLiteral("isContainer")] = ItemActions::isContainer(d.item.type);
        sendJson(safeSocket, 200, o);
    });
}

void WebRemoteServer::handleApiSeasons(QSslSocket *socket, const QString &seriesId)
{
    if (!m_client || !m_client->hasSession()) {
        sendError(socket, 503, QStringLiteral("StrmQt is not signed in to a server"));
        return;
    }
    QPointer<QSslSocket> safeSocket(socket);
    m_client->seasons(seriesId).then(this, [this, safeSocket](const Result<ItemsPage> &res) {
        if (!safeSocket)
            return;
        if (!res.ok()) {
            sendError(safeSocket, 502, QStringLiteral("Could not load seasons: %1").arg(res.error));
            return;
        }
        sendJson(safeSocket, 200, pageJson(res.value.items, res.value.totalRecordCount, 0));
    });
}

void WebRemoteServer::handleApiEpisodes(QSslSocket *socket, const QString &seriesId,
                                        const HttpRequest &req)
{
    if (!m_client || !m_client->hasSession()) {
        sendError(socket, 503, QStringLiteral("StrmQt is not signed in to a server"));
        return;
    }
    QString seasonId = QUrlQuery(req.url).queryItemValue(QStringLiteral("seasonId"));
    if (!seasonId.isEmpty() && !isSafeId(seasonId)) {
        sendError(socket, 400, QStringLiteral("Invalid season id"));
        return;
    }
    QPointer<QSslSocket> safeSocket(socket);
    m_client->episodes(seriesId, seasonId).then(this, [this, safeSocket](const Result<ItemsPage> &res) {
        if (!safeSocket)
            return;
        if (!res.ok()) {
            sendError(safeSocket, 502, QStringLiteral("Could not load episodes: %1").arg(res.error));
            return;
        }
        sendJson(safeSocket, 200, pageJson(res.value.items, res.value.totalRecordCount, 0));
    });
}

void WebRemoteServer::handleApiPlaylistItems(QSslSocket *socket, const QString &playlistId)
{
    if (!m_client || !m_client->hasSession()) {
        sendError(socket, 503, QStringLiteral("StrmQt is not signed in to a server"));
        return;
    }
    QPointer<QSslSocket> safeSocket(socket);
    m_client->playlistItems(playlistId, 0, 500)
        .then(this, [this, safeSocket](const Result<ItemsPage> &res) {
            if (!safeSocket)
                return;
            if (!res.ok()) {
                sendError(safeSocket, 502,
                          QStringLiteral("Could not load the playlist: %1").arg(res.error));
                return;
            }
            sendJson(safeSocket, 200, pageJson(res.value.items, res.value.totalRecordCount, 0));
        });
}

void WebRemoteServer::handleApiSearch(QSslSocket *socket, const QString &query)
{
    if (!m_client || !m_client->hasSession()) {
        sendError(socket, 503, QStringLiteral("StrmQt is not signed in to a server"));
        return;
    }
    if (query.trimmed().isEmpty()) {
        sendJson(socket, 200, pageJson({}, 0, 0));
        return;
    }

    ItemsQuery q;
    q.searchTerm = query.trimmed();
    q.limit = 80;
    q.recursive = true;
    q.includeItemTypes = {QStringLiteral("Movie"),       QStringLiteral("Series"),
                          QStringLiteral("Episode"),     QStringLiteral("MusicAlbum"),
                          QStringLiteral("MusicArtist"), QStringLiteral("Audio"),
                          QStringLiteral("BoxSet"),      QStringLiteral("Playlist"),
                          QStringLiteral("MusicVideo"),  QStringLiteral("Video")};

    QPointer<QSslSocket> safeSocket(socket);
    m_client->items(q).then(this, [this, safeSocket](const Result<ItemsPage> &res) {
        if (!safeSocket)
            return;
        if (!res.ok()) {
            sendError(safeSocket, 502, QStringLiteral("Search failed: %1").arg(res.error));
            return;
        }
        sendJson(safeSocket, 200, pageJson(res.value.items, res.value.totalRecordCount, 0));
    });
}

void WebRemoteServer::handleApiImage(QSslSocket *socket, const QString &itemId,
                                     const QString &imageType, const HttpRequest &req)
{
    if (!m_client || !m_client->hasSession()) {
        sendResponse(socket, 404, "text/plain", "No session");
        return;
    }

    // The upstream path is built here from an id and a type that each match
    // a closed form; nothing else the phone sent reaches the path.
    if (!isEmbyItemId(itemId) || !isImageType(imageType)) {
        sendResponse(socket, 400, "text/plain", "Invalid image parameters");
        return;
    }
    const QUrlQuery q(req.url);
    const int width = qBound(64, q.queryItemValue(QStringLiteral("w")).toInt() > 0
                                     ? q.queryItemValue(QStringLiteral("w")).toInt()
                                     : 400,
                             1920);
    QString tag = q.queryItemValue(QStringLiteral("tag"));
    if (!tag.isEmpty() && !isSafeId(tag))
        tag.clear();

    const QUrl imageUrl = m_client->imageUrl(itemId, imageType, width, tag);

    QNetworkRequest netReq(imageUrl);
    netReq.setRawHeader("X-Emby-Token", m_client->accessToken().toUtf8());

    QPointer<QSslSocket> safeSocket(socket);
    const bool tagged = !tag.isEmpty();
    QNetworkReply *reply = m_imageNam->get(netReq);
    connect(reply, &QNetworkReply::finished, this, [this, safeSocket, reply, tagged] {
        reply->deleteLater();
        if (!safeSocket)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            sendResponse(safeSocket, 404, "text/plain", "Image not found");
            return;
        }
        const QByteArray data = reply->readAll();
        // Only a raster image goes back, typed by its own bytes rather than
        // by whatever the upstream claimed. A login page or error body from a
        // proxy in front of Emby is not relayed, and neither is an SVG: it
        // can carry a <script> that would run on the remote's own origin,
        // where the phone keeps its token.
        const QString mime = QMimeDatabase().mimeTypeForData(data).name();
        if (!isAllowedImageMime(mime)) {
            sendResponse(safeSocket, 502, "text/plain", "Image unavailable");
            return;
        }
        QHash<QByteArray, QByteArray> headers;
        // A tag names one version of the image, so a tagged URL never changes.
        headers["Cache-Control"] = tagged ? "private, max-age=2592000, immutable"
                                          : "private, max-age=3600";
        // Opened as a page of its own, it still runs nothing.
        headers["Content-Security-Policy"] = "default-src 'none'; sandbox";
        sendResponse(safeSocket, 200, mime.toLatin1(), data, headers);
    });
}

// ── Verbs ────────────────────────────────────────────────────────────────────

void WebRemoteServer::handleApiPlay(QSslSocket *socket, const QJsonObject &body)
{
    if (!m_actions) {
        sendError(socket, 503, QStringLiteral("Playback is not available"));
        return;
    }

    const QString mode = body.value(QLatin1String("mode")).toString(QStringLiteral("play"));

    // Rows the phone already holds, started at one of them: an episode list,
    // an album's tracks, a playlist.
    if (mode == QLatin1String("list")) {
        QVariantList items;
        for (const QJsonValue &v : body.value(QLatin1String("items")).toArray()) {
            const QJsonObject o = v.toObject();
            if (isSafeId(o.value(QLatin1String("id")).toString()))
                items.append(itemMapFromClient(o));
        }
        if (items.isEmpty()) {
            sendError(socket, 400, QStringLiteral("Nothing to play"));
            return;
        }
        const int start = qBound(0, body.value(QLatin1String("startIndex")).toInt(),
                                 static_cast<int>(items.size()) - 1);
        m_actions->playAllFrom(items, start);
        sendOk(socket);
        return;
    }

    // A library, genre or search narrowed on the phone, shuffled as a whole.
    if (mode == QLatin1String("shuffleQuery")) {
        ItemsQuery query = itemsQueryFrom(body.value(QLatin1String("query")).toObject());
        query.startIndex = 0;
        m_actions->shuffleFiltered(query);
        sendOk(socket);
        return;
    }

    const QString itemId = body.value(QLatin1String("itemId")).toString();
    if (!isSafeId(itemId)) {
        sendError(socket, 400, QStringLiteral("Missing item id"));
        return;
    }
    const QString collectionType = body.value(QLatin1String("collectionType")).toString();

    if (mode == QLatin1String("playAll")) {
        m_actions->playAll(itemId, collectionType);
        sendOk(socket);
        return;
    }
    if (mode == QLatin1String("shuffle") && body.value(QLatin1String("type")).toString()
                                                == QLatin1String("Series")) {
        m_actions->shuffleSeries(itemId);
        sendOk(socket);
        return;
    }
    if (mode == QLatin1String("shuffle")) {
        m_actions->shuffle(itemId, collectionType);
        sendOk(socket);
        return;
    }

    if (!m_client || !m_client->hasSession()) {
        sendError(socket, 503, QStringLiteral("StrmQt is not signed in to a server"));
        return;
    }

    // Every other verb takes the whole item — its type decides whether "play"
    // means the item or its contents, and its position decides "resume".
    QPointer<QSslSocket> safeSocket(socket);
    m_client->itemDetails(itemId).then(this, [this, safeSocket, mode](const Result<ItemDetails> &res) {
        if (!safeSocket)
            return;
        if (!res.ok()) {
            sendError(safeSocket, 404, QStringLiteral("Item not found"));
            return;
        }
        const QVariantMap map = itemMap(res.value.item);
        if (mode == QLatin1String("next"))
            m_actions->playNext(map);
        else if (mode == QLatin1String("queue"))
            m_actions->addToQueue(map);
        else if (mode == QLatin1String("resume"))
            m_actions->resume(map);
        else if (mode == QLatin1String("fromStart"))
            m_actions->playFromStart(map);
        else if (mode == QLatin1String("instantMix"))
            m_actions->instantMix(map);
        else
            m_actions->play(map);
        sendOk(safeSocket);
    });
}

void WebRemoteServer::handleApiUserData(QSslSocket *socket, const QString &itemId,
                                        const QString &field, const QJsonObject &body)
{
    if (!m_actions) {
        sendError(socket, 503, QStringLiteral("Not available"));
        return;
    }
    const bool value = body.value(QLatin1String("value")).toBool();
    if (field == QLatin1String("favorite"))
        m_actions->setFavorite(itemId, value);
    else
        m_actions->setPlayed(itemId, value);
    sendOk(socket);
}

void WebRemoteServer::handleApiPlayback(QSslSocket *socket, const QJsonObject &body)
{
    if (!m_player) {
        sendError(socket, 503, QStringLiteral("Playback is not available"));
        return;
    }

    const QString action = body.value(QLatin1String("action")).toString();
    const QJsonValue val = body.value(QLatin1String("value"));

    if (action == QLatin1String("togglePause")) {
        m_player->togglePause();
    } else if (action == QLatin1String("play")) {
        m_player->setPaused(false);
    } else if (action == QLatin1String("pause")) {
        m_player->setPaused(true);
    } else if (action == QLatin1String("stop")) {
        m_player->stop();
    } else if (action == QLatin1String("next")) {
        m_player->playNext();
    } else if (action == QLatin1String("previous")) {
        m_player->playPrevious();
    } else if (action == QLatin1String("seekTo")) {
        m_player->seekTo(qMax<qint64>(0, val.toVariant().toLongLong()));
    } else if (action == QLatin1String("seekRelative")) {
        m_player->seekRelative(val.toVariant().toLongLong());
    } else if (action == QLatin1String("jumpQueue")) {
        if (m_player->queue())
            m_player->queue()->jumpTo(val.toInt());
    } else if (action == QLatin1String("setSpeed")) {
        const double speed = val.toDouble();
        if (speed < 0.25 || speed > 4.0) {
            sendError(socket, 400, QStringLiteral("Speed must be between 0.25 and 4"));
            return;
        }
        m_player->setPlaybackSpeed(speed);
    } else if (action == QLatin1String("setAudioDelay")) {
        m_player->setAudioDelayMs(qBound(-10000, val.toInt(), 10000));
    } else if (action == QLatin1String("setSubtitleDelay")) {
        m_player->setSubtitleDelayMs(qBound(-60000, val.toInt(), 60000));
    } else if (action == QLatin1String("nextChapter")) {
        m_player->nextChapter();
    } else if (action == QLatin1String("previousChapter")) {
        m_player->previousChapter();
    } else if (action == QLatin1String("seekChapter")) {
        m_player->seekToChapter(val.toInt());
    } else if (action == QLatin1String("setShuffle")) {
        if (m_player->queue())
            m_player->queue()->setShuffled(val.toBool());
    } else if (action == QLatin1String("setRepeat")) {
        if (PlayQueue *queue = m_player->queue()) {
            const QString mode = val.toString();
            queue->setRepeatMode(mode == QLatin1String("all")   ? PlayQueue::RepeatAll
                                 : mode == QLatin1String("one") ? PlayQueue::RepeatOne
                                                                : PlayQueue::RepeatOff);
        }
    } else if (action == QLatin1String("cancelUpNext")) {
        m_player->cancelUpNext();
    } else if (action == QLatin1String("setSource")) {
        m_player->setPreferredSource(val.toInt());
    } else if (action == QLatin1String("reloadStream")) {
        m_player->reloadStream();
    } else if (action == QLatin1String("frameStep")) {
        m_player->frameStep(val.toInt() < 0 ? -1 : 1);
    } else if (action == QLatin1String("screenshot")) {
        const QString path = m_player->takeScreenshot();
        QJsonObject resp;
        resp[QStringLiteral("ok")] = !path.isEmpty();
        resp[QStringLiteral("path")] = path;
        sendJson(socket, path.isEmpty() ? 500 : 200, resp);
        return;
    } else {
        sendError(socket, 400, QStringLiteral("Unknown playback action"));
        return;
    }

    sendOk(socket);
}

void WebRemoteServer::handleApiQueueAction(QSslSocket *socket, const QJsonObject &body)
{
    PlayQueue *queue = m_player ? m_player->queue() : nullptr;
    if (!queue) {
        sendError(socket, 503, QStringLiteral("Playback is not available"));
        return;
    }
    const QString action = body.value(QLatin1String("action")).toString();
    const int index = body.value(QLatin1String("index")).toInt(-1);
    const int count = queue->rowCount();
    const bool validIndex = index >= 0 && index < count;

    if (action == QLatin1String("jump") && validIndex) {
        queue->jumpTo(index);
    } else if (action == QLatin1String("remove") && validIndex) {
        queue->removeAt(index);
    } else if (action == QLatin1String("move") && validIndex) {
        const int to = body.value(QLatin1String("to")).toInt(-1);
        if (to < 0 || to >= count) {
            sendError(socket, 400, QStringLiteral("Invalid destination"));
            return;
        }
        queue->moveItem(index, to);
    } else if (action == QLatin1String("clear")) {
        queue->clear();
    } else {
        sendError(socket, 400, QStringLiteral("Unknown queue action or index"));
        return;
    }
    sendOk(socket);
}

void WebRemoteServer::handleApiVolume(QSslSocket *socket, const QJsonObject &body)
{
    if (!m_player) {
        sendError(socket, 503, QStringLiteral("Playback is not available"));
        return;
    }

    const QString action = body.value(QLatin1String("action")).toString();
    const int val = body.value(QLatin1String("value")).toInt();

    if (action == QLatin1String("set")) {
        m_player->setVolume(val);
    } else if (action == QLatin1String("up")) {
        m_player->adjustVolume(5);
    } else if (action == QLatin1String("down")) {
        m_player->adjustVolume(-5);
    } else if (action == QLatin1String("mute")) {
        m_player->setMuted(true);
    } else if (action == QLatin1String("unmute")) {
        m_player->setMuted(false);
    } else if (action == QLatin1String("toggleMute")) {
        m_player->toggleMute();
    } else {
        sendError(socket, 400, QStringLiteral("Unknown volume action"));
        return;
    }

    sendOk(socket);
}

void WebRemoteServer::handleApiStream(QSslSocket *socket, const QJsonObject &body)
{
    if (!m_player) {
        sendError(socket, 503, QStringLiteral("Playback is not available"));
        return;
    }

    // Engine track ids, as listed in status.playback.audioTracks/subtitleTracks.
    const QString type = body.value(QLatin1String("type")).toString();
    const int trackId = body.value(QLatin1String("trackId")).toInt(-1);

    if (type == QLatin1String("audio") && trackId >= 0) {
        m_player->setAudioTrack(trackId);
    } else if (type == QLatin1String("subtitle")) {
        m_player->setSubtitleTrack(trackId); // -1 turns subtitles off
    } else {
        sendError(socket, 400, QStringLiteral("Unknown track"));
        return;
    }

    sendOk(socket);
}

void WebRemoteServer::handleApiQuality(QSslSocket *socket, const QJsonObject &body)
{
    if (!m_settings) {
        sendError(socket, 503, QStringLiteral("Settings are not available"));
        return;
    }
    if (body.contains(QLatin1String("maxBitrateKbps"))) {
        const int kbps = body.value(QLatin1String("maxBitrateKbps")).toInt(-1);
        if (kbps < 0) {
            sendError(socket, 400, QStringLiteral("Invalid bitrate"));
            return;
        }
        m_settings->setMaxBitrateKbps(kbps);
    }
    if (body.contains(QLatin1String("playbackMode"))) {
        const QString mode = body.value(QLatin1String("playbackMode")).toString();
        static const QStringList modes{QStringLiteral("auto"), QStringLiteral("directPlay"),
                                       QStringLiteral("transcode")};
        if (!modes.contains(mode)) {
            sendError(socket, 400, QStringLiteral("Invalid playback mode"));
            return;
        }
        m_settings->setPlaybackMode(mode);
    }
    // Application pushes the new preferences to the client on the settings
    // signals above, synchronously, so a reload here already asks with them.
    if (body.value(QLatin1String("apply")).toBool() && m_player)
        m_player->reloadStream();
    sendOk(socket);
}

void WebRemoteServer::handleApiSubtitleStyle(QSslSocket *socket, const QJsonObject &body)
{
    if (!m_settings) {
        sendError(socket, 503, QStringLiteral("Settings are not available"));
        return;
    }
    if (body.contains(QLatin1String("scale")))
        m_settings->setSubtitleScale(body.value(QLatin1String("scale")).toInt());
    if (body.contains(QLatin1String("position")))
        m_settings->setSubtitlePosition(body.value(QLatin1String("position")).toInt());
    if (body.contains(QLatin1String("background")))
        m_settings->setSubtitleBackground(body.value(QLatin1String("background")).toInt());
    if (body.contains(QLatin1String("color"))) {
        static const QRegularExpression hex(
            QRegularExpression::anchoredPattern(QStringLiteral("#[0-9A-Fa-f]{6}")));
        const QString color = body.value(QLatin1String("color")).toString();
        if (!hex.match(color).hasMatch()) {
            sendError(socket, 400, QStringLiteral("Invalid colour"));
            return;
        }
        m_settings->setSubtitleColor(color);
    }
    sendOk(socket);
}

void WebRemoteServer::handleApiNavigate(QSslSocket *socket, const QJsonObject &body)
{
    const QString dest = body.value(QLatin1String("destination")).toString();
    const QString key = body.value(QLatin1String("key")).toString();

    static const QStringList destinations{QStringLiteral("home"), QStringLiteral("search"),
                                          QStringLiteral("settings"), QStringLiteral("back"),
                                          QStringLiteral("osd")};
    if (!dest.isEmpty() && destinations.contains(dest)) {
        if (dest == QLatin1String("osd"))
            emit actionRequested(QStringLiteral("player.toggleOsd"));
        else
            emit navigationRequested(dest);
    } else if (!key.isEmpty() && navigationKeys().contains(key)) {
        emit actionRequested(actionForNavigationKey(key));
    } else {
        sendError(socket, 400, QStringLiteral("Unknown destination or key"));
        return;
    }

    sendOk(socket);
}

// ── Events ───────────────────────────────────────────────────────────────────

void WebRemoteServer::handleApiEvents(QSslSocket *socket, const HttpRequest &req)
{
    // A stream is long-lived and mostly silent by design: neither the request
    // nor the idle timeout may close it. The keep-alive comment and the
    // backlog cap in writeSse take their place.
    exemptFromIdleTimeout(socket);
    socket->setProperty(kEventStreamProperty, true);

    const QByteArray sseHeaders =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream; charset=utf-8\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "X-Accel-Buffering: no\r\n"
        "X-Content-Type-Options: nosniff\r\n\r\n";

    socket->write(sseHeaders);
    // Reconnect quickly after a phone wakes.
    socket->write("retry: 2000\n\n");

    // One stream per signed-in phone: a phone that reconnects (a reload, a
    // wake from sleep) often leaves its old stream half-open, and it would
    // count as a second phone until the backlog cap finally dropped it.
    QString token;
    const QByteArray auth = req.headers.value("authorization");
    if (auth.startsWith("Bearer "))
        token = QString::fromUtf8(auth.mid(7)).trimmed();
    if (!m_authorizedTokens.contains(token))
        token = QUrlQuery(req.url).queryItemValue(QStringLiteral("token"));
    if (!m_authorizedTokens.contains(token))
        token.clear();
    const qsizetype streamsBefore = m_sseClients.size();
    QList<QPointer<QSslSocket>> replaced;
    if (!token.isEmpty()) {
        socket->setProperty(kEventTokenProperty, token);
        m_sseClients.removeIf([&](const QPointer<QSslSocket> &other) {
            if (!other || other == socket
                || other->property(kEventTokenProperty).toString() != token)
                return false;
            replaced.append(other);
            return true;
        });
    }
    // Already forgotten, so the disconnected handler has nothing to report.
    for (const QPointer<QSslSocket> &old : std::as_const(replaced)) {
        if (old)
            old->abort();
    }
    if (!m_sseClients.contains(socket))
        m_sseClients.append(socket);
    if (m_sseClients.size() != streamsBefore)
        emit connectedClientsChanged(connectedClientsCount());

    // Through writeSse like every later event: the preamble goes onto a
    // drained connection, so the cap only bites on a phone already backlogged.
    const QByteArray statusData = QJsonDocument(currentStatusJson()).toJson(QJsonDocument::Compact);
    const QByteArray queueData = QJsonDocument(currentQueueJson()).toJson(QJsonDocument::Compact);
    if (!writeSse(socket, "event: status\ndata: " + statusData + "\n\n")
        || !writeSse(socket, "event: queue\ndata: " + queueData + "\n\n"))
        return; // aborted: the disconnected handler has already forgotten it
    socket->flush();
}

bool WebRemoteServer::writeSse(QSslSocket *socket, const QByteArray &chunk)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return false;
    // The same rule as sendResponse: one chunk onto a drained connection is
    // always let through; a phone already backlogged past the pause threshold
    // that would now hold more than the hard cap has stopped reading.
    const qint64 backlogBefore = writeBacklog(socket);
    if (socket->write(chunk) == -1
        || (backlogBefore > kWritePauseBytes && writeBacklog(socket) > kWriteAbortBytes)) {
        qCWarning(logApp) << "webremote: event stream is not reading; closing it";
        socket->abort();
        return false;
    }
    return true;
}

void WebRemoteServer::writeToAllStreams(const QByteArray &chunk)
{
    // A copy: abort() emits disconnected synchronously, and its handler
    // removes the socket from m_sseClients.
    const QList<QPointer<QSslSocket>> streams = m_sseClients;
    for (const QPointer<QSslSocket> &socket : streams)
        writeSse(socket, chunk);
    // Whatever the disconnected handler did not already remove.
    const qsizetype removed = m_sseClients.removeIf([](const QPointer<QSslSocket> &socket) {
        return !socket || socket->state() != QAbstractSocket::ConnectedState;
    });
    if (removed > 0)
        emit connectedClientsChanged(connectedClientsCount());
}

void WebRemoteServer::broadcastSse(const QString &eventName, const QByteArray &data)
{
    writeToAllStreams("event: " + eventName.toUtf8() + "\ndata: " + data + "\n\n");
}

void WebRemoteServer::scheduleStatus()
{
    if (!m_sseClients.isEmpty() && !m_statusCoalesce.isActive())
        m_statusCoalesce.start();
}

void WebRemoteServer::broadcastPlayerStatus()
{
    if (m_sseClients.isEmpty())
        return;
    const QByteArray data = QJsonDocument(currentStatusJson()).toJson(QJsonDocument::Compact);
    broadcastSse(QStringLiteral("status"), data);
}

void WebRemoteServer::broadcastQueue()
{
    if (m_sseClients.isEmpty())
        return;
    const QByteArray data = QJsonDocument(currentQueueJson()).toJson(QJsonDocument::Compact);
    broadcastSse(QStringLiteral("queue"), data);
}

// ── Responses ────────────────────────────────────────────────────────────────

void WebRemoteServer::sendResponse(QSslSocket *socket, int statusCode, const QByteArray &contentType,
                                   const QByteArray &body, const QHash<QByteArray, QByteArray> &extraHeaders)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;

    QByteArray statusText = "OK";
    switch (statusCode) {
    case 204: statusText = "No Content"; break;
    case 400: statusText = "Bad Request"; break;
    case 401: statusText = "Unauthorized"; break;
    case 403: statusText = "Forbidden"; break;
    case 404: statusText = "Not Found"; break;
    case 413: statusText = "Payload Too Large"; break;
    case 415: statusText = "Unsupported Media Type"; break;
    case 429: statusText = "Too Many Requests"; break;
    case 431: statusText = "Request Header Fields Too Large"; break;
    case 500: statusText = "Internal Server Error"; break;
    case 501: statusText = "Not Implemented"; break;
    case 502: statusText = "Bad Gateway"; break;
    case 503: statusText = "Service Unavailable"; break;
    default: break;
    }

    // Keep-alive: a library grid is dozens of image requests, and a fresh TLS
    // handshake for each one is most of what made the old page feel slow.
    QByteArray res = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n" +
                     "Content-Type: " + contentType + "\r\n" +
                     "Content-Length: " + QByteArray::number(body.size()) + "\r\n" +
                     "X-Content-Type-Options: nosniff\r\n";

    for (auto it = extraHeaders.cbegin(); it != extraHeaders.cend(); ++it) {
        res += it.key() + ": " + it.value() + "\r\n";
    }
    res += "\r\n" + body;

    const qint64 backlogBefore = writeBacklog(socket);
    socket->write(res);
    // Parsing already pauses at kWritePauseBytes, so only responses that
    // arrive later (async handlers) can pile up. One response onto a drained
    // connection is always let through, however large.
    if (backlogBefore > kWritePauseBytes && writeBacklog(socket) > kWriteAbortBytes) {
        qCWarning(logApp) << "webremote: client is not reading; closing the connection";
        socket->abort();
        return;
    }
    if (!requestPending(socket))
        restartTimeout(socket, false);
}

void WebRemoteServer::sendJson(QSslSocket *socket, int statusCode, const QJsonObject &json)
{
    const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
    sendResponse(socket, statusCode, "application/json", body, {{"Cache-Control", "no-store"}});
}

void WebRemoteServer::sendJsonArray(QSslSocket *socket, int statusCode, const QJsonArray &json)
{
    const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
    sendResponse(socket, statusCode, "application/json", body, {{"Cache-Control", "no-store"}});
}

void WebRemoteServer::sendOk(QSslSocket *socket)
{
    QJsonObject resp;
    resp[QStringLiteral("ok")] = true;
    sendJson(socket, 200, resp);
}

void WebRemoteServer::sendError(QSslSocket *socket, int statusCode, const QString &message)
{
    QJsonObject err;
    err[QStringLiteral("error")] = message;
    sendJson(socket, statusCode, err);
}

} // namespace strmqt
