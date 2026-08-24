#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

#include "MockEmbyServer.h"
#include "app/controllers/DetailsController.h"
#include "app/controllers/LiveUpdateService.h"
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
    void supersededSearchAbortsEveryLane();
    void detailsExposesEveryEnrichment();
    void detailsPersonImageUsesSessionNamespace();
    void detailsClearsBetweenItems();
    void detailsEnsureJoinsUsableStateAndRetriesFailure();
    void detailsSimilarStatusFollowsItsOwnReply();
    void detailsPersonLoadRetiresAnInFlightItemOwner();
    void supersededDetailsAbortsEveryLane();
    void seriesFetchesItsOwnRecord();
    void seriesNextUnwatchedQueryIsBounded();
    void seriesNextUnwatchedRefetchesAfterPlayedChanges();
    void seriesIgnoresAnEmptyId();
    void seriesEmptySeasonsSettleAndRemainReusable();
    void seriesFailedSeasonsRetryTheSameScope();
    void seriesRestoresASeasonByStableIdentity();
    void sessionResetRetiresSearchDetailsAndSeriesReplies();
    void searchResetPreservesPerAccountHistory();
    void playlistFetchesDoNotStrandEachOther();
    void playlistSessionResetRetiresBothWalksAndMutations();
    void createWhileOpenLeavesTheOpenPlaylistAlone();
    void creationCarriesTheMediaTypeItWasGiven();
    void playlistMembersPageToTheEnd_data();
    void playlistMembersPageToTheEnd();
    void repeatedPlaylistMemberPageStopsTheWalk_data();
    void repeatedPlaylistMemberPageStopsTheWalk();
    void playlistMemberProgressUsesEntryIds();
    void playlistMemberWalkStopsAtTheSafetyLimit();
    void deletingOpenPlaylistEndsAnInFlightMemberLoad();
    void musicRetargetDropsTheInFlightPage();
    void thePlaylistListPagesToTheEnd();
    void aPlaylistWalkThatStoppedHalfwayIsRetried();

private:
    // `count` playlists numbered from `from`, as /Items answers them.
    static QByteArray playlistPage(int from, int count, int total)
    {
        QByteArray page = QByteArrayLiteral("{\"Items\":[");
        for (int i = 0; i < count; ++i) {
            if (i > 0)
                page += ',';
            page += QStringLiteral("{\"Id\":\"pl%1\",\"Name\":\"List %1\","
                                   "\"Type\":\"Playlist\"}")
                        .arg(from + i)
                        .toUtf8();
        }
        page += QStringLiteral("],\"TotalRecordCount\":%1}").arg(total).toUtf8();
        return page;
    }
    static QByteArray playlistMemberPage(int from, int count, int total,
                                         bool includeEntryIds = true)
    {
        QByteArray page = QByteArrayLiteral("{\"Items\":[");
        for (int i = 0; i < count; ++i) {
            if (i > 0)
                page += ',';
            page += QStringLiteral("{\"Id\":\"track%1\",\"Name\":\"Track %1\","
                                   "\"Type\":\"Audio\"%2}")
                        .arg(from + i)
                        .arg(includeEntryIds
                                 ? QStringLiteral(",\"PlaylistItemId\":\"entry%1\"").arg(from + i)
                                 : QString())
                        .toUtf8();
        }
        page += QStringLiteral("],\"TotalRecordCount\":%1}").arg(total).toUtf8();
        return page;
    }
    static QByteArray duplicateItemMemberPage(int from, int count, int total)
    {
        QByteArray page = QByteArrayLiteral("{\"Items\":[");
        for (int i = 0; i < count; ++i) {
            if (i > 0)
                page += ',';
            page += QStringLiteral("{\"Id\":\"same-track\",\"Name\":\"Encore\","
                                   "\"Type\":\"Audio\",\"PlaylistItemId\":\"entry%1\"}")
                        .arg(from + i)
                        .toUtf8();
        }
        page += QStringLiteral("],\"TotalRecordCount\":%1}").arg(total).toUtf8();
        return page;
    }
    int requestsFor(const QString &path) const
    {
        int count = 0;
        for (const MockEmbyServer::ReceivedRequest &request : m_mock->requests()) {
            if (request.path == path)
                ++count;
        }
        return count;
    }
    int indexOfNthRequest(const QString &path, int nth) const
    {
        int seen = 0;
        for (int i = 0; i < m_mock->requests().size(); ++i) {
            if (m_mock->requests().at(i).path != path)
                continue;
            if (seen == nth)
                return i;
            ++seen;
        }
        return -1;
    }

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
};

void ContentControllersTest::init()
{
    setEmbyImageSourceNamespace(QStringLiteral("test-session"));
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
    QCOMPARE(person.value(QStringLiteral("imageUrl")).toString(),
             QStringLiteral("image://emby/test-session/11818/Primary/tag"));

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

void ContentControllersTest::supersededSearchAbortsEveryLane()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"old-item\","
                                       "\"Name\":\"Old Item\",\"Type\":\"Movie\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Persons"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"old-person\","
                                       "\"Name\":\"Old Person\",\"Type\":\"Person\"}]}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Genres"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"old-genre\","
                                       "\"Name\":\"Old Genre\",\"Type\":\"Genre\"}]}"));
    for (const QString &path : {itemsPath, QStringLiteral("/Persons"),
                                QStringLiteral("/Genres")})
        m_mock->setRouteDelay(QStringLiteral("GET"), path, 1000);

    SearchController search(m_client);
    search.setQuery(QStringLiteral("old"));
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(itemsPath), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(QStringLiteral("/Persons")), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(QStringLiteral("/Genres")), 1, 5000);

    // Each route captures its response when the request arrives. Replacing the
    // route now makes the second query current while the old bodies stay held.
    for (const QString &path : {itemsPath, QStringLiteral("/Persons"),
                                QStringLiteral("/Genres")})
        m_mock->setRouteDelay(QStringLiteral("GET"), path, 0);
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"current-item\","
                                       "\"Name\":\"Current Item\",\"Type\":\"Movie\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Persons"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"current-person\","
                                       "\"Name\":\"Current Person\","
                                       "\"Type\":\"Person\"}]}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Genres"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"current-genre\","
                                       "\"Name\":\"Current Genre\","
                                       "\"Type\":\"Genre\"}]}"));

    search.setQuery(QStringLiteral("current"));
    for (const QString &path : {itemsPath, QStringLiteral("/Persons"),
                                QStringLiteral("/Genres")})
        QTRY_COMPARE_WITH_TIMEOUT(m_mock->abortedResponseCount(path), 1, 5000);

    QTRY_COMPARE_WITH_TIMEOUT(search.model()->rowCount(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(search.people().size(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(search.genres().size(), 1, 5000);
    QCOMPARE(search.model()->get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("Current Item"));
    QCOMPARE(search.people().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Current Person"));
    QCOMPARE(search.genres().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Current Genre"));
    QVERIFY(!search.searching());
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

void ContentControllersTest::detailsPersonImageUsesSessionNamespace()
{
    m_mock->addRoute(
        QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/person-1").arg(kUserId), 200,
        QByteArrayLiteral("{\"Id\":\"person-1\",\"Name\":\"Actor\",\"Type\":\"Person\","
                          "\"ImageTags\":{\"Primary\":\"headshot-tag\"}}"));
    DetailsController details(m_client);
    details.loadPerson(QStringLiteral("person-1"));
    QTRY_COMPARE_WITH_TIMEOUT(details.person().value(QStringLiteral("id")).toString(),
                              QStringLiteral("person-1"), 5000);
    QCOMPARE(details.person().value(QStringLiteral("imageUrl")).toString(),
             QStringLiteral("image://emby/test-session/person-1/Primary/headshot-tag"));
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
    QCOMPARE(details.itemId(), QStringLiteral("a"));
    QTRY_COMPARE_WITH_TIMEOUT(details.criticRating(), 93.0, 5000);

    // The second item has no rating and no genres. Anything left over would be
    // shown as if it belonged to this item — the failure the user actually
    // notices, because it is plausible rather than empty.
    details.load(QStringLiteral("b"));
    QCOMPARE(details.itemId(), QStringLiteral("b"));
    QCOMPARE(details.criticRating(), 0.0);
    QVERIFY(details.genres().isEmpty());
    QVERIFY(details.trailers().isEmpty());
    QVERIFY(details.collections().isEmpty());
}

void ContentControllersTest::detailsEnsureJoinsUsableStateAndRetriesFailure()
{
    const QString detailPath = QStringLiteral("/Users/%1/Items/joined").arg(kUserId);
    const QString similarPath = QStringLiteral("/Items/joined/Similar");
    const QString collectionsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), detailPath, 200,
                     QByteArrayLiteral("{\"Id\":\"joined\",\"Name\":\"Joined\","
                                       "\"Type\":\"Movie\",\"Taglines\":[\"Ready\"]}"));
    m_mock->addRoute(QStringLiteral("GET"), similarPath, 200,
                     QByteArrayLiteral("{\"Items\":[]}"));
    m_mock->addRoute(QStringLiteral("GET"), collectionsPath, 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->setRouteDelay(QStringLiteral("GET"), detailPath, 300);

    DetailsController details(m_client);
    details.ensureLoaded(QStringLiteral("joined"));
    QCOMPARE(details.itemId(), QStringLiteral("joined"));
    QVERIFY(details.itemLoading());
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(detailPath), 1, 5000);

    // A reconstructed page joins both a live request and its settled result.
    details.ensureLoaded(QStringLiteral("joined"));
    QTest::qWait(50);
    QCOMPARE(requestsFor(detailPath), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!details.itemLoading(), 5000);
    QCOMPARE(details.tagline(), QStringLiteral("Ready"));
    details.ensureLoaded(QStringLiteral("joined"));
    QTest::qWait(50);
    QCOMPARE(requestsFor(detailPath), 1);

    // A failed primary response must not leave the same id certifying that the
    // empty controller state is reusable. Re-registering the fixture makes the
    // next ensure an observable retry.
    m_mock->addRoute(QStringLiteral("GET"), detailPath, 500, QByteArrayLiteral("{}"));
    details.load(QStringLiteral("joined"));
    QTRY_VERIFY_WITH_TIMEOUT(details.itemId().isEmpty(), 5000);
    QCOMPARE(requestsFor(detailPath), 2);

    m_mock->addRoute(QStringLiteral("GET"), detailPath, 200,
                     QByteArrayLiteral("{\"Id\":\"joined\",\"Name\":\"Joined\","
                                       "\"Type\":\"Movie\",\"Taglines\":[\"Retried\"]}"));
    details.ensureLoaded(QStringLiteral("joined"));
    QTRY_COMPARE_WITH_TIMEOUT(details.tagline(), QStringLiteral("Retried"), 5000);
    QCOMPARE(details.itemId(), QStringLiteral("joined"));
    QCOMPARE(requestsFor(detailPath), 3);
}

void ContentControllersTest::detailsSimilarStatusFollowsItsOwnReply()
{
    const QString collectionsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), collectionsPath, 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));

    // Similar can settle before the primary item. Its owner is usable as soon
    // as its own model is populated; the primary lane must remain honestly
    // live until its delayed reply arrives.
    const QString slowDetails = QStringLiteral("/Users/%1/Items/slow-details").arg(kUserId);
    const QString fastSimilar = QStringLiteral("/Items/slow-details/Similar");
    m_mock->addRoute(QStringLiteral("GET"), slowDetails, 200,
                     QByteArrayLiteral("{\"Id\":\"slow-details\",\"Name\":\"Slow\","
                                       "\"Type\":\"Movie\"}"));
    m_mock->addRoute(
        QStringLiteral("GET"), fastSimilar, 200,
        QByteArrayLiteral("{\"Items\":[{\"Id\":\"fast-similar\",\"Name\":\"Fast Similar\","
                          "\"Type\":\"Movie\"}]}"));
    m_mock->setRouteDelay(QStringLiteral("GET"), slowDetails, 800);

    DetailsController details(m_client);
    details.load(QStringLiteral("slow-details"));
    QVERIFY(details.itemLoading());
    QVERIFY(details.similarLoading());
    QTRY_COMPARE_WITH_TIMEOUT(details.similar()->rowCount(), 1, 5000);
    QVERIFY(!details.similarLoading());
    QVERIFY(details.itemLoading());
    QTRY_VERIFY_WITH_TIMEOUT(!details.itemLoading(), 5000);

    // Reverse the order. A completed primary item cannot declare the similar
    // rail terminal while that rail's own request is still outstanding.
    const QString fastDetails = QStringLiteral("/Users/%1/Items/slow-similar").arg(kUserId);
    const QString slowSimilar = QStringLiteral("/Items/slow-similar/Similar");
    m_mock->addRoute(QStringLiteral("GET"), fastDetails, 200,
                     QByteArrayLiteral("{\"Id\":\"slow-similar\",\"Name\":\"Fast\","
                                       "\"Type\":\"Movie\",\"Taglines\":[\"Ready\"]}"));
    m_mock->addRoute(
        QStringLiteral("GET"), slowSimilar, 200,
        QByteArrayLiteral("{\"Items\":[{\"Id\":\"late-similar\",\"Name\":\"Late Similar\","
                          "\"Type\":\"Movie\"}]}"));
    m_mock->setRouteDelay(QStringLiteral("GET"), slowSimilar, 800);

    details.load(QStringLiteral("slow-similar"));
    QTRY_VERIFY_WITH_TIMEOUT(!details.itemLoading(), 5000);
    QCOMPARE(details.tagline(), QStringLiteral("Ready"));
    QVERIFY(details.similarLoading());
    QCOMPARE(details.similar()->rowCount(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(details.similar()->rowCount(), 1, 5000);
    QVERIFY(!details.similarLoading());

    details.resetSessionState();
    QVERIFY(!details.similarLoading());
}

void ContentControllersTest::detailsPersonLoadRetiresAnInFlightItemOwner()
{
    const QString detailPath = QStringLiteral("/Users/%1/Items/interrupted").arg(kUserId);
    const QString personPath = QStringLiteral("/Users/%1/Items/person").arg(kUserId);
    const QString similarPath = QStringLiteral("/Items/interrupted/Similar");
    const QString collectionsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), detailPath, 200,
                     QByteArrayLiteral("{\"Id\":\"interrupted\",\"Name\":\"Item\","
                                       "\"Type\":\"Movie\"}"));
    m_mock->addRoute(QStringLiteral("GET"), personPath, 200,
                     QByteArrayLiteral("{\"Id\":\"person\",\"Name\":\"Person\","
                                       "\"Type\":\"Person\"}"));
    m_mock->addRoute(QStringLiteral("GET"), similarPath, 200,
                     QByteArrayLiteral("{\"Items\":[]}"));
    m_mock->addRoute(QStringLiteral("GET"), collectionsPath, 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->setRouteDelay(QStringLiteral("GET"), detailPath, 1000);
    m_mock->setRouteDelay(QStringLiteral("GET"), personPath, 300);

    DetailsController details(m_client);
    details.ensureLoaded(QStringLiteral("interrupted"));
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(detailPath), 1, 5000);
    QVERIFY(details.itemLoading());

    details.loadPerson(QStringLiteral("person"));
    QVERIFY(details.itemId().isEmpty());
    QVERIFY(!details.itemLoading());
    QTRY_COMPARE_WITH_TIMEOUT(m_mock->abortedResponseCount(detailPath), 1, 5000);

    // Returning to the interrupted item cannot join the cancelled request.
    m_mock->setRouteDelay(QStringLiteral("GET"), detailPath, 0);
    details.ensureLoaded(QStringLiteral("interrupted"));
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(detailPath), 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!details.itemLoading(), 5000);
    QCOMPARE(details.itemId(), QStringLiteral("interrupted"));
}

void ContentControllersTest::supersededDetailsAbortsEveryLane()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    const QString oldDetailsPath = QStringLiteral("/Users/%1/Items/old").arg(kUserId);
    const QString oldSimilarPath = QStringLiteral("/Items/old/Similar");
    m_mock->addRoute(QStringLiteral("GET"), oldDetailsPath, 200,
                     QByteArrayLiteral("{\"Id\":\"old\",\"Name\":\"Old\","
                                       "\"Type\":\"Movie\",\"Taglines\":[\"Old Tag\"]}"));
    m_mock->addRoute(QStringLiteral("GET"), oldSimilarPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"old-similar\","
                                       "\"Name\":\"Old Similar\",\"Type\":\"Movie\"}]}"));
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"old-collection\","
                                       "\"Name\":\"Old Collection\",\"Type\":\"BoxSet\"}],"
                                       "\"TotalRecordCount\":1}"));
    for (const QString &path : {oldDetailsPath, oldSimilarPath, itemsPath})
        m_mock->setRouteDelay(QStringLiteral("GET"), path, 1000);

    DetailsController details(m_client);
    details.load(QStringLiteral("old"));
    for (const QString &path : {oldDetailsPath, oldSimilarPath, itemsPath})
        QTRY_COMPARE_WITH_TIMEOUT(requestsFor(path), 1, 5000);

    const QString currentDetailsPath =
        QStringLiteral("/Users/%1/Items/current").arg(kUserId);
    const QString currentSimilarPath = QStringLiteral("/Items/current/Similar");
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 0);
    m_mock->addRoute(QStringLiteral("GET"), currentDetailsPath, 200,
                     QByteArrayLiteral("{\"Id\":\"current\",\"Name\":\"Current\","
                                       "\"Type\":\"Movie\","
                                       "\"Taglines\":[\"Current Tag\"]}"));
    m_mock->addRoute(
        QStringLiteral("GET"), currentSimilarPath, 200,
        QByteArrayLiteral("{\"Items\":[{\"Id\":\"current-similar\","
                          "\"Name\":\"Current Similar\",\"Type\":\"Movie\"}]}"));
    m_mock->addRoute(
        QStringLiteral("GET"), itemsPath, 200,
        QByteArrayLiteral("{\"Items\":[{\"Id\":\"current-collection\","
                          "\"Name\":\"Current Collection\",\"Type\":\"BoxSet\"}],"
                          "\"TotalRecordCount\":1}"));

    details.load(QStringLiteral("current"));
    for (const QString &path : {oldDetailsPath, oldSimilarPath, itemsPath})
        QTRY_COMPARE_WITH_TIMEOUT(m_mock->abortedResponseCount(path), 1, 5000);

    QTRY_COMPARE_WITH_TIMEOUT(details.tagline(), QStringLiteral("Current Tag"), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(details.collections().size(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(details.similar()->rowCount(), 1, 5000);
    QCOMPARE(details.collections().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Current Collection"));
    QCOMPARE(details.similar()->get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("Current Similar"));
}

void ContentControllersTest::seriesFetchesItsOwnRecord()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Shows/100/Seasons"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"200\",\"Name\":\"Season 1\","
                                       "\"Type\":\"Season\",\"IndexNumber\":1}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Shows/100/Episodes"), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"),
                     QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
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

void ContentControllersTest::seriesNextUnwatchedQueryIsBounded()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Shows/100/Seasons"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"season-1\","
                                       "\"Name\":\"Season 1\",\"Type\":\"Season\","
                                       "\"IndexNumber\":1}],\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Shows/100/Episodes"), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"),
                     QStringLiteral("/Users/%1/Items/100").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Id\":\"100\",\"Name\":\"Series\","
                                       "\"Type\":\"Series\"}"));
    // The answer is deliberately in season two while the page opens season
    // one. A selected-season lookup cannot produce this result.
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"s2e1\","
                                       "\"Name\":\"Season Two Premiere\","
                                       "\"Type\":\"Episode\","
                                       "\"ParentIndexNumber\":2,\"IndexNumber\":1}],"
                                       "\"TotalRecordCount\":400}"));

    SeriesController series(m_client);
    series.open(QStringLiteral("100"), QStringLiteral("Series"));

    QTRY_COMPARE_WITH_TIMEOUT(
        series.nextUnwatched().value(QStringLiteral("itemId")).toString(),
        QStringLiteral("s2e1"), 5000);
    QCOMPARE(requestsFor(itemsPath), 1);

    const QUrlQuery query(m_mock->lastRequestFor(QStringLiteral("GET"), itemsPath).query);
    QCOMPARE(query.queryItemValue(QStringLiteral("ParentId")), QStringLiteral("100"));
    QCOMPARE(query.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
    QCOMPARE(query.queryItemValue(QStringLiteral("IncludeItemTypes")),
             QStringLiteral("Episode"));
    QCOMPARE(query.queryItemValue(QStringLiteral("Filters")), QStringLiteral("IsUnplayed"));
    QCOMPARE(query.queryItemValue(QStringLiteral("SortBy")),
             QStringLiteral("PremiereDate,SortName"));
    QCOMPARE(query.queryItemValue(QStringLiteral("SortOrder")), QStringLiteral("Ascending"));
    QCOMPARE(query.queryItemValue(QStringLiteral("StartIndex")), QStringLiteral("0"));
    QCOMPARE(query.queryItemValue(QStringLiteral("Limit")), QStringLiteral("1"));
    QVERIFY(!query.hasQueryItem(QStringLiteral("Fields")));
}

void ContentControllersTest::seriesNextUnwatchedRefetchesAfterPlayedChanges()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Shows/100/Seasons"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"season-1\","
                                       "\"Name\":\"Season 1\",\"Type\":\"Season\","
                                       "\"IndexNumber\":1}],\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Shows/100/Episodes"), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"),
                     QStringLiteral("/Users/%1/Items/100").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Id\":\"100\",\"Name\":\"Series\","
                                       "\"Type\":\"Series\"}"));
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"first\","
                                       "\"Name\":\"First\",\"Type\":\"Episode\"}],"
                                       "\"TotalRecordCount\":2}"));

    SeriesController series(m_client);
    LiveUpdateService live(m_client, nullptr);
    series.bindLiveUpdates(&live);
    series.open(QStringLiteral("100"), QStringLiteral("Series"));
    QTRY_COMPARE_WITH_TIMEOUT(
        series.nextUnwatched().value(QStringLiteral("itemId")).toString(),
        QStringLiteral("first"), 5000);

    // Marking the current answer played advances to the server's next bounded
    // result, including when that episode belongs to another season.
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"second\","
                                       "\"Name\":\"Second\",\"Type\":\"Episode\","
                                       "\"ParentIndexNumber\":2}],"
                                       "\"TotalRecordCount\":1}"));
    series.notePlayed(QStringLiteral("first"), true);
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(itemsPath), 2, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(
        series.nextUnwatched().value(QStringLiteral("itemId")).toString(),
        QStringLiteral("second"), 5000);

    // Marking an earlier episode unplayed can move the answer backwards even
    // though that episode is not in the selected season model.
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"first\","
                                       "\"Name\":\"First\",\"Type\":\"Episode\"}],"
                                       "\"TotalRecordCount\":2}"));
    series.notePlayed(QStringLiteral("first"), false);
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(itemsPath), 3, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(
        series.nextUnwatched().value(QStringLiteral("itemId")).toString(),
        QStringLiteral("first"), 5000);

    // An empty bounded answer means every episode is now watched.
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    series.notePlayed(QStringLiteral("first"), true);
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(itemsPath), 4, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!series.hasNextUnwatched(), 5000);

    // A phone/player update reaches SeriesController through the same
    // debounced invalidation channel as every other live consumer and causes
    // one bounded reconciliation, not a retained whole-series snapshot.
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"live-first\","
                                       "\"Name\":\"Changed Elsewhere\","
                                       "\"Type\":\"Episode\"}],"
                                       "\"TotalRecordCount\":1}"));
    live.refreshNow();
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(itemsPath), 5, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(
        series.nextUnwatched().value(QStringLiteral("itemId")).toString(),
        QStringLiteral("live-first"), 5000);
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

void ContentControllersTest::seriesEmptySeasonsSettleAndRemainReusable()
{
    const QString seasonsPath = QStringLiteral("/Shows/empty/Seasons");
    const QString detailsPath = QStringLiteral("/Users/%1/Items/empty").arg(kUserId);
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), seasonsPath, 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"), detailsPath, 200,
                     QByteArrayLiteral("{\"Id\":\"empty\",\"Name\":\"Empty\","
                                       "\"Type\":\"Series\"}"));
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));

    SeriesController series(m_client);
    series.ensureOpen(QStringLiteral("empty"), QStringLiteral("Empty"), {});
    QVERIFY(series.loading());
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(seasonsPath), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!series.loading(), 5000);
    QCOMPARE(series.seriesId(), QStringLiteral("empty"));
    QCOMPARE(series.seasons()->rowCount(), 0);
    QCOMPARE(series.currentSeason(), -1);
    QVERIFY(series.currentSeasonId().isEmpty());

    // A successful empty list is a terminal, usable scope rather than a
    // failure sentinel. Reconstructing the same history route must not loop.
    series.ensureOpen(QStringLiteral("empty"), QStringLiteral("Empty"), {});
    QTest::qWait(100);
    QCOMPARE(requestsFor(seasonsPath), 1);
    QVERIFY(!series.loading());
}

void ContentControllersTest::seriesFailedSeasonsRetryTheSameScope()
{
    const QString seasonsPath = QStringLiteral("/Shows/retry/Seasons");
    const QString episodesPath = QStringLiteral("/Shows/retry/Episodes");
    const QString detailsPath = QStringLiteral("/Users/%1/Items/retry").arg(kUserId);
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), seasonsPath, 500, QByteArrayLiteral("{}"));
    m_mock->addRoute(QStringLiteral("GET"), detailsPath, 200,
                     QByteArrayLiteral("{\"Id\":\"retry\",\"Name\":\"Retry\","
                                       "\"Type\":\"Series\"}"));
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));

    SeriesController series(m_client);
    series.ensureOpen(QStringLiteral("retry"), QStringLiteral("Retry"),
                      QStringLiteral("season-b"));
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(seasonsPath), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!series.loading(), 5000);
    // The display identity may remain, but it cannot certify the failed scope
    // as reusable: ensureOpen owns that distinction and retries the same id.
    QCOMPARE(series.seriesId(), QStringLiteral("retry"));
    QCOMPARE(series.seasons()->rowCount(), 0);

    m_mock->addRoute(
        QStringLiteral("GET"), seasonsPath, 200,
        QByteArrayLiteral("{\"Items\":["
                          "{\"Id\":\"season-a\",\"Name\":\"Season A\",\"Type\":\"Season\"},"
                          "{\"Id\":\"season-b\",\"Name\":\"Season B\",\"Type\":\"Season\"}],"
                          "\"TotalRecordCount\":2}"));
    m_mock->addRoute(QStringLiteral("GET"), episodesPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"episode-b\","
                                       "\"Name\":\"Episode B\",\"Type\":\"Episode\"}],"
                                       "\"TotalRecordCount\":1}"));

    series.ensureOpen(QStringLiteral("retry"), QStringLiteral("Retry"),
                      QStringLiteral("season-b"));
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(seasonsPath), 2, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(series.currentSeasonId(), QStringLiteral("season-b"), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(series.episodes()->rowCount(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!series.loading(), 5000);
    const QUrlQuery query(m_mock->lastRequestFor(QStringLiteral("GET"), episodesPath).query);
    QCOMPARE(query.queryItemValue(QStringLiteral("SeasonId")), QStringLiteral("season-b"));
}

void ContentControllersTest::seriesRestoresASeasonByStableIdentity()
{
    const QString seasonsPath = QStringLiteral("/Shows/stable/Seasons");
    const QString episodesPath = QStringLiteral("/Shows/stable/Episodes");
    const QString detailsPath = QStringLiteral("/Users/%1/Items/stable").arg(kUserId);
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(
        QStringLiteral("GET"), seasonsPath, 200,
        QByteArrayLiteral("{\"Items\":["
                          "{\"Id\":\"season-a\",\"Name\":\"Season A\",\"Type\":\"Season\"},"
                          "{\"Id\":\"season-b\",\"Name\":\"Season B\",\"Type\":\"Season\"}],"
                          "\"TotalRecordCount\":2}"));
    m_mock->addRoute(QStringLiteral("GET"), episodesPath, 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"), detailsPath, 200,
                     QByteArrayLiteral("{\"Id\":\"stable\",\"Name\":\"Stable\","
                                       "\"Type\":\"Series\"}"));
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));

    SeriesController series(m_client);
    series.open(QStringLiteral("stable"), QStringLiteral("Stable"));
    QTRY_COMPARE_WITH_TIMEOUT(series.currentSeasonId(), QStringLiteral("season-a"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!series.loading(), 5000);
    QCOMPARE(requestsFor(seasonsPath), 1);
    QCOMPARE(requestsFor(episodesPath), 1);

    // Reconstructing another history entry for the same series selects the
    // stable server id without discarding and re-fetching the seasons scope.
    series.ensureOpen(QStringLiteral("stable"), QStringLiteral("Stable"),
                      QStringLiteral("season-b"));
    QTRY_COMPARE_WITH_TIMEOUT(series.currentSeasonId(), QStringLiteral("season-b"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!series.loading(), 5000);
    QCOMPARE(requestsFor(seasonsPath), 1);
    QCOMPARE(requestsFor(episodesPath), 2);
    const QUrlQuery query(m_mock->lastRequestFor(QStringLiteral("GET"), episodesPath).query);
    QCOMPARE(query.queryItemValue(QStringLiteral("SeasonId")), QStringLiteral("season-b"));
}

void ContentControllersTest::sessionResetRetiresSearchDetailsAndSeriesReplies()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);

    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"a-search\",\"Name\":\"A "
                                       "Search\",\"Type\":\"Movie\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Persons"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"a-person\",\"Name\":\"A "
                                       "Person\",\"Type\":\"Person\"}]}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Genres"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"a-genre\",\"Name\":\"A "
                                       "Genre\",\"Type\":\"Genre\"}]}"));
    for (const QString &path : {itemsPath, QStringLiteral("/Persons"),
                                QStringLiteral("/Genres")})
        m_mock->setRouteDelay(QStringLiteral("GET"), path, 300);

    SearchController search(m_client);
    search.setQuery(QStringLiteral("account a"));
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(itemsPath), 1, 5000);
    QVERIFY(search.searching());
    search.resetSessionState();
    QVERIFY(search.query().isEmpty());
    QVERIFY(!search.searching());
    QCOMPARE(search.model()->rowCount(), 0);
    QTest::qWait(400);
    QCOMPARE(search.model()->rowCount(), 0);
    QVERIFY(search.people().isEmpty());
    QVERIFY(search.genres().isEmpty());

    const QString detailPath = QStringLiteral("/Users/%1/Items/shared").arg(kUserId);
    const QString similarPath = QStringLiteral("/Items/shared/Similar");
    m_mock->addRoute(QStringLiteral("GET"), detailPath, 200,
                     QByteArrayLiteral("{\"Id\":\"shared\",\"Name\":\"A Detail\","
                                       "\"Type\":\"Movie\",\"Genres\":[\"A Genre\"]}"));
    m_mock->addRoute(QStringLiteral("GET"), similarPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"similar-a\","
                                       "\"Name\":\"A Similar\",\"Type\":\"Movie\"}]}"));
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"collection-a\","
                                       "\"Name\":\"A Collection\",\"Type\":\"BoxSet\"}],"
                                       "\"TotalRecordCount\":1}"));
    for (const QString &path : {detailPath, similarPath, itemsPath})
        m_mock->setRouteDelay(QStringLiteral("GET"), path, 300);

    DetailsController details(m_client);
    details.load(QStringLiteral("shared"));
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(detailPath), 1, 5000);
    details.resetSessionState();
    QVERIFY(details.genres().isEmpty());
    QVERIFY(details.collections().isEmpty());
    QCOMPARE(details.similar()->rowCount(), 0);
    QTest::qWait(400);
    QVERIFY(details.genres().isEmpty());
    QVERIFY(details.collections().isEmpty());
    QCOMPARE(details.similar()->rowCount(), 0);

    const QString seasonsPath = QStringLiteral("/Shows/shared/Seasons");
    const QString episodesPath = QStringLiteral("/Shows/shared/Episodes");
    m_mock->addRoute(QStringLiteral("GET"), seasonsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"season-a\",\"Name\":\"A "
                                       "Season\",\"Type\":\"Season\",\"IndexNumber\":1}]}"));
    m_mock->addRoute(QStringLiteral("GET"), episodesPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"episode-a\",\"Name\":\"A "
                                       "Episode\",\"Type\":\"Episode\","
                                       "\"ParentIndexNumber\":1}]}"));
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"episode-a\",\"Name\":\"A "
                                       "Episode\",\"Type\":\"Episode\","
                                       "\"ParentIndexNumber\":1}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), detailPath, 200,
                     QByteArrayLiteral("{\"Id\":\"shared\",\"Name\":\"A Series\","
                                       "\"Type\":\"Series\"}"));
    for (const QString &path : {seasonsPath, episodesPath, detailPath, itemsPath})
        m_mock->setRouteDelay(QStringLiteral("GET"), path, 300);

    SeriesController series(m_client);
    series.open(QStringLiteral("shared"), QStringLiteral("A Series"));
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(seasonsPath), 1, 5000);
    QVERIFY(series.loading());
    series.resetSessionState();
    QVERIFY(!series.loading());
    QVERIFY(series.seriesId().isEmpty());
    QVERIFY(series.seriesName().isEmpty());
    QVERIFY(series.series().isEmpty());
    QCOMPARE(series.seasons()->rowCount(), 0);
    QCOMPARE(series.episodes()->rowCount(), 0);
    QTest::qWait(400);
    QVERIFY(series.series().isEmpty());
    QCOMPARE(series.seasons()->rowCount(), 0);
    QCOMPARE(series.episodes()->rowCount(), 0);
}

void ContentControllersTest::searchResetPreservesPerAccountHistory()
{
    const auto userB = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    SearchController search(m_client);
    search.clearRecentQueries();
    search.noteQueryUsed(QStringLiteral("A only"));
    QCOMPARE(search.recentQueries(), QStringList{QStringLiteral("A only")});

    search.resetSessionState();
    QVERIFY(search.recentQueries().isEmpty());
    m_client->setSession(kToken, userB);
    search.clearRecentQueries();
    search.noteQueryUsed(QStringLiteral("B only"));
    QCOMPARE(search.recentQueries(), QStringList{QStringLiteral("B only")});

    m_client->setSession(kToken, kUserId);
    QCOMPARE(search.recentQueries(), QStringList{QStringLiteral("A only")});
    search.clearRecentQueries();
    m_client->setSession(kToken, userB);
    QCOMPARE(search.recentQueries(), QStringList{QStringLiteral("B only")});
    search.clearRecentQueries();
}

void ContentControllersTest::playlistSessionResetRetiresBothWalksAndMutations()
{
    const auto userB = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    const QString listA = QStringLiteral("/Users/%1/Items").arg(kUserId);
    const QString listB = QStringLiteral("/Users/%1/Items").arg(userB);
    const QString members = QStringLiteral("/Playlists/shared/Items");
    m_mock->addRoute(QStringLiteral("GET"), listA, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"a-list\",\"Name\":\"A "
                                       "List\",\"Type\":\"Playlist\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), members, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"track-a\",\"Name\":\"A "
                                       "Track\",\"Type\":\"Audio\","
                                       "\"PlaylistItemId\":\"entry-a\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Playlists"), 200,
                     QByteArrayLiteral("{\"Id\":\"created-a\",\"ItemAddedCount\":1}"));
    for (const auto &route : {qMakePair(QStringLiteral("GET"), listA),
                              qMakePair(QStringLiteral("GET"), members),
                              qMakePair(QStringLiteral("POST"), QStringLiteral("/Playlists"))})
        m_mock->setRouteDelay(route.first, route.second, 350);

    PlaylistController playlists(m_client);
    QSignalSpy succeeded(&playlists, &PlaylistController::actionSucceeded);
    QSignalSpy mutated(&playlists, &PlaylistController::playlistsMutated);
    playlists.refresh();
    playlists.open(QStringLiteral("shared"), QStringLiteral("A List"));
    playlists.create(QStringLiteral("A Created"), {QStringLiteral("track-a")});
    QTRY_COMPARE_WITH_TIMEOUT(m_mock->requestCount(), 3, 5000);
    QVERIFY(playlists.loading());

    playlists.resetSessionState();
    QVERIFY(!playlists.loading());
    QVERIFY(playlists.currentId().isEmpty());
    QVERIFY(playlists.errorMessage().isEmpty());
    QCOMPARE(playlists.playlists()->rowCount(), 0);
    QCOMPARE(playlists.items()->rowCount(), 0);
    QVERIFY(!playlists.playlistsComplete());

    m_client->setSession(kToken, userB);
    m_mock->addRoute(QStringLiteral("GET"), listB, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"shared\",\"Name\":\"B "
                                       "List\",\"Type\":\"Playlist\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), members, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"track-b\",\"Name\":\"B "
                                       "Track\",\"Type\":\"Audio\","
                                       "\"PlaylistItemId\":\"entry-b\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->setRouteDelay(QStringLiteral("GET"), members, 0);
    playlists.refresh();
    playlists.open(QStringLiteral("shared"), QStringLiteral("B List"));
    QTRY_VERIFY_WITH_TIMEOUT(!playlists.loading(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(playlists.playlists()->rowCount(), 1, 5000);
    QCOMPARE(playlists.items()->rowCount(), 1);
    QVERIFY(playlists.playlistsComplete());
    QCOMPARE(playlists.playlists()->get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("B List"));
    QCOMPARE(playlists.items()->get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("B Track"));

    QTest::qWait(450);
    QCOMPARE(playlists.playlists()->get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("B List"));
    QCOMPARE(playlists.items()->get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("B Track"));
    QCOMPARE(succeeded.count(), 0);
    QCOMPARE(mutated.count(), 0);
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

// The create-while-open sequence MUSIC.md §3 asks for once music starts leaning
// on this controller: open a playlist, then make another one while the first
// one's members are still on the wire.
//
// It is not the same test as the one above. That one calls refresh() directly;
// this one goes through create(), so the refresh happens inside the POST's
// continuation — a second event-loop turn later, with the members fetch still
// outstanding. That is the ordering a shared generation counter turns into a
// spinner over an empty list, so the members route is held back deliberately.
// Nothing was found to fix: the split holds, and this is what pins it.
void ContentControllersTest::createWhileOpenLeavesTheOpenPlaylistAlone()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"pl1\",\"Name\":\"Road "
                                       "Trip\",\"Type\":\"Playlist\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Playlists/pl1/Items"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"t1\",\"Name\":\"Bad\","
                                       "\"Type\":\"Audio\",\"PlaylistItemId\":\"e1\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Playlists"), 200,
                     QByteArrayLiteral("{\"Id\":\"pl2\",\"ItemAddedCount\":1}"));
    // Long enough that the POST lands, its continuation calls refresh(), and
    // that refresh's own reply comes back — all while this one is still held.
    m_mock->setRouteDelay(QStringLiteral("GET"), QStringLiteral("/Playlists/pl1/Items"), 400);

    PlaylistController playlists(m_client);
    QSignalSpy failed(&playlists, &PlaylistController::actionFailed);
    playlists.open(QStringLiteral("pl1"), QStringLiteral("Road Trip"));
    QVERIFY(playlists.loading());

    playlists.create(QStringLiteral("New One"), {QStringLiteral("t9")},
                     QStringLiteral("Audio"));

    // The list refresh create() ends in has already come and gone…
    QTRY_COMPARE_WITH_TIMEOUT(playlists.playlists()->rowCount(), 1, 5000);
    // …and the open playlist's members, which were in flight the whole time,
    // still land: they are guarded by their own counter, so the refresh above
    // could not retire them.
    QTRY_COMPARE_WITH_TIMEOUT(playlists.items()->rowCount(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!playlists.loading(), 5000);
    QCOMPARE(playlists.currentId(), QStringLiteral("pl1"));
    QCOMPARE(failed.count(), 0);
}

// The one thing that made an audio playlist an audio playlist, and which
// nothing in the app passed before MUSIC.md §3. Emby publishes no media type on
// a playlist, so this parameter at creation is the only record of what kind of
// list it is — and the music library's Playlists tab is filtered on the server's
// reading of it.
void ContentControllersTest::creationCarriesTheMediaTypeItWasGiven()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Playlists"), 200,
                     QByteArrayLiteral("{\"Id\":\"pl2\",\"ItemAddedCount\":1}"));

    PlaylistController playlists(m_client);
    QSignalSpy mutated(&playlists, &PlaylistController::playlistsMutated);

    playlists.create(QStringLiteral("Late Night"), {QStringLiteral("t1")},
                     QStringLiteral("Audio"));
    QTRY_COMPARE_WITH_TIMEOUT(mutated.count(), 1, 5000);
    QUrlQuery created(
        m_mock->lastRequestFor(QStringLiteral("POST"), QStringLiteral("/Playlists")).query);
    QCOMPARE(created.queryItemValue(QStringLiteral("MediaType")), QStringLiteral("Audio"));
    QCOMPARE(created.queryItemValue(QStringLiteral("Ids")), QStringLiteral("t1"));

    // And a non-music caller keeps exactly what it had: no MediaType at all,
    // rather than a default this change would have imposed on every film list.
    playlists.create(QStringLiteral("Rewatch"), {QStringLiteral("m1")});
    QTRY_COMPARE_WITH_TIMEOUT(mutated.count(), 2, 5000);
    created = QUrlQuery(
        m_mock->lastRequestFor(QStringLiteral("POST"), QStringLiteral("/Playlists")).query);
    QVERIFY(!created.hasQueryItem(QStringLiteral("MediaType")));
}

void ContentControllersTest::deletingOpenPlaylistEndsAnInFlightMemberLoad()
{
    const QString membersPath = QStringLiteral("/Playlists/pl1/Items");
    m_mock->addRoute(QStringLiteral("GET"), membersPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"t1\",\"Name\":\"Late\","
                                       "\"Type\":\"Audio\",\"PlaylistItemId\":\"e1\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->setRouteDelay(QStringLiteral("GET"), membersPath, 400);
    m_mock->addRoute(QStringLiteral("DELETE"), QStringLiteral("/Items/pl1"), 204, {});
    m_mock->addRoute(QStringLiteral("GET"),
                     QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));

    PlaylistController playlists(m_client);
    QSignalSpy removed(&playlists, &PlaylistController::currentRemoved);
    playlists.open(QStringLiteral("pl1"), QStringLiteral("Road Trip"));
    QVERIFY(playlists.loading());

    playlists.remove(QStringLiteral("pl1"));
    QTRY_COMPARE_WITH_TIMEOUT(removed.count(), 1, 5000);
    QVERIFY(!playlists.loading());
    QVERIFY(playlists.currentId().isEmpty());
    QCOMPARE(playlists.items()->rowCount(), 0);

    // The delayed reply was retired by the deletion and cannot re-latch the
    // spinner or repopulate the deleted playlist.
    QTest::qWait(500);
    QVERIFY(!playlists.loading());
    QCOMPARE(playlists.items()->rowCount(), 0);
}

void ContentControllersTest::playlistMembersPageToTheEnd_data()
{
    QTest::addColumn<int>("total");
    QTest::newRow("short-first-page-499") << 499;
    QTest::newRow("exact-first-page-500") << 500;
    QTest::newRow("second-page-501") << 501;
    QTest::newRow("multiple-pages-1201") << 1201;
}

void ContentControllersTest::playlistMembersPageToTheEnd()
{
    QFETCH(int, total);
    const QString path = QStringLiteral("/Playlists/pl1/Items");
    const int firstCount = qMin(total, 500);
    m_mock->addRoute(QStringLiteral("GET"), path, 200,
                     playlistMemberPage(0, firstCount, total));

    PlaylistController playlists(m_client);
    int armedAt = firstCount;
    const auto armNextPage = [this, &playlists, path, total, &armedAt] {
        const int held = playlists.items()->rowCount();
        if (held != armedAt || held >= total)
            return;
        const int count = qMin(500, total - held);
        m_mock->addRoute(QStringLiteral("GET"), path, 200,
                         playlistMemberPage(held, count, total));
        armedAt += count;
    };
    connect(playlists.items(), &QAbstractItemModel::modelReset, &playlists, armNextPage);
    connect(playlists.items(), &QAbstractItemModel::rowsInserted, &playlists, armNextPage);

    playlists.open(QStringLiteral("pl1"), QStringLiteral("Long playlist"));
    QTRY_COMPARE_WITH_TIMEOUT(playlists.items()->rowCount(), total, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!playlists.loading(), 5000);
    QVERIFY(playlists.errorMessage().isEmpty());

    const int expectedRequests = (total + 499) / 500;
    QCOMPARE(requestsFor(path), expectedRequests);
    for (int page = 0; page < expectedRequests; ++page) {
        const QUrlQuery query(m_mock->requests().at(indexOfNthRequest(path, page)).query);
        QCOMPARE(query.queryItemValue(QStringLiteral("StartIndex")),
                 QString::number(page * 500));
        QCOMPARE(query.queryItemValue(QStringLiteral("Limit")), QStringLiteral("500"));
    }
    QCOMPARE(playlists.items()
                 ->get(total - 1)
                 .value(QStringLiteral("playlistItemId"))
                 .toString(),
             QStringLiteral("entry%1").arg(total - 1));
}

void ContentControllersTest::repeatedPlaylistMemberPageStopsTheWalk_data()
{
    QTest::addColumn<bool>("includeEntryIds");
    QTest::newRow("playlist-entry-ids") << true;
    QTest::newRow("malformed-rows-use-fallback") << false;
}

void ContentControllersTest::repeatedPlaylistMemberPageStopsTheWalk()
{
    QFETCH(bool, includeEntryIds);
    const QString path = QStringLiteral("/Playlists/pl1/Items");
    // The mock keeps serving this route, exactly like a server that ignores
    // StartIndex. TotalRecordCount=0 removes the ordinary advertised-total end.
    m_mock->addRoute(QStringLiteral("GET"), path, 200,
                     playlistMemberPage(0, 500, 0, includeEntryIds));

    PlaylistController playlists(m_client);
    QSignalSpy surfaced(&playlists, &PlaylistController::actionFailed);
    playlists.open(QStringLiteral("pl1"), QStringLiteral("Looping playlist"));

    QTRY_VERIFY_WITH_TIMEOUT(!playlists.loading(), 5000);
    QCOMPARE(requestsFor(path), 2);
    QCOMPARE(playlists.items()->rowCount(), 500);
    QVERIFY(playlists.errorMessage().contains(QStringLiteral("did not advance")));
    QVERIFY(playlists.errorMessage().contains(QStringLiteral("Reload")));
    QCOMPARE(surfaced.count(), 1);
    QCOMPARE(surfaced.first().first().toString(), playlists.errorMessage());

    // A manual retry is a new walk: neither the repeated-page signature nor
    // the error from the retired walk may poison it.
    m_mock->addRoute(QStringLiteral("GET"), path, 200,
                     playlistMemberPage(2000, 1, 1, includeEntryIds));
    playlists.reload();
    QTRY_VERIFY_WITH_TIMEOUT(!playlists.loading(), 5000);
    QCOMPARE(playlists.items()->rowCount(), 1);
    QVERIFY(playlists.errorMessage().isEmpty());
    QCOMPARE(requestsFor(path), 3);
}

void ContentControllersTest::playlistMemberProgressUsesEntryIds()
{
    const QString path = QStringLiteral("/Playlists/pl1/Items");
    m_mock->addRoute(QStringLiteral("GET"), path, 200, duplicateItemMemberPage(0, 500, 501));

    PlaylistController playlists(m_client);
    connect(playlists.items(), &QAbstractItemModel::modelReset, &playlists,
            [this, path, &playlists] {
                if (playlists.items()->rowCount() == 500)
                    m_mock->addRoute(QStringLiteral("GET"), path, 200,
                                     duplicateItemMemberPage(500, 1, 501));
            });

    playlists.open(QStringLiteral("pl1"), QStringLiteral("Encore twice"));
    QTRY_VERIFY_WITH_TIMEOUT(!playlists.loading(), 5000);
    QCOMPARE(playlists.items()->rowCount(), 501);
    QVERIFY(playlists.errorMessage().isEmpty());
    QCOMPARE(requestsFor(path), 2);
    QCOMPARE(playlists.items()->get(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("same-track"));
    QCOMPARE(playlists.items()->get(500).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("same-track"));
    QCOMPARE(playlists.items()->get(500).value(QStringLiteral("playlistItemId")).toString(),
             QStringLiteral("entry500"));
}

void ContentControllersTest::playlistMemberWalkStopsAtTheSafetyLimit()
{
    const QString path = QStringLiteral("/Playlists/pl1/Items");
    m_mock->addRoute(QStringLiteral("GET"), path, 200, playlistMemberPage(0, 500, 0));

    PlaylistController playlists(m_client);
    QSignalSpy surfaced(&playlists, &PlaylistController::actionFailed);
    int armedAt = 500;
    const auto armNextPage = [this, path, &playlists, &armedAt] {
        const int held = playlists.items()->rowCount();
        if (held != armedAt || held >= 10'000)
            return;
        m_mock->addRoute(QStringLiteral("GET"), path, 200,
                         playlistMemberPage(held, 500, 0));
        armedAt += 500;
    };
    connect(playlists.items(), &QAbstractItemModel::modelReset, &playlists, armNextPage);
    connect(playlists.items(), &QAbstractItemModel::rowsInserted, &playlists, armNextPage);

    playlists.open(QStringLiteral("pl1"), QStringLiteral("Huge playlist"));
    QTRY_VERIFY_WITH_TIMEOUT(!playlists.loading(), 10'000);
    QCOMPARE(playlists.items()->rowCount(), 10'000);
    QCOMPARE(requestsFor(path), 20);
    QVERIFY(playlists.errorMessage().contains(QStringLiteral("safety limit")));
    QVERIFY(playlists.errorMessage().contains(QStringLiteral("incomplete")));
    QCOMPARE(surfaced.count(), 1);
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

    // setLibrary() announces each of the four lists it just emptied, because
    // canLoadMoreAlbums, canLoadMoreArtists and artistMode all notify on those
    // signals and would otherwise keep answering for the library just left. So
    // the count of interest starts here: what is being watched for below is a
    // SECOND emission per list, meaning a dropped reply landed anyway.
    albums.clear();
    artists.clear();

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

// ── The playlist list is the WHOLE list, or it is a wrong answer ────────────
// One request returns at most 500 and this server has 1,564 playlists, so the
// list used to stop at the first page. That is not merely an incomplete browse
// rail: PlaylistPicker offers to CREATE when the typed name matches nothing it
// holds, so every name sorting past the 500th read as free and Return made a
// second playlist with a name the user already had. Nothing in the app can tell
// the two apart afterwards.
//
// Emby has no server-side answer to "is this name taken" either — measured on
// 4.9.5.0, SearchTerm is a word-prefix match ("Waltz" finds "Waltzes", "altz"
// finds nothing), so it can confirm a hit and never an absence. The list has to
// be held in full, so it pages itself to the end.
void ContentControllersTest::thePlaylistListPagesToTheEnd()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    // A FULL page — 500 is what makes the walk ask for another one — with a
    // total the walk deliberately does not steer by.
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200, playlistPage(0, 500, 1564));

    PlaylistController playlists(m_client);
    // The second page is swapped in from the first reply's own signal, so the
    // break is deterministic rather than a race with the event loop.
    bool swapped = false;
    connect(&playlists, &PlaylistController::playlistsChanged, &playlists,
            [this, &swapped, itemsPath, &playlists] {
                // Only once page 0 is actually in the model: refresh() also
                // emits this up front, to retract `playlistsComplete`.
                if (swapped || playlists.playlists()->rowCount() < 500)
                    return;
                swapped = true;
                m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                                 playlistPage(500, 64, 1564));
            });

    QVERIFY(!playlists.playlistsComplete());
    playlists.refresh();

    QTRY_COMPARE_WITH_TIMEOUT(playlists.playlists()->rowCount(), 564, 5000);
    QVERIFY(playlists.playlistsComplete());
    QCOMPARE(requestsFor(itemsPath), 2);

    // The second page was asked for from the right offset: the number of rows
    // actually received, not the count the server claims.
    const QUrlQuery second(
        m_mock->requests().at(indexOfNthRequest(itemsPath, 1)).query);
    QCOMPARE(second.queryItemValue(QStringLiteral("StartIndex")), QStringLiteral("500"));
    QCOMPARE(second.queryItemValue(QStringLiteral("Limit")), QStringLiteral("500"));
    QCOMPARE(second.queryItemValue(QStringLiteral("IncludeItemTypes")),
             QStringLiteral("Playlist"));
    // The tail is really in the model — this is the row the picker could not see.
    QCOMPARE(playlists.playlists()->get(563).value(QStringLiteral("name")).toString(),
             QStringLiteral("List 563"));

    // And it is idempotent: every surface that is about to read the list may ask.
    playlists.ensureAllPlaylists();
    QTest::qWait(80);
    QCOMPARE(requestsFor(itemsPath), 2);
}

// A walk that answered page 0 and then failed holds 500 of 1,564 — which looks
// exactly like the bug it replaced. `playlistsComplete` is what tells them
// apart, and it is what the picker keys its create offer on, so a broken walk
// must leave it false and must be resumable from the page that broke it.
void ContentControllersTest::aPlaylistWalkThatStoppedHalfwayIsRetried()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200, playlistPage(0, 500, 1564));

    PlaylistController playlists(m_client);
    bool broken = false;
    const QMetaObject::Connection breaker =
        connect(&playlists, &PlaylistController::playlistsChanged, &playlists,
                [this, &broken, itemsPath, &playlists] {
                    if (broken || playlists.playlists()->rowCount() < 500)
                        return;
                    broken = true;
                    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 500,
                                     QByteArrayLiteral("{}"));
                });

    playlists.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(playlists.playlists()->rowCount(), 500, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!playlists.errorMessage().isEmpty(), 5000);
    disconnect(breaker);
    // 500 rows in hand and NOT complete: the picker must not offer to create a
    // name it cannot know is free.
    QVERIFY(!playlists.playlistsComplete());
    QCOMPARE(requestsFor(itemsPath), 2);

    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200, playlistPage(500, 64, 1564));
    playlists.ensureAllPlaylists();
    QTRY_VERIFY_WITH_TIMEOUT(playlists.playlistsComplete(), 5000);
    // Resumed, not restarted: StartIndex 500, and the 500 already held are
    // neither refetched nor duplicated.
    QCOMPARE(playlists.playlists()->rowCount(), 564);
    QCOMPARE(requestsFor(itemsPath), 3);
    QCOMPARE(QUrlQuery(m_mock->requests().at(indexOfNthRequest(itemsPath, 2)).query)
                 .queryItemValue(QStringLiteral("StartIndex")),
             QStringLiteral("500"));
}

QTEST_MAIN(ContentControllersTest)
#include "tst_content_controllers.moc"
