#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "server/emby/EmbyDtoMapper.h"

using namespace strmqt;

namespace {

QJsonDocument loadFixture(const QString &name)
{
    QFile file(QStringLiteral(STRMQT_FIXTURES_DIR "/") + name);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll());
}

} // namespace

class EmbyDtoTest : public QObject
{
    Q_OBJECT

private slots:
    void authResult();
    void views();
    void itemsPage();
    void resumeEpisode();
    void latestArray();
    void sparseItemUsesDefaults();
    void emptyJsonDoesNotCrash();
    void wideArtPicksPerItemKind();
};

// Which 16:9 source exists depends entirely on the item kind, so a wide card
// cannot just ask for one image type. Shapes below are the ones the live server
// actually returns for a Continue Watching rail.
void EmbyDtoTest::wideArtPicksPerItemKind()
{
    // An EPISODE's Primary *is* its 16:9 still, and it has no Thumb and no
    // backdrop of its own -- it inherits its series'. Drawing that still in a
    // 2:3 poster frame is what made Continue Watching look like cropped
    // screencaps.
    const MediaItem episode = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "1830955", "Name": "Wanheda (2)", "Type": "Episode",
        "ImageTags": { "Primary": "ep-still-tag" },
        "ParentBackdropImageTags": ["series-backdrop-tag"],
        "ParentBackdropItemId": 1830890,
        "SeriesId": "1830890"
    })json").object());
    MediaItem::ImageRef ref = episode.thumbSource();
    QVERIFY(ref.isValid());
    QCOMPARE(ref.itemId, QStringLiteral("1830955"));
    QCOMPARE(ref.imageType, QStringLiteral("Primary"));
    QCOMPARE(ref.tag, QStringLiteral("ep-still-tag"));

    // A MOVIE carries a real Thumb, which beats its poster for a wide card.
    const MediaItem movie = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "1856141", "Name": "Spider-Man: No Way Home", "Type": "Movie",
        "ImageTags": { "Primary": "poster-tag", "Thumb": "thumb-tag", "Logo": "logo-tag" },
        "BackdropImageTags": ["backdrop-tag"]
    })json").object());
    ref = movie.thumbSource();
    QCOMPARE(ref.imageType, QStringLiteral("Thumb"));
    QCOMPARE(ref.tag, QStringLiteral("thumb-tag"));

    // No Thumb: the item's own backdrop is still 16:9. The poster is not.
    const MediaItem noThumb = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "9", "Name": "Some Film", "Type": "Movie",
        "ImageTags": { "Primary": "poster-tag" },
        "BackdropImageTags": ["backdrop-tag"]
    })json").object());
    QCOMPARE(noThumb.thumbSource().imageType, QStringLiteral("Backdrop"));

    // An episode with no still of its own falls back to the SERIES' backdrop,
    // fetched from the parent's id -- not the episode's, which has no such image.
    const MediaItem stillless = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "77", "Name": "Untagged", "Type": "Episode",
        "ParentBackdropImageTags": ["series-backdrop-tag"],
        "ParentBackdropItemId": 1830890
    })json").object());
    ref = stillless.thumbSource();
    QCOMPARE(ref.itemId, QStringLiteral("1830890"));
    QCOMPARE(ref.imageType, QStringLiteral("Backdrop"));

    // Nothing wide at all: invalid, so the caller draws the poster rather than
    // a hole. A movie poster stretched into 16:9 is worse than no wide art.
    const MediaItem posterOnly = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "5", "Name": "Poster Only", "Type": "Series",
        "ImageTags": { "Primary": "poster-tag" }
    })json").object());
    // A non-episode may use its poster as a last resort; an episode may not.
    QCOMPARE(posterOnly.thumbSource().imageType, QStringLiteral("Primary"));
    const MediaItem bareEpisode = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "6", "Name": "Bare", "Type": "Episode"
    })json").object());
    QVERIFY(!bareEpisode.thumbSource().isValid());
}

void EmbyDtoTest::authResult()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("auth_by_name.json"));
    QVERIFY(doc.isObject());

    const SessionInfo session = emby::parseAuthResult(doc.object());
    QVERIFY(session.isValid());
    QCOMPARE(session.accessToken, QStringLiteral("not-a-real-token-fixture-only"));
    QCOMPARE(session.serverId, QStringLiteral("6c17e0e282e84f7c92e8358ebf054111"));
    QCOMPARE(session.user.name, QStringLiteral("mike"));
    QCOMPARE(session.user.id, QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90"));
}

void EmbyDtoTest::views()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("views.json"));
    QVERIFY(doc.isObject());

    const QList<Library> views = emby::parseViews(doc.object());
    QCOMPARE(views.size(), 3);
    QCOMPARE(views[0].name, QStringLiteral("Movies"));
    QCOMPARE(views[0].collectionType, QStringLiteral("movies"));
    QCOMPARE(views[0].primaryImageTag, QStringLiteral("c78d3f5e6a1b2c3d4e5f6a7b8c9d0e1f"));
    QCOMPARE(views[1].collectionType, QStringLiteral("tvshows"));
    // Folder without CollectionType/ImageTags stays usable with defaults.
    QCOMPARE(views[2].collectionType, QString());
    QCOMPARE(views[2].primaryImageTag, QString());
}

void EmbyDtoTest::itemsPage()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("items_movies.json"));
    QVERIFY(doc.isObject());

    const ItemsPage page = emby::parseItemsPage(doc.object());
    QCOMPARE(page.totalRecordCount, 42);
    QCOMPARE(page.startIndex, 0);
    QCOMPARE(page.items.size(), 3);

    const MediaItem &matrix = page.items[0];
    QCOMPARE(matrix.name, QStringLiteral("The Matrix"));
    QCOMPARE(matrix.type, QStringLiteral("Movie"));
    QCOMPARE(matrix.productionYear, 1999);
    QCOMPARE(matrix.communityRating, 8.7);
    QCOMPARE(matrix.runtimeTicks, Q_INT64_C(81840000000));
    QCOMPARE(matrix.runtimeMs(), Q_INT64_C(8184000));
    QVERIFY(matrix.played);
    QVERIFY(matrix.favorite);
    QVERIFY(!matrix.isResumable());
    QCOMPARE(matrix.primaryImageTag, QStringLiteral("aaa111"));
    QCOMPARE(matrix.backdropImageTags,
             (QStringList{QStringLiteral("ccc333"), QStringLiteral("ddd444")}));

    const MediaItem &bladeRunner = page.items[1];
    QVERIFY(bladeRunner.isResumable());
    QCOMPARE(bladeRunner.positionMs(), Q_INT64_C(2340000));
    QVERIFY(bladeRunner.playedPercentage > 23.0 && bladeRunner.playedPercentage < 24.0);
}

void EmbyDtoTest::resumeEpisode()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("resume.json"));
    QVERIFY(doc.isObject());

    const ItemsPage page = emby::parseItemsPage(doc.object());
    QCOMPARE(page.items.size(), 1);
    const MediaItem &episode = page.items[0];
    QCOMPARE(episode.type, QStringLiteral("Episode"));
    QCOMPARE(episode.seriesName, QStringLiteral("Breaking Bad"));
    QCOMPARE(episode.seriesId, QStringLiteral("400001"));
    QCOMPARE(episode.indexNumber, 14);
    QCOMPARE(episode.parentIndexNumber, 5);
    QVERIFY(episode.isResumable());
    QCOMPARE(episode.positionMs(), Q_INT64_C(1200000));
}

void EmbyDtoTest::latestArray()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("latest.json"));
    QVERIFY(doc.isArray());

    const QList<MediaItem> items = emby::parseItemArray(doc.array());
    QCOMPARE(items.size(), 2);
    QCOMPARE(items[0].name, QStringLiteral("Dune: Part Two"));
    QCOMPARE(items[1].type, QStringLiteral("Series"));
}

void EmbyDtoTest::sparseItemUsesDefaults()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("items_movies.json"));
    const ItemsPage page = emby::parseItemsPage(doc.object());
    const MediaItem &sparse = page.items[2];

    QCOMPARE(sparse.id, QStringLiteral("301003"));
    QCOMPARE(sparse.runtimeTicks, Q_INT64_C(0));
    QCOMPARE(sparse.indexNumber, -1);
    QCOMPARE(sparse.parentIndexNumber, -1);
    QCOMPARE(sparse.productionYear, 0);
    QCOMPARE(sparse.communityRating, 0.0);
    QVERIFY(!sparse.played);
    QVERIFY(!sparse.favorite);
    QVERIFY(!sparse.isResumable());
    QCOMPARE(sparse.primaryImageTag, QString());
    QVERIFY(sparse.backdropImageTags.isEmpty());
}

void EmbyDtoTest::emptyJsonDoesNotCrash()
{
    // Total API drift: empty object / wrong shapes must map to default DTOs.
    const MediaItem item = emby::parseMediaItem(QJsonObject());
    QCOMPARE(item.id, QString());

    const SessionInfo session = emby::parseAuthResult(QJsonObject());
    QVERIFY(!session.isValid());

    QVERIFY(emby::parseViews(QJsonObject()).isEmpty());

    const ItemsPage page = emby::parseItemsPage(QJsonObject());
    QVERIFY(page.items.isEmpty());
    QCOMPARE(page.totalRecordCount, 0);
}

QTEST_GUILESS_MAIN(EmbyDtoTest)
#include "tst_emby_dto.moc"
