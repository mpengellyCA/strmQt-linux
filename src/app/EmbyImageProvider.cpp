#include "EmbyImageProvider.h"

#include "ImageLimits.h"
#include "core/Log.h"
#include "server/emby/EmbyClient.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

#include <memory>

namespace strmqt {

namespace {

// Wide enough for a lock screen and a media notification on a HiDPI panel;
// small enough that the extra on-disk copy is a few tens of KB.
constexpr int kExportWidth = 512;
// Roughly an album's worth of distinct covers. Bounded, and small enough that
// listing the directory to prune it is cheaper than the write it follows.
constexpr int kMaxExportedFiles = 24;
struct BoundedReplyState
{
    QByteArray bytes;
    bool overflow = false;
};

void drainBounded(QNetworkReply *reply, const std::shared_ptr<BoundedReplyState> &state)
{
    if (state->overflow)
        return;
    const qint64 remaining = imagelimits::kMaxEncodedBytes - state->bytes.size();
    state->bytes += reply->read(remaining + 1);
    if (!imagelimits::encodedBytesAllowed(state->bytes.size())) {
        state->overflow = true;
        reply->abort();
    }
}

std::shared_ptr<BoundedReplyState> boundReply(QNetworkReply *reply)
{
    auto state = std::make_shared<BoundedReplyState>();
    state->bytes.reserve(256 * 1024);
    QObject::connect(reply, &QNetworkReply::readyRead, reply,
                     [reply, state] { drainBounded(reply, state); });
    return state;
}

bool imageMetadataAllowed(QImageReader *reader)
{
    return imagelimits::decodedSizeAllowed(reader->size());
}

bool decodeBounded(const QByteArray &bytes, QImage *image)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return false;
    QImageReader reader(&buffer);
    if (!imageMetadataAllowed(&reader))
        return false;
    *image = reader.read();
    return !image->isNull();
}

// Item ids and image tags come off the wire, so they reach the filesystem only
// as [A-Za-z0-9_]. '-' is deliberately excluded: it is the separator between the
// two halves of the filename, and letting it through either half would make
// "a-b"+"c" and "a"+"b-c" the same file.
QString safeName(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        const bool safe = c.unicode() < 128 && (c.isLetterOrNumber() || c == QLatin1Char('_'));
        out.append(safe ? c : QLatin1Char('_'));
    }
    return out;
}

// The request carries no extension to guess from and Emby serves whatever the
// item actually holds (JPEG for most covers, PNG for anything with alpha), so
// the format comes from the bytes.
QString suffixFor(const QByteArray &bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};
    QImageReader reader(&buffer);
    if (!imageMetadataAllowed(&reader))
        return {};
    const QByteArray format = reader.format();
    return format.isEmpty() ? QString() : QString::fromLatin1(format).toLower();
}

void pruneExports(const QDir &dir)
{
    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Time);
    for (qsizetype i = kMaxExportedFiles; i < files.size(); ++i)
        QFile::remove(files.at(i).absoluteFilePath());
}

} // namespace

QQuickTextureFactory *EmbyImageResponse::textureFactory() const
{
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

QString EmbyImageResponse::errorString() const
{
    return m_error;
}

void EmbyImageResponse::complete(QImage image, const QString &error)
{
    m_image = std::move(image);
    m_error = error;
    emit finished();
}

EmbyImageFetcher::EmbyImageFetcher(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_nam(new QNetworkAccessManager(this))
{
    m_cache = new QNetworkDiskCache(m_nam);
    m_cache->setMaximumCacheSize(256 * 1024 * 1024);
    m_nam->setCache(m_cache);
    m_nam->setAutoDeleteReplies(true);
    if (m_client) {
        connect(m_client, &emby::EmbyClient::identityChanged, this,
                &EmbyImageFetcher::resetCachePartition);
    }
    // Enough to keep the network fed, few enough to leave the machine to the
    // UI and the video decoder. Decoding is memory-hungry, not latency-bound.
    m_decodePool.setMaxThreadCount(qBound(2, QThread::idealThreadCount() / 4, 4));
    m_decodePool.setObjectName(QStringLiteral("image-decode"));
    resetCachePartition();
}

EmbyImageFetcher::~EmbyImageFetcher()
{
    // Before any member a running decode might touch is destroyed.
    m_decodePool.waitForDone();
}

void EmbyImageFetcher::resetCachePartition()
{
    for (QNetworkReply *reply : m_nam->findChildren<QNetworkReply *>()) {
        if (reply && !reply->isFinished())
            reply->abort();
    }
    if (!m_cache)
        return;
    m_cache->clear();
    QByteArray identity;
    if (m_client) {
        identity = m_client->baseUrl().toString(QUrl::FullyEncoded).toUtf8() + '\0' +
                   m_client->userId().toUtf8();
    }
    const QString partition = QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    // Wipe the OUTGOING partition's decoded-asset store for the same reason the
    // network cache above is cleared: one user's artwork must not survive on
    // disk into another's session.
    if (!m_assetDir.isEmpty())
        QDir(m_assetDir).removeRecursively();
    const QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                         QStringLiteral("/images/") + partition;
    m_cache->setCacheDirectory(root);
    m_assetDir = root + QStringLiteral("/assets");
    QDir().mkpath(m_assetDir);
    for (const QString &path : m_exportDirectories) {
        QDir dir(path);
        for (const QString &file : dir.entryList(QDir::Files))
            dir.remove(file);
    }
}

QNetworkRequest EmbyImageFetcher::imageRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    request.setTransferTimeout(15'000);
    if (!m_client->accessToken().isEmpty())
        request.setRawHeader("X-Emby-Token", m_client->accessToken().toUtf8());
    return request;
}

QString EmbyImageFetcher::assetPathFor(const QUrl &url) const
{
    if (m_assetDir.isEmpty())
        return {};
    const QByteArray digest = QCryptographicHash::hash(
        url.toString(QUrl::FullyEncoded).toUtf8(), QCryptographicHash::Sha256);
    return m_assetDir + QLatin1Char('/') + QString::fromLatin1(digest.toHex());
}

void EmbyImageFetcher::storeAsset(const QString &path, const QByteArray &bytes)
{
    if (path.isEmpty() || bytes.isEmpty())
        return;
    // QSaveFile so a half-written entry can never be read back as an image.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    if (file.write(bytes) != bytes.size() || !file.commit())
        return;
    pruneAssetsIfNeeded();
}

void EmbyImageFetcher::pruneAssetsIfNeeded()
{
    // Scanning the directory costs a stat per file, so it happens once per
    // batch of writes rather than on each one.
    constexpr int kWritesBetweenPrunes = 64;
    constexpr qint64 kMaxAssetBytes = 512LL * 1024 * 1024;

    QMutexLocker locker(&m_assetMutex);
    if (++m_assetWrites < kWritesBetweenPrunes)
        return;
    m_assetWrites = 0;
    QDir dir(m_assetDir);
    // Oldest last, so the newest entries are the ones kept.
    QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Time);
    qint64 total = 0;
    for (const QFileInfo &info : std::as_const(files))
        total += info.size();
    for (auto it = files.crbegin(); it != files.crend() && total > kMaxAssetBytes; ++it) {
        const qint64 size = it->size();
        if (QFile::remove(it->absoluteFilePath()))
            total -= size;
    }
}

void EmbyImageFetcher::decodeAsync(EmbyImageResponse *response, const QString &id,
                                   const QString &cachePath, QByteArray bytes, bool storeToCache,
                                   const QSize &requestedSize)
{
    const bool fromCache = bytes.isEmpty();
    m_decodePool.start([this, response, id, cachePath, bytes = std::move(bytes), storeToCache,
                        fromCache, requestedSize]() mutable {
        if (fromCache) {
            QFile file(cachePath);
            if (file.open(QIODevice::ReadOnly))
                bytes = file.read(imagelimits::kMaxEncodedBytes + 1);
        }
        QImage image;
        const bool ok = imagelimits::encodedBytesAllowed(bytes.size()) && !bytes.isEmpty()
                     && decodeBounded(bytes, &image);
        if (ok && storeToCache)
            storeAsset(cachePath, bytes);

        // Back to the GUI thread: imageDecoded() has listeners that assume it,
        // and completing the response there keeps the reply-lifetime rules the
        // network path has always had.
        QMetaObject::invokeMethod(this, [this, response, id, image, ok, fromCache, cachePath,
                                         requestedSize] {
            if (ok) {
                // Before the move: QImage is implicitly shared, so the listener
                // gets a reference to the same pixels rather than a copy.
                QImage decoded = image;
                emit imageDecoded(id, decoded);
                response->complete(std::move(decoded), {});
                return;
            }
            if (fromCache) {
                // A corrupt or truncated entry is not a failed image: drop it
                // and ask the server, exactly as if it had never been cached.
                QFile::remove(cachePath);
                fetchFromNetwork(response, id, requestedSize);
                return;
            }
            response->complete({}, QStringLiteral("undecodable or oversized image"));
        });
    });
}

void EmbyImageFetcher::fetchFromNetwork(EmbyImageResponse *response, const QString &id,
                                        const QSize &requestedSize)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    const int maxWidth = requestedSize.width() > 0 ? requestedSize.width() : 480;
    const QUrl url = m_client->imageUrl(parts[0], parts[1], maxWidth, parts[2]);
    QNetworkReply *reply = m_nam->get(imageRequest(url));
    const auto state = boundReply(reply);
    const QString cachePath = assetPathFor(url);
    // Context must be `this` (GUI thread): the reply is auto-deleted right after
    // finished() is delivered on this thread, so handling it queued on the
    // response's QQuickPixmapReader thread would be use-after-free. The engine
    // keeps `response` alive until it emits finished(), which is thread-safe.
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, response, id, state, cachePath, requestedSize] {
        drainBounded(reply, state);
        if (state->overflow) {
            response->complete({}, QStringLiteral("image response exceeds size limit"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            response->complete({}, reply->errorString());
            return;
        }
        decodeAsync(response, id, cachePath, state->bytes, /*storeToCache=*/true, requestedSize);
    });
}

void EmbyImageFetcher::fetch(EmbyImageResponse *response, const QString &id,
                             const QSize &requestedSize)
{
    // id = "{itemId}/{imageType}/{tag}"
    const QStringList parts = id.split(QLatin1Char('/'));
    if (parts.size() != 3) {
        response->complete({}, QStringLiteral("bad image id: %1").arg(id));
        return;
    }

    // A hit skips the network entirely. The URL is content-addressed through
    // Emby's image tag, so there is nothing to revalidate and no request to
    // make: revisiting a library page costs a file read and a decode, both off
    // this thread, instead of a round trip per card.
    const int maxWidth = requestedSize.width() > 0 ? requestedSize.width() : 480;
    const QString cachePath =
        assetPathFor(m_client->imageUrl(parts[0], parts[1], maxWidth, parts[2]));
    if (!cachePath.isEmpty() && QFileInfo::exists(cachePath)) {
        decodeAsync(response, id, cachePath, {}, /*storeToCache=*/false, requestedSize);
        return;
    }
    fetchFromNetwork(response, id, requestedSize);
}

void EmbyImageFetcher::exportToFile(const QString &id, const QString &subdir)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    if (parts.size() != 3 || parts[0].isEmpty() || parts[2].isEmpty()) {
        emit fileExported(id, {});
        return;
    }

    QNetworkReply *reply =
        m_nam->get(imageRequest(m_client->imageUrl(parts[0], parts[1], kExportWidth, parts[2])));
    const auto state = boundReply(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, subdir, parts, state] {
        drainBounded(reply, state);
        if (state->overflow) {
            qCDebug(logApp) << "image export exceeds size limit for" << id;
            emit fileExported(id, {});
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            qCDebug(logApp) << "image export failed for" << id << reply->errorString();
            emit fileExported(id, {});
            return;
        }
        const QByteArray &bytes = state->bytes;
        const QString suffix = suffixFor(bytes);
        if (suffix.isEmpty()) {
            qCDebug(logApp) << "image export got undecodable bytes for" << id;
            emit fileExported(id, {});
            return;
        }

        QDir dir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                 QLatin1Char('/') + subdir);
        if (!dir.mkpath(QStringLiteral("."))) {
            qCWarning(logApp) << "could not create image export dir" << dir.path();
            emit fileExported(id, {});
            return;
        }
        if (!m_exportDirectories.contains(dir.absolutePath()))
            m_exportDirectories.append(dir.absolutePath());
        const QString path = dir.filePath(safeName(parts[0]) + QLatin1Char('-') +
                                          safeName(parts[2]) + QLatin1Char('.') + suffix);

        // QSaveFile, not QFile: another process reads this path, and a partial
        // write is a corrupt image rather than a missing one.
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
            !file.commit()) {
            qCWarning(logApp) << "could not write image export" << path << file.errorString();
            emit fileExported(id, {});
            return;
        }
        pruneExports(dir);
        emit fileExported(id, QUrl::fromLocalFile(path));
    });
}

EmbyImageProvider::EmbyImageProvider(EmbyImageFetcher *fetcher) : m_fetcher(fetcher) {}

QQuickImageResponse *EmbyImageProvider::requestImageResponse(const QString &id,
                                                             const QSize &requestedSize)
{
    auto *response = new EmbyImageResponse;
    // Hop to the GUI thread where the QNAM lives.
    QMetaObject::invokeMethod(m_fetcher, [fetcher = m_fetcher, response, id, requestedSize] {
        fetcher->fetch(response, id, requestedSize);
    });
    return response;
}

} // namespace strmqt
