#include "WebRemoteServer.h"

#include "NetworkAddressHelper.h"
#include "TlsCertificateGenerator.h"
#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/HomeController.h"
#include "app/controllers/PlayerController.h"
#include "app/controllers/SessionController.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "playback/PlayerBackend.h"
#include "server/dto/ItemsQuery.h"
#include "server/emby/EmbyClient.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSslServer>
#include <QSslSocket>
#include <QUrlQuery>
#include <QUuid>

namespace strmqt {

namespace {

QByteArray mimeTypeForPath(const QString &path)
{
    if (path.endsWith(QLatin1String(".html"))) return "text/html; charset=utf-8";
    if (path.endsWith(QLatin1String(".css"))) return "text/css; charset=utf-8";
    if (path.endsWith(QLatin1String(".js"))) return "application/javascript; charset=utf-8";
    if (path.endsWith(QLatin1String(".json"))) return "application/json";
    if (path.endsWith(QLatin1String(".svg"))) return "image/svg+xml";
    if (path.endsWith(QLatin1String(".png"))) return "image/png";
    if (path.endsWith(QLatin1String(".jpg")) || path.endsWith(QLatin1String(".jpeg"))) return "image/jpeg";
    if (path.endsWith(QLatin1String(".ttf"))) return "font/ttf";
    return "application/octet-stream";
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

    // Player signal bindings for live status push
    if (m_player) {
        connect(m_player, &PlayerController::activeChanged, this, &WebRemoteServer::broadcastPlayerStatus);
        connect(m_player, &PlayerController::pausedChanged, this, &WebRemoteServer::broadcastPlayerStatus);
        connect(m_player, &PlayerController::titleChanged, this, &WebRemoteServer::broadcastPlayerStatus);
        connect(m_player, &PlayerController::volumeChanged, this, &WebRemoteServer::broadcastPlayerStatus);
        connect(m_player, &PlayerController::mutedChanged, this, &WebRemoteServer::broadcastPlayerStatus);
        connect(m_player, &PlayerController::sourcesChanged, this, &WebRemoteServer::broadcastPlayerStatus);
        connect(m_player, &PlayerController::queueStateChanged, this, [this] {
            broadcastPlayerStatus();
            broadcastQueue();
        });
        if (m_player->queue()) {
            connect(m_player->queue(), &PlayQueue::queueChanged, this, &WebRemoteServer::broadcastQueue);
            connect(m_player->queue(), &PlayQueue::currentChanged, this, &WebRemoteServer::broadcastQueue);
        }
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

    qCInfo(logApp) << "webremote: listening on" << bindAddr.toString() << port;
    emit runningChanged(true);
    return true;
}

void WebRemoteServer::stop()
{
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

void WebRemoteServer::onStartedEncryptionHandshake(QSslSocket *socket)
{
    qCInfo(logApp) << "webremote: startedEncryptionHandshake" << socket;
    connect(socket, &QSslSocket::encrypted, this, [this, socket] {
        qCInfo(logApp) << "webremote: socket encrypted:" << socket;
        // Dequeue from QSslServer/QTcpServer internal pending queue to prevent queue buildup
        m_server->nextPendingConnection();

        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QSslSocket::readyRead, this, [this, socket, buffer] {
            handleReadyRead(socket, *buffer);
        });
        connect(socket, &QAbstractSocket::disconnected, this, &WebRemoteServer::onClientDisconnected);
        connect(socket, &QAbstractSocket::disconnected, socket, &QObject::deleteLater);
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

    qCInfo(logApp) << "webremote: client disconnected:" << socket;
    m_sseClients.removeAll(socket);
    emit connectedClientsChanged(m_sseClients.size());
}

void WebRemoteServer::handleReadyRead(QSslSocket *socket, QByteArray &buffer)
{
    const QByteArray incoming = socket->readAll();
    qCInfo(logApp) << "webremote: handleReadyRead incoming bytes:" << incoming.size();
    buffer.append(incoming);

    while (!buffer.isEmpty()) {
        const int headerEnd = static_cast<int>(buffer.indexOf("\r\n\r\n"));
        if (headerEnd < 0)
            return; // Headers incomplete

        const QByteArray headerBlock = buffer.left(headerEnd);
        const QList<QByteArray> lines = headerBlock.split('\r');

        HttpRequest req;
        qsizetype contentLength = 0;

        const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
        req.method = QString::fromLatin1(requestLine.value(0)).toUpper();
        req.url = QUrl(QString::fromLatin1(requestLine.value(1)), QUrl::TolerantMode);
        req.path = req.url.path();

        for (qsizetype i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines[i].trimmed();
            const int colon = static_cast<int>(line.indexOf(':'));
            if (colon <= 0)
                continue;
            const QByteArray key = line.left(colon).toLower();
            const QByteArray val = line.mid(colon + 1).trimmed();
            req.headers.insert(key, val);
            if (key == "content-length")
                contentLength = val.toLongLong();
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
    qCInfo(logApp) << "webremote: dispatchRequest" << req.method << req.path;

    // Handle CORS preflight
    if (req.method == QLatin1String("OPTIONS")) {
        sendResponse(socket, 204, "text/plain", "");
        return;
    }

    // Static Assets
    if (req.method == QLatin1String("GET")) {
        if (req.path == QLatin1String("/") || req.path == QLatin1String("/index.html")) {
            handleStaticFile(socket, QStringLiteral(":/webremote/index.html"));
            return;
        }
        if (req.path == QLatin1String("/app.js")) {
            handleStaticFile(socket, QStringLiteral(":/webremote/app.js"));
            return;
        }
        if (req.path == QLatin1String("/style.css")) {
            handleStaticFile(socket, QStringLiteral(":/webremote/style.css"));
            return;
        }
        if (req.path == QLatin1String("/manifest.json")) {
            handleStaticFile(socket, QStringLiteral(":/webremote/manifest.json"));
            return;
        }
        if (req.path == QLatin1String("/icon.svg")) {
            handleStaticFile(socket, QStringLiteral(":/webremote/icon.svg"));
            return;
        }
    }

    // PIN Authentication Check
    if (req.path == QLatin1String("/api/auth/pin")) {
        if (req.method == QLatin1String("GET")) {
            QJsonObject res;
            res[QStringLiteral("required")] = m_settings ? m_settings->webRemoteRequirePin() : false;
            sendJson(socket, 200, res);
            return;
        }
        if (req.method == QLatin1String("POST")) {
            const QJsonObject body = QJsonDocument::fromJson(req.body).object();
            handleApiAuthPin(socket, body);
            return;
        }
    }

    if (m_settings && m_settings->webRemoteRequirePin()) {
        if (req.path.startsWith(QLatin1String("/api/")) && !isAuthorized(req)) {
            QJsonObject err;
            err[QStringLiteral("error")] = QStringLiteral("Unauthorized");
            err[QStringLiteral("requirePin")] = true;
            sendJson(socket, 401, err);
            return;
        }
    }

    // API Routes
    if (req.path == QLatin1String("/api/status")) {
        handleApiStatus(socket);
        return;
    }
    if (req.path == QLatin1String("/api/queue")) {
        handleApiQueue(socket);
        return;
    }
    if (req.path == QLatin1String("/api/home")) {
        handleApiHome(socket);
        return;
    }
    if (req.path == QLatin1String("/api/libraries")) {
        handleApiLibraries(socket);
        return;
    }
    if (req.path.startsWith(QLatin1String("/api/library/")) && req.path.endsWith(QLatin1String("/items"))) {
        handleApiLibraryItems(socket, req);
        return;
    }
    if (req.path.startsWith(QLatin1String("/api/item/"))) {
        const QString itemId = req.path.mid(10);
        handleApiItemDetails(socket, itemId);
        return;
    }
    if (req.path == QLatin1String("/api/search")) {
        const QString q = QUrlQuery(req.url).queryItemValue(QStringLiteral("q"));
        handleApiSearch(socket, q);
        return;
    }
    if (req.path.startsWith(QLatin1String("/api/image/"))) {
        const QStringList parts = req.path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        // /api/image/{id}/{type}
        if (parts.size() >= 4) {
            handleApiImage(socket, parts.at(2), parts.at(3));
            return;
        }
    }
    if (req.path == QLatin1String("/api/play") && req.method == QLatin1String("POST")) {
        handleApiPlay(socket, QJsonDocument::fromJson(req.body).object());
        return;
    }
    if (req.path == QLatin1String("/api/playback") && req.method == QLatin1String("POST")) {
        handleApiPlayback(socket, QJsonDocument::fromJson(req.body).object());
        return;
    }
    if (req.path == QLatin1String("/api/volume") && req.method == QLatin1String("POST")) {
        handleApiVolume(socket, QJsonDocument::fromJson(req.body).object());
        return;
    }
    if (req.path == QLatin1String("/api/stream") && req.method == QLatin1String("POST")) {
        handleApiStream(socket, QJsonDocument::fromJson(req.body).object());
        return;
    }
    if (req.path == QLatin1String("/api/navigate") && req.method == QLatin1String("POST")) {
        handleApiNavigate(socket, QJsonDocument::fromJson(req.body).object());
        return;
    }
    if (req.path == QLatin1String("/api/events") && req.method == QLatin1String("GET")) {
        handleApiEvents(socket);
        return;
    }

    sendResponse(socket, 404, "text/plain", "Not Found");
}

void WebRemoteServer::handleStaticFile(QSslSocket *socket, const QString &resPath)
{
    QFile file(resPath);
    if (!file.open(QIODevice::ReadOnly)) {
        sendResponse(socket, 404, "text/plain", "File not found");
        return;
    }
    const QByteArray data = file.readAll();
    file.close();

    QHash<QByteArray, QByteArray> headers;
    headers["Cache-Control"] = "public, max-age=3600";
    sendResponse(socket, 200, mimeTypeForPath(resPath), data, headers);
}

bool WebRemoteServer::isAuthorized(const HttpRequest &req) const
{
    const QByteArray auth = req.headers.value("authorization");
    if (auth.startsWith("Bearer ")) {
        const QString token = QString::fromUtf8(auth.mid(7)).trimmed();
        if (m_authorizedTokens.contains(token))
            return true;
    }
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
    if (m_settings && pin == m_settings->webRemotePin()) {
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

QJsonObject WebRemoteServer::currentStatusJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("version")] = QStringLiteral(STRMQT_VERSION);

    QJsonObject playback;
    if (!m_player) {
        playback[QStringLiteral("active")] = false;
    } else {
        playback[QStringLiteral("active")] = m_player->active();
        playback[QStringLiteral("paused")] = m_player->paused();
        playback[QStringLiteral("positionMs")] = m_player->positionMs();
        playback[QStringLiteral("durationMs")] = m_player->durationMs();
        playback[QStringLiteral("title")] = m_player->title();
        playback[QStringLiteral("streamMethod")] = m_player->streamMethod();
        playback[QStringLiteral("volume")] = m_player->volume();
        playback[QStringLiteral("muted")] = m_player->muted();

        // Extract current item details if playing
        if (m_player->queue() && m_player->queue()->currentIndex() >= 0) {
            const auto entry = m_player->queue()->currentItem();
            playback[QStringLiteral("itemId")] = entry.value(QStringLiteral("itemId")).toString();
            playback[QStringLiteral("subtitle")] = entry.value(QStringLiteral("artist")).toString().isEmpty()
                                                  ? entry.value(QStringLiteral("series")).toString()
                                                  : entry.value(QStringLiteral("artist")).toString();
            playback[QStringLiteral("mediaType")] = entry.value(QStringLiteral("type")).toString();
            playback[QStringLiteral("queueIndex")] = m_player->queue()->currentIndex();
        }

        if (auto *be = qobject_cast<PlayerBackend *>(m_player->backendObject())) {
            playback[QStringLiteral("currentAudioIndex")] = be->currentAudioTrackId();
            playback[QStringLiteral("currentSubtitleIndex")] = be->currentSubtitleTrackId();
        }

        // Audio & Subtitle Streams
        QJsonArray audioArr;
        for (const QVariant &st : m_player->audioStreams()) {
            const QVariantMap m = st.toMap();
            QJsonObject s;
            s[QStringLiteral("index")] = m.value(QStringLiteral("index")).toInt();
            s[QStringLiteral("title")] = m.value(QStringLiteral("title")).toString();
            s[QStringLiteral("language")] = m.value(QStringLiteral("language")).toString();
            s[QStringLiteral("codec")] = m.value(QStringLiteral("codec")).toString();
            s[QStringLiteral("channels")] = m.value(QStringLiteral("channels")).toInt();
            audioArr.append(s);
        }
        playback[QStringLiteral("audioStreams")] = audioArr;

        QJsonArray subArr;
        for (const QVariant &st : m_player->subtitleStreams()) {
            const QVariantMap m = st.toMap();
            QJsonObject s;
            s[QStringLiteral("index")] = m.value(QStringLiteral("index")).toInt();
            s[QStringLiteral("title")] = m.value(QStringLiteral("title")).toString();
            s[QStringLiteral("language")] = m.value(QStringLiteral("language")).toString();
            s[QStringLiteral("isDefault")] = m.value(QStringLiteral("isDefault")).toBool();
            s[QStringLiteral("isForced")] = m.value(QStringLiteral("isForced")).toBool();
            subArr.append(s);
        }
        playback[QStringLiteral("subtitleStreams")] = subArr;
    }

    obj[QStringLiteral("playback")] = playback;
    // Mirror playback fields into root of obj for direct access by client
    for (auto it = playback.begin(); it != playback.end(); ++it) {
        obj.insert(it.key(), it.value());
    }

    return obj;
}

QJsonArray WebRemoteServer::currentQueueJson() const
{
    QJsonArray arr;
    if (!m_player || !m_player->queue())
        return arr;

    for (int i = 0; i < m_player->queue()->rowCount(); ++i) {
        const QVariantMap item = m_player->queue()->itemAt(i);
        QJsonObject o;
        o[QStringLiteral("id")] = item.value(QStringLiteral("itemId")).toString();
        o[QStringLiteral("name")] = item.value(QStringLiteral("name")).toString();
        o[QStringLiteral("artist")] = item.value(QStringLiteral("artist")).toString();
        o[QStringLiteral("series")] = item.value(QStringLiteral("series")).toString();
        o[QStringLiteral("type")] = item.value(QStringLiteral("type")).toString();
        arr.append(o);
    }
    return arr;
}

void WebRemoteServer::handleApiStatus(QSslSocket *socket)
{
    sendJson(socket, 200, currentStatusJson());
}

void WebRemoteServer::handleApiQueue(QSslSocket *socket)
{
    const QByteArray data = QJsonDocument(currentQueueJson()).toJson(QJsonDocument::Compact);
    sendResponse(socket, 200, "application/json", data);
}

void WebRemoteServer::handleApiHome(QSslSocket *socket)
{
    QJsonObject homeObj;
    const auto serializeModel = [](MediaItemModel *m) -> QJsonArray {
        QJsonArray arr;
        if (!m) return arr;
        for (int i = 0; i < m->rowCount(); ++i) {
            const QModelIndex idx = m->index(i);
            QJsonObject o;
            o[QStringLiteral("id")] = m->data(idx, MediaItemModel::IdRole).toString();
            o[QStringLiteral("name")] = m->data(idx, MediaItemModel::NameRole).toString();
            o[QStringLiteral("type")] = m->data(idx, MediaItemModel::TypeRole).toString();
            o[QStringLiteral("seriesName")] = m->data(idx, MediaItemModel::SeriesNameRole).toString();
            o[QStringLiteral("playedPercentage")] = m->data(idx, MediaItemModel::ProgressRole).toDouble() * 100.0;
            arr.append(o);
        }
        return arr;
    };

    if (m_home) {
        homeObj[QStringLiteral("resume")] = serializeModel(m_home->resume());
        homeObj[QStringLiteral("nextUp")] = serializeModel(m_home->nextUp());
        homeObj[QStringLiteral("favorites")] = serializeModel(m_home->favorites());
    }

    sendJson(socket, 200, homeObj);
}

void WebRemoteServer::handleApiLibraries(QSslSocket *socket)
{
    if (!m_client) {
        sendJson(socket, 200, {});
        return;
    }

    QPointer<QSslSocket> safeSocket(socket);
    m_client->userViews().then(this, [this, safeSocket](const Result<QList<Library>> &res) {
        if (!safeSocket || !safeSocket->isOpen())
            return;
        if (!res.ok()) {
            sendResponse(safeSocket, 500, "application/json", "{\"error\":\"Failed to fetch views\"}");
            return;
        }
        QJsonArray arr;
        for (const Library &lib : res.value) {
            QJsonObject o;
            o[QStringLiteral("id")] = lib.id;
            o[QStringLiteral("name")] = lib.name;
            o[QStringLiteral("type")] = lib.collectionType;
            arr.append(o);
        }
        const QByteArray data = QJsonDocument(arr).toJson(QJsonDocument::Compact);
        sendResponse(safeSocket, 200, "application/json", data);
    });
}

void WebRemoteServer::handleApiLibraryItems(QSslSocket *socket, const HttpRequest &req)
{
    if (!m_client) {
        sendJson(socket, 200, {});
        return;
    }

    // /api/library/{id}/items
    const QStringList parts = req.path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QString libId = parts.size() >= 3 ? parts.at(2) : QString();

    const QUrlQuery q(req.url);
    ItemsQuery query;
    query.parentId = libId;
    query.startIndex = q.queryItemValue(QStringLiteral("startIndex")).toInt();
    query.limit = q.queryItemValue(QStringLiteral("limit")).isEmpty() ? 50 : q.queryItemValue(QStringLiteral("limit")).toInt();
    query.sortBy = q.queryItemValue(QStringLiteral("sortBy")).isEmpty() ? QStringLiteral("SortName") : q.queryItemValue(QStringLiteral("sortBy"));
    query.sortDescending = (q.queryItemValue(QStringLiteral("sortOrder")) == QLatin1String("Descending"));

    QPointer<QSslSocket> safeSocket(socket);
    m_client->items(query).then(this, [this, safeSocket](const Result<ItemsPage> &res) {
        if (!safeSocket || !safeSocket->isOpen())
            return;
        if (!res.ok()) {
            sendResponse(safeSocket, 500, "application/json", "{\"error\":\"Failed to fetch items\"}");
            return;
        }
        QJsonArray arr;
        for (const MediaItem &it : res.value.items) {
            QJsonObject o;
            o[QStringLiteral("id")] = it.id;
            o[QStringLiteral("name")] = it.name;
            o[QStringLiteral("type")] = it.type;
            o[QStringLiteral("productionYear")] = it.productionYear;
            o[QStringLiteral("seriesName")] = it.seriesName;
            arr.append(o);
        }
        const QByteArray data = QJsonDocument(arr).toJson(QJsonDocument::Compact);
        sendResponse(safeSocket, 200, "application/json", data);
    });
}

void WebRemoteServer::handleApiItemDetails(QSslSocket *socket, const QString &itemId)
{
    if (!m_client) {
        sendResponse(socket, 500, "application/json", "{}");
        return;
    }

    QPointer<QSslSocket> safeSocket(socket);
    m_client->itemDetails(itemId).then(this, [this, safeSocket, itemId](const Result<ItemDetails> &res) {
        if (!safeSocket || !safeSocket->isOpen())
            return;
        if (!res.ok()) {
            sendResponse(safeSocket, 404, "application/json", "{\"error\":\"Item not found\"}");
            return;
        }
        const ItemDetails &d = res.value;
        QJsonObject o;
        o[QStringLiteral("id")] = d.item.id;
        o[QStringLiteral("name")] = d.item.name;
        o[QStringLiteral("type")] = d.item.type;
        o[QStringLiteral("overview")] = d.item.overview;
        o[QStringLiteral("productionYear")] = d.item.productionYear;
        o[QStringLiteral("runtimeTicks")] = d.item.runtimeTicks;

        QJsonArray genreArr;
        for (const QString &g : d.genres)
            genreArr.append(g);
        o[QStringLiteral("genres")] = genreArr;

        // If it's a TV series, fetch episodes
        if (d.item.type == QLatin1String("Series")) {
            m_client->episodes(itemId, QString()).then(this, [this, safeSocket, o](const Result<ItemsPage> &epRes) mutable {
                if (!safeSocket || !safeSocket->isOpen())
                    return;
                QJsonArray epArr;
                if (epRes.ok()) {
                    for (const MediaItem &ep : epRes.value.items) {
                        QJsonObject epObj;
                        epObj[QStringLiteral("id")] = ep.id;
                        epObj[QStringLiteral("name")] = ep.name;
                        epObj[QStringLiteral("indexNumber")] = ep.indexNumber;
                        epObj[QStringLiteral("parentIndexNumber")] = ep.parentIndexNumber;
                        epArr.append(epObj);
                    }
                }
                o[QStringLiteral("episodes")] = epArr;
                sendJson(safeSocket, 200, o);
            });
            return;
        }

        sendJson(safeSocket, 200, o);
    });
}

void WebRemoteServer::handleApiSearch(QSslSocket *socket, const QString &query)
{
    if (!m_client || query.isEmpty()) {
        sendJson(socket, 200, {});
        return;
    }

    ItemsQuery q;
    q.searchTerm = query;
    q.limit = 30;
    q.recursive = true;

    QPointer<QSslSocket> safeSocket(socket);
    m_client->items(q).then(this, [this, safeSocket](const Result<ItemsPage> &res) {
        if (!safeSocket || !safeSocket->isOpen())
            return;
        if (!res.ok()) {
            sendResponse(safeSocket, 500, "application/json", "{\"error\":\"Search failed\"}");
            return;
        }
        QJsonArray arr;
        for (const MediaItem &it : res.value.items) {
            QJsonObject o;
            o[QStringLiteral("id")] = it.id;
            o[QStringLiteral("name")] = it.name;
            o[QStringLiteral("type")] = it.type;
            o[QStringLiteral("seriesName")] = it.seriesName;
            arr.append(o);
        }
        const QByteArray data = QJsonDocument(arr).toJson(QJsonDocument::Compact);
        sendResponse(safeSocket, 200, "application/json", data);
    });
}

void WebRemoteServer::handleApiImage(QSslSocket *socket, const QString &itemId, const QString &imageType)
{
    if (!m_client || !m_client->hasSession()) {
        sendResponse(socket, 404, "text/plain", "No session");
        return;
    }

    // Sanitize itemId and imageType against path traversal
    static const QRegularExpression safeRegex(QStringLiteral("^[a-zA-Z0-9_-]+$"));
    if (!safeRegex.match(itemId).hasMatch() || !safeRegex.match(imageType).hasMatch()) {
        sendResponse(socket, 400, "text/plain", "Invalid image parameters");
        return;
    }

    const QUrl imageUrl = m_client->baseUrl().resolved(
        QUrl(QStringLiteral("/Items/%1/Images/%2").arg(itemId, imageType)));

    QNetworkRequest req(imageUrl);
    req.setRawHeader("X-Emby-Token", m_client->accessToken().toUtf8());

    QPointer<QSslSocket> safeSocket(socket);
    QNetworkReply *reply = m_imageNam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, safeSocket, reply] {
        reply->deleteLater();
        if (!safeSocket || !safeSocket->isOpen())
            return;
        if (reply->error() != QNetworkReply::NoError) {
            sendResponse(safeSocket, 404, "text/plain", "Image not found");
            return;
        }
        const QByteArray data = reply->readAll();
        const QByteArray cType = reply->rawHeader("Content-Type");
        QHash<QByteArray, QByteArray> headers;
        headers["Cache-Control"] = "public, max-age=86400";
        sendResponse(safeSocket, 200, cType.isEmpty() ? "image/jpeg" : cType, data, headers);
    });
}

void WebRemoteServer::handleApiPlay(QSslSocket *socket, const QJsonObject &body)
{
    const QString itemId = body.value(QLatin1String("itemId")).toString();
    const QString mode = body.value(QLatin1String("mode")).toString();

    if (!m_actions || itemId.isEmpty()) {
        sendJson(socket, 400, {});
        return;
    }

    QVariantMap item = m_actions->itemFor(itemId);
    if (item.isEmpty()) {
        item.insert(QStringLiteral("itemId"), itemId);
        item.insert(QStringLiteral("name"), QString());
    }

    if (mode == QLatin1String("next")) {
        m_actions->playNext(item);
    } else if (mode == QLatin1String("queue")) {
        m_actions->addToQueue(item);
    } else {
        m_actions->play(item);
    }

    QJsonObject resp;
    resp[QStringLiteral("ok")] = true;
    sendJson(socket, 200, resp);
}

void WebRemoteServer::handleApiPlayback(QSslSocket *socket, const QJsonObject &body)
{
    if (!m_player) {
        sendJson(socket, 500, {});
        return;
    }

    const QString action = body.value(QLatin1String("action")).toString();
    const QJsonValue val = body.value(QLatin1String("value"));

    if (action == QLatin1String("togglePause")) {
        m_player->togglePause();
    } else if (action == QLatin1String("play")) {
        if (m_player->paused())
            m_player->togglePause();
    } else if (action == QLatin1String("pause")) {
        if (!m_player->paused())
            m_player->togglePause();
    } else if (action == QLatin1String("stop")) {
        m_player->stop();
    } else if (action == QLatin1String("next")) {
        m_player->playNext();
    } else if (action == QLatin1String("previous")) {
        m_player->playPrevious();
    } else if (action == QLatin1String("seekTo")) {
        m_player->seekTo(val.toVariant().toLongLong());
    } else if (action == QLatin1String("seekRelative")) {
        m_player->seekRelative(val.toVariant().toLongLong());
    } else if (action == QLatin1String("jumpQueue")) {
        if (m_player->queue()) {
            m_player->queue()->jumpTo(val.toInt());
        }
    }

    QJsonObject resp;
    resp[QStringLiteral("ok")] = true;
    sendJson(socket, 200, resp);
}

void WebRemoteServer::handleApiVolume(QSslSocket *socket, const QJsonObject &body)
{
    if (!m_player) {
        sendJson(socket, 500, {});
        return;
    }

    const QString action = body.value(QLatin1String("action")).toString();
    const int val = body.value(QLatin1String("value")).toInt();

    if (action == QLatin1String("set")) {
        m_player->setVolume(val);
    } else if (action == QLatin1String("up")) {
        m_player->setVolume(m_player->volume() + 5);
    } else if (action == QLatin1String("down")) {
        m_player->setVolume(m_player->volume() - 5);
    } else if (action == QLatin1String("mute")) {
        m_player->setMuted(true);
    } else if (action == QLatin1String("unmute")) {
        m_player->setMuted(false);
    } else if (action == QLatin1String("toggleMute")) {
        m_player->setMuted(!m_player->muted());
    }

    QJsonObject resp;
    resp[QStringLiteral("ok")] = true;
    sendJson(socket, 200, resp);
}

void WebRemoteServer::handleApiStream(QSslSocket *socket, const QJsonObject &body)
{
    if (!m_player) {
        sendJson(socket, 500, {});
        return;
    }

    const QString type = body.value(QLatin1String("type")).toString();
    const int trackId = body.value(QLatin1String("trackId")).toInt();

    if (type == QLatin1String("audio")) {
        m_player->setAudioTrack(trackId);
    } else if (type == QLatin1String("subtitle")) {
        m_player->setSubtitleTrack(trackId);
    }

    QJsonObject resp;
    resp[QStringLiteral("ok")] = true;
    sendJson(socket, 200, resp);
}

void WebRemoteServer::handleApiNavigate(QSslSocket *socket, const QJsonObject &body)
{
    const QString dest = body.value(QLatin1String("destination")).toString();
    const QString key = body.value(QLatin1String("key")).toString();

    if (!dest.isEmpty()) {
        if (dest == QLatin1String("osd"))
            emit osdToggleRequested();
        else
            emit navigationRequested(dest);
    } else if (!key.isEmpty()) {
        emit keyNavigationRequested(key);
    }

    QJsonObject resp;
    resp[QStringLiteral("ok")] = true;
    sendJson(socket, 200, resp);
}

void WebRemoteServer::handleApiEvents(QSslSocket *socket)
{
    QByteArray sseHeaders =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream; charset=utf-8\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n\r\n";

    socket->write(sseHeaders);
    socket->flush();

    m_sseClients.append(socket);
    emit connectedClientsChanged(m_sseClients.size());

    // Push initial status and queue
    const QByteArray statusData = QJsonDocument(currentStatusJson()).toJson(QJsonDocument::Compact);
    socket->write("event: status\r\ndata: " + statusData + "\r\n\r\n");

    const QByteArray queueData = QJsonDocument(currentQueueJson()).toJson(QJsonDocument::Compact);
    socket->write("event: queue\r\ndata: " + queueData + "\r\n\r\n");
    socket->flush();
}

void WebRemoteServer::broadcastSse(const QString &eventName, const QByteArray &data)
{
    const QByteArray msg = "event: " + eventName.toUtf8() + "\r\ndata: " + data + "\r\n\r\n";

    auto it = m_sseClients.begin();
    while (it != m_sseClients.end()) {
        QSslSocket *sock = *it;
        if (!sock || !sock->isOpen() || sock->state() != QAbstractSocket::ConnectedState) {
            it = m_sseClients.erase(it);
            emit connectedClientsChanged(m_sseClients.size());
            continue;
        }
        if (sock->write(msg) == -1) {
            it = m_sseClients.erase(it);
            emit connectedClientsChanged(m_sseClients.size());
            continue;
        }
        sock->flush();
        ++it;
    }
}

void WebRemoteServer::broadcastPlayerStatus()
{
    const QByteArray data = QJsonDocument(currentStatusJson()).toJson(QJsonDocument::Compact);
    broadcastSse(QStringLiteral("status"), data);
}

void WebRemoteServer::broadcastQueue()
{
    const QByteArray data = QJsonDocument(currentQueueJson()).toJson(QJsonDocument::Compact);
    broadcastSse(QStringLiteral("queue"), data);
}

void WebRemoteServer::sendResponse(QSslSocket *socket, int statusCode, const QByteArray &contentType,
                                   const QByteArray &body, const QHash<QByteArray, QByteArray> &extraHeaders)
{
    if (!socket || !socket->isOpen())
        return;

    QByteArray statusText = "OK";
    if (statusCode == 204) statusText = "No Content";
    else if (statusCode == 400) statusText = "Bad Request";
    else if (statusCode == 401) statusText = "Unauthorized";
    else if (statusCode == 403) statusText = "Forbidden";
    else if (statusCode == 404) statusText = "Not Found";
    else if (statusCode == 429) statusText = "Too Many Requests";
    else if (statusCode == 500) statusText = "Internal Server Error";

    QByteArray res = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n" +
                     "Content-Type: " + contentType + "\r\n" +
                     "Content-Length: " + QByteArray::number(body.size()) + "\r\n" +
                     "Connection: close\r\n" +
                     "Access-Control-Allow-Origin: *\r\n" +
                     "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n" +
                     "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";

    for (auto it = extraHeaders.cbegin(); it != extraHeaders.cend(); ++it) {
        res += it.key() + ": " + it.value() + "\r\n";
    }
    res += "\r\n" + body;

    qCInfo(logApp) << "webremote: sendResponse" << statusCode << "bytes:" << res.size();
    socket->write(res);
    socket->flush();
}

void WebRemoteServer::sendJson(QSslSocket *socket, int statusCode, const QJsonObject &json)
{
    const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
    sendResponse(socket, statusCode, "application/json", body);
}

} // namespace strmqt
