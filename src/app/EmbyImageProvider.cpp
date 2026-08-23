#include "EmbyImageProvider.h"

#include "core/Log.h"
#include "server/emby/EmbyClient.h"

#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QStandardPaths>

namespace strmqt {

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
    auto *cache = new QNetworkDiskCache(m_nam);
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                             QStringLiteral("/images"));
    cache->setMaximumCacheSize(256 * 1024 * 1024);
    m_nam->setCache(cache);
    m_nam->setAutoDeleteReplies(true);
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
    const QUrl url = m_client->imageUrl(parts[0], parts[1], maxWidth, parts[2]);

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    request.setTransferTimeout(15'000);
    if (!m_client->accessToken().isEmpty())
        request.setRawHeader("X-Emby-Token", m_client->accessToken().toUtf8());

    QNetworkReply *reply = m_nam->get(request);
    // Context must be `this` (GUI thread): the reply is auto-deleted right after
    // finished() is delivered on this thread, so handling it queued on the
    // response's QQuickPixmapReader thread would be use-after-free. The engine
    // keeps `response` alive until it emits finished(), which is thread-safe.
    connect(reply, &QNetworkReply::finished, this, [reply, response] {
        if (reply->error() != QNetworkReply::NoError) {
            response->complete({}, reply->errorString());
            return;
        }
        QImage image;
        if (!image.loadFromData(reply->readAll())) {
            response->complete({}, QStringLiteral("undecodable image"));
            return;
        }
        response->complete(std::move(image), {});
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
