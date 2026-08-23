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
    void squareArtPrefersTheAlbumCover();
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

// The square is the unit for music, and for a TRACK the trustworthy square is
// the album's, not the one the ripper embedded in the file. This inversion is
// audio-only: everywhere else an item's own Primary is the only poster it has.
void EmbyDtoTest::squareArtPrefersTheAlbumCover()
{
    // A CD rip carries no embedded art at all. Before this chain existed the
    // queue, the mini player and the album's own track rows all drew holes.
    const MediaItem bareTrack = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "90210", "Name": "Threnody", "Type": "Audio",
        "AlbumId": "88001", "Album": "Lift Yr Skinny Fists",
        "AlbumPrimaryImageTag": "album-cover-tag"
    })json").object());
    MediaItem::ImageRef ref = bareTrack.coverSource();
    QVERIFY(ref.isValid());
    // The album's tag against the ALBUM's id: fetching it from the track's id
    // is a URL for an image that does not exist.
    QCOMPARE(ref.itemId, QStringLiteral("88001"));
    QCOMPARE(ref.imageType, QStringLiteral("Primary"));
    QCOMPARE(ref.tag, QStringLiteral("album-cover-tag"));

    // A track that DOES have embedded art still yields to the album: per-file
    // art is frequently a low-res scan or a different pressing, and one record
    // whose tracks came from several sources is what made a queue look like a
    // ransom note.
    const MediaItem taggedTrack = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "90211", "Name": "Storm", "Type": "Audio",
        "ImageTags": { "Primary": "embedded-tag" },
        "AlbumId": "88001", "AlbumPrimaryImageTag": "album-cover-tag"
    })json").object());
    QCOMPARE(taggedTrack.coverSource().tag, QStringLiteral("album-cover-tag"));

    // No album cover: the parent, then the file's own art. Last resort, but a
    // resort — a single loose track has nothing else.
    const MediaItem orphan = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "90212", "Name": "Loose", "Type": "Audio",
        "ImageTags": { "Primary": "embedded-tag" }
    })json").object());
    ref = orphan.coverSource();
    QCOMPARE(ref.itemId, QStringLiteral("90212"));
    QCOMPARE(ref.tag, QStringLiteral("embedded-tag"));

    const MediaItem viaParent = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "90213", "Name": "Inherited", "Type": "Audio",
        "ParentPrimaryImageItemId": 88002, "ParentPrimaryImageTag": "parent-tag"
    })json").object());
    ref = viaParent.coverSource();
    QCOMPARE(ref.itemId, QStringLiteral("88002"));
    QCOMPARE(ref.tag, QStringLiteral("parent-tag"));

    // An AlbumId with no tag beside it is not an image: fetching it would 404.
    const MediaItem noArt = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "90214", "Name": "Nothing", "Type": "Audio", "AlbumId": "88001"
    })json").object());
    QVERIFY(!noArt.coverSource().isValid());

    // An ALBUM uses its own cover first, and only then the artist's photo.
    const MediaItem album = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "88001", "Name": "Lift Yr Skinny Fists", "Type": "MusicAlbum",
        "ImageTags": { "Primary": "album-cover-tag" },
        "ParentPrimaryImageItemId": 77001, "ParentPrimaryImageTag": "artist-tag"
    })json").object());
    QCOMPARE(album.coverSource().tag, QStringLiteral("album-cover-tag"));
    const MediaItem coverless = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "88002", "Name": "Coverless", "Type": "MusicAlbum",
        "ParentPrimaryImageItemId": 77001, "ParentPrimaryImageTag": "artist-tag"
    })json").object());
    QCOMPARE(coverless.coverSource().itemId, QStringLiteral("77001"));

    // Video is untouched. A movie's own Primary is the only poster it has, and
    // a movie inside a box-set folder must not start wearing the folder's art.
    const MediaItem movie = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "5", "Name": "Some Film", "Type": "Movie",
        "ImageTags": { "Primary": "poster-tag" },
        "ParentPrimaryImageItemId": 4, "ParentPrimaryImageTag": "collection-tag"
    })json").object());
    QCOMPARE(movie.coverSource().itemId, QStringLiteral("5"));
    QCOMPARE(movie.coverSource().tag, QStringLiteral("poster-tag"));
    const MediaItem posterless = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "7", "Name": "Posterless", "Type": "Movie",
        "ParentPrimaryImageItemId": 4, "ParentPrimaryImageTag": "collection-tag"
    })json").object());
    QVERIFY(!posterless.coverSource().isValid());
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
