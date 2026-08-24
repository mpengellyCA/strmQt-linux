#pragma once

#include <QImage>
#include <QMutex>
#include <QNetworkRequest>
#include <QThreadPool>
#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkDiskCache;

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
    ~EmbyImageFetcher() override;

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
    // Decoding used to happen in the reply handler, on the GUI thread: a
    // library page brings dozens of posters in at once and each JPEG decode
    // froze the frame it landed on, which is the whole of the "loading a
    // library page stutters" complaint. Decode and disk I/O both run here now
    // and only the finished QImage goes back to the GUI thread.
    //
    // Deliberately small: the point is to keep the UI thread free, not to win
    // a decode race, and sixteen concurrent full-size decodes is a memory
    // spike for no gain.
    void decodeAsync(EmbyImageResponse *response, const QString &id, const QString &cachePath,
                     QByteArray bytes, bool storeToCache, const QSize &requestedSize);
    void fetchFromNetwork(EmbyImageResponse *response, const QString &id,
                          const QSize &requestedSize);
    // <cache>/images/<identity>/assets/<sha256 of the URL>. The URL carries the
    // item, the image type, the server-side width AND Emby's image tag, so an
    // entry can never be stale: new artwork is a new tag is a new file. That is
    // what makes this cache safe to keep without revalidating, which is the
    // difference between a warm page and one that re-fetches on every visit.
    QString assetPathFor(const QUrl &url) const;
    void storeAsset(const QString &path, const QByteArray &bytes);
    void pruneAssetsIfNeeded();

    QNetworkRequest imageRequest(const QUrl &url) const;
    void resetCachePartition();

    emby::EmbyClient *m_client;
    QNetworkAccessManager *m_nam;
    QNetworkDiskCache *m_cache = nullptr;
    QString m_assetDir;
    QMutex m_assetMutex; // serialises pruning and the write counter
    int m_assetWrites = 0;
    QStringList m_exportDirectories;
    // Declared last and drained explicitly in the destructor, so no worker can
    // still be holding this object when its members start going away.
    QThreadPool m_decodePool;
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
