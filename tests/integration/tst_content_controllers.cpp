#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

#include "MockEmbyServer.h"
#include "app/controllers/DetailsController.h"
#include "app/controllers/MusicController.h"
#include "app/controllers/PlaylistController.h"
#include "app/controllers/SearchController.h"
#include "app/controllers/SeriesController.h"
#include "app/models/MediaItemModel.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;

namespace {
const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
} // namespace

// SearchController, SeriesController and DetailsController shipped in M2 and
// have had no coverage since (ARCHITECTURE.md). Each has since grown behaviour
// that is invisible from the outside and easy to break: a debounce, a
// generation counter, and a second request whose reply can land after a newer
// one. Those are what this covers — not the happy path.
class ContentControllersTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void searchDebouncesAndClearsOnEmptyQuery();
    void searchFacetsUseTheirOwnEndpoints();
    void staleSearchReplyIsDiscarded();
    void detailsExposesEveryEnrichment();
    void detailsClearsBetweenItems();
    void seriesFetchesItsOwnRecord();
    void seriesIgnoresAnEmptyId();
    void playlistFetchesDoNotStrandEachOther();
    void musicRetargetDropsTheInFlightPage();

private:
    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
};

void ContentControllersTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    m_client = new emby::EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setSession(kToken, kUserId);
}

void ContentControllersTest::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_mock;
    m_mock = nullptr;
}

void ContentControllersTest::searchDebouncesAndClearsOnEmptyQuery()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"1\",\"Name\":\"Arrival\","
                                       "\"Type\":\"Movie\"}],\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Persons"), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Genres"), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));

    SearchController search(m_client);
    search.setQuery(QStringLiteral("arr"));
    // Typing must not have hit the network yet: the debounce is the whole
    // reason a per-keystroke search does not flood the server.
    QCOMPARE(search.model()->rowCount(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(search.model()->rowCount(), 1, 5000);

    // Clearing is immediate and must not leave the previous results on screen.
    search.setQuery(QString());
    QCOMPARE(search.model()->rowCount(), 0);
    QVERIFY(search.people().isEmpty());
    QVERIFY(search.genres().isEmpty());
}

void ContentControllersTest::searchFacetsUseTheirOwnEndpoints()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    // The live server reports TotalRecordCount = 0 on these endpoints even when
    // Items is populated, so anything that pages on that count renders nothing.
    // The fixture reproduces that exactly.
    m_mock->addRoute(
        QStringLiteral("GET"), QStringLiteral("/Persons"), 200,
        QByteArrayLiteral("{\"Items\":[{\"Id\":\"11818\",\"Name\":\"Tom Holland\","
                          "\"Type\":\"Person\",\"ImageTags\":{\"Primary\":\"tag\"}}],"
                          "\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Genres"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"8124\",\"Name\":\"Science "
                                       "Fiction\",\"Type\":\"Genre\"}],"
                                       "\"TotalRecordCount\":0}"));

    SearchController search(m_client);
    QSignalSpy facets(&search, &SearchController::facetsChanged);
    search.setQuery(QStringLiteral("holland"));
    QTRY_VERIFY_WITH_TIMEOUT(!search.people().isEmpty(), 5000);

    QCOMPARE(search.people().size(), 1);
    const QVariantMap person = search.people().first().toMap();
    QCOMPARE(person.value(QStringLiteral("name")).toString(), QStringLiteral("Tom Holland"));
    // Both image forms, so PersonCard does not have to parse a tag back out of
    // a URL the controller just built from it.
    QCOMPARE(person.value(QStringLiteral("primaryImageTag")).toString(), QStringLiteral("tag"));
    QVERIFY(person.value(QStringLiteral("imageUrl")).toString().contains(QStringLiteral("tag")));

    QTRY_VERIFY_WITH_TIMEOUT(!search.genres().isEmpty(), 5000);
    QCOMPARE(search.genres().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Science Fiction"));

    // BoxSet must be in the item query, or the search page's Collections
    // section is correct and permanently empty.
    const QString types =
        QUrlQuery(m_mock
                      ->lastRequestFor(QStringLiteral("GET"),
                                       QStringLiteral("/Users/%1/Items").arg(kUserId))
                      .query)
            .queryItemValue(QStringLiteral("IncludeItemTypes"));
    QVERIFY2(types.contains(QStringLiteral("BoxSet")), qPrintable(types));
}

void ContentControllersTest::staleSearchReplyIsDiscarded()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Persons"), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Genres"), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"1\",\"Name\":\"First\","
                                       "\"Type\":\"Movie\"}],\"TotalRecordCount\":1}"));

    SearchController search(m_client);
    search.setQuery(QStringLiteral("first"));
    QTRY_COMPARE_WITH_TIMEOUT(search.model()->rowCount(), 1, 5000);

    // A newer query supersedes the older one. Without the generation counter a
    // slow first reply lands after the second and the user sees results for a
    // query they have already replaced.
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"2\",\"Name\":\"Second\","
                                       "\"Type\":\"Movie\"},{\"Id\":\"3\",\"Name\":\"Third\","
                                       "\"Type\":\"Movie\"}],\"TotalRecordCount\":2}"));
    search.setQuery(QStringLiteral("second"));
    QTRY_COMPARE_WITH_TIMEOUT(search.model()->rowCount(), 2, 5000);
    QCOMPARE(search.model()->get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("Second"));
}

void ContentControllersTest::detailsExposesEveryEnrichment()
{
    const QByteArray payload = QByteArrayLiteral(
        "{\"Id\":\"1856141\",\"Name\":\"No Way Home\",\"Type\":\"Movie\","
        "\"Taglines\":[\"Enter the Multiverse.\"],"
        "\"Genres\":[\"Action\"],"
        "\"GenreItems\":[{\"Name\":\"Action\",\"Id\":8122}],"
        "\"Studios\":[{\"Name\":\"Marvel Studios\",\"Id\":8901}],"
        "\"People\":[{\"Name\":\"Tom Holland\",\"Id\":\"11818\",\"Role\":\"Peter\","
        "\"Type\":\"Actor\",\"PrimaryImageTag\":\"t\"},"
        "{\"Name\":\"Jon Watts\",\"Id\":\"20001\",\"Type\":\"Director\"}],"
        "\"ExternalUrls\":[{\"Name\":\"IMDb\",\"Url\":\"https://imdb.test/tt1\"}],"
        "\"RemoteTrailers\":[{\"Url\":\"https://youtu.be/a\"},{\"Url\":\"https://youtu.be/b\"}],"
        "\"ProviderIds\":{\"Imdb\":\"tt1\"},"
        "\"CriticRating\":93,\"PremiereDate\":\"2021-12-15T05:00:00.0000000Z\"}");
    m_mock->addRoute(QStringLiteral("GET"),
                     QStringLiteral("/Users/%1/Items/1856141").arg(kUserId), 200, payload);
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Items/1856141/Similar"), 200,
                     QByteArrayLiteral("{\"Items\":[]}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"77\",\"Name\":\"Spider-Man "
                                       "Collection\",\"Type\":\"BoxSet\"}],"
                                       "\"TotalRecordCount\":1}"));

    DetailsController details(m_client);
    details.load(QStringLiteral("1856141"));
    QTRY_VERIFY_WITH_TIMEOUT(!details.cast().isEmpty(), 5000);

    QCOMPARE(details.cast().size(), 1);
    QCOMPARE(details.crew().size(), 1); // a director is crew, not cast
    QCOMPARE(details.genres().size(), 1);
    QCOMPARE(details.genres().first().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("8122"));
    QCOMPARE(details.studios().size(), 1);
    QCOMPARE(details.externalLinks().size(), 1);
    QCOMPARE(details.criticRating(), 93.0);

    // Unnamed trailers are numbered rather than dropped: a trailer with no
    // label is still a trailer.
    QCOMPARE(details.trailers().size(), 2);
    QVERIFY(!details.trailers().first().toMap().value(QStringLiteral("name")).toString().isEmpty());

    // Collection membership is a SECOND request with its own signal, because
    // nothing on the item payload carries it.
    QTRY_VERIFY_WITH_TIMEOUT(!details.collections().isEmpty(), 5000);
    QCOMPARE(details.collections().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Spider-Man Collection"));
    const QString listIds =
        QUrlQuery(m_mock
                      ->lastRequestFor(QStringLiteral("GET"),
                                       QStringLiteral("/Users/%1/Items").arg(kUserId))
                      .query)
            .queryItemValue(QStringLiteral("ListItemIds"));
    QCOMPARE(listIds, QStringLiteral("1856141"));
}

void ContentControllersTest::detailsClearsBetweenItems()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/a").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Id\":\"a\",\"Name\":\"A\",\"Type\":\"Movie\","
                                       "\"Genres\":[\"Action\"],\"CriticRating\":93}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/b").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Id\":\"b\",\"Name\":\"B\",\"Type\":\"Movie\"}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Items/a/Similar"), 200,
                     QByteArrayLiteral("{\"Items\":[]}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Items/b/Similar"), 200,
                     QByteArrayLiteral("{\"Items\":[]}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));

    DetailsController details(m_client);
    details.load(QStringLiteral("a"));
    QTRY_COMPARE_WITH_TIMEOUT(details.criticRating(), 93.0, 5000);

    // The second item has no rating and no genres. Anything left over would be
    // shown as if it belonged to this item — the failure the user actually
    // notices, because it is plausible rather than empty.
    details.load(QStringLiteral("b"));
    QCOMPARE(details.criticRating(), 0.0);
    QVERIFY(details.genres().isEmpty());
    QVERIFY(details.trailers().isEmpty());
    QVERIFY(details.collections().isEmpty());
}

void ContentControllersTest::seriesFetchesItsOwnRecord()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Shows/100/Seasons"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"200\",\"Name\":\"Season 1\","
                                       "\"Type\":\"Season\",\"IndexNumber\":1}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Shows/100/Episodes"), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/100").arg(kUserId),
                     200,
                     QByteArrayLiteral("{\"Id\":\"100\",\"Name\":\"Severance\","
                                       "\"Type\":\"Series\",\"Status\":\"Continuing\","
                                       "\"ProductionYear\":2022,"
                                       "\"Overview\":\"Work-life balance.\"}"));

    SeriesController series(m_client);
    series.open(QStringLiteral("100"), QStringLiteral("Severance"));

    // The record is fetched, not looked up in a model. That is the whole point:
    // arriving from an episode's "go to series" means the series was never in
    // any model, and a lookup returns nothing.
    QTRY_VERIFY_WITH_TIMEOUT(!series.series().isEmpty(), 5000);
    QCOMPARE(series.series().value(QStringLiteral("itemId")).toString(), QStringLiteral("100"));
    QCOMPARE(series.series().value(QStringLiteral("status")).toString(),
             QStringLiteral("Continuing"));
    QCOMPARE(series.series().value(QStringLiteral("overview")).toString(),
             QStringLiteral("Work-life balance."));
}

// An empty id reaches here whenever the page is reset rather than opened. The
// guard used to cover the details fetch only, so /Shows//Episodes and
// /Shows//Seasons went out anyway and came back 404 — and `loading`, set above
// them, was left true because no 404 path clears it.
void ContentControllersTest::seriesIgnoresAnEmptyId()
{
    SeriesController series(m_client);
    series.open(QString(), QString());

    QVERIFY(!series.loading());
    QTest::qWait(150);
    QCOMPARE(m_mock->requestCount(), 0);
    QVERIFY(series.series().isEmpty());
    QCOMPARE(series.seasons()->rowCount(), 0);
}

// The playlist LIST and the members of the OPEN playlist are independent
// fetches. They shared one generation counter, so each silently cancelled the
// other: create()/rename()/remove() end in refresh(), and that refresh dropped
// an in-flight reload() reply ABOVE setLoading(false) — an empty list under a
// spinner that never stopped.
void ContentControllersTest::playlistFetchesDoNotStrandEachOther()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"pl1\",\"Name\":\"Road "
                                       "Trip\",\"Type\":\"Playlist\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Playlists/pl1/Items"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"t1\",\"Name\":\"Bad\","
                                       "\"Type\":\"Audio\",\"PlaylistItemId\":\"e1\"}],"
                                       "\"TotalRecordCount\":1}"));

    PlaylistController playlists(m_client);
    playlists.open(QStringLiteral("pl1"), QStringLiteral("Road Trip"));
    QVERIFY(playlists.loading());

    // Making a playlist from a picker refreshes the list while the open
    // playlist's members are still on the wire.
    playlists.refresh();

    QTRY_COMPARE_WITH_TIMEOUT(playlists.items()->rowCount(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!playlists.loading(), 5000);
    QCOMPARE(playlists.playlists()->rowCount(), 1);

    // And the other way round: opening a playlist must not cancel a page of the
    // list that is already on its way, or the picker silently loses it.
    PlaylistController second(m_client);
    second.refresh();
    second.open(QStringLiteral("pl1"), QStringLiteral("Road Trip"));

    QTRY_COMPARE_WITH_TIMEOUT(second.playlists()->rowCount(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(second.items()->rowCount(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!second.loading(), 5000);
}

// Re-targeting the music scope is a supersede like any other (ARCHITECTURE.md):
// a page requested for one library must not land under another. Clearing the
// models was not enough — the reply in flight put the old library's albums
// straight back, under the new library's name.
void ContentControllersTest::musicRetargetDropsTheInFlightPage()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"al1\",\"Name\":\"Kind Of "
                                       "Blue\",\"Type\":\"MusicAlbum\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Artists/AlbumArtists"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"ar1\",\"Name\":\"Miles "
                                       "Davis\",\"Type\":\"MusicArtist\"}],"
                                       "\"TotalRecordCount\":1}"));

    MusicController music(m_client);
    QSignalSpy albums(&music, &MusicController::albumsChanged);
    QSignalSpy artists(&music, &MusicController::artistsChanged);

    music.setLibrary(QStringLiteral("lib-a"));
    music.loadAlbums();
    music.loadArtists();
    QVERIFY(music.loading());

    music.setLibrary(QStringLiteral("lib-b"));
    // Nothing has been asked for in the new scope, and the replies that would
    // have cleared the flag are now going to be dropped, so the spinner has to
    // come down here or it never does.
    QVERIFY(!music.loading());

    music.loadAlbums();
    music.loadArtists();
    QTRY_COMPARE_WITH_TIMEOUT(albums.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(artists.count(), 1, 5000);

    // Library A's pages have long since been served; if either landed, its
    // model signal fired a second time.
    QTest::qWait(250);
    QCOMPARE(albums.count(), 1);
    QCOMPARE(artists.count(), 1);
    QCOMPARE(music.albums()->rowCount(), 1);
    QCOMPARE(music.artists()->rowCount(), 1);
    QCOMPARE(QUrlQuery(m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath).query)
                 .queryItemValue(QStringLiteral("ParentId")),
             QStringLiteral("lib-b"));
}

QTEST_MAIN(ContentControllersTest)
#include "tst_content_controllers.moc"
