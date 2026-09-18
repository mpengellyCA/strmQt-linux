#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUrlQuery>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "app/controllers/music/MusicHomeController.h"
#include "app/models/MediaItemModel.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"
#include "app/music/UserDataPatch.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"
#include "server/emby/MusicServerCapabilities.h"

using namespace strmqt;
using namespace strmqt::music;

namespace {

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
const auto kLibrary = QStringLiteral("1868998");
const auto kOtherLibrary = QStringLiteral("2000001");

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

QString itemsPath() { return QStringLiteral("/Users/%1/Items").arg(kUserId); }
QString itemPath(const QString &id) { return QStringLiteral("/Users/%1/Items/%2").arg(kUserId, id); }

QByteArray page(const QJsonArray &items, int total = -1)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("Items"), items},
                                     {QStringLiteral("TotalRecordCount"), total < 0 ? items.size() : total}})
        .toJson(QJsonDocument::Compact);
}

QByteArray object(const QJsonObject &json) { return QJsonDocument(json).toJson(QJsonDocument::Compact); }

QJsonObject albumJson(const QString &id, const QString &artistId = QStringLiteral("ar1"),
                      int tracks = 10, qint64 minutes = 45)
{
    return {{"Id", id}, {"Name", "Album " + id}, {"Type", "MusicAlbum"},
            {"AlbumArtists", QJsonArray{QJsonObject{{"Id", artistId}, {"Name", "Artist " + artistId}}}},
            {"ChildCount", tracks}, {"CumulativeRunTimeTicks", minutes * 60 * 10'000'000LL},
            {"ImageTags", QJsonObject{{"Primary", "tag-" + id}}}};
}

QJsonObject trackJson(const QString &id, const QString &albumId, int number, const QString &artistId)
{
    return {{"Id", id}, {"Name", "Track " + id}, {"Type", "Audio"}, {"AlbumId", albumId},
            {"Album", "Album " + albumId}, {"ParentIndexNumber", 1}, {"IndexNumber", number},
            {"RunTimeTicks", 4 * 60 * 10'000'000LL},
            {"ArtistItems", QJsonArray{QJsonObject{{"Id", artistId}, {"Name", "Artist " + artistId}}}},
            {"AlbumArtists", QJsonArray{QJsonObject{{"Id", artistId}, {"Name", "Artist " + artistId}}}},
            {"MediaStreams", QJsonArray{QJsonObject{{"Type", "Audio"}, {"Codec", "flac"},
                                                    {"BitDepth", 16}, {"SampleRate", 44100}}}}};
}

QJsonObject playedTrack(const QString &id, const QString &albumId, const QString &artistId,
                        const QString &lastPlayed)
{
    QJsonObject track = trackJson(id, albumId, 1, artistId);
    track.insert("UserData", QJsonObject{{"Played", true},
                                         {"PlaybackPositionTicks", 0},
                                         {"LastPlayedDate", lastPlayed}});
    return track;
}

using Q = QList<QPair<QString, QString>>;

} // namespace

class MusicHomeControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { setEmbyImageSourceNamespace(QStringLiteral("t")); }
    void init();
    void cleanup();

    void heroResumesTheLastAlbum();
    void heroPullsOneOutWithoutHistory();
    void lanesFailIndependently();
    void staleRepliesAreDroppedOnReopen();
    void reshuffleRefillsOnlyThePullLane();
    void stationsResolveAndPlay();
    void refreshStaleIsFreeWithinTheTtl();
    void coldOpenThenRefreshStaleFetchesOnce();
    void cycleSectionWalksTheStrip();
    void resetSessionStateForgetsTheLibrary();
    void anotherOneSurvivesARefresh();
    void allGenresBinUsesOnlyTheFirstThreeBins();
    void resetSessionStateDropsInFlightReplies();

private:
    void routeLibrary(const QString &library, const QString &tag, bool history);
    void routeFavourites(const QString &library, const QString &tag, int status);
    void routeGenres(const QString &library, const QString &tag, int status);
    QList<MusicLane *> lanes() const;
    bool allReady() const;
    PlayQueue *queue() const { return m_player->queue(); }

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_player = nullptr;
    ItemActions *m_actions = nullptr;
    MusicRepository *m_repo = nullptr;
    MusicPlayback *m_playback = nullptr;
    MusicHomeController *m_home = nullptr;
    QDateTime m_now = QDateTime::fromSecsSinceEpoch(1'800'000'000, QTimeZone::utc());
};

void MusicHomeControllerTest::init()
{
    m_now = QDateTime::fromSecsSinceEpoch(1'800'000'000, QTimeZone::utc());
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    for (const char *id : {"a-t1", "a-t2", "a-t3"}) {
        QVERIFY(m_mock->addRouteFromFile("POST", QStringLiteral("/Items/%1/PlaybackInfo").arg(QLatin1String(id)),
                                         fixturePath(QStringLiteral("playback_info.json"))));
    }
    m_mock->addRoute("POST", "/Sessions/Playing", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Progress", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Stopped", 204, {});
    // Anything a test did not route is an empty page, not a 404.
    m_mock->addRoute("GET", itemsPath(), 200, page({}));

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
    m_repo->setShuffleSeedForTests(7);
    m_playback = new MusicPlayback(m_repo, m_actions, this);
    m_home = new MusicHomeController(m_repo, m_playback, this);
}

void MusicHomeControllerTest::cleanup()
{
    delete m_home;
    delete m_playback;
    delete m_repo;
    delete m_actions;
    delete m_player;
    delete m_settings;
    delete m_dir;
    delete m_backend;
    delete m_client;
    delete m_mock;
    m_home = nullptr;
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

// Every id a library answers with is prefixed by its tag ("a-hero", "b-hero"),
// so a reply that lands in the wrong library is visible in any assertion.
void MusicHomeControllerTest::routeLibrary(const QString &library, const QString &tag, bool history)
{
    const auto id = [&tag](const char *suffix) { return tag + QLatin1Char('-') + QLatin1String(suffix); };

    QJsonArray played;
    if (history) {
        played = {playedTrack(id("t2"), id("hero"), id("ar"), "2026-09-15T20:00:00Z"),
                  playedTrack(id("x1"), id("r1"), id("ar"), "2026-09-15T19:00:00Z"),
                  playedTrack(id("x2"), id("r2"), id("ar"), "2026-09-15T18:00:00Z")};
    }
    for (int limit : {1, 200, 500}) {
        m_mock->addQueryRoute("GET", itemsPath(),
                              Q{{"ParentId", library}, {"IncludeItemTypes", "Audio"},
                                {"SortBy", "DatePlayed"}, {"Limit", QString::number(limit)}},
                              200, page(limit == 1 && history ? QJsonArray{played.at(0)} : played));
    }

    QJsonObject hero = albumJson(id("hero"), id("ar"), 4, 16);
    hero.insert("ProductionYear", 1997);
    m_mock->addRoute("GET", itemPath(id("hero")), 200, object(hero));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", id("hero")}, {"IncludeItemTypes", "Audio"}}, 200,
                          page({trackJson(id("t1"), id("hero"), 1, id("ar")),
                                trackJson(id("t2"), id("hero"), 2, id("ar")),
                                trackJson(id("t3"), id("hero"), 3, id("ar")),
                                trackJson(id("t4"), id("hero"), 4, id("ar"))}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"Ids", id("hero") + "," + id("r1") + "," + id("r2")}}, 200,
                          page({albumJson(id("r2"), id("ar")), hero, albumJson(id("r1"), id("ar"))}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"Ids", id("ar")}}, 200,
                          page({QJsonObject{{"Id", id("ar")}, {"Name", "Artist " + id("ar")},
                                            {"Type", "MusicArtist"},
                                            {"ImageTags", QJsonObject{{"Primary", "tag-" + id("ar")}}}}}));

    const Q albums{{"ParentId", library}, {"IncludeItemTypes", "MusicAlbum"}};
    m_mock->addQueryRoute("GET", itemsPath(), Q(albums) << qMakePair(QStringLiteral("SortBy"), QStringLiteral("DateCreated")),
                          200, page({albumJson(id("n1"), id("ar")), albumJson(id("n2"), id("ar"))}));
    m_mock->addQueryRoute("GET", itemsPath(), Q(albums) << qMakePair(QStringLiteral("Limit"), QStringLiteral("0")),
                          200, page({}, 6));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q(albums) << qMakePair(QStringLiteral("SortBy"), QStringLiteral("Random"))
                                    << qMakePair(QStringLiteral("Limit"), QStringLiteral("20")),
                          200, page({albumJson(id("p1"), id("ar")), albumJson(id("p2"), id("ar"))}));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q(albums) << qMakePair(QStringLiteral("SortBy"), QStringLiteral("Random"))
                                    << qMakePair(QStringLiteral("Limit"), QStringLiteral("1")),
                          200, page({albumJson(id("pick"), id("ar"))}));
    routeFavourites(library, tag, 200);
    routeGenres(library, tag, 200);

    const Q audio{{"ParentId", library}, {"IncludeItemTypes", "Audio"}};
    m_mock->addQueryRoute("GET", itemsPath(), Q(audio) << qMakePair(QStringLiteral("SortBy"), QStringLiteral("PlayCount")),
                          200, page({trackJson(id("t1"), id("hero"), 1, id("ar")),
                                     trackJson(id("t2"), id("hero"), 2, id("ar"))}));
    m_mock->addQueryRoute("GET", itemsPath(), Q(audio) << qMakePair(QStringLiteral("Filters"), QStringLiteral("IsFavorite")),
                          200, page({trackJson(id("t3"), id("hero"), 3, id("ar"))}));
}

void MusicHomeControllerTest::routeFavourites(const QString &library, const QString &tag, int status)
{
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"ParentId", library}, {"IncludeItemTypes", "MusicAlbum"}, {"Filters", "IsFavorite"}},
                          status, status == 200 ? page({albumJson(tag + "-f1", tag + "-ar")}) : QByteArray("{}"));
}

void MusicHomeControllerTest::routeGenres(const QString &library, const QString &tag, int status)
{
    const QByteArray failed("{}");
    QJsonArray genres{QJsonObject{{"Id", tag + "-g1"}, {"Name", "Rock " + tag}, {"AlbumCount", 3}},
                      QJsonObject{{"Id", tag + "-g2"}, {"Name", "Folk " + tag}, {"AlbumCount", 2}},
                      QJsonObject{{"Id", tag + "-g3"}, {"Name", "Jazz " + tag}, {"AlbumCount", 1}}};
    m_mock->addQueryRoute("GET", "/MusicGenres", Q{{"ParentId", library}}, status,
                          status == 200 ? page(genres) : failed);
    if (!emby::caps::kGenreItemCounts) {
        QJsonArray walk;
        const QList<std::pair<QString, int>> counts{{tag + "-g1", 3}, {tag + "-g2", 2}, {tag + "-g3", 1}};
        int n = 0;
        for (const auto &[genreId, count] : counts) {
            const QString name = genres.at(n++).toObject().value("Name").toString();
            for (int i = 0; i < count; ++i) {
                QJsonObject album = albumJson(QStringLiteral("%1-w%2-%3").arg(tag, genreId).arg(i));
                album.insert("GenreItems", QJsonArray{QJsonObject{{"Id", genreId}, {"Name", name}}});
                walk.append(album);
            }
        }
        m_mock->addQueryRoute("GET", itemsPath(),
                              Q{{"ParentId", library}, {"IncludeItemTypes", "MusicAlbum"}, {"Fields", "Genres"}},
                              status, status == 200 ? page(walk) : failed);
    }
    for (int i = 1; i <= 3; ++i) {
        const QString genreId = QStringLiteral("%1-g%2").arg(tag).arg(i);
        m_mock->addQueryRoute("GET", itemsPath(), Q{{"GenreIds", genreId}, {"SortBy", "Random"}}, 200,
                              page({albumJson("c-" + genreId)}));
    }
}

QList<MusicLane *> MusicHomeControllerTest::lanes() const
{
    return {m_home->heroLane(),  m_home->recentLane(), m_home->newLane(),
            m_home->stationLane(), m_home->genreLane(), m_home->artistLane(),
            m_home->forgottenLane(), m_home->pullLane()};
}

bool MusicHomeControllerTest::allReady() const
{
    for (MusicLane *lane : lanes()) {
        if (!lane->ready() || lane->loading())
            return false;
    }
    return true;
}

void MusicHomeControllerTest::heroResumesTheLastAlbum()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    m_home->open(kLibrary);
    QCOMPARE(m_home->libraryId(), kLibrary);
    QVERIFY(m_home->heroLane()->loading());
    QTRY_VERIFY(allReady());

    const QVariantMap hero = m_home->hero();
    QCOMPARE(hero.value("mode").toString(), QStringLiteral("resume"));
    QCOMPARE(hero.value("albumId").toString(), QStringLiteral("a-hero"));
    QCOMPARE(hero.value("title").toString(), QStringLiteral("Album a-hero"));
    QCOMPARE(hero.value("artist").toString(), QStringLiteral("Artist a-ar"));
    QCOMPARE(hero.value("artistId").toString(), QStringLiteral("a-ar"));
    QCOMPARE(hero.value("year").toInt(), 1997);
    QCOMPARE(hero.value("summary").toString(), QStringLiteral("Artist a-ar · 1997 · 4 tracks · 16 min"));
    QCOMPARE(hero.value("resumeIndex").toInt(), 2); // finished track 2 → resume track 3
    QCOMPARE(hero.value("resumeLabel").toString(), QStringLiteral("Resume track 3"));
    QCOMPARE(hero.value("progress").toDouble(), 0.5);
    QVERIFY(!hero.value("coverUrl").toString().isEmpty());
    QCOMPARE(hero.value("albumItem").toMap().value("itemId").toString(), QStringLiteral("a-hero"));

    // Recently played beside the hero: the recent shelf minus the hero album.
    QCOMPARE(m_home->recentLane()->typedModel()->count(), 3);
    QCOMPARE(m_home->recentLane()->typedModel()->idAt(0), QStringLiteral("a-hero"));
    MusicModelBase *beside = m_home->heroLane()->typedModel();
    QCOMPARE(beside->count(), 2);
    QCOMPARE(beside->idAt(0), QStringLiteral("a-r1"));
    QCOMPARE(beside->idAt(1), QStringLiteral("a-r2"));

    QCOMPARE(m_home->newLane()->typedModel()->count(), 2);
    QCOMPARE(m_home->addedThisWeekText(),
             emby::caps::kMinDateCreated ? QStringLiteral("6 added this week") : QString());
    QCOMPARE(m_home->artistLane()->typedModel()->idAt(0), QStringLiteral("a-ar"));
    QCOMPARE(m_home->forgottenLane()->typedModel()->idAt(0), QStringLiteral("a-f1"));
    QCOMPARE(m_home->pullLane()->typedModel()->count(), 2);

    MusicModelBase *genres = m_home->genreLane()->typedModel();
    QCOMPARE(genres->count(), 4);
    QCOMPARE(genres->get(0).value("name").toString(), QStringLiteral("Rock a"));
    QCOMPARE(m_home->allGenresText(), QStringLiteral("All 3 genres"));
    const QVariantMap all = genres->get(3);
    QVERIFY(all.value("itemId").toString().isEmpty());
    QCOMPARE(all.value("name").toString(), QStringLiteral("All 3 genres"));
    QCOMPARE(all.value("covers").toStringList().size(), 3);

    // The heart follows the relay's in-place patch of the hidden hero model.
    MusicModelBase *heroModel = nullptr;
    for (MusicModelBase *model : m_home->models()) {
        if (model->count() == 1 && model->idAt(0) == QLatin1String("a-hero"))
            heroModel = model;
    }
    QVERIFY(heroModel);
    UserDataPatch patch;
    patch.favourite = true;
    heroModel->applyUserData(QStringLiteral("a-hero"), patch);
    QCOMPARE(m_home->hero().value("favourite").toBool(), true);
}

void MusicHomeControllerTest::heroPullsOneOutWithoutHistory()
{
    routeLibrary(kLibrary, QStringLiteral("a"), false);
    m_home->open(kLibrary);
    QTRY_VERIFY(allReady());

    QVariantMap hero = m_home->hero();
    QCOMPARE(hero.value("mode").toString(), QStringLiteral("pullOne"));
    QCOMPARE(hero.value("albumId").toString(), QStringLiteral("a-pick"));
    QCOMPARE(hero.value("resumeIndex").toInt(), 0);
    QCOMPARE(hero.value("resumeLabel").toString(), QStringLiteral("Play"));
    QCOMPARE(hero.value("progress").toDouble(), 0.0);
    QVERIFY(hero.value("summary").toString().startsWith(QStringLiteral("Artist a-ar")));

    // No history: the listening shelves hide themselves, the others do not.
    QVERIFY(m_home->recentLane()->empty());
    QVERIFY(m_home->artistLane()->empty());
    QCOMPARE(m_home->heroLane()->typedModel()->count(), 0);
    QVERIFY(!m_home->pullLane()->empty());
    QCOMPARE(m_home->stationLane()->typedModel()->count(), 4); // no "More like"

    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"ParentId", kLibrary}, {"IncludeItemTypes", "MusicAlbum"},
                            {"SortBy", "Random"}, {"Limit", "1"}},
                          200, page({albumJson("a-pick2", "a-ar")}));
    m_home->anotherOne();
    QVERIFY(!m_home->heroLane()->loading()); // the current record stays up meanwhile
    QTRY_COMPARE(m_home->hero().value("albumId").toString(), QStringLiteral("a-pick2"));

    // A refresh keeps the record the user pulled rather than swapping it.
    m_home->refreshStale();
    QTest::qWait(250);
    QCOMPARE(m_home->hero().value("albumId").toString(), QStringLiteral("a-pick2"));
}

void MusicHomeControllerTest::lanesFailIndependently()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    routeFavourites(kLibrary, QStringLiteral("a"), 500);
    routeGenres(kLibrary, QStringLiteral("a"), 500);
    m_home->open(kLibrary);

    QTRY_VERIFY(!m_home->forgottenLane()->error().isEmpty());
    QTRY_VERIFY(!m_home->genreLane()->error().isEmpty());
    QVERIFY(!m_home->forgottenLane()->empty()); // an error line, not a hidden shelf
    for (MusicLane *lane : {m_home->heroLane(), m_home->recentLane(), m_home->newLane(),
                            m_home->stationLane(), m_home->artistLane(), m_home->pullLane()}) {
        QTRY_VERIFY(lane->ready());
        QVERIFY(lane->error().isEmpty());
    }

    routeFavourites(kLibrary, QStringLiteral("a"), 200);
    routeGenres(kLibrary, QStringLiteral("a"), 200);
    m_home->forgottenLane()->retry();
    m_home->genreLane()->retry();
    QTRY_VERIFY(m_home->forgottenLane()->ready());
    QTRY_VERIFY(m_home->genreLane()->ready());
    QVERIFY(m_home->forgottenLane()->error().isEmpty());
    QCOMPARE(m_home->forgottenLane()->typedModel()->count(), 1);
    QCOMPARE(m_home->genreLane()->typedModel()->count(), 4);
}

void MusicHomeControllerTest::staleRepliesAreDroppedOnReopen()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    routeLibrary(kOtherLibrary, QStringLiteral("b"), true);
    // The first library's hero lands well after the second library is done.
    m_mock->setRouteDelay("GET", itemPath(QStringLiteral("a-hero")), 400);

    m_home->open(kLibrary);
    m_home->open(kOtherLibrary);
    QCOMPARE(m_home->libraryId(), kOtherLibrary);
    QTRY_VERIFY(allReady());
    QTest::qWait(600);

    QCOMPARE(m_home->hero().value("albumId").toString(), QStringLiteral("b-hero"));
    QCOMPARE(m_home->heroLane()->typedModel()->idAt(0), QStringLiteral("b-r1"));
    QCOMPARE(m_home->recentLane()->typedModel()->idAt(0), QStringLiteral("b-hero"));
    QCOMPARE(m_home->newLane()->typedModel()->idAt(0), QStringLiteral("b-n1"));
    QCOMPARE(m_home->artistLane()->typedModel()->idAt(0), QStringLiteral("b-ar"));
    QCOMPARE(m_home->forgottenLane()->typedModel()->idAt(0), QStringLiteral("b-f1"));
    QCOMPARE(m_home->pullLane()->typedModel()->idAt(0), QStringLiteral("b-p1"));
    QCOMPARE(m_home->genreLane()->typedModel()->get(0).value("name").toString(), QStringLiteral("Rock b"));
    QCOMPARE(m_home->stationLane()->typedModel()->get(3).value("label").toString(),
             QStringLiteral("More like Artist b-ar"));
    for (MusicModelBase *model : m_home->models()) {
        for (int row = 0; row < model->count(); ++row)
            QVERIFY2(!model->idAt(row).startsWith(QLatin1String("a-")), qPrintable(model->idAt(row)));
    }
}

void MusicHomeControllerTest::reshuffleRefillsOnlyThePullLane()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    m_home->open(kLibrary);
    QTRY_VERIFY(allReady());
    QCOMPARE(m_home->pullLane()->typedModel()->idAt(0), QStringLiteral("a-p1"));

    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"ParentId", kLibrary}, {"IncludeItemTypes", "MusicAlbum"},
                            {"SortBy", "Random"}, {"Limit", "20"}},
                          200, page({albumJson("a-p3", "a-ar")}));
    const int before = m_mock->requestCount();
    m_home->reshuffle();
    QVERIFY(!m_home->pullLane()->loading()); // the old records stay until the new ones land
    QTRY_COMPARE(m_home->pullLane()->typedModel()->idAt(0), QStringLiteral("a-p3"));

    const auto &requests = m_mock->requests();
    QVERIFY(requests.size() > before);
    for (qsizetype i = before; i < requests.size(); ++i) {
        const QUrlQuery query(requests.at(i).query);
        QCOMPARE(query.queryItemValue("SortBy"), QStringLiteral("Random"));
        QCOMPARE(query.queryItemValue("IncludeItemTypes"), QStringLiteral("MusicAlbum"));
    }
    QCOMPARE(m_home->stationLane()->typedModel()->count(), 5);
}

void MusicHomeControllerTest::stationsResolveAndPlay()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    m_home->open(kLibrary);
    QTRY_VERIFY(allReady());

    MusicModelBase *stations = m_home->stationLane()->typedModel();
    QCOMPARE(stations->count(), 5);
    QCOMPARE(stations->get(0).value("itemId").toString(), QStringLiteral("heavyRotation"));
    QCOMPARE(stations->get(3).value("label").toString(), QStringLiteral("More like Artist a-ar"));
    QCOMPARE(stations->get(0).value("covers").toStringList().size(), 4);

    m_home->playStation(0);
    QTRY_COMPARE(queue()->rowCount(), 2);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Station · Heavy rotation"));

    m_home->queueStation(1);
    QTRY_COMPARE(queue()->rowCount(), 3);
    QCOMPARE(queue()->itemAt(2).value("itemId").toString(), QStringLiteral("a-t3"));

    m_home->shuffleStation(1);
    QTRY_COMPARE(queue()->sourceLabel(), QStringLiteral("Station · Favourites"));
    QCOMPARE(queue()->rowCount(), 1);

    m_home->playStation(99); // out of range: nothing happens
    QTest::qWait(100);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Station · Favourites"));
}

void MusicHomeControllerTest::refreshStaleIsFreeWithinTheTtl()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    m_home->open(kLibrary);
    QTRY_VERIFY(allReady());
    QTest::qWait(100);

    const int before = m_mock->requestCount();
    m_home->refreshStale();
    m_home->open(kLibrary); // the same library again is a refresh, not a reload
    QTest::qWait(300);
    QCOMPARE(m_mock->requestCount(), before);
    QVERIFY(allReady());
    QCOMPARE(m_home->recentLane()->typedModel()->count(), 3);
    QCOMPARE(m_home->hero().value("albumId").toString(), QStringLiteral("a-hero"));

    m_now = m_now.addSecs(6 * 60);
    m_home->refreshStale();
    QVERIFY(!m_home->recentLane()->loading()); // quiet: the shelves stay on screen
    QTRY_VERIFY(m_mock->requestCount() > before);
    QTest::qWait(300); // a quiet refresh keeps allReady() true: let its replies land
    QTRY_VERIFY(allReady());
    QCOMPARE(m_home->recentLane()->typedModel()->count(), 3);
}

// Ruling P2-R3: the page calls refreshStale() on activation in the same turn
// as open(). A lane still on its first load is not refetched, so every
// first-load request goes out once.
void MusicHomeControllerTest::coldOpenThenRefreshStaleFetchesOnce()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    m_home->open(kLibrary);
    m_home->refreshStale();
    QTRY_VERIFY(allReady());
    QTest::qWait(300);

    QHash<QString, int> sent;
    for (const MockEmbyServer::ReceivedRequest &request : m_mock->requests())
        ++sent[request.method + QLatin1Char(' ') + request.path + QLatin1Char('?') + request.query];
    for (auto it = sent.cbegin(); it != sent.cend(); ++it)
        QVERIFY2(it.value() == 1, qPrintable(QStringLiteral("%1 sent %2 times").arg(it.key()).arg(it.value())));
    QCOMPARE(m_home->hero().value("albumId").toString(), QStringLiteral("a-hero"));
    QCOMPARE(m_home->recentLane()->typedModel()->count(), 3);
}

void MusicHomeControllerTest::cycleSectionWalksTheStrip()
{
    QCOMPARE(MusicHomeController::sectionKeys(),
             (QStringList{"home", "albums", "artists", "songs", "genres", "playlists"}));
    QCOMPARE(m_home->cycleSection(1), QStringLiteral("albums"));
    QCOMPARE(m_home->cycleSection(-1), QStringLiteral("playlists"));
    QCOMPARE(m_home->cycleSection(7), QStringLiteral("albums"));
}

void MusicHomeControllerTest::resetSessionStateForgetsTheLibrary()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    m_home->open(kLibrary);
    QTRY_VERIFY(allReady());

    m_home->resetSessionState();
    QVERIFY(m_home->libraryId().isEmpty());
    QVERIFY(m_home->hero().isEmpty());
    for (MusicModelBase *model : m_home->models())
        QCOMPARE(model->count(), 0);
    for (MusicLane *lane : lanes())
        QVERIFY(!lane->ready());
}

void MusicHomeControllerTest::anotherOneSurvivesARefresh()
{
    routeLibrary(kLibrary, QStringLiteral("a"), false);
    m_home->open(kLibrary);
    QTRY_VERIFY(allReady());
    QCOMPARE(m_home->hero().value("albumId").toString(), QStringLiteral("a-pick"));

    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"ParentId", kLibrary}, {"IncludeItemTypes", "MusicAlbum"},
                            {"SortBy", "Random"}, {"Limit", "1"}},
                          200, page({albumJson("a-pick2", "a-ar")}));
    // The page refreshes in the same turn as the press: the pull must not be dropped.
    m_home->anotherOne();
    m_home->refreshStale();
    QTRY_COMPARE(m_home->hero().value("albumId").toString(), QStringLiteral("a-pick2"));
    QTest::qWait(300);
    QCOMPARE(m_home->hero().value("albumId").toString(), QStringLiteral("a-pick2"));
    QVERIFY(m_home->heroLane()->error().isEmpty());
}

void MusicHomeControllerTest::allGenresBinUsesOnlyTheFirstThreeBins()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    // Four genres; the largest has no covers. The all-genres bin takes the first
    // cover of the first three bins only, so it shows two covers, not three.
    QJsonArray genres;
    QJsonArray walk;
    for (int i = 1; i <= 4; ++i) {
        const QString genreId = QStringLiteral("a-g%1").arg(i);
        const QString name = QStringLiteral("Genre %1").arg(i);
        genres.append(QJsonObject{{"Id", genreId}, {"Name", name}, {"AlbumCount", 5 - i}});
        for (int k = 0; k < 5 - i; ++k) {
            QJsonObject album = albumJson(QStringLiteral("a-w%1-%2").arg(genreId).arg(k));
            album.insert("GenreItems", QJsonArray{QJsonObject{{"Id", genreId}, {"Name", name}}});
            walk.append(album);
        }
        m_mock->addQueryRoute("GET", itemsPath(), Q{{"GenreIds", genreId}, {"SortBy", "Random"}}, 200,
                              i == 1 ? page({}) : page({albumJson("c-" + genreId)}));
    }
    m_mock->addQueryRoute("GET", "/MusicGenres", Q{{"ParentId", kLibrary}}, 200, page(genres));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"ParentId", kLibrary}, {"IncludeItemTypes", "MusicAlbum"}, {"Fields", "Genres"}},
                          200, page(walk));

    m_home->open(kLibrary);
    QTRY_VERIFY(allReady());
    MusicModelBase *bins = m_home->genreLane()->typedModel();
    QCOMPARE(bins->count(), 5);
    QCOMPARE(bins->get(0).value("name").toString(), QStringLiteral("Genre 1"));
    QCOMPARE(m_home->allGenresText(), QStringLiteral("All 4 genres"));
    QCOMPARE(bins->get(4).value("covers").toStringList().size(), 2);
}

void MusicHomeControllerTest::resetSessionStateDropsInFlightReplies()
{
    routeLibrary(kLibrary, QStringLiteral("a"), true);
    m_home->open(kLibrary);
    m_home->resetSessionState();
    QTest::qWait(300);

    QVERIFY(m_home->libraryId().isEmpty());
    QVERIFY(m_home->hero().isEmpty());
    for (MusicModelBase *model : m_home->models())
        QCOMPARE(model->count(), 0);
    for (MusicLane *lane : lanes()) {
        QVERIFY(!lane->ready());
        QVERIFY(!lane->loading());
    }
}

QTEST_MAIN(MusicHomeControllerTest)
#include "tst_music_home_controller.moc"
