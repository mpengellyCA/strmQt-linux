#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <QUrlQuery>
#include <QtTest>

#include "MockEmbyServer.h"
#include "app/music/MusicRepository.h"
#include "server/emby/EmbyClient.h"
#include "server/emby/EmbyMusicMapper.h"
#include "server/emby/MusicServerCapabilities.h"

using namespace strmqt;
using namespace strmqt::music;
using emby::EmbyClient;

namespace {

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
const auto kLibrary = QStringLiteral("1868998");

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

QJsonObject playlistJson(const QString &id, const QString &mediaType = QString())
{
    QJsonObject json{{"Id", id}, {"Name", "Playlist " + id}, {"Type", "Playlist"}, {"ChildCount", 5}};
    if (!mediaType.isEmpty())
        json.insert("MediaType", mediaType);
    return json;
}

QJsonObject trackJson(const QString &id, const QString &albumId, int disc, int number,
                      const QString &artistId = QStringLiteral("ar1"))
{
    return {{"Id", id}, {"Name", "Track " + id}, {"Type", "Audio"}, {"AlbumId", albumId},
            {"Album", "Album " + albumId}, {"ParentIndexNumber", disc}, {"IndexNumber", number},
            {"RunTimeTicks", 4 * 60 * 10'000'000LL},
            {"ArtistItems", QJsonArray{QJsonObject{{"Id", artistId}, {"Name", "Artist " + artistId}}}},
            {"AlbumArtists", QJsonArray{QJsonObject{{"Id", "ar1"}, {"Name", "Artist ar1"}}}},
            {"MediaStreams", QJsonArray{QJsonObject{{"Type", "Audio"}, {"Codec", "flac"},
                                                    {"BitDepth", 16}, {"SampleRate", 44100}}}}};
}

QJsonObject playedTrack(const QString &id, const QString &albumId, const QString &artistId,
                       const QString &lastPlayed, qint64 positionTicks = 0)
{
    QJsonObject track = trackJson(id, albumId, 1, 1, artistId);
    track.insert("UserData", QJsonObject{{"Played", positionTicks == 0},
                                         {"PlaybackPositionTicks", positionTicks},
                                         {"LastPlayedDate", lastPlayed}});
    return track;
}

template<class T> Result<T> waitFor(QFuture<Result<T>> future)
{
    if (!QTest::qWaitFor([&] { return future.isFinished(); }, 5000))
        return Result<T>::failure(QStringLiteral("timeout waiting for future"));
    return future.result();
}

using Q = QList<QPair<QString, QString>>;

} // namespace

class MusicRepositoryTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void sleeveComposesAlbumTracksAndMoreBy();
    void sleeveSurvivesMoreByFailure();
    void sleeveFailsWhenTheAlbumFails();
    void sleeveIsCachedUntilTtl();
    void identityChangeClearsCaches();
    void artistProfileGroupsReleases();
    void artistProfileFailsOnlyOnTheArtist();

    void continueListeningResumesTheNextTrack();
    void continueListeningResumesPartiallyPlayedTrack();
    void continueListeningIsEmptyWithoutHistory();
    void recentAlbumsDedupeInPlayOrder();
    void newAlbumsCountThisWeek();
    void genreBinsSampleCoversOnce();
    void coverGenresSamplesOnlyBinsWithoutCovers();
    void coverGenresRefusesStaleCoversAfterIdentityChanges();
    void coverCacheExpiresAfterTtl();
    void topArtistsRankByPlays();
    void stationsDescribeFiveTilesWithCovers();
    void heavyRotationIsShuffledPlayedTracks();
    void moreLikeDeduplicatesInstantMix();
    void markStaleRefetchesListeningShelves();

    void browseAlbumsPagesWithTheTranslatedQuery();
    void browseArtistsPicksTheEndpointByMode();
    void browsePlaylistsKeepsAudioAndDropsVideo();
    void browseTracksAddsHiResOnlyWhenMeasured();
    void sampleTracksIsRandomAcrossTheFilteredScope();
    void userDataChangeDropsCachesHoldingTheItem();
    void artistTracksAreRandomAndScopedToTheArtist();

private:
    void routeAlbum(const QString &albumId, int trackCount);

    MockEmbyServer *m_mock = nullptr;
    EmbyClient *m_client = nullptr;
    MusicRepository *m_repo = nullptr;
    QDateTime m_now = QDateTime::fromSecsSinceEpoch(1'800'000'000, QTimeZone::utc());
};

void MusicRepositoryTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    m_client = new EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setDeviceId(QStringLiteral("test-device-id"));
    m_client->setDeviceName(QStringLiteral("test-host"));
    m_client->setSession(kToken, kUserId);
    m_repo = new MusicRepository(m_client, this);
    m_repo->setClockForTests([this] { return m_now; });
    m_repo->setShuffleSeedForTests(7);
}

void MusicRepositoryTest::cleanup()
{
    delete m_repo;
    delete m_client;
    delete m_mock;
    m_repo = nullptr;
    m_client = nullptr;
    m_mock = nullptr;
}

void MusicRepositoryTest::routeAlbum(const QString &albumId, int trackCount)
{
    m_mock->addRoute(QStringLiteral("GET"), itemPath(albumId), 200, object(albumJson(albumId)));
    QJsonArray tracks;
    for (int i = 1; i <= trackCount; ++i)
        tracks.append(trackJson(albumId + "-t" + QString::number(i), albumId, i > 6 ? 2 : 1, i));
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(),
                          Q{{"ParentId", albumId}, {"IncludeItemTypes", "Audio"}}, 200, page(tracks));
}

void MusicRepositoryTest::sleeveComposesAlbumTracksAndMoreBy()
{
    routeAlbum(QStringLiteral("al1"), 9);
    QJsonArray more{albumJson("al1"), albumJson("al2"), albumJson("al3")};
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(),
                          Q{{"AlbumArtistIds", "ar1"}, {"IncludeItemTypes", "MusicAlbum"}}, 200, page(more));

    const auto result = waitFor(m_repo->albumSleeve(QStringLiteral("al1")));
    QVERIFY2(result.ok(), qPrintable(result.error));
    const AlbumSleeve &sleeve = result.value;
    QCOMPARE(sleeve.album.id, QStringLiteral("al1"));
    QCOMPARE(sleeve.album.trackCount, 9);
    QCOMPARE(sleeve.album.discCount, 2);
    QCOMPARE(sleeve.album.formatSummary, QStringLiteral("FLAC 16/44.1"));
    QCOMPARE(sleeve.discs.size(), 2);
    QCOMPARE(sleeve.discs.at(0).tracks.size(), 6);
    QCOMPARE(sleeve.moreByArtist.size(), 2); // al1 itself excluded
    QCOMPARE(sleeve.moreByArtist.at(0).id, QStringLiteral("al2"));

    bool sawTracks = false;
    for (const auto &request : m_mock->requests()) {
        const QUrlQuery q(request.query);
        if (q.queryItemValue(QStringLiteral("ParentId")) == QLatin1String("al1")) {
            sawTracks = true;
            QCOMPARE(q.queryItemValue(QStringLiteral("SortBy")),
                     QStringLiteral("ParentIndexNumber,IndexNumber,SortName"));
            QCOMPARE(q.queryItemValue(QStringLiteral("Limit")), QStringLiteral("1000"));
        }
    }
    QVERIFY(sawTracks);
}

void MusicRepositoryTest::sleeveSurvivesMoreByFailure()
{
    routeAlbum(QStringLiteral("al1"), 3);
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(), Q{{"AlbumArtistIds", "ar1"}}, 500, "{}");
    const auto result = waitFor(m_repo->albumSleeve(QStringLiteral("al1")));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.value.moreByArtist.isEmpty());
    QCOMPARE(result.value.discs.first().tracks.size(), 3);
}

void MusicRepositoryTest::sleeveFailsWhenTheAlbumFails()
{
    m_mock->addRoute(QStringLiteral("GET"), itemPath(QStringLiteral("gone")), 404, "{}");
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(), Q{{"ParentId", "gone"}}, 200, page({}));
    const auto result = waitFor(m_repo->albumSleeve(QStringLiteral("gone")));
    QVERIFY(!result.ok());
}

void MusicRepositoryTest::sleeveIsCachedUntilTtl()
{
    routeAlbum(QStringLiteral("al1"), 2);
    QVERIFY(waitFor(m_repo->albumSleeve(QStringLiteral("al1"))).ok());
    const int afterFirst = m_mock->requestCount();

    m_now = m_now.addSecs(9 * 60);
    QVERIFY(waitFor(m_repo->albumSleeve(QStringLiteral("al1"))).ok());
    QCOMPARE(m_mock->requestCount(), afterFirst);

    m_now = m_now.addSecs(2 * 60); // 11 minutes after the first fetch
    QVERIFY(waitFor(m_repo->albumSleeve(QStringLiteral("al1"))).ok());
    QVERIFY(m_mock->requestCount() > afterFirst);
}

void MusicRepositoryTest::identityChangeClearsCaches()
{
    routeAlbum(QStringLiteral("al1"), 2);
    QVERIFY(waitFor(m_repo->albumTracks(QStringLiteral("al1"))).ok());
    const int afterFirst = m_mock->requestCount();
    QVERIFY(waitFor(m_repo->albumTracks(QStringLiteral("al1"))).ok());
    QCOMPARE(m_mock->requestCount(), afterFirst);

    m_client->setSession(kToken, QStringLiteral("ffffffffffffffffffffffffffffffff"));
    m_client->setSession(kToken, kUserId);
    QVERIFY(waitFor(m_repo->albumTracks(QStringLiteral("al1"))).ok());
    QVERIFY(m_mock->requestCount() > afterFirst);
}

void MusicRepositoryTest::artistProfileGroupsReleases()
{
    m_mock->addRoute(QStringLiteral("GET"), itemPath(QStringLiteral("ar1")), 200,
                     object({{"Id", "ar1"}, {"Name", "Artist ar1"}, {"Type", "MusicArtist"}}));
    QJsonArray filed{albumJson("lp", "ar1", 10, 45), albumJson("ep", "ar1", 5, 20),
                     albumJson("single", "ar1", 2, 7)};
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(),
                          Q{{"AlbumArtistIds", "ar1"}, {"IncludeItemTypes", "MusicAlbum"}}, 200, page(filed));
    QJsonArray appears{albumJson("lp", "ar1"), albumJson("guest", "ar9")};
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(),
                          Q{{"ArtistIds", "ar1"}, {"IncludeItemTypes", "MusicAlbum"}}, 200, page(appears));
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(),
                          Q{{"ArtistIds", "ar1"}, {"IncludeItemTypes", "Audio"}}, 200,
                          page({trackJson("hit", "lp", 1, 1)}));
    if (emby::caps::kSimilarArtists) {
        const QString similarPath = QString::fromLatin1(emby::caps::kSimilarArtistsPath)
                                        .replace(QStringLiteral("{id}"), QStringLiteral("ar1"));
        m_mock->addRoute(QStringLiteral("GET"), similarPath, 200,
                         page({QJsonObject{{"Id", "ar2"}, {"Name", "Kin"}, {"Type", "MusicArtist"}}}));
    }

    const auto result = waitFor(m_repo->artistProfile(kLibrary, QStringLiteral("ar1")));
    QVERIFY2(result.ok(), qPrintable(result.error));
    const ArtistProfile &profile = result.value;
    QCOMPARE(profile.artist.name, QStringLiteral("Artist ar1"));
    QCOMPARE(profile.albums.size(), 1);
    QCOMPARE(profile.albums.first().id, QStringLiteral("lp"));
    QCOMPARE(profile.epsAndSingles.size(), 2);
    QCOMPARE(profile.appearsOn.size(), 1);
    QCOMPARE(profile.appearsOn.first().id, QStringLiteral("guest"));
    QCOMPARE(profile.topTracks.size(), 1);
    QCOMPARE(profile.similar.size(), emby::caps::kSimilarArtists ? 1 : 0);
}

void MusicRepositoryTest::artistProfileFailsOnlyOnTheArtist()
{
    m_mock->addRoute(QStringLiteral("GET"), itemPath(QStringLiteral("ar1")), 200,
                     object({{"Id", "ar1"}, {"Name", "Solo"}}));
    // Every secondary request 404s (no routes): the profile still resolves.
    const auto partial = waitFor(m_repo->artistProfile(kLibrary, QStringLiteral("ar1")));
    QVERIFY2(partial.ok(), qPrintable(partial.error));
    QVERIFY(partial.value.albums.isEmpty());

    const auto missing = waitFor(m_repo->artistProfile(kLibrary, QStringLiteral("nobody")));
    QVERIFY(!missing.ok());
}

void MusicRepositoryTest::continueListeningResumesTheNextTrack()
{
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "DatePlayed"}, {"Limit", "1"}}, 200,
                          page({playedTrack("al1-t3", "al1", "ar1", "2026-09-15T20:00:00Z")}));
    routeAlbum(QStringLiteral("al1"), 4);

    const auto result = waitFor(m_repo->continueListening(kLibrary));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.value.isValid());
    QCOMPARE(result.value.album.id, QStringLiteral("al1"));
    QCOMPARE(result.value.resumeIndex, 3); // finished track 3 → resume track 4
    QCOMPARE(result.value.resumeTrack.id, QStringLiteral("al1-t4"));
    QCOMPARE(result.value.progress, 0.75);

    bool sawContinueQuery = false;
    for (const auto &request : m_mock->requests()) {
        const QUrlQuery q(request.query);
        if (q.queryItemValue("SortBy") == QLatin1String("DatePlayed")
            && q.queryItemValue("Limit") == QLatin1String("1")) {
            sawContinueQuery = true;
            // Ruling P1-9: the hero query must not require a completed play —
            // a partially played, never-finished track must still be able to
            // seed continue-listening. Recent/top/history queries (below)
            // keep Filters=IsPlayed.
            QVERIFY(!q.hasQueryItem("Filters"));
            QCOMPARE(q.queryItemValue("SortOrder"), QStringLiteral("Descending"));
        }
    }
    QVERIFY(sawContinueQuery);
}

void MusicRepositoryTest::continueListeningResumesPartiallyPlayedTrack()
{
    // Fix round 1, item 2: a partially played, never-completed track (Played
    // false, PlaybackPositionTicks > 0) must resume at its OWN index, not the
    // next one.
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "DatePlayed"}, {"Limit", "1"}}, 200,
                          page({playedTrack("al1-t2", "al1", "ar1", "2026-09-15T20:00:00Z",
                                            /*positionTicks=*/12'345)}));
    routeAlbum(QStringLiteral("al1"), 4);

    const auto result = waitFor(m_repo->continueListening(kLibrary));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.value.isValid());
    QCOMPARE(result.value.album.id, QStringLiteral("al1"));
    QCOMPARE(result.value.resumeIndex, 1); // "al1-t2" is index 1, partially played
    QCOMPARE(result.value.resumeTrack.id, QStringLiteral("al1-t2"));
}

void MusicRepositoryTest::continueListeningIsEmptyWithoutHistory()
{
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "DatePlayed"}, {"Limit", "1"}}, 200,
                          page({}));
    const auto result = waitFor(m_repo->continueListening(kLibrary));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(!result.value.isValid());
}

void MusicRepositoryTest::recentAlbumsDedupeInPlayOrder()
{
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "DatePlayed"}, {"Limit", "200"}}, 200,
                          page({playedTrack("x1", "alB", "ar1", "2026-09-15T20:00:00Z"),
                                playedTrack("x2", "alA", "ar1", "2026-09-15T19:00:00Z"),
                                playedTrack("x3", "alB", "ar1", "2026-09-15T18:00:00Z"),
                                playedTrack("x4", "alC", "ar1", "2026-09-15T17:00:00Z")}));
    // The server answers Ids in its own order; the repository restores play order.
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"Ids", "alB,alA"}}, 200,
                          page({albumJson("alA"), albumJson("alB")}));

    const auto result = waitFor(m_repo->recentAlbums(kLibrary, 2));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.size(), 2);
    QCOMPARE(result.value.at(0).id, QStringLiteral("alB"));
    QCOMPARE(result.value.at(1).id, QStringLiteral("alA"));
}

void MusicRepositoryTest::newAlbumsCountThisWeek()
{
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "MusicAlbum"}, {"SortBy", "DateCreated"}},
                          200, page({albumJson("n1"), albumJson("n2")}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "MusicAlbum"}, {"Limit", "0"}}, 200,
                          page({}, 6));
    const auto result = waitFor(m_repo->newAlbums(kLibrary, 20));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.albums.size(), 2);
    QCOMPARE(result.value.addedThisWeek, emby::caps::kMinDateCreated ? 6 : -1);
    if (emby::caps::kMinDateCreated) {
        bool sawBound = false;
        for (const auto &request : m_mock->requests()) {
            const QString bound = QUrlQuery(request.query).queryItemValue("MinDateCreated");
            if (!bound.isEmpty()) {
                sawBound = true;
                QCOMPARE(QDateTime::fromString(bound, Qt::ISODate), m_now.addDays(-7));
            }
        }
        QVERIFY(sawBound);
    }
}

void MusicRepositoryTest::genreBinsSampleCoversOnce()
{
    QJsonArray genres{QJsonObject{{"Id", "g1"}, {"Name", "Jazz"}, {"AlbumCount", 3}},
                      QJsonObject{{"Id", "g2"}, {"Name", "Rock"}, {"AlbumCount", 40}},
                      QJsonObject{{"Id", "g3"}, {"Name", "Folk"}, {"AlbumCount", 12}}};
    m_mock->addRoute("GET", "/MusicGenres", 200, page(genres));
    if (!emby::caps::kGenreItemCounts) {
        // Counts come from an album walk instead.
        QJsonArray albums;
        auto tagged = [](const QString &id, const QString &genreId, const QString &name) {
            QJsonObject album = albumJson(id);
            album.insert("GenreItems", QJsonArray{QJsonObject{{"Id", genreId}, {"Name", name}}});
            return album;
        };
        for (int i = 0; i < 40; ++i)
            albums.append(tagged("r" + QString::number(i), "g2", "Rock"));
        for (int i = 0; i < 12; ++i)
            albums.append(tagged("f" + QString::number(i), "g3", "Folk"));
        for (int i = 0; i < 3; ++i)
            albums.append(tagged("j" + QString::number(i), "g1", "Jazz"));
        m_mock->addQueryRoute("GET", itemsPath(),
                              Q{{"IncludeItemTypes", "MusicAlbum"}, {"Fields", "Genres"}}, 200, page(albums));
    }
    for (const char *id : {"g1", "g2", "g3"}) {
        m_mock->addQueryRoute("GET", itemsPath(), Q{{"GenreIds", id}, {"SortBy", "Random"}}, 200,
                              page({albumJson(QString("c-") + id)}));
    }

    const auto first = waitFor(m_repo->genreBins(kLibrary, 2));
    QVERIFY2(first.ok(), qPrintable(first.error));
    QCOMPARE(first.value.totalRecordCount, 3);
    QCOMPARE(first.value.items.size(), 2);
    QCOMPARE(first.value.items.at(0).name, QStringLiteral("Rock"));
    QCOMPARE(first.value.items.at(0).recordCount, 40);
    QCOMPARE(first.value.items.at(1).name, QStringLiteral("Folk"));
    QCOMPARE(first.value.items.at(0).covers.size(), 1);

    m_repo->markStale(Freshness::Everything);
    const int before = m_mock->requestCount();
    QVERIFY(waitFor(m_repo->genreBins(kLibrary, 2)).ok());
    for (qsizetype i = before; i < m_mock->requests().size(); ++i)
        QVERIFY(!QUrlQuery(m_mock->requests().at(i).query).hasQueryItem("GenreIds")); // covers cached
}

void MusicRepositoryTest::coverGenresSamplesOnlyBinsWithoutCovers()
{
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"GenreIds", "g1"}, {"SortBy", "Random"}}, 200,
                          page({albumJson("c-g1a"), albumJson("c-g1b")}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"GenreIds", "g3"}, {"SortBy", "Random"}}, 500, "{}");

    GenreBin jazz;
    jazz.id = QStringLiteral("g1");
    jazz.name = QStringLiteral("Jazz");
    GenreBin rock;
    rock.id = QStringLiteral("g2");
    rock.name = QStringLiteral("Rock");
    rock.covers = {ImageRef{QStringLiteral("kept"), QStringLiteral("Primary"), QStringLiteral("tag-kept")}};
    GenreBin folk;
    folk.id = QStringLiteral("g3");
    folk.name = QStringLiteral("Folk");

    const auto result = waitFor(m_repo->coverGenres(kLibrary, {jazz, rock, folk}));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.size(), 3);
    QCOMPARE(result.value.at(0).id, QStringLiteral("g1"));
    QCOMPARE(result.value.at(0).covers.size(), 2);
    QCOMPARE(result.value.at(1).covers.size(), 1);
    QCOMPARE(result.value.at(1).covers.first().itemId, QStringLiteral("kept"));
    QVERIFY(result.value.at(2).covers.isEmpty()); // a failed sample degrades, never fails

    for (const auto &request : m_mock->requests())
        QVERIFY(QUrlQuery(request.query).queryItemValue("GenreIds") != QStringLiteral("g2"));
}

void MusicRepositoryTest::coverGenresRefusesStaleCoversAfterIdentityChanges()
{
    // Binding ruling P3-R1 (T10 epoch ruling extended to coverGenres): a
    // cover sample still in flight when the identity changes must not let
    // the fan-out resolve success (even with an emptied-out bin) under a
    // session different from the one that started the call.
    m_mock->addRoute("GET", itemsPath(), 200, page({albumJson("late-cover")}));
    m_mock->setRouteDelay("GET", itemsPath(), 400);

    GenreBin jazz;
    jazz.id = QStringLiteral("g1");
    jazz.name = QStringLiteral("Jazz");

    auto future = m_repo->coverGenres(kLibrary, {jazz});
    // The genre-cover fetch is still held back by the mock; change identity
    // before it lands. setSession() aborts the outstanding request.
    m_client->setSession(kToken, QStringLiteral("ffffffffffffffffffffffffffffffff"));

    const auto result = waitFor(std::move(future));
    QVERIFY(!result.ok());
    QCOMPARE(result.error, QStringLiteral("request canceled"));
}

void MusicRepositoryTest::coverCacheExpiresAfterTtl()
{
    // perf-fix-b (2026-09-16): m_coverCache used to be built with a TTL of -1
    // (never expires), so a genre's sampled covers were pinned for the life
    // of the process (289 of them for the owner's library). This pins the
    // fix: once the TTL has actually elapsed, the next sample re-fetches
    // instead of serving the same cached list forever.
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"GenreIds", "g1"}, {"SortBy", "Random"}}, 200,
                          page({albumJson("c-g1")}));

    GenreBin jazz;
    jazz.id = QStringLiteral("g1");
    jazz.name = QStringLiteral("Jazz");

    QVERIFY(waitFor(m_repo->coverGenres(kLibrary, {jazz})).ok());
    const int afterFirst = m_mock->requestCount();

    m_now = m_now.addSecs(4 * 60);
    QVERIFY(waitFor(m_repo->coverGenres(kLibrary, {jazz})).ok());
    QCOMPARE(m_mock->requestCount(), afterFirst); // still within the cover cache's TTL

    m_now = m_now.addSecs(2 * 60); // 6 minutes after the first sample
    QVERIFY(waitFor(m_repo->coverGenres(kLibrary, {jazz})).ok());
    QVERIFY(m_mock->requestCount() > afterFirst); // expired: re-fetched, not served forever
}

void MusicRepositoryTest::topArtistsRankByPlays()
{
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "DatePlayed"}, {"Limit", "500"}}, 200,
                          page({playedTrack("a", "al", "arX", "2026-09-15T20:00:00Z"),
                                playedTrack("b", "al", "arY", "2026-09-15T19:00:00Z"),
                                playedTrack("c", "al", "arY", "2026-09-15T18:00:00Z")}));
    // Measured live 2026-09-17: /Users/{uid}/Items?Ids=<artist ids> returns 0
    // items unless IncludeItemTypes names MusicArtist, so the shelf and the
    // "More like" station came back empty against the real server.
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"Ids", "arY,arX"}, {"IncludeItemTypes", "MusicArtist"}}, 200,
                          page({QJsonObject{{"Id", "arX"}, {"Name", "X"}}, QJsonObject{{"Id", "arY"}, {"Name", "Y"}}}));
    const auto result = waitFor(m_repo->topArtists(kLibrary, 5));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.size(), 2);
    QCOMPARE(result.value.at(0).id, QStringLiteral("arY"));
}

void MusicRepositoryTest::stationsDescribeFiveTilesWithCovers()
{
    QList<Album> pool;
    for (int i = 0; i < 6; ++i)
        pool.append(emby::parseAlbum(albumJson("p" + QString::number(i))));
    Artist top;
    top.id = QStringLiteral("ar1");
    top.name = QStringLiteral("Nina Simone");

    const QList<Station> withTop = MusicRepository::stations(pool, top);
    QCOMPARE(withTop.size(), 5);
    QCOMPARE(withTop.at(3).kind, StationKind::MoreLike);
    QCOMPARE(withTop.at(3).label, QStringLiteral("More like Nina Simone"));
    QCOMPARE(withTop.at(3).seedId, QStringLiteral("ar1"));
    for (const Station &station : withTop)
        QCOMPARE(station.covers.size(), 4);

    const QList<Station> without = MusicRepository::stations({}, Artist{});
    QCOMPARE(without.size(), 4);
    QVERIFY(without.first().covers.isEmpty());
}

void MusicRepositoryTest::heavyRotationIsShuffledPlayedTracks()
{
    QJsonArray tracks;
    QStringList insertionOrder;
    for (int i = 0; i < 20; ++i) {
        const QString id = "h" + QString::number(i);
        tracks.append(trackJson(id, "al", 1, i + 1));
        insertionOrder.append(id);
    }
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "PlayCount"}, {"Filters", "IsPlayed"}}, 200,
                          page(tracks));
    Station station;
    station.kind = StationKind::HeavyRotation;
    const auto result = waitFor(m_repo->resolveStation(kLibrary, station));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.size(), 20);
    QStringList ids;
    for (const Track &track : result.value)
        ids.append(track.id);
    // Ruling P1-3: the brief's "!= alphabetically sorted" check was vacuous —
    // "h0".."h19" don't sort into insertion order to begin with, so it passed
    // even for an unshuffled result. Compare against the server's own
    // (insertion) order instead; seed 7 (set in init()) makes this
    // deterministic.
    QVERIFY(ids != insertionOrder);
    QCOMPARE(QSet<QString>(ids.cbegin(), ids.cend()).size(), 20);
}

void MusicRepositoryTest::moreLikeDeduplicatesInstantMix()
{
    m_mock->addRoute("GET", "/Items/ar1/InstantMix", 200,
                     page({trackJson("m1", "al", 1, 1), trackJson("m2", "al", 1, 2), trackJson("m1", "al", 1, 1)}));
    Station station;
    station.kind = StationKind::MoreLike;
    station.seedId = QStringLiteral("ar1");
    const auto result = waitFor(m_repo->resolveStation(kLibrary, station));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.size(), 2);
    QCOMPARE(QUrlQuery(m_mock->lastRequestFor("GET", "/Items/ar1/InstantMix").query).queryItemValue("UserId"),
             kUserId);
}

void MusicRepositoryTest::markStaleRefetchesListeningShelves()
{
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "DatePlayed"}, {"Limit", "200"}}, 200,
                          page({playedTrack("x1", "alA", "ar1", "2026-09-15T20:00:00Z")}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"Ids", "alA"}}, 200, page({albumJson("alA")}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"Filters", "IsFavorite"}, {"IncludeItemTypes", "MusicAlbum"}},
                          200, page({albumJson("fav")}));

    QVERIFY(waitFor(m_repo->recentAlbums(kLibrary)).ok());
    QVERIFY(waitFor(m_repo->forgottenFavourites(kLibrary)).ok());
    int count = m_mock->requestCount();

    m_repo->markStale(Freshness::Listening);
    QVERIFY(waitFor(m_repo->forgottenFavourites(kLibrary)).ok());
    QCOMPARE(m_mock->requestCount(), count); // favourites untouched
    QVERIFY(waitFor(m_repo->recentAlbums(kLibrary)).ok());
    QVERIFY(m_mock->requestCount() > count);

    count = m_mock->requestCount();
    m_repo->markStale(Freshness::Favourites);
    QVERIFY(waitFor(m_repo->forgottenFavourites(kLibrary)).ok());
    QVERIFY(m_mock->requestCount() > count);
}

void MusicRepositoryTest::browseAlbumsPagesWithTheTranslatedQuery()
{
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "MusicAlbum"}, {"StartIndex", "50"}}, 200,
                          page({albumJson("b1"), albumJson("b2")}, 10)); // under-reported total
    MusicQuery query;
    query.libraryId = kLibrary;
    query.sortKey = QStringLiteral("year");
    query.descending = true;
    query.favouritesOnly = true;
    const auto result = waitFor(m_repo->browseAlbums(query, 50, 50));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.items.size(), 2);
    QCOMPARE(result.value.startIndex, 50);
    QCOMPARE(result.value.totalRecordCount, 52);
    const QUrlQuery sent(m_mock->lastRequestFor("GET", itemsPath()).query);
    QCOMPARE(sent.queryItemValue("SortBy"), QStringLiteral("ProductionYear,PremiereDate,SortName"));
    QCOMPARE(sent.queryItemValue("SortOrder"), QStringLiteral("Descending"));
    QCOMPARE(sent.queryItemValue("Filters"), QStringLiteral("IsFavorite"));
    QCOMPARE(sent.queryItemValue("ParentId"), kLibrary);
}

void MusicRepositoryTest::browseArtistsPicksTheEndpointByMode()
{
    const QByteArray body = page({QJsonObject{{"Id", "ar1"}, {"Name", "A"}, {"AlbumCount", 4}}}, 1);
    m_mock->addRoute("GET", "/Artists/AlbumArtists", 200, body);
    m_mock->addRoute("GET", "/Artists", 200, body);
    MusicQuery query;
    query.libraryId = kLibrary;
    query.section = Section::Artists;
    query.letter = QStringLiteral("A");

    const auto filed = waitFor(m_repo->browseArtists(query, 0, 100));
    QVERIFY2(filed.ok(), qPrintable(filed.error));
    QCOMPARE(filed.value.items.first().albumCount, 4);
    const QUrlQuery sent(m_mock->lastRequestFor("GET", "/Artists/AlbumArtists").query);
    QCOMPARE(sent.queryItemValue("UserId"), kUserId);
    QCOMPARE(sent.queryItemValue("NameStartsWithOrGreater"), QStringLiteral("A"));
    QCOMPARE(sent.queryItemValue("NameLessThan"), QStringLiteral("B"));
    QVERIFY(sent.queryItemValue("Fields").contains("ItemCounts"));

    query.artistMode = ArtistMode::Everyone;
    QVERIFY(waitFor(m_repo->browseArtists(query, 0, 100)).ok());
    QCOMPARE(m_mock->lastRequestFor("GET", "/Artists").path, QStringLiteral("/Artists"));
}

void MusicRepositoryTest::browsePlaylistsKeepsAudioAndDropsVideo()
{
    // Defect 2, half 2 (visual-fix-1): dropping the ParentId scope means a bare
    // IncludeItemTypes=Playlist query also returns video playlists. MediaTypes=Audio
    // is asked for, but this server cannot be measured from here for whether it
    // honours MediaTypes on a Playlist query, so browsePlaylists() must also filter
    // client-side: keep "Audio" and rows with no MediaType at all, drop "Video".
    m_mock->addRoute("GET", itemsPath(), 200,
                     page({playlistJson("p-audio", QStringLiteral("Audio")),
                           playlistJson("p-video", QStringLiteral("Video")), playlistJson("p-unknown")}));
    MusicQuery query;
    query.libraryId = kLibrary;
    const auto result = waitFor(m_repo->browsePlaylists(query, 0, 100));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QStringList ids;
    for (const Playlist &playlist : result.value.items)
        ids.append(playlist.id);
    QCOMPARE(ids, (QStringList{QStringLiteral("p-audio"), QStringLiteral("p-unknown")}));

    const QUrlQuery sent(m_mock->lastRequestFor("GET", itemsPath()).query);
    QVERIFY(!sent.hasQueryItem("ParentId"));
    QCOMPARE(sent.queryItemValue("Recursive"), QStringLiteral("true"));
    QCOMPARE(sent.queryItemValue("MediaTypes"), QStringLiteral("Audio"));
}

void MusicRepositoryTest::browseTracksAddsHiResOnlyWhenMeasured()
{
    m_mock->addRoute("GET", itemsPath(), 200, page({trackJson("t", "al", 1, 1)}));
    MusicQuery query;
    query.libraryId = kLibrary;
    query.section = Section::Songs;
    query.format = FormatFilter::HiRes;
    QVERIFY(waitFor(m_repo->browseTracks(query, 0, 100)).ok());
    const QUrlQuery sent(m_mock->lastRequestFor("GET", itemsPath()).query);
    const QString key = QString::fromLatin1(emby::caps::kHiResQueryKey);
    if (emby::caps::kHiResFilter && emby::caps::kAudioCodecsFiltersAudio)
        QCOMPARE(sent.queryItemValue(key), QString::fromLatin1(emby::caps::kHiResQueryValue));
    else
        QVERIFY(key.isEmpty() || !sent.hasQueryItem(key));
}

void MusicRepositoryTest::sampleTracksIsRandomAcrossTheFilteredScope()
{
    m_mock->addRoute("GET", itemsPath(), 200, page({trackJson("s1", "al", 1, 1)}));
    MusicQuery query;
    query.libraryId = kLibrary;
    query.section = Section::Albums;
    query.sortKey = QStringLiteral("name");
    query.letter = QStringLiteral("Q");
    query.genreIds = {QStringLiteral("g1")};
    const auto result = waitFor(m_repo->sampleTracks(query, 150));
    QVERIFY2(result.ok(), qPrintable(result.error));
    const QUrlQuery sent(m_mock->lastRequestFor("GET", itemsPath()).query);
    QCOMPARE(sent.queryItemValue("IncludeItemTypes"), QStringLiteral("Audio"));
    QCOMPARE(sent.queryItemValue("SortBy"), QStringLiteral("Random"));
    QCOMPARE(sent.queryItemValue("GenreIds"), QStringLiteral("g1"));
    QCOMPARE(sent.queryItemValue("Limit"), QStringLiteral("150"));
    QVERIFY(!sent.hasQueryItem("NameLessThan"));
}

void MusicRepositoryTest::userDataChangeDropsCachesHoldingTheItem()
{
    routeAlbum(QStringLiteral("al1"), 3);
    routeAlbum(QStringLiteral("al2"), 3);
    QVERIFY(waitFor(m_repo->albumSleeve(QStringLiteral("al1"))).ok());
    QVERIFY(waitFor(m_repo->albumSleeve(QStringLiteral("al2"))).ok());
    const int cached = m_mock->requestCount();

    m_repo->noteUserDataChanged(QStringLiteral("al1-t2"));
    QVERIFY(waitFor(m_repo->albumSleeve(QStringLiteral("al2"))).ok());
    QCOMPARE(m_mock->requestCount(), cached); // untouched album stays cached
    QVERIFY(waitFor(m_repo->albumSleeve(QStringLiteral("al1"))).ok());
    QVERIFY(m_mock->requestCount() > cached);
}

// ⇄ Shuffle artist: one random draw of everything the artist performs on. It
// is not scoped to a library and never cached, because each press is a new draw.
void MusicRepositoryTest::artistTracksAreRandomAndScopedToTheArtist()
{
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(),
                          Q{{"ArtistIds", "ar1"}, {"IncludeItemTypes", "Audio"}, {"SortBy", "Random"}},
                          200, page({trackJson("r2", "alX", 1, 2), trackJson("r1", "alY", 1, 1)}));

    const auto first = waitFor(m_repo->artistTracks(QStringLiteral("ar1"), 150));
    QVERIFY2(first.ok(), qPrintable(first.error));
    QCOMPARE(first.value.size(), 2);
    QCOMPARE(first.value.at(0).id, QStringLiteral("r2")); // the server's order, untouched

    const QUrlQuery query(m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath()).query);
    QCOMPARE(query.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
    QCOMPARE(query.queryItemValue(QStringLiteral("Limit")), QStringLiteral("150"));
    QVERIFY(!query.hasQueryItem(QStringLiteral("ParentId")));

    const int before = m_mock->requestCount();
    const auto second = waitFor(m_repo->artistTracks(QStringLiteral("ar1"), 150));
    QVERIFY(second.ok());
    QCOMPARE(m_mock->requestCount(), before + 1);

    const auto none = waitFor(m_repo->artistTracks(QString()));
    QVERIFY(!none.ok());
}

QTEST_MAIN(MusicRepositoryTest)
#include "tst_music_repository.moc"
