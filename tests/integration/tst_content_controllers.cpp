#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

#include "MockEmbyServer.h"
#include "app/controllers/DetailsController.h"
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

QTEST_MAIN(ContentControllersTest)
#include "tst_content_controllers.moc"
