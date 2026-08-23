#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

#include "MockEmbyServer.h"
#include "app/controllers/MusicController.h"
#include "app/models/MediaItemModel.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;

namespace {
const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
const auto kMusicLibrary = QStringLiteral("1868998");

const auto kEmptyPage = QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}");
} // namespace

// The music library's query axes (MUSIC.md §2), which are all just query
// parameters — and that is exactly why they need a test. A misspelled sort key
// does not fail on Emby 4.9.5.0: it silently returns the default order and
// reads as a sort that "did nothing". Every parameter asserted here was first
// run against the live server.
class MusicQueryTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void sortIsPerTabAndReachesTheWire();
    void seededDirectionsAreLeftToTheBar();
    void filtersAreSharedAcrossTabsAndInvalidateThem();
    void songsAreTheirOwnModelWithTheirOwnQuery();
    void artistEndpointsCarryTheNarrowingAxes();
    void genresPageOnTheArrayNotTheCount();
    void aPreferenceSetBeforeAnythingLoadsFiresNoRequest();

private:
    QUrlQuery lastItemsQuery() const
    {
        return QUrlQuery(
            m_mock->lastRequestFor(QStringLiteral("GET"),
                                   QStringLiteral("/Users/%1/Items").arg(kUserId))
                .query);
    }
    QUrlQuery lastQueryFor(const QString &path) const
    {
        return QUrlQuery(m_mock->lastRequestFor(QStringLiteral("GET"), path).query);
    }
    // Index into requests() of the nth (0-based) request for `path`, or -1.
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
    int requestsFor(const QString &path) const
    {
        int count = 0;
        for (const MockEmbyServer::ReceivedRequest &request : m_mock->requests()) {
            if (request.path == path)
                ++count;
        }
        return count;
    }
    void settle() { QTRY_VERIFY_WITH_TIMEOUT(!m_music->loading(), 5000); }

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    MusicController *m_music = nullptr;
};

void MusicQueryTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     kEmptyPage);
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Artists"), 200, kEmptyPage);
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Artists/AlbumArtists"), 200,
                     kEmptyPage);
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/MusicGenres"), 200, kEmptyPage);

    m_client = new emby::EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setSession(kToken, kUserId);
    m_music = new MusicController(m_client, this);
    m_music->setLibrary(kMusicLibrary);
}

void MusicQueryTest::cleanup()
{
    delete m_music;
    m_music = nullptr;
    delete m_client;
    m_client = nullptr;
    delete m_mock;
    m_mock = nullptr;
}

// Sort belongs to the TAB, not to the controller: "Track number" is meaningless
// for an artist and "Release year" for a song, so each tab remembers its own
// field and direction and switching between them restores rather than resets.
void MusicQueryTest::sortIsPerTabAndReachesTheWire()
{
    const auto keys = [this] {
        QStringList out;
        for (const QVariant &entry : m_music->availableSorts())
            out.append(entry.toMap().value(QStringLiteral("key")).toString());
        return out;
    };

    m_music->loadAlbums();
    settle();
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("SortBy")),
             QStringLiteral("SortName"));
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("SortOrder")),
             QStringLiteral("Ascending"));
    QVERIFY(keys().contains(QStringLiteral("ProductionYear")));
    QVERIFY(keys().contains(QStringLiteral("CommunityRating")));
    // A track-number sort on a grid of albums would be nonsense.
    QVERIFY(!keys().contains(QStringLiteral("ParentIndexNumber,IndexNumber,SortName")));

    m_music->setSort(QStringLiteral("ProductionYear"), true);
    settle();
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("SortBy")),
             QStringLiteral("ProductionYear"));
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("SortOrder")),
             QStringLiteral("Descending"));
    // A sort change is a new result set: page 0, not an append.
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("StartIndex")), QStringLiteral("0"));

    m_music->setTab(QStringLiteral("songs"));
    settle();
    // The songs tab has its own sort and has never been touched, so it opens on
    // its own default rather than inheriting the albums grid's year sort.
    QCOMPARE(m_music->sortBy(), QStringLiteral("SortName"));
    QVERIFY(!m_music->sortDescending());
    QVERIFY(keys().contains(QStringLiteral("ParentIndexNumber,IndexNumber,SortName")));
    QVERIFY(keys().contains(QStringLiteral("AlbumArtist,Album,SortName")));
    QVERIFY(!keys().contains(QStringLiteral("ProductionYear")));

    m_music->setSort(QStringLiteral("Album,SortName"), false);
    settle();
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("SortBy")),
             QStringLiteral("Album,SortName"));

    // Back to albums: the year sort is still there.
    m_music->setTab(QStringLiteral("albums"));
    QCOMPARE(m_music->sortBy(), QStringLiteral("ProductionYear"));
    QVERIFY(m_music->sortDescending());

    // The artists tab's list is genuinely the short one — a person has no
    // release year and no date added.
    m_music->setTab(QStringLiteral("artists"));
    settle();
    QCOMPARE(keys().size(), 3);
    QVERIFY(keys().contains(QStringLiteral("PlayCount")));
    QVERIFY(keys().contains(QStringLiteral("Random")));

    // An unknown tab name is the albums tab, not a fourth state.
    m_music->setTab(QStringLiteral("nonsense"));
    QCOMPARE(m_music->tab(), QStringLiteral("albums"));
}

// The controller stores a direction; it never seeds one. FilterBar's
// defaultDescendingFor() is what decides that a freshly-picked "Date added"
// means newest-first, and duplicating the rule here would give the app two
// answers to the same question.
void MusicQueryTest::seededDirectionsAreLeftToTheBar()
{
    m_music->loadAlbums();
    settle();
    m_music->setSort(QStringLiteral("DateCreated"), false);
    settle();
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("SortOrder")),
             QStringLiteral("Ascending"));

    // Re-asking for exactly what is already set must not refetch: a select
    // re-emits on every open, and each one would be a round trip.
    const int before = requestsFor(QStringLiteral("/Users/%1/Items").arg(kUserId));
    m_music->setSort(QStringLiteral("DateCreated"), false);
    m_music->setSort(QString(), true);
    QCOMPARE(requestsFor(QStringLiteral("/Users/%1/Items").arg(kUserId)), before);
}

// A genre or a letter is a statement about the music, so it survives switching
// how you look at it — and it invalidates the two tabs that are not on screen
// rather than refetching all three at once.
void MusicQueryTest::filtersAreSharedAcrossTabsAndInvalidateThem()
{
    m_music->loadAlbums();
    settle();
    QVERIFY(!m_music->filtered());

    m_music->setNameStartsWith(QStringLiteral("T"));
    settle();
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("NameStartsWith")),
             QStringLiteral("T"));
    QVERIFY(m_music->filtered());

    // Self-toggling, exactly as LibraryController's is: tapping the lit letter
    // clears it rather than re-running the identical query.
    m_music->setNameStartsWith(QStringLiteral("T"));
    settle();
    QVERIFY(!lastItemsQuery().hasQueryItem(QStringLiteral("NameStartsWith")));

    m_music->setGenreIds({QStringLiteral("1937444"), QStringLiteral("1933045")});
    settle();
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("GenreIds")),
             QStringLiteral("1937444,1933045"));

    m_music->setFavoritesOnly(true);
    settle();
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("Filters")),
             QStringLiteral("IsFavorite"));

    m_music->setYearFilters({QStringLiteral("1994")});
    settle();
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("Years")), QStringLiteral("1994"));

    // The tabs the user is not looking at were emptied, so the same narrowing
    // is what they refill with when they are next opened.
    QCOMPARE(m_music->songs()->rowCount(), 0);
    m_music->setTab(QStringLiteral("songs"));
    settle();
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("GenreIds")),
             QStringLiteral("1937444,1933045"));
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("Filters")),
             QStringLiteral("IsFavorite"));

    m_music->clearFilters();
    settle();
    QVERIFY(!m_music->filtered());
    QVERIFY(!lastItemsQuery().hasQueryItem(QStringLiteral("GenreIds")));
    QVERIFY(!lastItemsQuery().hasQueryItem(QStringLiteral("Filters")));
    QVERIFY(!lastItemsQuery().hasQueryItem(QStringLiteral("Years")));
    QVERIFY(!lastItemsQuery().hasQueryItem(QStringLiteral("NameStartsWith")));
}

// The Songs tab must never be the open album's `tracks` model. Sharing it is
// what produced the playAlbum() side channel a previous phase removed, and it
// would refill the album page out from under itself.
void MusicQueryTest::songsAreTheirOwnModelWithTheirOwnQuery()
{
    QVERIFY(m_music->songs() != m_music->tracks());

    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"1\",\"Name\":\"Threnody\","
                                       "\"Type\":\"Audio\"}],\"TotalRecordCount\":56283}"));

    m_music->loadSongs();
    settle();
    const QUrlQuery query = lastItemsQuery();
    QCOMPARE(query.queryItemValue(QStringLiteral("IncludeItemTypes")), QStringLiteral("Audio"));
    QCOMPARE(query.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
    QCOMPARE(query.queryItemValue(QStringLiteral("ParentId")), kMusicLibrary);
    QCOMPARE(query.queryItemValue(QStringLiteral("Limit")), QStringLiteral("100"));
    const QString fields = query.queryItemValue(QStringLiteral("Fields"));
    QVERIFY(fields.contains(QStringLiteral("MediaSources")));
    QVERIFY(fields.contains(QStringLiteral("Genres")));
    QVERIFY(fields.contains(QStringLiteral("ParentIndexNumber")));

    QCOMPARE(m_music->songs()->rowCount(), 1);
    QCOMPARE(m_music->tracks()->rowCount(), 0);
    QVERIFY(m_music->canLoadMoreSongs());

    m_music->loadMoreSongs();
    settle();
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("StartIndex")), QStringLiteral("1"));

    // Opening an album fills `tracks` and leaves the songs list alone.
    m_music->openAlbum(QStringLiteral("88001"), QStringLiteral("Lift Yr Skinny Fists"));
    settle();
    QCOMPARE(m_music->tracks()->rowCount(), 1);
    QVERIFY(m_music->songs()->rowCount() >= 1);
}

// /Artists and /Artists/AlbumArtists are separate endpoints and take the
// narrowing axes as query parameters, measured live. Years is deliberately not
// among them: a release year is not a property of a person, and this server
// answers a year-filtered artist query with nothing at all.
void MusicQueryTest::artistEndpointsCarryTheNarrowingAxes()
{
    m_music->setTab(QStringLiteral("artists"));
    m_music->setNameStartsWith(QStringLiteral("T"));
    m_music->setGenreIds({QStringLiteral("1937444")});
    m_music->setYearFilters({QStringLiteral("1994")});
    m_music->loadArtists();
    settle();

    QUrlQuery query = lastQueryFor(QStringLiteral("/Artists/AlbumArtists"));
    QCOMPARE(query.queryItemValue(QStringLiteral("ParentId")), kMusicLibrary);
    QCOMPARE(query.queryItemValue(QStringLiteral("NameStartsWith")), QStringLiteral("T"));
    QCOMPARE(query.queryItemValue(QStringLiteral("GenreIds")), QStringLiteral("1937444"));
    QCOMPARE(query.queryItemValue(QStringLiteral("SortBy")), QStringLiteral("SortName"));
    QVERIFY(!query.hasQueryItem(QStringLiteral("Years")));

    m_music->setSort(QStringLiteral("PlayCount"), true);
    settle();
    query = lastQueryFor(QStringLiteral("/Artists/AlbumArtists"));
    QCOMPARE(query.queryItemValue(QStringLiteral("SortBy")), QStringLiteral("PlayCount"));
    QCOMPARE(query.queryItemValue(QStringLiteral("SortOrder")), QStringLiteral("Descending"));

    // The other endpoint, and it really is another endpoint rather than a flag.
    m_music->setArtistMode(QStringLiteral("artists"));
    settle();
    query = lastQueryFor(QStringLiteral("/Artists"));
    QCOMPARE(query.queryItemValue(QStringLiteral("NameStartsWith")), QStringLiteral("T"));
    QCOMPARE(query.queryItemValue(QStringLiteral("SortBy")), QStringLiteral("PlayCount"));
}

// ARCHITECTURE.md §2: this family of endpoints reports TotalRecordCount = 0
// while returning rows, so the walk pages on the array's own size. /MusicGenres
// was measured to tell the truth on 4.9.5.0 — but a caller that believed the
// count would stop after one page the day it stops.
void MusicQueryTest::genresPageOnTheArrayNotTheCount()
{
    // A full page (the controller asks for 200) with a lying count, so a walk
    // driven by TotalRecordCount would stop here and a walk driven by the array
    // asks for more.
    QByteArray firstPage = QByteArrayLiteral("{\"Items\":[");
    for (int i = 0; i < 200; ++i) {
        if (i > 0)
            firstPage += ',';
        firstPage += QStringLiteral("{\"Id\":\"g%1\",\"Name\":\"Genre %1\","
                                    "\"Type\":\"MusicGenre\"}")
                         .arg(i)
                         .toUtf8();
    }
    firstPage += QByteArrayLiteral("],\"TotalRecordCount\":0}");
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/MusicGenres"), 200, firstPage);

    m_music->loadGenres();

    // The second page was asked for at all — which a walk driven by
    // TotalRecordCount would not have done — and from the right offset: the
    // number of rows actually received.
    QTRY_VERIFY_WITH_TIMEOUT(requestsFor(QStringLiteral("/MusicGenres")) >= 2, 5000);
    const MockEmbyServer::ReceivedRequest second =
        m_mock->requests().at(indexOfNthRequest(QStringLiteral("/MusicGenres"), 1));
    QCOMPARE(QUrlQuery(second.query).queryItemValue(QStringLiteral("StartIndex")),
             QStringLiteral("200"));
    QCOMPARE(QUrlQuery(second.query).queryItemValue(QStringLiteral("ParentId")), kMusicLibrary);
    QCOMPARE(QUrlQuery(second.query).queryItemValue(QStringLiteral("Limit")),
             QStringLiteral("200"));

    // …and it STOPS. This mock answers a full page forever, which is the shape
    // of a server that ignores StartIndex; without the hard stop in the walk
    // that is an infinite request loop, and the failure mode is a hung app
    // rather than a wrong list. 25 pages of 200 is the cap.
    QTRY_COMPARE_WITH_TIMEOUT(m_music->genreOptions().size(), 5000, 20000);
    QCOMPARE(requestsFor(QStringLiteral("/MusicGenres")), 25);

    // Shape check: {key, label}, which is what a select renders.
    const QVariantMap first = m_music->genreOptions().first().toMap();
    QCOMPARE(first.value(QStringLiteral("key")).toString(), QStringLiteral("g0"));
    QCOMPARE(first.value(QStringLiteral("label")).toString(), QStringLiteral("Genre 0"));

    // Idempotent: the filter control asks on every visit to the page.
    const int walked = requestsFor(QStringLiteral("/MusicGenres"));
    m_music->loadGenres();
    QTest::qWait(50);
    QCOMPARE(requestsFor(QStringLiteral("/MusicGenres")), walked);
}

// A sort or a filter set before any list was asked for is a preference, not a
// query. LibraryController guards this with hasQuery(); an empty library id
// cannot serve as the guard here, because it legitimately means "every music
// library".
void MusicQueryTest::aPreferenceSetBeforeAnythingLoadsFiresNoRequest()
{
    auto *fresh = new MusicController(m_client, this);
    fresh->setLibrary(kMusicLibrary);
    const int before = m_mock->requestCount();
    fresh->setSort(QStringLiteral("DateCreated"), true);
    fresh->setNameStartsWith(QStringLiteral("Q"));
    fresh->setFavoritesOnly(true);
    QTest::qWait(50);
    QCOMPARE(m_mock->requestCount(), before);
    // …and the preference is still there when the first real load happens.
    fresh->loadAlbums();
    QTRY_VERIFY_WITH_TIMEOUT(!fresh->loading(), 5000);
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("SortBy")),
             QStringLiteral("DateCreated"));
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("NameStartsWith")),
             QStringLiteral("Q"));
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("Filters")),
             QStringLiteral("IsFavorite"));
    delete fresh;
}

QTEST_MAIN(MusicQueryTest)
#include "tst_music_query.moc"
