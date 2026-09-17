#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <QUrlQuery>
#include <QtTest>

#include "MockEmbyServer.h"
#include "app/music/MusicRepository.h"
#include "server/emby/EmbyClient.h"
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

QTEST_MAIN(MusicRepositoryTest)
#include "tst_music_repository.moc"
