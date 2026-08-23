#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

#include "MockEmbyServer.h"
#include "app/controllers/LibraryController.h"
#include "app/models/MediaItemModel.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;

namespace {
const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
} // namespace

// Sort, filter and the scoped browse views (ARCHITECTURE.md) are all just
// query parameters, which is exactly why they need a test: a misspelled key
// does not fail, it silently returns the whole library and looks like a sort
// that "did nothing". Every parameter asserted here was first verified against
// the live Emby 4.9.5.0 server.
class LibraryQueryTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void sortKeyAndDirectionReachTheWire();
    void watchedFilterComposesWithFavorites();
    void alphabetJumpTogglesOff();
    void genrePersonAndStudioScopes();
    void scopeChangeDoesNotInheritFilters();
    void redundantSetterDoesNotRefetch();
    void collectionScopeListsDirectMembers();

private:
    QUrlQuery lastQuery() const
    {
        return QUrlQuery(
            m_mock->lastRequestFor(QStringLiteral("GET"),
                                   QStringLiteral("/Users/%1/Items").arg(kUserId))
                .query);
    }
    // Waits for the controller to finish the fetch it just started.
    void settle() { QTRY_VERIFY_WITH_TIMEOUT(!m_library->loading(), 5000); }

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    LibraryController *m_library = nullptr;
};

void LibraryQueryTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));

    m_client = new emby::EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setSession(kToken, kUserId);
    m_library = new LibraryController(m_client, this);
}

void LibraryQueryTest::cleanup()
{
    delete m_library;
    m_library = nullptr;
    delete m_client;
    m_client = nullptr;
    delete m_mock;
    m_mock = nullptr;
}

void LibraryQueryTest::sortKeyAndDirectionReachTheWire()
{
    m_library->open(QStringLiteral("4"), QStringLiteral("Movies"), QStringLiteral("movies"));
    settle();
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("SortBy")), QStringLiteral("SortName"));
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("SortOrder")), QStringLiteral("Ascending"));

    m_library->setSort(QStringLiteral("DateCreated"), true);
    settle();
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("SortBy")), QStringLiteral("DateCreated"));
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("SortOrder")), QStringLiteral("Descending"));
    // A sort change is a new result set, so it must restart at page 0 rather
    // than appending onto whatever the grid had already paged in.
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("StartIndex")), QStringLiteral("0"));

    // Critic rating is offered for films and withheld where the whole library
    // would sort by a field it leaves at zero.
    const auto keys = [this] {
        QStringList out;
        for (const QVariant &entry : m_library->availableSorts())
            out.append(entry.toMap().value(QStringLiteral("key")).toString());
        return out;
    };
    QVERIFY(keys().contains(QStringLiteral("CriticRating")));
    m_library->open(QStringLiteral("1868998"), QStringLiteral("Music"), QStringLiteral("music"));
    settle();
    QVERIFY(!keys().contains(QStringLiteral("CriticRating")));
    QVERIFY(keys().contains(QStringLiteral("AlbumArtist")));
}

void LibraryQueryTest::watchedFilterComposesWithFavorites()
{
    m_library->openFavorites();
    settle();
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("Filters")), QStringLiteral("IsFavorite"));

    // The watched axis is orthogonal to the scope: "favourites, unplayed" is a
    // legitimate view, so it appends rather than replacing.
    m_library->setWatchedFilter(QStringLiteral("unplayed"));
    settle();
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("Filters")),
             QStringLiteral("IsFavorite,IsUnplayed"));
    QVERIFY(m_library->filtered());

    m_library->setWatchedFilter(QStringLiteral("all"));
    settle();
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("Filters")), QStringLiteral("IsFavorite"));
    QVERIFY(!m_library->filtered()); // the scope's own filter is not "filtered"
}

void LibraryQueryTest::alphabetJumpTogglesOff()
{
    m_library->open(QStringLiteral("4"), QStringLiteral("Movies"), QStringLiteral("movies"));
    settle();
    QVERIFY(!lastQuery().hasQueryItem(QStringLiteral("NameStartsWith")));

    m_library->setNameStartsWith(QStringLiteral("Q"));
    settle();
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("NameStartsWith")), QStringLiteral("Q"));

    // Tapping the active letter clears it instead of re-running the same query.
    m_library->setNameStartsWith(QStringLiteral("Q"));
    settle();
    QVERIFY(!lastQuery().hasQueryItem(QStringLiteral("NameStartsWith")));

    m_library->setNameStartsWith(QStringLiteral("B"));
    settle();
    m_library->clearFilters();
    settle();
    QVERIFY(!lastQuery().hasQueryItem(QStringLiteral("NameStartsWith")));
    QVERIFY(!m_library->filtered());
}

void LibraryQueryTest::genrePersonAndStudioScopes()
{
    m_library->openGenre(QStringLiteral("8122"), QStringLiteral("Action"));
    settle();
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("GenreIds")), QStringLiteral("8122"));
    // A genre spans every library, so it carries no parent and must restrict the
    // item kinds a poster grid can render.
    QVERIFY(!lastQuery().hasQueryItem(QStringLiteral("ParentId")));
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("IncludeItemTypes")),
             QStringLiteral("Movie,Series,Episode,BoxSet"));
    QCOMPARE(m_library->title(), QStringLiteral("Action"));

    // A filmography reads as a career, so it opens newest-first.
    m_library->openPerson(QStringLiteral("11818"), QStringLiteral("Tom Holland"));
    settle();
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("PersonIds")), QStringLiteral("11818"));
    QVERIFY(!lastQuery().hasQueryItem(QStringLiteral("GenreIds"))); // scope replaced, not merged
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("SortBy")), QStringLiteral("PremiereDate"));
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("SortOrder")), QStringLiteral("Descending"));

    m_library->openStudio(QStringLiteral("8901"), QStringLiteral("Marvel Studios"));
    settle();
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("StudioIds")), QStringLiteral("8901"));
    QVERIFY(!lastQuery().hasQueryItem(QStringLiteral("PersonIds")));

    // An empty id is not a link and must not blow the current view away.
    const QString before = m_library->title();
    m_library->openGenre(QString(), QStringLiteral("Nothing"));
    QCOMPARE(m_library->title(), before);
}

void LibraryQueryTest::scopeChangeDoesNotInheritFilters()
{
    m_library->open(QStringLiteral("4"), QStringLiteral("Movies"), QStringLiteral("movies"));
    settle();
    m_library->setWatchedFilter(QStringLiteral("unplayed"));
    m_library->setNameStartsWith(QStringLiteral("Q"));
    settle();

    // Arriving at a cast member still filtered to unplayed titles beginning with
    // Q looks like an empty filmography rather than a filter.
    m_library->openPerson(QStringLiteral("11818"), QStringLiteral("Tom Holland"));
    settle();
    QVERIFY(!lastQuery().hasQueryItem(QStringLiteral("NameStartsWith")));
    QVERIFY(!lastQuery().hasQueryItem(QStringLiteral("Filters")));
    QCOMPARE(m_library->watchedFilter(), QStringLiteral("all"));
    QVERIFY(!m_library->filtered());
}

void LibraryQueryTest::redundantSetterDoesNotRefetch()
{
    m_library->open(QStringLiteral("4"), QStringLiteral("Movies"), QStringLiteral("movies"));
    settle();
    QSignalSpy spy(m_library, &LibraryController::queryChanged);

    // A menu that re-emits its current value on open must not cost a round trip.
    m_library->setSort(QStringLiteral("SortName"), false);
    m_library->setWatchedFilter(QStringLiteral("all"));
    m_library->setNameStartsWith(QString());
    m_library->clearFilters();
    QCOMPARE(spy.count(), 0);

    m_library->setSort(QStringLiteral("Random"), false);
    QCOMPARE(spy.count(), 1);
}

void LibraryQueryTest::collectionScopeListsDirectMembers()
{
    m_library->openCollection(QStringLiteral("1992265"),
                              QStringLiteral("Ace Ventura Collection"));
    settle();
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("ParentId")), QStringLiteral("1992265"));
    // NOT recursive: a collection lists its direct members, and recursing would
    // pull in every episode of any series it contains.
    QVERIFY(!lastQuery().hasQueryItem(QStringLiteral("Recursive")));
    // No SortBy: a franchise reads in the server's order, not alphabetically
    // ("Ace Ventura: When Nature Calls" would otherwise precede "Pet Detective").
    QVERIFY(!lastQuery().hasQueryItem(QStringLiteral("SortBy")));
    QCOMPARE(m_library->scopeKey(), QStringLiteral("collection:1992265"));

    // Leaving the scope restores a normal recursive library listing.
    m_library->open(QStringLiteral("4"), QStringLiteral("Movies"), QStringLiteral("movies"));
    settle();
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
    QCOMPARE(lastQuery().queryItemValue(QStringLiteral("SortBy")), QStringLiteral("SortName"));

    m_library->openCollection(QString(), QStringLiteral("nothing"));
    QCOMPARE(m_library->title(), QStringLiteral("Movies"));
}

QTEST_MAIN(LibraryQueryTest)
#include "tst_library_query.moc"
