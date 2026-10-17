#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "server/dto/ItemsQuery.h"
#include "server/dto/PlaybackTicket.h"
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
    void specialsFollowTheSeasons();
    void authResult();
    void views();
    void itemsPage();
    void resumeEpisode();
    void latestArray();
    void sparseItemUsesDefaults();
    void emptyJsonDoesNotCrash();
    void wideArtPicksPerItemKind();
    void squareArtPrefersTheAlbumCover();
    void clearLogoIsOwnThenTheSeries();
    void musicGenresMapAndPageOnTheArray();
    void artistIdsLineUpWithTheNamesTheyBelongTo();
    void streamUrlKeepsPercentEncodedQueryValues();
    void externalSubtitleDeliveryIsResolvedForTheEngineOnly();
    void subtitleDeliveryFieldsAreOptional();
    void externalSubtitleUrlsAreVetted_data();
    void externalSubtitleUrlsAreVetted();
};

// /MusicGenres, from a payload recorded off the live 4.9.5.0 server.
//
// Two things this guards, and the second is the one that has already burned a
// caller of the sibling endpoint:
//
//  1. A genre is an ordinary item — id, name, Type "MusicGenre" — so it maps
//     through the same parser everything else does, including the row whose
//     ImageTags object is present but empty.
//  2. **Page on the array's own size, never on TotalRecordCount.** The recorded
//     payload happens to tell the truth (289, and /Genres does not — see
//     ARCHITECTURE.md §2), but a StartIndex past the end answers 0 with an
//     empty array, and a server that answered 0 with rows would render an empty
//     filter for a library full of genres. The second half of this test is that
//     shape.
void EmbyDtoTest::musicGenresMapAndPageOnTheArray()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("music_genres.json"));
    QVERIFY(doc.isObject());

    const ItemsPage page = emby::parseItemsPage(doc.object());
    QCOMPARE(page.items.size(), 4);
    QCOMPARE(page.items.at(0).id, QStringLiteral("1937444"));
    QCOMPARE(page.items.at(0).name, QStringLiteral("Acid"));
    QCOMPARE(page.items.at(0).type, QStringLiteral("MusicGenre"));
    QCOMPARE(page.items.at(0).primaryImageTag,
             QStringLiteral("d2ffa659c4998fc6d4d14ff0aa03ab95"));
    // "Afro-pop": ImageTags present but empty. It is still a genre, and a filter
    // that dropped it because it has no artwork would be missing a row of a list
    // artwork has nothing to do with.
    QCOMPARE(page.items.at(3).name, QStringLiteral("Afro-pop"));
    QCOMPARE(page.items.at(3).primaryImageTag, QString());
    // Truthful here — recorded, not assumed.
    QCOMPARE(page.totalRecordCount, 289);

    // The /Genres shape, which this family of endpoints can produce: rows with a
    // count of zero beside them. The rows are the answer.
    const ItemsPage lying = emby::parseItemsPage(QJsonDocument::fromJson(R"json({
        "Items": [
            { "Id": "1", "Name": "Doom Metal", "Type": "MusicGenre" },
            { "Id": "2", "Name": "Drone", "Type": "MusicGenre" }
        ],
        "TotalRecordCount": 0
    })json").object());
    QCOMPARE(lying.totalRecordCount, 0);
    QCOMPARE(lying.items.size(), 2);
    QCOMPARE(lying.items.at(1).name, QStringLiteral("Drone"));

    // The end of the walk: no rows, whatever the count says.
    const ItemsPage past = emby::parseItemsPage(QJsonDocument::fromJson(R"json({
        "Items": [], "TotalRecordCount": 0
    })json").object());
    QVERIFY(past.items.isEmpty());
}

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

// ClearLogo. Hand-written payloads in the BaseItemDto shape: a film or series
// carries ImageTags.Logo; an episode or season carries none of its own and
// names its series' through ParentLogoItemId / ParentLogoImageTag, the same
// pairing as the parent backdrop (which this server sends as a number).
void EmbyDtoTest::clearLogoIsOwnThenTheSeries()
{
    const MediaItem episode = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "402015", "Name": "Granite State", "Type": "Episode",
        "SeriesId": "400001",
        "ImageTags": { "Primary": "still-tag" },
        "ParentLogoItemId": 400001,
        "ParentLogoImageTag": "series-logo-tag"
    })json").object());
    QVERIFY(episode.logoImageTag.isEmpty());
    QCOMPARE(episode.parentLogoItemId, QStringLiteral("400001"));
    MediaItem::ImageRef ref = episode.logoSource();
    QVERIFY(ref.isValid());
    // The series' tag against the SERIES' id: the episode has no such image.
    QCOMPARE(ref.itemId, QStringLiteral("400001"));
    QCOMPARE(ref.imageType, QStringLiteral("Logo"));
    QCOMPARE(ref.tag, QStringLiteral("series-logo-tag"));

    // An item's own logo wins over a parent's.
    const MediaItem season = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "400500", "Name": "Season 5", "Type": "Season",
        "ImageTags": { "Logo": "season-logo-tag" },
        "ParentLogoItemId": "400001", "ParentLogoImageTag": "series-logo-tag"
    })json").object());
    QCOMPARE(season.logoSource().itemId, QStringLiteral("400500"));
    QCOMPARE(season.logoSource().tag, QStringLiteral("season-logo-tag"));

    // A tag without the item it belongs to addresses nothing, and SeriesId is
    // never used to guess one: the server says when the series has a logo.
    const MediaItem orphanTag = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "402016", "Type": "Episode", "SeriesId": "400001",
        "ParentLogoImageTag": "series-logo-tag"
    })json").object());
    QVERIFY(!orphanTag.logoSource().isValid());

    // Wrong types on the wire are absence, not a crash (AGENTS.md).
    const MediaItem drift = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "9", "Type": "Movie", "ImageTags": { "Logo": 17 },
        "ParentLogoImageTag": ["x"]
    })json").object());
    QVERIFY(!drift.logoSource().isValid());
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
    QCOMPARE(matrix.logoImageTag, QStringLiteral("bbb222"));
    QCOMPARE(matrix.logoSource().itemId, QStringLiteral("301001"));
    QCOMPARE(matrix.logoSource().imageType, QStringLiteral("Logo"));
    // No logo on the wire is the common case, and it is no logo — not a guess.
    QVERIFY(!page.items[1].logoSource().isValid());

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

// `Artists` and `ArtistItems` are two different lists, not one list twice:
// Emby credits performers by name whether or not it has an artist item for
// them. Appending the ids in ArtistItems order therefore paired a name with
// whatever id happened to land at the same index, and "go to artist" read one
// artist and navigated to another.
void EmbyDtoTest::artistIdsLineUpWithTheNamesTheyBelongTo()
{
    const MediaItem track = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "90210", "Name": "Threnody", "Type": "Audio",
        "AlbumArtist": "Main Band",
        "Artists": ["Featured Guest", "Main Band"],
        "ArtistItems": [ { "Id": "ar-band", "Name": "Main Band" } ]
    })json").object());
    QCOMPARE(track.artists,
             (QStringList{QStringLiteral("Featured Guest"), QStringLiteral("Main Band")}));
    // The band's id sits under the band's name; the guest, whom the server has
    // no artist item for, keeps a hole rather than borrowing it.
    QCOMPARE(track.artistIds, (QStringList{QString(), QStringLiteral("ar-band")}));

    // An artist item the name list did not mention is still credited, and still
    // brings its own id with it.
    const MediaItem extra = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "90211", "Name": "Storm", "Type": "Audio",
        "Artists": ["Main Band"],
        "ArtistItems": [ { "Id": "ar-band", "Name": "Main Band" },
                         { "Id": "ar-guest", "Name": "Featured Guest" } ]
    })json").object());
    QCOMPARE(extra.artists,
             (QStringList{QStringLiteral("Main Band"), QStringLiteral("Featured Guest")}));
    QCOMPARE(extra.artistIds,
             (QStringList{QStringLiteral("ar-band"), QStringLiteral("ar-guest")}));

    // Names with no ArtistItems array at all: no ids, and nothing invented.
    const MediaItem namesOnly = emby::parseMediaItem(QJsonDocument::fromJson(R"json({
        "Id": "90212", "Name": "Loose", "Type": "Audio",
        "Artists": ["Main Band"]
    })json").object());
    QCOMPARE(namesOnly.artists, QStringList{QStringLiteral("Main Band")});
    QVERIFY(namesOnly.artistIds.isEmpty());
}

// The server hands back a finished stream URL and the client's whole job is to
// put a host in front of it. A value the server percent-encoded has to arrive
// as the same bytes: decoding `%26` to `&` on the way through turns one
// parameter into two, and `%2B` to `+` turns a plus into a space.
void EmbyDtoTest::streamUrlKeepsPercentEncodedQueryValues()
{
    const QString query =
        QStringLiteral("DeviceId=test-device&Tag=a%26b%3Dc&Plus=1%2B1&Path=%2Fmnt%2Fa%20b"
                       "&Pct=100%25&Hash=a%23b&Name=caf%C3%A9&api_key=tok%2Ben");
    QJsonObject source;
    source.insert(QLatin1String("Id"), QStringLiteral("ms1"));
    source.insert(QLatin1String("SupportsTranscoding"), true);
    source.insert(QLatin1String("TranscodingUrl"),
                  QStringLiteral("/videos/1/master.m3u8?") + query);
    source.insert(QLatin1String("TranscodingContainer"), QStringLiteral("ts"));
    QJsonObject json;
    json.insert(QLatin1String("MediaSources"), QJsonArray{source});
    json.insert(QLatin1String("PlaySessionId"), QStringLiteral("ps1"));

    const PlaybackTicket ticket =
        emby::parsePlaybackTicket(json, QUrl(QStringLiteral("https://server.example/emby")),
                                  QStringLiteral("1"), QStringLiteral("other-token"),
                                  QStringLiteral("test-device"));
    QVERIFY(ticket.isValid());
    const StreamCandidate *hls = ticket.candidate(0, 0);
    QVERIFY(hls);
    QCOMPARE(hls->method, PlayMethod::Transcode);
    QCOMPARE(hls->url.path(), QStringLiteral("/emby/videos/1/master.m3u8"));
    // Byte for byte what the server sent, and no second api_key appended.
    QCOMPARE(hls->url.query(QUrl::FullyEncoded), query);
    QCOMPARE(QString::fromLatin1(hls->url.toEncoded()),
             QStringLiteral("https://server.example/emby/videos/1/master.m3u8?") + query);
}

namespace {

const MediaStream *streamWithIndex(const MediaSource &source, int index)
{
    for (const MediaStream &stream : source.streams) {
        if (stream.index == index)
            return &stream;
    }
    return nullptr;
}

} // namespace

// With "Method": "External" declared, the server stops delivering a sidecar
// subtitle inside the stream and names a URL for it instead. That URL is what
// the player engine fetches, so it has to come out absolute and authenticated
// exactly like a stream URL — and, being a credential, come out nowhere else.
void EmbyDtoTest::externalSubtitleDeliveryIsResolvedForTheEngineOnly()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("playback_info_external_subtitle.json"));
    QVERIFY(doc.isObject());
    const QString token = QStringLiteral("session-token");
    const PlaybackTicket ticket =
        emby::parsePlaybackTicket(doc.object(), QUrl(QStringLiteral("https://server.example/emby")),
                                  QStringLiteral("7700"), token, QStringLiteral("test-device"));
    QVERIFY(ticket.isValid());
    const MediaSource &source = ticket.sources.at(0).source;
    // The subtitle the server chose for this user, by MediaStream index.
    QCOMPARE(source.defaultSubtitleStreamIndex, 3);

    // A sidecar whose DeliveryUrl already carries a key keeps the server's own,
    // behind the reverse-proxy base path, with no second key appended.
    const MediaStream *dutch = streamWithIndex(source, 3);
    QVERIFY(dutch);
    QVERIFY(dutch->isExternal);
    QCOMPARE(dutch->deliveryMethod, QStringLiteral("External"));
    QCOMPARE(dutch->deliveryUrl,
             QStringLiteral("/Videos/7700/ms7700/Subtitles/3/0/Stream.srt"
                            "?api_key=not-a-real-token-fixture-only"));
    QVERIFY(dutch->isDeliveredExternally());
    QCOMPARE(QString::fromLatin1(dutch->externalUrl.toEncoded()),
             QStringLiteral("https://server.example/emby/Videos/7700/ms7700/Subtitles/3/0/"
                            "Stream.srt?api_key=not-a-real-token-fixture-only"));

    // One that carries none is given the session's.
    const MediaStream *english = streamWithIndex(source, 4);
    QVERIFY(english);
    QVERIFY(english->isDeliveredExternally());
    QCOMPARE(QString::fromLatin1(english->externalUrl.toEncoded()),
             QStringLiteral("https://server.example/emby/Videos/7700/ms7700/Subtitles/4/0/"
                            "Stream.ass?api_key=session-token"));

    // Streams delivered in the file have nothing to fetch.
    for (int index : {2, 5}) {
        const MediaStream *embedded = streamWithIndex(source, index);
        QVERIFY(embedded);
        QCOMPARE(embedded->deliveryMethod, QStringLiteral("Embed"));
        QVERIFY(!embedded->isDeliveredExternally());
        QVERIFY(embedded->externalUrl.isEmpty());
    }
    // Neither do video and audio, whatever a server writes next to them.
    QVERIFY(!streamWithIndex(source, 0)->isDeliveredExternally());
    QVERIFY(!streamWithIndex(source, 1)->isDeliveredExternally());

    // The map is what QML and the web remote's status JSON are built from. No
    // token in it, the server's or ours, and no delivery URL at all.
    const QByteArray published =
        QJsonDocument::fromVariant(source.toVariantMap()).toJson(QJsonDocument::Compact);
    QVERIFY(published.contains("Dutch (SRT)"));
    QVERIFY2(!published.contains("api_key"), published.constData());
    QVERIFY2(!published.contains("not-a-real-token-fixture-only"), published.constData());
    QVERIFY2(!published.contains("session-token"), published.constData());
    QVERIFY2(!published.contains("Stream.srt"), published.constData());

    // Parsed on its own — an item's details, where there is no base URL and no
    // session to resolve against — the stream says what the server said and
    // offers nothing to load.
    const MediaSource bare = emby::parseMediaSource(
        doc.object().value(QLatin1String("MediaSources")).toArray().at(0).toObject());
    QCOMPARE(streamWithIndex(bare, 3)->deliveryMethod, QStringLiteral("External"));
    QVERIFY(!streamWithIndex(bare, 3)->isDeliveredExternally());
}

// Every recorded PlaybackInfo predates these fields. Their absence, and any
// nonsense in their place, is "the server did not say".
void EmbyDtoTest::subtitleDeliveryFieldsAreOptional()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("playback_info_multi_version.json"));
    QVERIFY(doc.isObject());
    const PlaybackTicket ticket =
        emby::parsePlaybackTicket(doc.object(), QUrl(QStringLiteral("https://server.example")),
                                  QStringLiteral("4242"), QStringLiteral("t"),
                                  QStringLiteral("test-device"));
    QVERIFY(ticket.isValid());
    for (const MediaSourceCandidates &entry : ticket.sources) {
        QCOMPARE(entry.source.defaultSubtitleStreamIndex, -1);
        for (const MediaStream &stream : entry.source.streams) {
            QVERIFY(stream.deliveryMethod.isEmpty());
            QVERIFY(stream.deliveryUrl.isEmpty());
            QVERIFY(!stream.isDeliveredExternally());
        }
    }

    const QJsonObject wrong{
        {QLatin1String("Type"), QStringLiteral("Subtitle")},
        {QLatin1String("Index"), 2},
        {QLatin1String("DeliveryMethod"), 7},
        {QLatin1String("DeliveryUrl"), QJsonArray{QStringLiteral("/x.srt")}},
        {QLatin1String("IsExternalUrl"), QStringLiteral("yes")},
    };
    const QJsonObject source{
        {QLatin1String("Id"), QStringLiteral("ms1")},
        {QLatin1String("Container"), QStringLiteral("mkv")},
        {QLatin1String("SupportsDirectPlay"), true},
        {QLatin1String("DefaultSubtitleStreamIndex"), QStringLiteral("two")},
        {QLatin1String("MediaStreams"), QJsonArray{wrong}},
    };
    const PlaybackTicket odd = emby::parsePlaybackTicket(
        QJsonObject{{QLatin1String("MediaSources"), QJsonArray{source}}},
        QUrl(QStringLiteral("https://server.example")), QStringLiteral("1"), QStringLiteral("t"),
        QStringLiteral("test-device"));
    QVERIFY(odd.isValid());
    QCOMPARE(odd.sources.at(0).source.defaultSubtitleStreamIndex, -1);
    const MediaStream &stream = odd.sources.at(0).source.streams.at(0);
    QVERIFY(stream.deliveryMethod.isEmpty());
    QVERIFY(stream.deliveryUrl.isEmpty());
    QVERIFY(!stream.isExternalUrl);
    QVERIFY(!stream.isDeliveredExternally());
}

void EmbyDtoTest::externalSubtitleUrlsAreVetted_data()
{
    QTest::addColumn<QString>("type");
    QTest::addColumn<QString>("method");
    QTest::addColumn<QString>("deliveryUrl");
    QTest::addColumn<bool>("isExternalUrl");
    QTest::addColumn<QString>("expected"); // empty: nothing for the engine to load

    const QString sub = QStringLiteral("Subtitle");
    QTest::newRow("relative, ours")
        << sub << QStringLiteral("External") << QStringLiteral("/Videos/1/ms1/Subtitles/2/0/Stream.vtt")
        << false << QStringLiteral("https://server.example/emby/Videos/1/ms1/Subtitles/2/0/Stream.vtt?api_key=tok");
    QTest::newRow("method spelled in lower case")
        << sub << QStringLiteral("external") << QStringLiteral("/s.srt") << false
        << QStringLiteral("https://server.example/emby/s.srt?api_key=tok");
    // A subtitle hosted elsewhere is fetched from there as it is. Our token is
    // for our server and is never sent to another host.
    QTest::newRow("another host gets no token")
        << sub << QStringLiteral("External") << QStringLiteral("https://subs.example/a/b.srt?k=v")
        << true << QStringLiteral("https://subs.example/a/b.srt?k=v");
    // An absolute URL the server does not flag as foreign is treated as a path
    // on our own server: the token still only ever goes to the configured host.
    QTest::newRow("unflagged absolute URL stays on our host")
        << sub << QStringLiteral("External") << QStringLiteral("https://evil.example/Videos/s.srt")
        << false << QStringLiteral("https://server.example/emby/Videos/s.srt?api_key=tok");
    // The engine opens anything it is handed. The server names http(s) only.
    QTest::newRow("local file") << sub << QStringLiteral("External")
                                << QStringLiteral("file:///etc/passwd") << true << QString();
    QTest::newRow("no host") << sub << QStringLiteral("External")
                             << QStringLiteral("/relative/but/flagged.srt") << true << QString();
    QTest::newRow("other scheme") << sub << QStringLiteral("External")
                                  << QStringLiteral("smb://nas/share/a.srt") << true << QString();
    QTest::newRow("external without a URL")
        << sub << QStringLiteral("External") << QString() << false << QString();
    QTest::newRow("embedded with a URL")
        << sub << QStringLiteral("Embed") << QStringLiteral("/s.srt") << false << QString();
    QTest::newRow("burned in") << sub << QStringLiteral("Encode") << QStringLiteral("/s.srt")
                               << false << QString();
    QTest::newRow("hls") << sub << QStringLiteral("Hls") << QStringLiteral("/s.m3u8") << false
                         << QString();
    QTest::newRow("not a subtitle") << QStringLiteral("Audio") << QStringLiteral("External")
                                    << QStringLiteral("/s.srt") << false << QString();
}

void EmbyDtoTest::externalSubtitleUrlsAreVetted()
{
    QFETCH(QString, type);
    QFETCH(QString, method);
    QFETCH(QString, deliveryUrl);
    QFETCH(bool, isExternalUrl);
    QFETCH(QString, expected);

    const QJsonObject stream{
        {QLatin1String("Type"), type},
        {QLatin1String("Index"), 2},
        {QLatin1String("DeliveryMethod"), method},
        {QLatin1String("DeliveryUrl"), deliveryUrl},
        {QLatin1String("IsExternalUrl"), isExternalUrl},
    };
    const QJsonObject source{
        {QLatin1String("Id"), QStringLiteral("ms1")},
        {QLatin1String("Container"), QStringLiteral("mkv")},
        {QLatin1String("SupportsDirectPlay"), true},
        {QLatin1String("MediaStreams"), QJsonArray{stream}},
    };
    const PlaybackTicket ticket = emby::parsePlaybackTicket(
        QJsonObject{{QLatin1String("MediaSources"), QJsonArray{source}}},
        QUrl(QStringLiteral("https://server.example/emby")), QStringLiteral("1"),
        QStringLiteral("tok"), QStringLiteral("test-device"));
    QVERIFY(ticket.isValid());
    const MediaStream &parsed = ticket.sources.at(0).source.streams.at(0);
    QCOMPARE(QString::fromLatin1(parsed.externalUrl.toEncoded()), expected);
    QCOMPARE(parsed.isDeliveredExternally(), !expected.isEmpty());
}

QTEST_GUILESS_MAIN(EmbyDtoTest)
// The server sorts season 0 first. A special is not where a series starts.
void EmbyDtoTest::specialsFollowTheSeasons()
{
    const auto episode = [](int season, int number) {
        MediaItem item;
        item.type = QStringLiteral("Episode");
        item.parentIndexNumber = season;
        item.indexNumber = number;
        return item;
    };
    QList<MediaItem> items{episode(0, 1), episode(0, 2), episode(1, 1), episode(1, 2),
                             episode(2, 1)};
    moveSpecialsLast(items);
    QCOMPARE(items.at(0).parentIndexNumber, 1);
    QCOMPARE(items.at(0).indexNumber, 1);
    QCOMPARE(items.at(2).parentIndexNumber, 2);
    QCOMPARE(items.at(3).parentIndexNumber, 0);
    QCOMPARE(items.at(3).indexNumber, 1); // their own order is kept
    QCOMPARE(items.at(4).indexNumber, 2);
}

#include "tst_emby_dto.moc"
