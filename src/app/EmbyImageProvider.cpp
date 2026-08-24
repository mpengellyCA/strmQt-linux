#include "EmbyImageProvider.h"

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

#include <memory>

namespace strmqt {

namespace {

// Wide enough for a lock screen and a media notification on a HiDPI panel;
// small enough that the extra on-disk copy is a few tens of KB.
constexpr int kExportWidth = 512;
// Roughly an album's worth of distinct covers. Bounded, and small enough that
// listing the directory to prune it is cheaper than the write it follows.
constexpr int kMaxExportedFiles = 24;
constexpr qint64 kMaxEncodedBytes = 16 * 1024 * 1024;
constexpr qint64 kMaxDecodedPixels = 20'000'000;
constexpr int kMaxImageDimension = 8192;

struct BoundedReplyState
{
    QByteArray bytes;
    bool overflow = false;
};

void drainBounded(QNetworkReply *reply, const std::shared_ptr<BoundedReplyState> &state)
{
    if (state->overflow)
        return;
    const qint64 remaining = kMaxEncodedBytes - state->bytes.size();
    state->bytes += reply->read(remaining + 1);
    if (state->bytes.size() > kMaxEncodedBytes) {
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
    const QSize size = reader->size();
    return size.isValid() && size.width() <= kMaxImageDimension &&
           size.height() <= kMaxImageDimension &&
           qint64(size.width()) * size.height() <= kMaxDecodedPixels;
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
    connect(m_client, &emby::EmbyClient::identityChanged, this,
            &EmbyImageFetcher::resetCachePartition);
    resetCachePartition();
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
    const QByteArray identity = m_client->baseUrl().toString(QUrl::FullyEncoded).toUtf8() + '\0' +
                                m_client->userId().toUtf8();
    const QString partition = QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    m_cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                               QStringLiteral("/images/") + partition);
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

void EmbyImageFetcher::fetch(EmbyImageResponse *response, const QString &id,
                             const QSize &requestedSize)
{
    // id = "{itemId}/{imageType}/{tag}"
    const QStringList parts = id.split(QLatin1Char('/'));
    if (parts.size() != 3) {
        response->complete({}, QStringLiteral("bad image id: %1").arg(id));
        return;
    }

    const int maxWidth = requestedSize.width() > 0 ? requestedSize.width() : 480;
    QNetworkReply *reply =
        m_nam->get(imageRequest(m_client->imageUrl(parts[0], parts[1], maxWidth, parts[2])));
    const auto state = boundReply(reply);
    // Context must be `this` (GUI thread): the reply is auto-deleted right after
    // finished() is delivered on this thread, so handling it queued on the
    // response's QQuickPixmapReader thread would be use-after-free. The engine
    // keeps `response` alive until it emits finished(), which is thread-safe.
    connect(reply, &QNetworkReply::finished, this, [this, reply, response, id, state] {
        drainBounded(reply, state);
        if (state->overflow) {
            response->complete({}, QStringLiteral("image response exceeds size limit"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            response->complete({}, reply->errorString());
            return;
        }
        QImage image;
        if (!decodeBounded(state->bytes, &image)) {
            response->complete({}, QStringLiteral("undecodable or oversized image"));
            return;
        }
        // Before the move: QImage is implicitly shared, so the listener gets a
        // reference to the same pixels rather than a copy of them.
        emit imageDecoded(id, image);
        response->complete(std::move(image), {});
    });
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
