#pragma once

#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>

class QNetworkAccessManager;

namespace strmqt {

namespace emby {
class EmbyClient;
}

class EmbyImageResponse : public QQuickImageResponse
{
    Q_OBJECT

public:
    QQuickTextureFactory *textureFactory() const override;
    QString errorString() const override;

    // Called on the fetcher (GUI) thread; emits finished().
    void complete(QImage image, const QString &error);

private:
    QImage m_image;
    QString m_error;
};

// Lives on the GUI thread; performs the actual HTTP fetch with the session token
// and a disk cache, downscaled server-side via maxWidth.
class EmbyImageFetcher : public QObject
{
    Q_OBJECT

public:
    explicit EmbyImageFetcher(emby::EmbyClient *client, QObject *parent = nullptr);

    Q_INVOKABLE void fetch(strmqt::EmbyImageResponse *response, const QString &id,
                           const QSize &requestedSize);

private:
    emby::EmbyClient *m_client;
    QNetworkAccessManager *m_nam;
};

// Resolves image://emby/{itemId}/{imageType}/{tag} (see MediaItemModel role URLs).
class EmbyImageProvider : public QQuickAsyncImageProvider
{
public:
    // fetcher must outlive the provider's responses; owned by the caller (Application).
    explicit EmbyImageProvider(EmbyImageFetcher *fetcher);

    QQuickImageResponse *requestImageResponse(const QString &id,
                                              const QSize &requestedSize) override;

private:
    EmbyImageFetcher *m_fetcher;
};

} // namespace strmqt
