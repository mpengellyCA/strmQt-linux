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
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSslServer>
#include <QSslSocket>
#include <QUrlQuery>
#include <QUuid>

#include <memory>

namespace strmqt {

namespace {

// A request line plus headers larger than this is not a browser talking to us.
constexpr qsizetype kMaxHeaderBytes = 64 * 1024;
// Every JSON body the remote sends is a handful of fields.
constexpr qsizetype kMaxBodyBytes = 1024 * 1024;

constexpr int kStatusCoalesceMs = 120;
constexpr int kStatusTickMs = 5000;
constexpr int kKeepAliveMs = 20000;

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

bool isSafeId(const QString &value)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
    return re.match(value).hasMatch();
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
        for (const auto &socket : std::as_const(m_sseClients)) {
            if (socket && socket->state() == QAbstractSocket::ConnectedState)
                socket->write(": keep-alive\n\n");
        }
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

    // Ensure self-signed certificate exists with SANs
    const QStringList sanAddresses = NetworkAddressHelper::allHostAddressesForSan();
    if (!TlsCertificateGenerator::ensureCertificate(sanAddresses)) {
        qCWarning(logApp) << "webremote: failed to ensure TLS certificate";
        return false;
    }

    QSslCertificate cert;
    QSslKey key;
    if (!TlsCertificateGenerator::load(cert, key)) {
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
    const QHostAddress bindAddr = NetworkAddressHelper::resolveBindAddress(mode);

    if (!m_server->listen(bindAddr, static_cast<quint16>(port))) {
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
    m_statusTick.stop();
    m_keepAlive.stop();
    m_statusCoalesce.stop();
    for (auto &socket : m_sseClients) {
        if (socket) {
            socket->disconnectFromHost();
        }
    }
    m_sseClients.clear();
    emit connectedClientsChanged(0);

    if (m_server && m_server->isListening()) {
        m_server->close();
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
    connect(socket, &QSslSocket::encrypted, this, [this, socket] {
        // Dequeue from QSslServer/QTcpServer internal pending queue to prevent queue buildup
        m_server->nextPendingConnection();

        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QSslSocket::readyRead, this, [this, socket, buffer] {
            handleReadyRead(socket, *buffer);
        });
        connect(socket, &QAbstractSocket::disconnected, this, &WebRemoteServer::onClientDisconnected);
        if (socket->bytesAvailable() > 0) {
            handleReadyRead(socket, *buffer);
        }
    });
    connect(socket, &QAbstractSocket::disconnected, socket, &QObject::deleteLater);
}

void WebRemoteServer::onClientDisconnected()
{
    auto *socket = qobject_cast<QSslSocket *>(sender());
    if (!socket)
        return;

    if (m_sseClients.removeAll(socket) > 0)
        emit connectedClientsChanged(m_sseClients.size());
}

void WebRemoteServer::handleReadyRead(QSslSocket *socket, QByteArray &buffer)
{
    buffer.append(socket->readAll());

    while (!buffer.isEmpty()) {
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            if (buffer.size() > kMaxHeaderBytes) {
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
            if (key == "content-length")
                contentLength = qMax<qsizetype>(0, val.toLongLong());
        }

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

        dispatchRequest(socket, req);
    }
}

void WebRemoteServer::dispatchRequest(QSslSocket *socket, const HttpRequest &req)
{
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
            handleApiEvents(socket);
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
        static const QRegularExpression safe(
            QStringLiteral("^/([a-z0-9][a-z0-9-]*\\.(?:js|css|svg|json|png))$"));
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
    // EventSource and <img> cannot set headers.
    const QString queryToken = QUrlQuery(req.url).queryItemValue(QStringLiteral("token"));
    if (!queryToken.isEmpty() && m_authorizedTokens.contains(queryToken))
        return true;

    return false;
}

void WebRemoteServer::handleApiAuthPin(QSslSocket *socket, const QJsonObject &body)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_failedPinAttempts >= 5 && (now - m_lastFailedPinTimeMs) < 2000) {
        QJsonObject err;
        err[QStringLiteral("error")] = QStringLiteral("Too many failed attempts. Please wait.");
        sendJson(socket, 429, err);
        return;
    }

    const QString pin = body.value(QLatin1String("pin")).toString();
    if (m_settings && !pin.isEmpty() && pin == m_settings->webRemotePin()) {
        m_failedPinAttempts = 0;
        const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_authorizedTokens.insert(token);
        QJsonObject resp;
        resp[QStringLiteral("token")] = token;
        sendJson(socket, 200, resp);
    } else {
        m_failedPinAttempts++;
        m_lastFailedPinTimeMs = now;
        QJsonObject err;
        err[QStringLiteral("error")] = QStringLiteral("Invalid PIN");
        sendJson(socket, 403, err);
    }
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

    static const QRegularExpression typeRegex(QStringLiteral("^[A-Za-z]{1,24}$"));
    if (!isSafeId(itemId) || !typeRegex.match(imageType).hasMatch()) {
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
        const QByteArray cType = reply->rawHeader("Content-Type");
        QHash<QByteArray, QByteArray> headers;
        // A tag names one version of the image, so a tagged URL never changes.
        headers["Cache-Control"] = tagged ? "private, max-age=2592000, immutable"
                                          : "private, max-age=3600";
        sendResponse(safeSocket, 200, cType.isEmpty() ? "image/jpeg" : cType, data, headers);
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
        static const QRegularExpression hex(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
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
            emit osdToggleRequested();
        else
            emit navigationRequested(dest);
    } else if (!key.isEmpty() && navigationKeys().contains(key)) {
        emit keyNavigationRequested(key);
    } else {
        sendError(socket, 400, QStringLiteral("Unknown destination or key"));
        return;
    }

    sendOk(socket);
}

// ── Events ───────────────────────────────────────────────────────────────────

void WebRemoteServer::handleApiEvents(QSslSocket *socket)
{
    const QByteArray sseHeaders =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream; charset=utf-8\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "X-Accel-Buffering: no\r\n\r\n";

    socket->write(sseHeaders);
    // Reconnect quickly after a phone wakes.
    socket->write("retry: 2000\n\n");

    if (!m_sseClients.contains(socket)) {
        m_sseClients.append(socket);
        emit connectedClientsChanged(m_sseClients.size());
    }

    const QByteArray statusData = QJsonDocument(currentStatusJson()).toJson(QJsonDocument::Compact);
    socket->write("event: status\ndata: " + statusData + "\n\n");

    const QByteArray queueData = QJsonDocument(currentQueueJson()).toJson(QJsonDocument::Compact);
    socket->write("event: queue\ndata: " + queueData + "\n\n");
    socket->flush();
}

void WebRemoteServer::broadcastSse(const QString &eventName, const QByteArray &data)
{
    const QByteArray msg = "event: " + eventName.toUtf8() + "\ndata: " + data + "\n\n";

    bool changed = false;
    auto it = m_sseClients.begin();
    while (it != m_sseClients.end()) {
        QSslSocket *sock = *it;
        if (!sock || sock->state() != QAbstractSocket::ConnectedState || sock->write(msg) == -1) {
            it = m_sseClients.erase(it);
            changed = true;
            continue;
        }
        ++it;
    }
    if (changed)
        emit connectedClientsChanged(m_sseClients.size());
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

    socket->write(res);
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
