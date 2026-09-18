#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSignalSpy>
#include <QTimeZone>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "app/controllers/music/AlbumController.h"
#include "app/controllers/music/ArtistController.h"
#include "app/models/MediaItemModel.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"
#include "server/emby/MusicServerCapabilities.h"

using namespace strmqt;
using namespace strmqt::music;

namespace {

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
const auto kLibrary = QStringLiteral("1868998");
constexpr qint64 kTicksPerMinute = 60 * 10'000'000LL;

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

QString itemsPath() { return QStringLiteral("/Users/%1/Items").arg(kUserId); }
QString itemPath(const QString &id) { return itemsPath() + QLatin1Char('/') + id; }

QByteArray page(const QJsonArray &items)
{
    return QJsonDocument(QJsonObject{{"Items", items}, {"TotalRecordCount", items.size()}})
        .toJson(QJsonDocument::Compact);
}

QByteArray object(const QJsonObject &json)
{
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

QJsonArray refs(const QString &id, const QString &name)
{
    return {QJsonObject{{"Id", id}, {"Name", name}}};
}

QJsonObject flac2496()
{
    return {{"Type", "Audio"}, {"Codec", "flac"}, {"BitDepth", 24}, {"SampleRate", 96000}};
}

// One five-minute track filed under Hollow Coves (ar1). `withFormat` false
// sends no streams at all, so the album has no format summary.
QJsonObject trackJson(const QString &id, const QString &albumId, int disc, int number,
                      const QString &artistId = QStringLiteral("ar1"),
                      const QString &artistName = QStringLiteral("Hollow Coves"),
                      bool withFormat = true)
{
    QJsonObject track{{"Id", id}, {"Name", "Track " + id}, {"Type", "Audio"},
                      {"AlbumId", albumId}, {"Album", "Album " + albumId},
                      {"ParentIndexNumber", disc}, {"IndexNumber", number},
                      {"RunTimeTicks", 5 * kTicksPerMinute},
                      {"ArtistItems", refs(artistId, artistName)},
                      {"AlbumArtists", refs(QStringLiteral("ar1"), QStringLiteral("Hollow Coves"))}};
    if (withFormat) {
        track.insert("MediaStreams", QJsonArray{flac2496()});
        track.insert("MediaSources",
                     QJsonArray{QJsonObject{{"MediaStreams", QJsonArray{flac2496()}}}});
    }
    return track;
}

// A top track on `albumTitle`, filed under `albumArtistId`.
QJsonObject topTrackJson(const QString &id, const QString &albumId, const QString &albumTitle,
                         const QString &albumArtistId, const QString &albumArtistName)
{
    return {{"Id", id}, {"Name", "Track " + id}, {"Type", "Audio"},
            {"AlbumId", albumId}, {"Album", albumTitle},
            {"RunTimeTicks", 4 * kTicksPerMinute},
            {"ArtistItems", refs(QStringLiteral("ar1"), QStringLiteral("Hollow Coves"))},
            {"AlbumArtists", refs(albumArtistId, albumArtistName)}};
}

// One audio stream, so a fixture can mix formats within an album.
QJsonObject audioStream(const QString &codec, int bitDepth, int sampleRate, int bitrate = 0)
{
    QJsonObject stream{{"Type", "Audio"}, {"Codec", codec}};
    if (bitDepth > 0)
        stream.insert("BitDepth", bitDepth);
    if (sampleRate > 0)
        stream.insert("SampleRate", sampleRate);
    if (bitrate > 0)
        stream.insert("BitRate", bitrate);
    return stream;
}

QJsonObject trackWithStream(const QString &id, const QString &albumId, int number,
                            const QJsonObject &stream)
{
    QJsonObject track = trackJson(id, albumId, 1, number, QStringLiteral("ar1"),
                                  QStringLiteral("Hollow Coves"), false);
    track.insert("MediaStreams", QJsonArray{stream});
    return track;
}

QJsonObject albumJson(const QString &id, const QString &name, int trackCount,
                      const QString &artistId = QStringLiteral("ar1"),
                      const QString &artistName = QStringLiteral("Hollow Coves"))
{
    return {{"Id", id}, {"Name", name}, {"Type", "MusicAlbum"},
            {"AlbumArtists", refs(artistId, artistName)},
            {"ChildCount", trackCount},
            {"CumulativeRunTimeTicks", trackCount * 5 * kTicksPerMinute},
            {"ImageTags", QJsonObject{{"Primary", "tag-" + id}}}};
}

QStringList queueIds(PlayQueue *queue)
{
    QStringList ids;
    for (int row = 0; row < queue->rowCount(); ++row)
        ids.append(queue->itemAt(row).value(QStringLiteral("itemId")).toString());
    return ids;
}

QVariantMap noteFor(const QVariantList &notes, const QString &label)
{
    for (const QVariant &row : notes) {
        if (row.toMap().value(QStringLiteral("label")).toString() == label)
            return row.toMap();
    }
    return {};
}

using Q = QList<QPair<QString, QString>>;

} // namespace

class AlbumArtistControllersTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void albumDisplayComesFromTheSleeve();
    void linerNotesOmitEmptyRows();
    void albumDropsStaleReplies();
    void albumFailureShowsErrorAndRetryRecovers();
    void albumVerbsCarrySourceLabels();
    void albumFavouriteFollowsItemActions();
    void albumReopenIsFreeAndResetClears();
    void hiResFollowsTheBadgeItPaints();
    void hiResIsUnanimousOnABareBadge();

    void artistProfileFillsTabsAndCaptions();
    void artistHidesEmptyTabs();
    void artistDropsStaleRepliesAndRecoversFromFailure();
    void artistVerbsCarrySourceLabels();
    void artistReopenIsFreeAndResetClears();

private:
    PlayQueue *queue() const { return m_player->queue(); }
    void routeFullAlbum();
    void routeBareAlbum(const QString &albumId);
    void routeMixedAlbum(const QString &albumId, const QJsonArray &tracks);
    void routeArtist();

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_player = nullptr;
    ItemActions *m_actions = nullptr;
    MusicRepository *m_repo = nullptr;
    MusicPlayback *m_playback = nullptr;
    AlbumController *m_album = nullptr;
    ArtistController *m_artist = nullptr;
    QDateTime m_now = QDateTime::fromSecsSinceEpoch(1'800'000'000, QTimeZone::utc());
};

void AlbumArtistControllersTest::initTestCase()
{
    // "14 April 2023" is a locale decision. Pin it so the test does not depend
    // on the machine it runs on.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
    // coverUrl()/backdropUrl() go through the emby image provider, which
    // yields nothing until a session namespace is set (as Application does).
    setEmbyImageSourceNamespace(QStringLiteral("t"));
}

void AlbumArtistControllersTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    const QStringList playable{"t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8",
                               "m1", "m2", "hit", "feature"};
    for (const QString &id : playable) {
        QVERIFY(m_mock->addRouteFromFile("POST", QStringLiteral("/Items/%1/PlaybackInfo").arg(id),
                                         fixturePath(QStringLiteral("playback_info.json"))));
    }
    m_mock->addRoute("POST", "/Sessions/Playing", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Progress", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Stopped", 204, {});

    m_client = new emby::EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setDeviceId(QStringLiteral("test-device"));
    m_client->setSession(kToken, kUserId);

    m_backend = new FakePlayerBackend(this);
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_settings = new Settings(m_dir->filePath(QStringLiteral("settings.ini")), this);
    m_player = new PlayerController(m_client, m_backend, m_settings, this);
    m_actions = new ItemActions(m_client, m_player, this);
    m_repo = new MusicRepository(m_client, this);
    m_repo->setClockForTests([this] { return m_now; });
    m_playback = new MusicPlayback(m_repo, m_actions, this);
    m_album = new AlbumController(m_repo, m_playback, this);
    m_album->setClockForTests([this] { return m_now; });
    m_artist = new ArtistController(m_repo, m_playback, this);
}

void AlbumArtistControllersTest::cleanup()
{
    delete m_artist;
    delete m_album;
    delete m_playback;
    delete m_repo;
    delete m_actions;
    delete m_player;
    delete m_settings;
    delete m_dir;
    delete m_backend;
    delete m_client;
    delete m_mock;
    m_artist = nullptr;
    m_album = nullptr;
    m_playback = nullptr;
    m_repo = nullptr;
    m_actions = nullptr;
    m_player = nullptr;
    m_settings = nullptr;
    m_dir = nullptr;
    m_backend = nullptr;
    m_client = nullptr;
    m_mock = nullptr;
}

// "Sunburned Almanac": 8 FLAC 24/96 tracks over two discs, the last one by a
// guest, plus one other album by the same artist.
void AlbumArtistControllersTest::routeFullAlbum()
{
    QJsonObject album = albumJson(QStringLiteral("al1"), QStringLiteral("Sunburned Almanac"), 8);
    album.insert("PremiereDate", "2023-04-14T00:00:00.0000000Z");
    album.insert("ProductionYear", 2023);
    album.insert("GenreItems", QJsonArray{QJsonObject{{"Id", "g1"}, {"Name", "Post-rock"}},
                                          QJsonObject{{"Id", "g2"}, {"Name", "Ambient"}}});
    album.insert("Studios", QJsonArray{QJsonObject{{"Name", "Driftwood Records"}}});
    album.insert("DateCreated", m_now.addDays(-21).toString(Qt::ISODate));
    album.insert("UserData", QJsonObject{{"PlayCount", 7}, {"IsFavorite", false}});
    m_mock->addRoute("GET", itemPath(QStringLiteral("al1")), 200, object(album));

    QJsonArray tracks;
    for (int i = 1; i <= 8; ++i) {
        const QString id = QStringLiteral("t%1").arg(i);
        const int disc = i <= 4 ? 1 : 2;
        const int number = i <= 4 ? i : i - 4;
        tracks.append(i == 8 ? trackJson(id, "al1", disc, number, "ar9", "Kin")
                             : trackJson(id, "al1", disc, number));
    }
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", "al1"}, {"IncludeItemTypes", "Audio"}},
                          200, page(tracks));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"AlbumArtistIds", "ar1"}, {"IncludeItemTypes", "MusicAlbum"}}, 200,
                          page({albumJson("al1", "Sunburned Almanac", 8),
                                albumJson("al2", "Low Tide", 10)}));
}

// Two tracks, no dates, no genres, no label, no streams, no play count.
void AlbumArtistControllersTest::routeBareAlbum(const QString &albumId)
{
    m_mock->addRoute("GET", itemPath(albumId), 200,
                     object(albumJson(albumId, QStringLiteral("Bare Bones"), 2)));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", albumId}, {"IncludeItemTypes", "Audio"}},
                          200,
                          page({trackJson(albumId + "-1", albumId, 1, 1, "ar1", "Hollow Coves", false),
                                trackJson(albumId + "-2", albumId, 1, 2, "ar1", "Hollow Coves", false)}));
}

// An album whose tracks do not share one format.
void AlbumArtistControllersTest::routeMixedAlbum(const QString &albumId, const QJsonArray &tracks)
{
    m_mock->addRoute("GET", itemPath(albumId), 200,
                     object(albumJson(albumId, QStringLiteral("Mixed Bag"),
                                      static_cast<int>(tracks.size()))));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"ParentId", albumId}, {"IncludeItemTypes", "Audio"}}, 200,
                          page(tracks));
}

void AlbumArtistControllersTest::routeArtist()
{
    m_mock->addRoute("GET", itemPath(QStringLiteral("ar1")), 200,
                     object({{"Id", "ar1"}, {"Name", "Hollow Coves"}, {"Type", "MusicArtist"},
                             {"SongCount", 71},
                             {"ImageTags", QJsonObject{{"Primary", "tag-ar1"}}},
                             {"BackdropImageTags", QJsonArray{"bd-ar1"}}}));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"AlbumArtistIds", "ar1"}, {"IncludeItemTypes", "MusicAlbum"}}, 200,
                          page({albumJson("lp", "Album lp", 10), albumJson("ep", "Album ep", 5),
                                albumJson("single", "Album single", 2)}));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"ArtistIds", "ar1"}, {"IncludeItemTypes", "MusicAlbum"}}, 200,
                          page({albumJson("lp", "Album lp", 10),
                                albumJson("guest", "Album guest", 9, "ar9", "Kin")}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ArtistIds", "ar1"}, {"IncludeItemTypes", "Audio"}},
                          200,
                          page({topTrackJson("hit", "lp", "Album lp", "ar1", "Hollow Coves"),
                                topTrackJson("feature", "guest", "Album guest", "ar9", "Kin")}));
    if (emby::caps::kSimilarArtists) {
        const QString similarPath = QString::fromLatin1(emby::caps::kSimilarArtistsPath)
                                        .replace(QStringLiteral("{id}"), QStringLiteral("ar1"));
        m_mock->addRoute("GET", similarPath, 200,
                         page({QJsonObject{{"Id", "ar2"}, {"Name", "Kin"}, {"Type", "MusicArtist"}}}));
    }
}

void AlbumArtistControllersTest::albumDisplayComesFromTheSleeve()
{
    routeFullAlbum();

    m_album->open(QStringLiteral("al1"), QStringLiteral("Sunburned Almanac"));
    QCOMPARE(m_album->albumId(), QStringLiteral("al1"));
    QCOMPARE(m_album->title(), QStringLiteral("Sunburned Almanac")); // the pending name
    QVERIFY(m_album->loading());

    QTRY_VERIFY(!m_album->loading());
    QVERIFY2(m_album->error().isEmpty(), qPrintable(m_album->error()));
    QCOMPARE(m_album->artist(), QStringLiteral("Hollow Coves"));
    QCOMPARE(m_album->artistId(), QStringLiteral("ar1"));
    QCOMPARE(m_album->kicker(), QStringLiteral("Album · 2023"));
    QVERIFY(!m_album->coverUrl().isEmpty());
    QCOMPARE(m_album->formatBadge(), QStringLiteral("FLAC 24/96"));
    QVERIFY(m_album->isHiRes());
    QCOMPARE(m_album->discCount(), 2);
    QCOMPARE(m_album->discBadge(), QStringLiteral("2 DISCS"));
    QCOMPARE(m_album->trackSummary(), QStringLiteral("8 tracks · 40 min"));
    QVERIFY(m_album->showArtistColumn());

    QCOMPARE(m_album->tracks()->count(), 8);
    // The whole order, not just its head: discs() places the side headings by
    // row, so a re-sort that kept t1 first (by track number, forgetting the
    // disc) would interleave the discs under headings that still say 0 and 4.
    QCOMPARE(m_album->trackIds(),
             (QStringList{QStringLiteral("t1"), QStringLiteral("t2"), QStringLiteral("t3"),
                          QStringLiteral("t4"), QStringLiteral("t5"), QStringLiteral("t6"),
                          QStringLiteral("t7"), QStringLiteral("t8")}));

    const QVariantList discs = m_album->discs();
    QCOMPARE(discs.size(), 2);
    QCOMPARE(discs.at(0).toMap().value("title").toString(), QStringLiteral("Side A"));
    QCOMPARE(discs.at(0).toMap().value("firstRow").toInt(), 0);
    QCOMPARE(discs.at(1).toMap().value("title").toString(), QStringLiteral("Side B"));
    QCOMPARE(discs.at(1).toMap().value("detail").toString(), QStringLiteral("Disc 2 · 20 min"));
    QCOMPARE(discs.at(1).toMap().value("number").toInt(), 2);
    QCOMPARE(discs.at(1).toMap().value("firstRow").toInt(), 4);

    const QVariantList notes = m_album->linerNotes();
    QCOMPARE(notes.size(), 5);
    QCOMPARE(noteFor(notes, "Released").value("value").toString(), QStringLiteral("14 April 2023"));
    const QVariantMap genre = noteFor(notes, "Genre");
    QCOMPARE(genre.value("value").toString(), QStringLiteral("Post-rock · Ambient"));
    const QVariantList links = genre.value("links").toList();
    QCOMPARE(links.size(), 2);
    QCOMPARE(links.at(0).toMap().value("id").toString(), QStringLiteral("g1"));
    QCOMPARE(links.at(0).toMap().value("name").toString(), QStringLiteral("Post-rock"));
    QCOMPARE(noteFor(notes, "Label").value("value").toString(), QStringLiteral("Driftwood Records"));
    QCOMPARE(noteFor(notes, "Format").value("value").toString(), QStringLiteral("FLAC 24/96"));
    QCOMPARE(noteFor(notes, "Added").value("value").toString(),
             QStringLiteral("3 weeks ago · played 7×"));
    QVERIFY(noteFor(notes, "Label").value("links").toList().isEmpty());

    QCOMPARE(m_album->moreBy()->count(), 1); // al1 itself is excluded
    QCOMPARE(m_album->moreByTitle(), QStringLiteral("More by Hollow Coves"));
    QCOMPARE(m_album->albumItem().value("itemId").toString(), QStringLiteral("al1"));
}

void AlbumArtistControllersTest::linerNotesOmitEmptyRows()
{
    routeBareAlbum(QStringLiteral("bare"));

    m_album->open(QStringLiteral("bare"), QStringLiteral("Bare Bones"));
    QTRY_VERIFY(!m_album->loading());
    QVERIFY2(m_album->error().isEmpty(), qPrintable(m_album->error()));

    QVERIFY(m_album->linerNotes().isEmpty());
    QVERIFY(m_album->formatBadge().isEmpty());
    QVERIFY(!m_album->isHiRes());
    QVERIFY(m_album->discBadge().isEmpty());
    QVERIFY(m_album->discs().isEmpty());
    QVERIFY(!m_album->showArtistColumn());
    QCOMPARE(m_album->moreBy()->count(), 0);
    QCOMPARE(m_album->tracks()->count(), 2);
}

void AlbumArtistControllersTest::albumDropsStaleReplies()
{
    routeFullAlbum();
    routeBareAlbum(QStringLiteral("al2"));
    m_mock->setRouteDelay("GET", itemPath(QStringLiteral("al1")), 300);

    m_album->open(QStringLiteral("al1"), QStringLiteral("Sunburned Almanac"));
    m_album->open(QStringLiteral("al2"), QStringLiteral("Bare Bones"));
    QCOMPARE(m_album->title(), QStringLiteral("Bare Bones"));

    QTRY_VERIFY(!m_album->loading());
    QCOMPARE(m_album->tracks()->count(), 2);

    // al1's reply lands now. It belongs to a superseded generation.
    QTest::qWait(450);
    QCOMPARE(m_album->albumId(), QStringLiteral("al2"));
    QCOMPARE(m_album->title(), QStringLiteral("Bare Bones"));
    QCOMPARE(m_album->tracks()->count(), 2);
    QVERIFY(!m_album->loading());
}

void AlbumArtistControllersTest::albumFailureShowsErrorAndRetryRecovers()
{
    m_mock->addRoute("GET", itemPath(QStringLiteral("gone")), 500, "{}");

    m_album->open(QStringLiteral("gone"), QStringLiteral("Gone"));
    QTRY_VERIFY(!m_album->loading());
    QVERIFY(!m_album->error().isEmpty());
    QCOMPARE(m_album->tracks()->count(), 0);
    QCOMPARE(m_album->title(), QStringLiteral("Gone"));

    routeBareAlbum(QStringLiteral("gone"));
    m_album->retry();
    QVERIFY(m_album->loading());
    QTRY_VERIFY(!m_album->loading());
    QVERIFY2(m_album->error().isEmpty(), qPrintable(m_album->error()));
    QCOMPARE(m_album->tracks()->count(), 2);
    QCOMPARE(m_album->title(), QStringLiteral("Bare Bones"));
}

void AlbumArtistControllersTest::albumVerbsCarrySourceLabels()
{
    routeFullAlbum();
    m_mock->addRoute("GET", "/Items/al1/InstantMix", 200,
                     page({trackJson("m1", "x", 1, 1), trackJson("m2", "x", 1, 2)}));
    m_album->open(QStringLiteral("al1"), QStringLiteral("Sunburned Almanac"));
    QTRY_VERIFY(!m_album->loading());

    m_album->play(2);
    QTRY_COMPARE(queue()->rowCount(), 8);
    QCOMPARE(queue()->currentIndex(), 2);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Sunburned Almanac"));

    m_album->play(99); // clamped to the last row
    QTRY_COMPARE(queue()->currentIndex(), 7);

    m_album->shuffle();
    QTRY_COMPARE(queue()->sourceLabel(), QStringLiteral("Shuffle · Sunburned Almanac"));
    QCOMPARE(queue()->rowCount(), 8);

    m_album->radio();
    QTRY_COMPARE(queue()->sourceLabel(), QStringLiteral("Radio · Sunburned Almanac"));
    QCOMPARE(queueIds(queue()), (QStringList{QStringLiteral("m1"), QStringLiteral("m2")}));
}

void AlbumArtistControllersTest::albumFavouriteFollowsItemActions()
{
    routeFullAlbum();
    m_album->open(QStringLiteral("al1"), QStringLiteral("Sunburned Almanac"));
    QTRY_VERIFY(!m_album->loading());
    QVERIFY(!m_album->favourite());

    QSignalSpy spy(m_album, &AlbumController::favouriteChanged);
    m_album->noteFavourite(QStringLiteral("someone-else"), true);
    QCOMPARE(spy.count(), 0);

    m_album->noteFavourite(QStringLiteral("al1"), true);
    QCOMPARE(spy.count(), 1);
    QVERIFY(m_album->favourite());
    QVERIFY(m_album->albumItem().value("favorite").toBool());

    m_album->noteFavourite(QStringLiteral("al1"), true); // unchanged: no signal
    QCOMPARE(spy.count(), 1);
}

void AlbumArtistControllersTest::albumReopenIsFreeAndResetClears()
{
    routeFullAlbum();
    m_album->open(QStringLiteral("al1"), QStringLiteral("Sunburned Almanac"));
    QTRY_VERIFY(!m_album->loading());

    const int before = m_mock->requestCount();
    // The sleeve is cached, so a needless refetch would send nothing: watch
    // stateChanged too, which only a second load() would emit.
    QSignalSpy state(m_album, &AlbumController::stateChanged);
    m_album->open(QStringLiteral("al1"), QStringLiteral("Sunburned Almanac"));
    QVERIFY(!m_album->loading());
    QCOMPARE(m_mock->requestCount(), before);
    QCOMPARE(state.count(), 0);

    m_album->resetSessionState();
    QVERIFY(m_album->albumId().isEmpty());
    QVERIFY(m_album->title().isEmpty());
    QCOMPARE(m_album->tracks()->count(), 0);
    QCOMPARE(m_album->moreBy()->count(), 0);
    QVERIFY(!m_album->loading());
}

// formatBadge names one format and isHiRes colours that same badge, so the two
// must be one decision. Both halves of this test are albums where a plurality
// (which names the badge) and a majority (the rule this controller used to
// apply) disagree.
void AlbumArtistControllersTest::hiResFollowsTheBadgeItPaints()
{
    // Two 24/96 tracks name the badge; only half the album is hi-res.
    routeMixedAlbum(QStringLiteral("mixA"),
                    {trackWithStream("mx1", "mixA", 1, audioStream("flac", 24, 96000)),
                     trackWithStream("mx2", "mixA", 2, audioStream("flac", 24, 96000)),
                     trackWithStream("mx3", "mixA", 3, audioStream("flac", 16, 44100)),
                     trackWithStream("mx4", "mixA", 4, audioStream("alac", 16, 44100))});

    m_album->open(QStringLiteral("mixA"), QStringLiteral("Mixed Bag"));
    QTRY_VERIFY(!m_album->loading());
    QVERIFY2(m_album->error().isEmpty(), qPrintable(m_album->error()));
    QCOMPARE(m_album->formatBadge(), QStringLiteral("FLAC 24/96"));
    QVERIFY(m_album->isHiRes()); // a strict majority over the tracks would say no

    // And the other way: three different hi-res formats, none of them the
    // badge, against two identical lossy tracks that are.
    routeMixedAlbum(QStringLiteral("mixB"),
                    {trackWithStream("mx5", "mixB", 1, audioStream("flac", 24, 96000)),
                     trackWithStream("mx6", "mixB", 2, audioStream("flac", 24, 192000)),
                     trackWithStream("mx7", "mixB", 3, audioStream("flac", 24, 88200)),
                     trackWithStream("mx8", "mixB", 4, audioStream("mp3", 0, 44100, 320000)),
                     trackWithStream("mx9", "mixB", 5, audioStream("mp3", 0, 44100, 320000))});

    m_album->open(QStringLiteral("mixB"), QStringLiteral("Mixed Bag"));
    QTRY_VERIFY(!m_album->loading());
    QVERIFY2(m_album->error().isEmpty(), qPrintable(m_album->error()));
    QCOMPARE(m_album->formatBadge(), QStringLiteral("MP3 320"));
    QVERIFY(!m_album->isHiRes()); // a strict majority over the tracks would say yes
}

// A lossless track missing BitDepth or SampleRate degrades its badge to the
// bare codec label, which promises nothing about the rate: two tracks can both
// wear "FLAC" while only one of them is hi-res. isHiRes() must require every
// track carrying that bare badge to be hi-res, in either track order, so the
// answer does not turn on which track happens to load first.
void AlbumArtistControllersTest::hiResIsUnanimousOnABareBadge()
{
    // Hi-res track first.
    routeMixedAlbum(QStringLiteral("degA"),
                    {trackWithStream("dg1", "degA", 1, audioStream("flac", 0, 96000)),
                     trackWithStream("dg2", "degA", 2, audioStream("flac", 0, 44100))});

    m_album->open(QStringLiteral("degA"), QStringLiteral("Mixed Bag"));
    QTRY_VERIFY(!m_album->loading());
    QVERIFY2(m_album->error().isEmpty(), qPrintable(m_album->error()));
    QCOMPARE(m_album->formatBadge(), QStringLiteral("FLAC"));
    QVERIFY(!m_album->isHiRes());

    // Same pair, non-hi-res track first: the answer must not flip.
    routeMixedAlbum(QStringLiteral("degB"),
                    {trackWithStream("dg3", "degB", 1, audioStream("flac", 0, 44100)),
                     trackWithStream("dg4", "degB", 2, audioStream("flac", 0, 96000))});

    m_album->open(QStringLiteral("degB"), QStringLiteral("Mixed Bag"));
    QTRY_VERIFY(!m_album->loading());
    QVERIFY2(m_album->error().isEmpty(), qPrintable(m_album->error()));
    QCOMPARE(m_album->formatBadge(), QStringLiteral("FLAC"));
    QVERIFY(!m_album->isHiRes());
}

void AlbumArtistControllersTest::artistProfileFillsTabsAndCaptions()
{
    routeArtist();

    m_artist->open(QStringLiteral("ar1"), QStringLiteral("Hollow Coves"), kLibrary);
    QCOMPARE(m_artist->name(), QStringLiteral("Hollow Coves"));
    QVERIFY(m_artist->loading());
    QTRY_VERIFY(!m_artist->loading());
    QVERIFY2(m_artist->error().isEmpty(), qPrintable(m_artist->error()));

    QCOMPARE(m_artist->artistId(), QStringLiteral("ar1"));
    QCOMPARE(m_artist->libraryId(), kLibrary);
    QCOMPARE(m_artist->kicker(), QStringLiteral("Artist · 3 records · 71 tracks"));
    QVERIFY(!m_artist->coverUrl().isEmpty());
    QVERIFY(!m_artist->backdropUrl().isEmpty());
    QCOMPARE(m_artist->artistItem().value("itemId").toString(), QStringLiteral("ar1"));

    const QVariantList tabs = m_artist->tabs();
    QCOMPARE(tabs.size(), 3);
    QCOMPARE(tabs.at(0).toMap().value("key").toString(), QStringLiteral("albums"));
    QCOMPARE(tabs.at(0).toMap().value("label").toString(), QStringLiteral("Albums"));
    QCOMPARE(tabs.at(0).toMap().value("count").toInt(), 1);
    QCOMPARE(tabs.at(1).toMap().value("key").toString(), QStringLiteral("epsAndSingles"));
    QCOMPARE(tabs.at(1).toMap().value("label").toString(), QStringLiteral("EPs & Singles"));
    QCOMPARE(tabs.at(1).toMap().value("count").toInt(), 2);
    QCOMPARE(tabs.at(2).toMap().value("key").toString(), QStringLiteral("appearsOn"));
    QCOMPARE(tabs.at(2).toMap().value("label").toString(), QStringLiteral("Appears on"));
    QCOMPARE(tabs.at(2).toMap().value("count").toInt(), 1);

    QCOMPARE(m_artist->albums()->count(), 1);
    QCOMPARE(m_artist->epsAndSingles()->count(), 2);
    QCOMPARE(m_artist->appearsOn()->count(), 1);
    QCOMPARE(m_artist->topTracks()->count(), 2);
    QCOMPARE(m_artist->topTrackCaptions(),
             (QStringList{QStringLiteral("Album lp"), QStringLiteral("Guest on Album guest")}));
    QCOMPARE(m_artist->similar()->count(), emby::caps::kSimilarArtists ? 1 : 0);

    m_artist->playTopTrack(1);
    QTRY_COMPARE(queueIds(queue()), (QStringList{QStringLiteral("hit"), QStringLiteral("feature")}));
    QCOMPARE(queue()->currentIndex(), 1);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Most played · Hollow Coves"));
}

void AlbumArtistControllersTest::artistHidesEmptyTabs()
{
    m_mock->addRoute("GET", itemPath(QStringLiteral("ar2")), 200,
                     object({{"Id", "ar2"}, {"Name", "Solo"}, {"Type", "MusicArtist"}}));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"AlbumArtistIds", "ar2"}, {"IncludeItemTypes", "MusicAlbum"}}, 200,
                          page({albumJson("solo-lp", "Solo LP", 11, "ar2", "Solo")}));
    // Appears-on, top tracks and similar are not routed: each 404s, and a
    // secondary failure comes back empty.

    m_artist->open(QStringLiteral("ar2"), QStringLiteral("Solo"));
    QTRY_VERIFY(!m_artist->loading());
    QVERIFY2(m_artist->error().isEmpty(), qPrintable(m_artist->error()));

    const QVariantList tabs = m_artist->tabs();
    QCOMPARE(tabs.size(), 1);
    QCOMPARE(tabs.at(0).toMap().value("key").toString(), QStringLiteral("albums"));
    QCOMPARE(m_artist->kicker(), QStringLiteral("Artist · 1 record"));
    QVERIFY(m_artist->topTrackCaptions().isEmpty());
    QVERIFY(m_artist->libraryId().isEmpty());
}

void AlbumArtistControllersTest::artistDropsStaleRepliesAndRecoversFromFailure()
{
    routeArtist();
    m_mock->addRoute("GET", itemPath(QStringLiteral("ar2")), 200,
                     object({{"Id", "ar2"}, {"Name", "Solo"}, {"Type", "MusicArtist"}}));
    m_mock->setRouteDelay("GET", itemPath(QStringLiteral("ar1")), 300);

    m_artist->open(QStringLiteral("ar1"), QStringLiteral("Hollow Coves"));
    m_artist->open(QStringLiteral("ar2"), QStringLiteral("Solo"));
    QTRY_VERIFY(!m_artist->loading());
    QTest::qWait(450);
    QCOMPARE(m_artist->artistId(), QStringLiteral("ar2"));
    QCOMPARE(m_artist->name(), QStringLiteral("Solo"));
    QCOMPARE(m_artist->albums()->count(), 0);

    m_artist->open(QStringLiteral("nobody"), QStringLiteral("Nobody"));
    QTRY_VERIFY(!m_artist->loading());
    QVERIFY(!m_artist->error().isEmpty());

    m_mock->addRoute("GET", itemPath(QStringLiteral("nobody")), 200,
                     object({{"Id", "nobody"}, {"Name", "Somebody"}, {"Type", "MusicArtist"}}));
    m_artist->retry();
    QTRY_VERIFY(!m_artist->loading());
    QVERIFY2(m_artist->error().isEmpty(), qPrintable(m_artist->error()));
    QCOMPARE(m_artist->name(), QStringLiteral("Somebody"));
}

void AlbumArtistControllersTest::artistVerbsCarrySourceLabels()
{
    // Top tracks are deliberately not routed: their query also carries
    // ArtistIds=ar1 and IncludeItemTypes=Audio, and the random draw below must
    // be the only route that answers it.
    m_mock->addRoute("GET", itemPath(QStringLiteral("ar1")), 200,
                     object({{"Id", "ar1"}, {"Name", "Hollow Coves"}, {"Type", "MusicArtist"}}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ArtistIds", "ar1"}, {"SortBy", "Random"}}, 200,
                          page({trackJson("m2", "x", 1, 2), trackJson("m1", "x", 1, 1)}));
    m_mock->addRoute("GET", "/Items/ar1/InstantMix", 200,
                     page({trackJson("m1", "x", 1, 1), trackJson("m2", "x", 1, 2)}));

    m_artist->open(QStringLiteral("ar1"), QStringLiteral("Hollow Coves"));
    QTRY_VERIFY(!m_artist->loading());

    m_artist->shuffle();
    QTRY_COMPARE(queueIds(queue()), (QStringList{QStringLiteral("m2"), QStringLiteral("m1")}));
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Shuffle · Hollow Coves"));

    m_artist->radio();
    QTRY_COMPARE(queue()->sourceLabel(), QStringLiteral("Radio · Hollow Coves"));

    QSignalSpy spy(m_artist, &ArtistController::favouriteChanged);
    m_artist->noteFavourite(QStringLiteral("someone-else"), true);
    QCOMPARE(spy.count(), 0);
    m_artist->noteFavourite(QStringLiteral("ar1"), true);
    QCOMPARE(spy.count(), 1);
    QVERIFY(m_artist->favourite());
}

void AlbumArtistControllersTest::artistReopenIsFreeAndResetClears()
{
    routeArtist();
    m_artist->open(QStringLiteral("ar1"), QStringLiteral("Hollow Coves"));
    QTRY_VERIFY(!m_artist->loading());
    QVERIFY2(m_artist->error().isEmpty(), qPrintable(m_artist->error()));
    QCOMPARE(m_artist->albums()->count(), 1);
    QVERIFY(m_artist->libraryId().isEmpty());

    // The same artist under the music-library route: free, but the library id
    // is part of the route and must follow it.
    QSignalSpy identity(m_artist, &ArtistController::artistChanged);
    QSignalSpy state(m_artist, &ArtistController::stateChanged);
    m_artist->open(QStringLiteral("ar1"), QStringLiteral("Hollow Coves"), kLibrary);
    QVERIFY(!m_artist->loading());
    QCOMPARE(state.count(), 0); // only a second load() emits this
    QCOMPARE(m_artist->libraryId(), kLibrary);
    QCOMPARE(identity.count(), 1);
    QCOMPARE(m_artist->albums()->count(), 1);

    // Reopening it again, unchanged, costs nothing at all.
    const int before = m_mock->requestCount();
    m_artist->open(QStringLiteral("ar1"), QStringLiteral("Hollow Coves"), kLibrary);
    QVERIFY(!m_artist->loading());
    QCOMPARE(state.count(), 0);
    QCOMPARE(identity.count(), 1);
    QTest::qWait(50);
    QCOMPARE(m_mock->requestCount(), before);

    // The reverse direction: a free reopen that carries no library id must
    // clear the one already set, not defensively keep it — the library id
    // belongs to the route, not to whichever page last set it.
    const int beforeClear = m_mock->requestCount();
    m_artist->open(QStringLiteral("ar1"), QStringLiteral("Hollow Coves"));
    QVERIFY(!m_artist->loading());
    QCOMPARE(state.count(), 0); // still free: no second load()
    QVERIFY(m_artist->libraryId().isEmpty());
    QCOMPARE(identity.count(), 2); // one more artistChanged, and only one
    QTest::qWait(50);
    QCOMPARE(m_mock->requestCount(), beforeClear);

    m_artist->resetSessionState();
    QVERIFY(m_artist->artistId().isEmpty());
    QVERIFY(m_artist->name().isEmpty());
    QVERIFY(m_artist->libraryId().isEmpty());
    QCOMPARE(m_artist->albums()->count(), 0);
    QCOMPARE(m_artist->epsAndSingles()->count(), 0);
    QCOMPARE(m_artist->appearsOn()->count(), 0);
    QCOMPARE(m_artist->topTracks()->count(), 0);
    QCOMPARE(m_artist->similar()->count(), 0);
    QVERIFY(m_artist->tabs().isEmpty());
    QVERIFY(m_artist->kicker().isEmpty());
    QVERIFY(!m_artist->loading());
}

QTEST_GUILESS_MAIN(AlbumArtistControllersTest)
#include "tst_album_artist_controllers.moc"
