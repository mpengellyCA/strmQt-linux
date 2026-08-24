#include "EmbyImageProvider.h"

#include "ImageLimits.h"
#include "core/Log.h"
#include "models/MediaItemModel.h"
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
#include <QSemaphore>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>

#include <atomic>
#include <memory>
#include <utility>

namespace strmqt {

namespace {

// Wide enough for a lock screen and a media notification on a HiDPI panel;
// small enough that the extra on-disk copy is a few tens of KB.
constexpr int kExportWidth = 512;
// Roughly an album's worth of distinct covers. Bounded, and small enough that
// listing the directory to prune it is cheaper than the write it follows.
constexpr int kMaxExportedFiles = 24;
constexpr qint64 kMaxAssetBytes = 512LL * 1024 * 1024;
// A prune can overshoot by at most this much plus the single image currently
// being stored (itself bounded by kMaxEncodedBytes). This replaces the former
// 64-image interval whose worst case was about one gigabyte.
constexpr qint64 kAssetBytesBetweenPrunes = 8LL * 1024 * 1024;
const auto kMprisExportSubdir = QStringLiteral("mpris");
struct BoundedReplyState
{
    QByteArray bytes;
    bool overflow = false;
};

void drainBounded(QNetworkReply *reply, const std::shared_ptr<BoundedReplyState> &state)
{
    if (state->overflow || !reply || !reply->isOpen())
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

bool decodeBounded(const QByteArray &bytes, const QSize &requestedSize, QImage *image)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return false;
    QImageReader reader(&buffer);
    if (!imageMetadataAllowed(&reader))
        return false;
    const QSize target = imagelimits::boundedTargetSize(reader.size(), requestedSize);
    if (!target.isValid())
        return false;
    if (target != reader.size())
        reader.setScaledSize(target);
    *image = reader.read();
    return !image->isNull() && qint64(image->width()) * image->height() <=
                                   imagelimits::kMaxTargetPixels;
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

struct EmbyImageFetcher::CachePartition
{
    QString assetDir;
    QString sourceNamespace;
    quint64 generation = 0;
    std::atomic_bool active = true;
    QMutex ioMutex;
    qint64 bytesSincePrune = 0;
};

EmbyImageResponse::EmbyImageResponse() : m_state(std::make_shared<State>()) {}

QQuickTextureFactory *EmbyImageResponse::textureFactory() const
{
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

QString EmbyImageResponse::errorString() const
{
    return m_error;
}

bool EmbyImageResponse::complete(QImage image, const QString &error)
{
    bool expected = false;
    if (!m_state->completed.compare_exchange_strong(expected, true))
        return false;
    m_image = std::move(image);
    m_error = error;
    emit finished();
    return true;
}

void EmbyImageResponse::cancel()
{
    m_state->canceled.store(true, std::memory_order_release);
    bool expected = false;
    if (!m_state->completed.compare_exchange_strong(expected, true))
        return;
    // Reserve completion before requesting the GUI-thread abort. A decode that
    // races cancellation now loses its own completion CAS and cannot publish.
    emit cancelRequested();
    m_image = {};
    m_error = QStringLiteral("request canceled");
    emit finished();
}

EmbyImageFetcher::EmbyImageFetcher(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_nam(new QNetworkAccessManager(this)),
      m_sourceNamespacePrefix(QUuid::createUuid().toString(QUuid::WithoutBraces))
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
    // MPRIS is the one supported external artwork consumer. Register its
    // directory before the first identity reset so exports left by an earlier
    // process are also retired, not only paths exported during this lifetime.
    m_exportDirectories.append(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                               QLatin1Char('/') + kMprisExportSubdir);
    resetCachePartition();
}

EmbyImageFetcher::~EmbyImageFetcher()
{
    // Before any member a running decode might touch is destroyed.
    m_decodePool.waitForDone();
}

void EmbyImageFetcher::resetCachePartition()
{
    if (!m_cache)
        return;
    QByteArray identity;
    if (m_client) {
        identity = m_client->baseUrl().toString(QUrl::FullyEncoded).toUtf8() + '\0' +
                   m_client->userId().toUtf8();
    }
    const QString partitionName = QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    const QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                         QStringLiteral("/images/") + partitionName;
    auto next = std::make_shared<CachePartition>();
    next->assetDir = root + QStringLiteral("/assets");
    next->generation = ++m_partitionGeneration;
    next->sourceNamespace =
        m_sourceNamespacePrefix + QLatin1Char('-') + QString::number(next->generation);

    CachePartitionPtr outgoing;
    {
        QMutexLocker locker(&m_partitionMutex);
        // Cleanup takes this same mutex across its active-path check and
        // tombstone rename. Creating the directory here makes the A1-cleanup/A2-
        // reactivation ordering atomic: cleanup either finishes first and A2
        // recreates it, or observes A2 as current and leaves it alone.
        QDir().mkpath(next->assetDir);
        outgoing = std::exchange(m_partition, next);
        if (outgoing)
            outgoing->active.store(false, std::memory_order_release);
    }
    setEmbyImageSourceNamespace(next->sourceNamespace);
    // Retire the generation before aborting replies: abort may deliver
    // finished() immediately, and that callback must already see itself as
    // stale. No worker can begin a store after this point.
    for (QNetworkReply *reply : m_nam->findChildren<QNetworkReply *>()) {
        if (reply && !reply->isFinished())
            reply->abort();
    }
    if (!m_cache->cacheDirectory().isEmpty())
        m_cache->clear();
    m_cache->setCacheDirectory(root);
    schedulePartitionMaintenance(next, outgoing);
    for (const QString &path : m_exportDirectories) {
        QDir dir(path);
        for (const QString &file : dir.entryList(QDir::Files))
            dir.remove(file);
    }
    emit cachePartitionChanged(next->generation);
    emit sourceNamespaceChanged();
}

quint64 EmbyImageFetcher::cachePartitionGeneration() const
{
    const CachePartitionPtr partition = partitionSnapshot();
    return partition ? partition->generation : 0;
}

QString EmbyImageFetcher::sourceNamespace() const
{
    const CachePartitionPtr partition = partitionSnapshot();
    return partition ? partition->sourceNamespace : QString();
}

QString EmbyImageFetcher::sourceFor(const QString &itemId, const QString &imageType,
                                    const QString &tag) const
{
    const CachePartitionPtr partition = partitionSnapshot();
    if (!partition || itemId.isEmpty() || imageType.isEmpty())
        return {};
    return QStringLiteral("image://emby/%1/%2/%3/%4")
        .arg(partition->sourceNamespace, itemId, imageType, tag);
}

EmbyImageFetcher::CachePartitionPtr EmbyImageFetcher::partitionSnapshot() const
{
    QMutexLocker locker(&m_partitionMutex);
    return m_partition;
}

void EmbyImageFetcher::schedulePartitionMaintenance(const CachePartitionPtr &partition,
                                                    const CachePartitionPtr &outgoing)
{
    // Startup pruning and partition cleanup are filesystem scans, never GUI
    // work. A per-partition mutex orders them against stores from decode jobs.
    m_decodePool.start([partition] { pruneAssets(partition, /*lockIo=*/true); });
    if (!outgoing || outgoing->assetDir == partition->assetDir)
        return;
    QSemaphore *cleanupGate = nullptr;
#ifdef STRMQT_IMAGE_CACHE_TESTS
    cleanupGate = m_cleanupGateForTests;
#endif
    m_decodePool.start([this, outgoing, cleanupGate] {
#ifdef STRMQT_IMAGE_CACHE_TESTS
        if (cleanupGate)
            cleanupGate->acquire();
#else
        Q_UNUSED(cleanupGate);
#endif
        QString retiredPath;
        {
            QMutexLocker ioLocker(&outgoing->ioMutex);
            QMutexLocker partitionLocker(&m_partitionMutex);
            if (m_partition && m_partition->assetDir == outgoing->assetDir)
                return;
            // Rename under the partition lock, then do the potentially long
            // recursive deletion outside it. A reset either publishes A2
            // first (and this skips A's live path), or recreates A/assets only
            // after A1 has moved to this generation-unique tombstone.
            retiredPath = outgoing->assetDir + QStringLiteral(".retired-") +
                          outgoing->sourceNamespace;
            if (!QDir().rename(outgoing->assetDir, retiredPath))
                return;
        }
        QDir(retiredPath).removeRecursively();
    });
}

#ifdef STRMQT_IMAGE_CACHE_TESTS
void EmbyImageFetcher::setCleanupGateForTests(QSemaphore *gate)
{
    m_cleanupGateForTests = gate;
}

void EmbyImageFetcher::waitForMaintenanceForTests()
{
    m_decodePool.waitForDone();
}
#endif

QNetworkRequest EmbyImageFetcher::imageRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    request.setTransferTimeout(15'000);
    if (!m_client->accessToken().isEmpty())
        request.setRawHeader("X-Emby-Token", m_client->accessToken().toUtf8());
    return request;
}

QString EmbyImageFetcher::assetPathFor(const CachePartitionPtr &partition, const QUrl &url)
{
    if (!partition || partition->assetDir.isEmpty())
        return {};
    const QByteArray digest = QCryptographicHash::hash(
        url.toString(QUrl::FullyEncoded).toUtf8(), QCryptographicHash::Sha256);
    return partition->assetDir + QLatin1Char('/') + QString::fromLatin1(digest.toHex());
}

void EmbyImageFetcher::storeAsset(const CachePartitionPtr &partition, const QString &path,
                                  const QByteArray &bytes)
{
    if (!partition || path.isEmpty() || bytes.isEmpty())
        return;
    QMutexLocker locker(&partition->ioMutex);
    if (!partition->active.load(std::memory_order_acquire))
        return;
    QDir().mkpath(partition->assetDir);
    // QSaveFile so a half-written entry can never be read back as an image.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    if (file.write(bytes) != bytes.size() || !file.commit())
        return;
    partition->bytesSincePrune += bytes.size();
    if (partition->bytesSincePrune >= kAssetBytesBetweenPrunes) {
        partition->bytesSincePrune = 0;
        pruneAssets(partition, /*lockIo=*/false);
    }
}

void EmbyImageFetcher::pruneAssets(const CachePartitionPtr &partition, bool lockIo)
{
    if (!partition)
        return;
    std::unique_ptr<QMutexLocker<QMutex>> locker;
    if (lockIo)
        locker = std::make_unique<QMutexLocker<QMutex>>(&partition->ioMutex);
    if (!partition->active.load(std::memory_order_acquire))
        return;
    QDir dir(partition->assetDir);
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
    partition->bytesSincePrune = 0;
}

void EmbyImageFetcher::decodeAsync(const QPointer<EmbyImageResponse> &response, const QString &id,
                                   const CachePartitionPtr &partition, const QString &cachePath,
                                   QByteArray bytes, bool storeToCache,
                                   const QSize &requestedSize)
{
    if (!response)
        return;
    const bool fromCache = bytes.isEmpty();
    const std::shared_ptr<EmbyImageResponse::State> responseState = response->state();
    m_decodePool.start([this, response, responseState, id, partition, cachePath,
                        bytes = std::move(bytes), storeToCache, fromCache,
                        requestedSize]() mutable {
        if (responseState->canceled.load(std::memory_order_acquire))
            return;
        bool stale = !partition->active.load(std::memory_order_acquire);
        if (fromCache && !stale) {
            QMutexLocker locker(&partition->ioMutex);
            stale = !partition->active.load(std::memory_order_acquire);
            if (!stale) {
                QFile file(cachePath);
                if (file.open(QIODevice::ReadOnly))
                    bytes = file.read(imagelimits::kMaxEncodedBytes + 1);
            }
        }
        if (responseState->canceled.load(std::memory_order_acquire))
            return;
        QImage image;
        const bool ok = !stale && imagelimits::encodedBytesAllowed(bytes.size()) &&
                        !bytes.isEmpty() && decodeBounded(bytes, requestedSize, &image);
        if (responseState->canceled.load(std::memory_order_acquire))
            return;
        stale = stale || !partition->active.load(std::memory_order_acquire);
        if (ok && storeToCache && !stale)
            storeAsset(partition, cachePath, bytes);

        // Back to the GUI thread: imageDecoded() has listeners that assume it,
        // and completing the response there keeps the reply-lifetime rules the
        // network path has always had.
        QMetaObject::invokeMethod(this, [this, response, responseState, id, partition, image, ok,
                                         fromCache, cachePath, requestedSize] {
            if (!response || responseState->canceled.load(std::memory_order_acquire))
                return;
            if (!partition->active.load(std::memory_order_acquire)) {
                response->complete({}, QStringLiteral("stale image request"));
                return;
            }
            if (ok) {
                // QImage is implicitly shared, so the response and tint sampler
                // reference the same pixels rather than copying the allocation.
                QImage decoded = image;
                if (response->complete(decoded, {}))
                    emit imageDecoded(id, decoded, partition->generation);
                return;
            }
            if (fromCache) {
                // A corrupt or truncated entry is not a failed image: drop it
                // and ask the server, exactly as if it had never been cached.
                {
                    QMutexLocker locker(&partition->ioMutex);
                    if (!partition->active.load(std::memory_order_acquire)) {
                        response->complete({}, QStringLiteral("stale image request"));
                        return;
                    }
                    QFile::remove(cachePath);
                }
                fetchFromNetwork(response, id, requestedSize, partition);
                return;
            }
            response->complete({}, QStringLiteral("undecodable or oversized image"));
        });
    });
}

void EmbyImageFetcher::fetchFromNetwork(const QPointer<EmbyImageResponse> &response,
                                        const QString &id, const QSize &requestedSize,
                                        const CachePartitionPtr &partition)
{
    if (!response || !partition || !partition->active.load(std::memory_order_acquire)) {
        if (response)
            response->complete({}, QStringLiteral("stale image request"));
        return;
    }
    const QStringList parts = id.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    const int maxWidth = imagelimits::boundedRequestWidth(requestedSize.width());
    const QUrl url = m_client->imageUrl(parts[1], parts[2], maxWidth, parts[3]);
    QNetworkReply *reply = m_nam->get(imageRequest(url));
    const QPointer<QNetworkReply> guardedReply(reply);
    connect(response, &EmbyImageResponse::cancelRequested, this,
            [guardedReply] {
                if (guardedReply && !guardedReply->isFinished())
                    guardedReply->abort();
            },
            Qt::QueuedConnection);
    if (response->state()->canceled.load(std::memory_order_acquire))
        reply->abort();
    const auto state = boundReply(reply);
    const QString cachePath = assetPathFor(partition, url);
    // Context must be `this` (GUI thread): the reply is auto-deleted right after
    // finished() is delivered on this thread, so handling it queued on the
    // response's QQuickPixmapReader thread would be use-after-free. The engine
    // keeps `response` alive until it emits finished(), which is thread-safe.
    connect(reply, &QNetworkReply::finished, this,
            [this, guardedReply, response, id, state, partition, cachePath, requestedSize] {
        if (!guardedReply)
            return;
        QNetworkReply *reply = guardedReply.data();
        drainBounded(reply, state);
        if (!response || response->state()->canceled.load(std::memory_order_acquire))
            return;
        if (!partition->active.load(std::memory_order_acquire)) {
            response->complete({}, QStringLiteral("stale image request"));
            return;
        }
        if (state->overflow) {
            response->complete({}, QStringLiteral("image response exceeds size limit"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            response->complete({}, reply->errorString());
            return;
        }
        decodeAsync(response, id, partition, cachePath, state->bytes, /*storeToCache=*/true,
                    requestedSize);
    });
}

void EmbyImageFetcher::fetch(EmbyImageResponse *response, const QString &id,
                             const QSize &requestedSize)
{
    const QPointer<EmbyImageResponse> guardedResponse(response);
    const CachePartitionPtr partition = partitionSnapshot();
    if (response->state()->canceled.load(std::memory_order_acquire))
        return;
    // id = "{opaque namespace}/{itemId}/{imageType}/{tag}"
    const QStringList parts = id.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    if (!partition || parts.size() != 4 || parts[0] != partition->sourceNamespace ||
        parts[1].isEmpty() || parts[2].isEmpty()) {
        response->complete({}, QStringLiteral("bad image id: %1").arg(id));
        return;
    }

    // A hit skips the network entirely. The URL is content-addressed through
    // Emby's image tag, so there is nothing to revalidate and no request to
    // make: revisiting a library page costs a file read and a decode, both off
    // this thread, instead of a round trip per card.
    const int maxWidth = imagelimits::boundedRequestWidth(requestedSize.width());
    const QString cachePath = assetPathFor(
        partition, m_client->imageUrl(parts[1], parts[2], maxWidth, parts[3]));
    if (!cachePath.isEmpty() && QFileInfo::exists(cachePath)) {
        decodeAsync(guardedResponse, id, partition, cachePath, {}, /*storeToCache=*/false,
                    requestedSize);
        return;
    }
    fetchFromNetwork(guardedResponse, id, requestedSize, partition);
}

void EmbyImageFetcher::exportToFile(const QString &id, const QString &subdir)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    if (parts.size() != 3 || parts[0].isEmpty() || parts[2].isEmpty()) {
        emit fileExported(id, {});
        return;
    }

    const CachePartitionPtr partition = partitionSnapshot();
    QNetworkReply *reply =
        m_nam->get(imageRequest(m_client->imageUrl(parts[0], parts[1], kExportWidth, parts[2])));
    const auto state = boundReply(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, id, subdir, parts, state, partition] {
        drainBounded(reply, state);
        if (!partition || !partition->active.load(std::memory_order_acquire))
            return;
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
    const QPointer<EmbyImageResponse> guardedResponse(response);
    const QPointer<EmbyImageFetcher> guardedFetcher(m_fetcher);
    if (!guardedFetcher) {
        response->complete({}, QStringLiteral("image fetcher unavailable"));
        return response;
    }
    QObject::connect(guardedFetcher, &QObject::destroyed, response,
                     [guardedResponse] {
                         if (guardedResponse)
                             guardedResponse->complete({},
                                                       QStringLiteral("image fetcher unavailable"));
                     },
                     Qt::QueuedConnection);
    // Hop to the GUI thread where the QNAM lives.
    const bool posted = QMetaObject::invokeMethod(
        guardedFetcher,
        [guardedFetcher, guardedResponse, id, requestedSize] {
            if (!guardedResponse)
                return;
            if (!guardedFetcher) {
                guardedResponse->complete({}, QStringLiteral("image fetcher unavailable"));
                return;
            }
            guardedFetcher->fetch(guardedResponse, id, requestedSize);
        });
    if (!posted)
        response->complete({}, QStringLiteral("image fetcher unavailable"));
    return response;
}

} // namespace strmqt
