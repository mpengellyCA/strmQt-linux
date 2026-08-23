#pragma once

#include <QImage>
#include <QNetworkRequest>
#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QUrl>

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

    // Writes one image to a file another *process* can open, and reports its
    // URL through fileExported().
    //
    // The disk cache below already holds these bytes, but QNetworkDiskCache's
    // filenames are an implementation detail — hashed, versioned, and with no
    // supported way to ask for one — so they cannot be handed out as a file://
    // URL. MPRIS's mpris:artUrl is exactly that: Plasma's applet, the lock
    // screen and the notification daemon open the path themselves.
    //
    // This is a genuine second download, not just a second copy: the export
    // asks for a fixed 512 px while fetch() asks for whatever width the
    // delegate wants, maxWidth is part of the query string, and so the two are
    // different cache entries. Deliberate — the sleeve on a HiDPI lock screen
    // wants the resolution, and with every grid delegate requesting its own
    // size there is no single width to share.
    //
    // `id` is the same "{itemId}/{imageType}/{tag}" the provider takes. Files
    // land in <CacheLocation>/<subdir>/, one per image tag: reusing a single
    // filename would be smaller, but clients cache thumbnails by URL, so the
    // panel would keep drawing the previous track's sleeve. The directory is
    // pruned instead, oldest first.
    void exportToFile(const QString &id, const QString &subdir);

signals:
    // Empty url when the image could not be fetched or decoded — callers omit
    // the artwork rather than publishing a URI that resolves to nothing.
    void fileExported(const QString &id, const QUrl &fileUrl);

    // Every cover the interface draws, at the moment it decodes. CoverTintService
    // samples these for the wash (MUSIC.md §4, Rule 2) rather than downloading
    // the sleeve a third time: the hero, the docked bar and the album header are
    // already drawing the exact image whose colour they want, and exportToFile()
    // above is a genuine second fetch precisely because it could not share one.
    // `id` is the same "{itemId}/{imageType}/{tag}" fetch() takes.
    void imageDecoded(const QString &id, const QImage &image);

private:
    QNetworkRequest imageRequest(const QUrl &url) const;

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
