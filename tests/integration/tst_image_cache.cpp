#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

#include "MockEmbyServer.h"
#include "app/EmbyImageProvider.h"
#include "app/ImageLimits.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;

namespace {

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
const auto kImageId = QStringLiteral("301001/Primary/tag-one");

QByteArray pngBytes(int width, int height, const QColor &colour = Qt::darkCyan)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(colour);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

} // namespace

// The artwork pipeline, which is what a library page spends its time on.
class ImageCacheTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void artworkIsServedFromDiskOnTheSecondAsk();
    void aCorruptCacheEntryFallsBackToTheServer();
    void fetchReturnsBeforeTheImageIsDecoded();
    void largeSourceIsDecodedNearTheRequestedSize();
    void canceledResponseAbortsAndSuppressesDecode();
    void identitySwitchRejectsDelayedOldWork();
    void startupPrunesAnOversizedAssetPartition();

private:
    // Drives one fetch to completion and hands back the image it produced.
    QImage fetchBlocking(const QString &id, const QSize &size = QSize(200, 0));

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    EmbyImageFetcher *m_fetcher = nullptr;
};

void ImageCacheTest::initTestCase()
{
    // Never the real user's cache directory.
    QStandardPaths::setTestModeEnabled(true);
}

void ImageCacheTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Items/301001/Images/Primary"), 200,
                     pngBytes(120, 80), "image/png");

    m_client = new emby::EmbyClient(this);
    m_client->setDeviceId(QStringLiteral("test-device"));
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setSession(kToken, kUserId);
    m_fetcher = new EmbyImageFetcher(m_client, this);
}

void ImageCacheTest::cleanup()
{
    delete m_fetcher;
    delete m_client;
    delete m_mock;
    m_fetcher = nullptr;
    m_client = nullptr;
    m_mock = nullptr;
    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).removeRecursively();
}

QImage ImageCacheTest::fetchBlocking(const QString &id, const QSize &size)
{
    EmbyImageResponse response;
    QSignalSpy finished(&response, &QQuickImageResponse::finished);
    m_fetcher->fetch(&response, id, size);
    if (!finished.wait(5000))
        return {};
    QQuickTextureFactory *factory = response.textureFactory();
    const QImage image = factory != nullptr ? factory->image() : QImage();
    delete factory;
    return image;
}

// Emby's image URLs carry the image tag, so an entry can never be stale: the
// same URL is always the same bytes. That is what makes it safe to answer the
// second ask from disk without revalidating, and it is the difference between
// a warm library page and one that re-fetches every card it scrolls past.
void ImageCacheTest::artworkIsServedFromDiskOnTheSecondAsk()
{
    const QImage first = fetchBlocking(kImageId);
    QVERIFY(!first.isNull());
    QCOMPARE(first.size(), QSize(120, 80));
    QCOMPARE(m_mock->requestCount(), 1);

    const QImage second = fetchBlocking(kImageId);
    QVERIFY(!second.isNull());
    QCOMPARE(second.size(), QSize(120, 80));
    QCOMPARE(m_mock->requestCount(), 1); // nothing went out for it

    // A different requested width is a different server-side image and so a
    // different entry: it must NOT be answered with the cached one.
    QVERIFY(!fetchBlocking(kImageId, QSize(400, 0)).isNull());
    QCOMPARE(m_mock->requestCount(), 2);
}

void ImageCacheTest::aCorruptCacheEntryFallsBackToTheServer()
{
    QVERIFY(!fetchBlocking(kImageId).isNull());
    QCOMPARE(m_mock->requestCount(), 1);

    // Truncate every entry the fetch wrote.
    const QString assets = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                           QStringLiteral("/images");
    QDirIterator it(assets, QDir::Files, QDirIterator::Subdirectories);
    int damaged = 0;
    while (it.hasNext()) {
        QFile file(it.next());
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write("not an image");
            ++damaged;
        }
    }
    QVERIFY(damaged > 0);

    // The image still arrives, and the server was asked again for it.
    QVERIFY(!fetchBlocking(kImageId).isNull());
    QCOMPARE(m_mock->requestCount(), 2);
}

// Decoding used to happen in the reply handler, which is to say on the thread
// that draws: every poster on a library page froze the frame it landed on. What
// is checkable from here is the shape that fixes it — fetch() returns with the
// work outstanding, and the result still arrives on the thread whose listeners
// expect it.
void ImageCacheTest::fetchReturnsBeforeTheImageIsDecoded()
{
    QThread *caller = QThread::currentThread();
    QThread *decodedOn = nullptr;
    connect(m_fetcher, &EmbyImageFetcher::imageDecoded, this,
            [&decodedOn](const QString &, const QImage &) {
                decodedOn = QThread::currentThread();
            });

    EmbyImageResponse response;
    QSignalSpy finished(&response, &QQuickImageResponse::finished);
    m_fetcher->fetch(&response, kImageId, QSize(200, 0));
    // Nothing has been decoded yet: the fetch only started work.
    QCOMPARE(finished.count(), 0);
    QVERIFY(finished.wait(5000));
    // ...and the completion still lands on the thread its listeners expect.
    QCOMPARE(decodedOn, caller);
}

void ImageCacheTest::largeSourceIsDecodedNearTheRequestedSize()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Items/301001/Images/Primary"),
                     200, pngBytes(5'000, 4'000), "image/png");

    const QImage image = fetchBlocking(kImageId, QSize(200, 0));
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(200, 160));
    QVERIFY(qint64(image.width()) * image.height() <= imagelimits::kMaxTargetPixels);
}

void ImageCacheTest::canceledResponseAbortsAndSuppressesDecode()
{
    m_mock->setRouteDelay(QStringLiteral("GET"),
                          QStringLiteral("/Items/301001/Images/Primary"), 500);
    QSignalSpy decoded(m_fetcher, &EmbyImageFetcher::imageDecoded);
    EmbyImageResponse response;
    QSignalSpy finished(&response, &QQuickImageResponse::finished);
    m_fetcher->fetch(&response, kImageId, QSize(200, 0));
    QTRY_COMPARE(m_mock->requestCount(), 1);

    response.cancel();
    QCOMPARE(finished.count(), 1);
    QCOMPARE(response.errorString(), QStringLiteral("request canceled"));
    QTest::qWait(600);
    QCOMPARE(decoded.count(), 0);
}

void ImageCacheTest::identitySwitchRejectsDelayedOldWork()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Items/301001/Images/Primary"),
                     200, pngBytes(120, 80, Qt::red), "image/png");
    m_mock->setRouteDelay(QStringLiteral("GET"),
                          QStringLiteral("/Items/301001/Images/Primary"), 500);
    QSignalSpy decoded(m_fetcher, &EmbyImageFetcher::imageDecoded);
    EmbyImageResponse oldResponse;
    QSignalSpy oldFinished(&oldResponse, &QQuickImageResponse::finished);
    m_fetcher->fetch(&oldResponse, kImageId, QSize(200, 0));
    QTRY_COMPARE(m_mock->requestCount(), 1);

    m_client->setSession(kToken, QStringLiteral("second-user"));
    m_mock->setRouteDelay(QStringLiteral("GET"),
                          QStringLiteral("/Items/301001/Images/Primary"), 0);
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Items/301001/Images/Primary"),
                     200, pngBytes(120, 80, Qt::blue), "image/png");
    const QImage current = fetchBlocking(kImageId);

    QVERIFY(!current.isNull());
    QCOMPARE(current.pixelColor(0, 0), QColor(Qt::blue));
    QTRY_COMPARE(oldFinished.count(), 1);
    QCOMPARE(decoded.count(), 1);
    QCOMPARE(decoded.at(0).at(2).toULongLong(), m_fetcher->cachePartitionGeneration());
}

void ImageCacheTest::startupPrunesAnOversizedAssetPartition()
{
    delete m_fetcher;
    m_fetcher = nullptr;

    const QByteArray identity =
        m_client->baseUrl().toString(QUrl::FullyEncoded).toUtf8() + '\0' + kUserId.toUtf8();
    const QString partition = QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    QDir assets(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                QStringLiteral("/images/") + partition + QStringLiteral("/assets"));
    QVERIFY(assets.mkpath(QStringLiteral(".")));
    for (int i = 0; i < 2; ++i) {
        QFile file(assets.filePath(QStringLiteral("sparse-%1").arg(i)));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(300LL * 1024 * 1024));
        QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(-60 * (i + 1)),
                                 QFileDevice::FileModificationTime));
    }

    m_fetcher = new EmbyImageFetcher(m_client, this);
    const auto totalSize = [&assets] {
        qint64 total = 0;
        for (const QFileInfo &info : assets.entryInfoList(QDir::Files))
            total += info.size();
        return total;
    };
    QTRY_VERIFY_WITH_TIMEOUT(totalSize() <= 512LL * 1024 * 1024, 5000);
}

QTEST_MAIN(ImageCacheTest)
#include "tst_image_cache.moc"
