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
    void repeatedAlbumEnsureKeepsTheInFlightPage();
    void tabAwayAndBackKeepsTheInFlightPageZero();
    void albumPagingIgnoresAnUnrelatedArtistRequest();
    void browseLoadingIsPublishedPerIndependentLane();
    void filtersAreSharedAcrossTabsAndInvalidateThem();
    void songsAreTheirOwnModelWithTheirOwnQuery();
    void artistEndpointsCarryTheNarrowingAxes();
    void genresPageOnTheArrayNotTheCount();
    void aGenreWalkThatStoppedHalfwayIsRetried();
    void switchingLibraryDropsThatLibrarysFilters();
    void switchingLibraryAnnouncesEveryListItCleared();
    void randomIsASinglePageSort();
    void aPreferenceSetBeforeAnythingLoadsFiresNoRequest();
    void artistModeAnnouncesItselfWithoutWaitingForTheWire();
    void artistModeSetBeforeAnythingLoadsFiresNoRequest();
    void playlistsAreScopedToTheLibraryNotProbedOneByOne();
    void anUnscopedControllerFetchesNoPlaylists();
    void createdPlaylistsReappearInTheMusicTab();
    void invalidatingPlaylistsFromAnotherTabDoesNotStrandLoading();
    void aPlaylistFetchWithNoLibraryDoesNotStrandLoading();
    void albumDetailStatusIsIsolatedFromListRequests();
    void albumDetailStatusRejectsRetiredReplies();
    void artistDetailStatusIsOwnedAndIsolated();
    void detailOwnersCanBeReacquiredAfterCrossNavigation();
    void sessionResetClearsScopeAndAllowsSameLibraryForNextUser();

private:
    // One row of `type`, over a total the server claims is much larger — the
    // shape that makes canLoadMore* true with a single page loaded.
    static QByteArray onePageOf(const char *type, int total)
    {
        return QStringLiteral("{\"Items\":[{\"Id\":\"x1\",\"Name\":\"One\",\"Type\":\"%1\"}],"
                              "\"TotalRecordCount\":%2}")
            .arg(QLatin1String(type))
            .arg(total)
            .toUtf8();
    }

    // `count` MusicGenre rows numbered from `from`, as /MusicGenres answers them.
    static QByteArray genrePage(int from, int count)
    {
        QByteArray page = QByteArrayLiteral("{\"Items\":[");
        for (int i = 0; i < count; ++i) {
            if (i > 0)
                page += ',';
            page += QStringLiteral("{\"Id\":\"g%1\",\"Name\":\"Genre %1\","
                                   "\"Type\":\"MusicGenre\"}")
                        .arg(from + i)
                        .toUtf8();
        }
        page += QByteArrayLiteral("],\"TotalRecordCount\":289}");
        return page;
    }

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

void MusicQueryTest::repeatedAlbumEnsureKeepsTheInFlightPage()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 300);

    m_music->loadAlbums();
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(itemsPath), 1, 5000);
    m_music->loadAlbums();
    QTest::qWait(50);

    // Main and MusicPage both ensure the default tab during reconstruction.
    // The second ensure must join the live lane, not retire its useful request
    // and replace it with another page-0 download.
    QCOMPARE(requestsFor(itemsPath), 1);
    settle();
}

void MusicQueryTest::tabAwayAndBackKeepsTheInFlightPageZero()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    const QString artistsPath = QStringLiteral("/Artists/AlbumArtists");

    // Start the controller and leave the Albums tab settled but empty. Empty is
    // a valid terminal model, so returning to it can issue another ensure; that
    // unrelated request must not affect admission on the Artists lane.
    m_music->loadAlbums();
    settle();
    m_mock->setRouteDelay(QStringLiteral("GET"), artistsPath, 800);

    m_music->setTab(QStringLiteral("artists"));
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(artistsPath), 1, 5000);
    QVERIFY(m_music->loading());

    // The Artists model is still empty because page zero is slow. Moving away
    // and back used to treat that as "not requested" and retire/duplicate the
    // useful lane. Matching in-flight state is the admission authority.
    m_music->setTab(QStringLiteral("albums"));
    m_music->setTab(QStringLiteral("artists"));
    QTest::qWait(100);
    QCOMPARE(requestsFor(artistsPath), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->loading(), 5000);
    QCOMPARE(requestsFor(artistsPath), 1);
    QCOMPARE(m_music->artists()->rowCount(), 0);
    QVERIFY(requestsFor(itemsPath) >= 1);
}

void MusicQueryTest::albumPagingIgnoresAnUnrelatedArtistRequest()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    const QString artistsPath = QStringLiteral("/Artists/AlbumArtists");
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     onePageOf("MusicAlbum", 2));
    m_mock->addRoute(QStringLiteral("GET"), artistsPath, 200,
                     onePageOf("MusicArtist", 1));

    m_music->loadAlbums();
    settle();
    QCOMPARE(m_music->albums()->rowCount(), 1);
    QVERIFY(m_music->canLoadMoreAlbums());

    m_mock->setRouteDelay(QStringLiteral("GET"), artistsPath, 500);
    m_music->setTab(QStringLiteral("artists"));
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(artistsPath), 1, 5000);
    QVERIFY(m_music->loading());

    // The page's nearEnd signal is throttled at this loaded count. An unrelated
    // artist request must not consume its sole opportunity to admit album page 2.
    m_music->loadMoreAlbums();
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(itemsPath), 2, 5000);
    const QUrlQuery secondAlbumQuery(
        m_mock->requests().at(indexOfNthRequest(itemsPath, 1)).query);
    QCOMPARE(secondAlbumQuery.queryItemValue(QStringLiteral("StartIndex")),
             QStringLiteral("1"));
    QTRY_COMPARE_WITH_TIMEOUT(m_music->albums()->rowCount(), 2, 5000);
    settle();
}

void MusicQueryTest::browseLoadingIsPublishedPerIndependentLane()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    const QString artistsPath = QStringLiteral("/Artists/AlbumArtists");
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     onePageOf("MusicAlbum", 1));
    m_mock->addRoute(QStringLiteral("GET"), artistsPath, 200,
                     onePageOf("MusicArtist", 1));
    m_mock->setRouteDelay(QStringLiteral("GET"), artistsPath, 700);
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 120);

    QSignalSpy laneSpy(m_music, &MusicController::browseLoadingChanged);
    // Albums remains the visible tab while a hidden Artists ensure is slow.
    m_music->loadArtists();
    m_music->loadAlbums();
    QVERIFY(m_music->artistsLoading());
    QVERIFY(m_music->albumsLoading());
    QVERIFY(m_music->loading());

    // The visible Albums owner may settle while a hidden Artists request is
    // still live. Its lifecycle must publish that transition even though the
    // aggregate `loading` value remains true throughout it.
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->albumsLoading(), 5000);
    QVERIFY(m_music->artistsLoading());
    QVERIFY(m_music->loading());
    QVERIFY(laneSpy.count() >= 3);

    QTRY_VERIFY_WITH_TIMEOUT(!m_music->artistsLoading(), 5000);
    QVERIFY(!m_music->loading());
    QVERIFY(!m_music->songsLoading());
    QVERIFY(!m_music->playlistsLoading());
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
    QVERIFY(!fields.contains(QStringLiteral("MediaSources")));
    QVERIFY(!fields.contains(QStringLiteral("MediaStreams")));
    QVERIFY(!fields.contains(QStringLiteral("Chapters")));
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
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->detailLoading(), 5000);
    QCOMPARE(m_music->tracks()->rowCount(), 1);
    QVERIFY(m_music->songs()->rowCount() >= 1);
}

// /Artists and /Artists/AlbumArtists are separate endpoints and take the
// narrowing axes as query parameters, measured live.
void MusicQueryTest::artistEndpointsCarryTheNarrowingAxes()
{
    m_music->setTab(QStringLiteral("artists"));
    m_music->setNameStartsWith(QStringLiteral("T"));
    m_music->setGenreIds({QStringLiteral("1937444")});
    m_music->loadArtists();
    settle();

    QUrlQuery query = lastQueryFor(QStringLiteral("/Artists/AlbumArtists"));
    QCOMPARE(query.queryItemValue(QStringLiteral("ParentId")), kMusicLibrary);
    QCOMPARE(query.queryItemValue(QStringLiteral("NameStartsWith")), QStringLiteral("T"));
    QCOMPARE(query.queryItemValue(QStringLiteral("GenreIds")), QStringLiteral("1937444"));
    QCOMPARE(query.queryItemValue(QStringLiteral("SortBy")), QStringLiteral("SortName"));

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

    // Filters is forwarded, but unlike every other axis above it is
    // *unmeasured*: this account has no music favourites, so 0 rows back
    // cannot be told apart from the parameter being ignored, and settling it
    // would mean writing a favourite into somebody's library. The header says
    // so; this pins the behaviour the header describes, so the two cannot drift
    // apart while the question is open.
    m_music->setFavoritesOnly(true);
    settle();
    query = lastQueryFor(QStringLiteral("/Artists"));
    QCOMPARE(query.queryItemValue(QStringLiteral("Filters")), QStringLiteral("IsFavorite"));
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

// A walk that answers page 0 and then fails on page 1 leaves the select holding
// 200 of the measured 289 genres. The old guard — "the list is non-empty, so we
// are done" — called that finished for the life of the scope and actively
// prevented the retry, so 89 genres were unreachable and nothing said why.
void MusicQueryTest::aGenreWalkThatStoppedHalfwayIsRetried()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/MusicGenres"), 200,
                     genrePage(0, 200));

    // Page 0 is taken in, and the route turns into a 500 before the walk can ask
    // for page 1. genresChanged is emitted from inside the reply handler, before
    // the follow-up request goes out, which makes the break deterministic rather
    // than a race with the event loop.
    bool broken = false;
    const QMetaObject::Connection breaker =
        connect(m_music, &MusicController::genresChanged, m_music, [this, &broken] {
            if (broken)
                return;
            broken = true;
            m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/MusicGenres"), 500,
                             QByteArrayLiteral("{}"));
        });

    m_music->loadGenres();
    QTRY_VERIFY_WITH_TIMEOUT(m_music->genresFailed(), 5000);
    disconnect(breaker);
    QCOMPARE(m_music->genreOptions().size(), 200);
    QCOMPARE(requestsFor(QStringLiteral("/MusicGenres")), 2);

    // Not setError(): a genre list that did not arrive is not a broken library.
    QVERIFY(m_music->errorMessage().isEmpty());

    // The retry RESUMES — StartIndex 200, not 0 — so the 200 already held are
    // not fetched twice and not duplicated into the options list.
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/MusicGenres"), 200,
                     genrePage(200, 89));
    m_music->loadGenres();
    QTRY_COMPARE_WITH_TIMEOUT(m_music->genreOptions().size(), 289, 5000);
    QVERIFY(!m_music->genresFailed());
    QCOMPARE(requestsFor(QStringLiteral("/MusicGenres")), 3);
    const QUrlQuery third(
        m_mock->requests().at(indexOfNthRequest(QStringLiteral("/MusicGenres"), 2)).query);
    QCOMPARE(third.queryItemValue(QStringLiteral("StartIndex")), QStringLiteral("200"));
    QCOMPARE(m_music->genreOptions().last().toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("g288"));

    // A short page ended the walk, so now it really is done and the page may go
    // on calling loadGenres() on every tab switch for nothing.
    m_music->loadGenres();
    QTest::qWait(50);
    QCOMPARE(requestsFor(QStringLiteral("/MusicGenres")), 3);
}

// A genre id is a ParentId-scoped MusicGenre row, so it means nothing in another
// library: carrying the selection across a scope change queries library A's ids
// against library B's parent, everything comes back empty, and the page says
// "Nothing matches these filters" over a Genre select reading "1 selected" whose
// selection is not in its own options list.
void MusicQueryTest::switchingLibraryDropsThatLibrarysFilters()
{
    m_music->loadAlbums();
    settle();
    m_music->setSort(QStringLiteral("DateCreated"), true);
    m_music->setGenreIds({QStringLiteral("1937444")});
    m_music->setNameStartsWith(QStringLiteral("T"));
    m_music->setFavoritesOnly(true);
    settle();
    QVERIFY(m_music->filtered());

    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/MusicGenres"), 200,
                     genrePage(0, 3));
    m_music->loadGenres();
    QTRY_COMPARE_WITH_TIMEOUT(m_music->genreOptions().size(), 3, 5000);

    QSignalSpy querySpy(m_music, &MusicController::queryChanged);
    m_music->setLibrary(QStringLiteral("2000001"));

    QVERIFY(!m_music->filtered());
    QVERIFY(m_music->genreIds().isEmpty());
    QVERIFY(m_music->nameStartsWith().isEmpty());
    QVERIFY(!m_music->favoritesOnly());
    // One notification, so the Clear button, the alphabet strip and the empty
    // state all move together.
    QCOMPARE(querySpy.count(), 1);
    // The options went with them: those ids belong to the old parent.
    QVERIFY(m_music->genreOptions().isEmpty());
    // The per-tab SORT is library-neutral and survives.
    QCOMPARE(m_music->sortBy(), QStringLiteral("DateCreated"));
    QVERIFY(m_music->sortDescending());

    // And the first query in the new scope really is unfiltered.
    m_music->loadAlbums();
    settle();
    const QUrlQuery query = lastItemsQuery();
    QCOMPARE(query.queryItemValue(QStringLiteral("ParentId")), QStringLiteral("2000001"));
    QVERIFY(!query.hasQueryItem(QStringLiteral("GenreIds")));
    QVERIFY(!query.hasQueryItem(QStringLiteral("NameStartsWith")));
    QVERIFY(!query.hasQueryItem(QStringLiteral("Filters")));

    // The genre walk restarts for the new scope rather than being turned away by
    // the completed walk of the old one.
    m_music->loadGenres();
    QTRY_COMPARE_WITH_TIMEOUT(m_music->genreOptions().size(), 3, 5000);
    QCOMPARE(QUrlQuery(m_mock->lastRequestFor(QStringLiteral("GET"),
                                              QStringLiteral("/MusicGenres"))
                           .query)
                 .queryItemValue(QStringLiteral("ParentId")),
             QStringLiteral("2000001"));
}

// Re-targeting clears all four models, so all four have to say so. Three of the
// properties bound to those signals are not about the rows at all —
// canLoadMoreAlbums, canLoadMoreArtists and artistMode all notify on
// albumsChanged/artistsChanged — and without an emit they keep answering for
// the library that has just been left: "there is more" in front of a grid
// holding nothing, which is a paging request against the wrong ParentId waiting
// for a scroll.
void MusicQueryTest::switchingLibraryAnnouncesEveryListItCleared()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     onePageOf("MusicAlbum", 5037));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Artists/AlbumArtists"), 200,
                     onePageOf("MusicArtist", 2394));

    m_music->loadAlbums();
    settle();
    m_music->loadArtists();
    settle();
    QVERIFY(m_music->canLoadMoreAlbums());
    QVERIFY(m_music->canLoadMoreArtists());
    QCOMPARE(m_music->albums()->rowCount(), 1);
    QCOMPARE(m_music->artists()->rowCount(), 1);

    QSignalSpy albumSpy(m_music, &MusicController::albumsChanged);
    QSignalSpy artistSpy(m_music, &MusicController::artistsChanged);
    QSignalSpy songSpy(m_music, &MusicController::songsChanged);
    QSignalSpy playlistSpy(m_music, &MusicController::playlistsChanged);

    m_music->setLibrary(QStringLiteral("2000001"));

    QCOMPARE(m_music->albums()->rowCount(), 0);
    QCOMPARE(m_music->artists()->rowCount(), 0);
    QVERIFY(!m_music->canLoadMoreAlbums());
    QVERIFY(!m_music->canLoadMoreArtists());
    // The two that were already announced, and the two that were not.
    QCOMPARE(songSpy.count(), 1);
    QCOMPARE(playlistSpy.count(), 1);
    QCOMPARE(albumSpy.count(), 1);
    QCOMPARE(artistSpy.count(), 1);
}

// Emby reshuffles SortBy=Random on every request and has no seed, so page 2
// drawn at StartIndex = rowCount() is a different shuffle: rows already on
// screen repeat and others never appear — and on the Songs tab those duplicates
// go into the play queue, which is built from every loaded row. The sort stays
// (MUSIC.md §2.1 asks for it on all three tabs); the paging is what goes.
void MusicQueryTest::randomIsASinglePageSort()
{
    const auto itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"1\",\"Name\":\"Threnody\","
                                       "\"Type\":\"Audio\"}],\"TotalRecordCount\":56283}"));

    m_music->setTab(QStringLiteral("songs"));
    m_music->loadSongs();
    settle();
    // 1 row of 56,283: under any other sort this list pages.
    QVERIFY(m_music->canLoadMoreSongs());

    QSignalSpy songsSpy(m_music, &MusicController::songsChanged);
    m_music->setSort(QStringLiteral("Random"), false);
    // Retracted at the moment of the pick, not whenever the next page happens to
    // land: the grid and the table both read canLoadMore* out of a prefetch
    // handler that can fire before the reply.
    QVERIFY(songsSpy.count() >= 1);
    QVERIFY(!m_music->canLoadMoreSongs());
    settle();
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("SortBy")),
             QStringLiteral("Random"));
    QVERIFY(!m_music->canLoadMoreSongs());

    // The prefetch is inert: loadMoreSongs() is what StrmGrid's and TrackTable's
    // nearEnd handlers call, and it must issue nothing.
    const int before = requestsFor(itemsPath);
    m_music->loadMoreSongs();
    QTest::qWait(50);
    QCOMPARE(requestsFor(itemsPath), before);

    // Leaving Random gives the list its second page back.
    m_music->setSort(QStringLiteral("SortName"), false);
    settle();
    QVERIFY(m_music->canLoadMoreSongs());
    m_music->loadMoreSongs();
    settle();
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("StartIndex")), QStringLiteral("1"));

    // Sort is per tab, so Random on the songs tab says nothing about the grids —
    // and each grid answers for its own sort.
    m_music->setTab(QStringLiteral("albums"));
    m_music->loadAlbums();
    settle();
    QVERIFY(m_music->canLoadMoreAlbums());
    m_music->setSort(QStringLiteral("Random"), false);
    QVERIFY(!m_music->canLoadMoreAlbums());
    settle();

    m_music->setTab(QStringLiteral("artists"));
    m_music->loadArtists();
    settle();
    // Still Random on the albums tab, and that is the albums tab's business.
    QVERIFY(!m_music->canLoadMoreAlbums());
    m_music->setSort(QStringLiteral("Random"), false);
    QVERIFY(!m_music->canLoadMoreArtists());
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

// `artistMode` notifies on artistsChanged, and the two chips that render it are
// bound straight to it. The only other emit on this path is inside
// fetchArtists()'s reply lambda, which is skipped on !ok() — so a setter with no
// emit of its own left the chips on the old mode until the network answered,
// and on it for good if the network failed. Both halves are checked here: a
// slow route and a failing one.
void MusicQueryTest::artistModeAnnouncesItselfWithoutWaitingForTheWire()
{
    m_music->setTab(QStringLiteral("artists"));
    m_music->loadArtists();
    settle();
    QCOMPARE(m_music->artistMode(), QStringLiteral("albumArtists"));

    // Slow: the chips move on the click, not on the reply.
    m_mock->setRouteDelay(QStringLiteral("GET"), QStringLiteral("/Artists"), 300);
    QSignalSpy slowSpy(m_music, &MusicController::artistsChanged);
    m_music->setArtistMode(QStringLiteral("artists"));
    QCOMPARE(m_music->artistMode(), QStringLiteral("artists"));
    QCOMPARE(slowSpy.count(), 1); // synchronous, with the request still in flight
    settle();

    // Failing: the reply lambda returns at setError() and never emits, so the
    // setter's own is the only notification there will ever be.
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Artists/AlbumArtists"), 500,
                     QByteArrayLiteral("{}"));
    QSignalSpy failSpy(m_music, &MusicController::artistsChanged);
    m_music->setArtistMode(QStringLiteral("albumArtists"));
    QCOMPARE(m_music->artistMode(), QStringLiteral("albumArtists"));
    QCOMPARE(failSpy.count(), 1);
    settle();
    QVERIFY(!m_music->errorMessage().isEmpty());
    QCOMPARE(failSpy.count(), 1);
}

// The same contract every other setter keeps (see
// aPreferenceSetBeforeAnythingLoadsFiresNoRequest and m_started): a mode chosen
// before the first list was asked for is a preference. setArtistMode() used to
// call loadArtists(), which sets m_started unconditionally and fires — the one
// thing the contract says a setter must not do.
void MusicQueryTest::artistModeSetBeforeAnythingLoadsFiresNoRequest()
{
    auto *fresh = new MusicController(m_client, this);
    fresh->setLibrary(kMusicLibrary);
    const int before = m_mock->requestCount();
    QSignalSpy spy(fresh, &MusicController::artistsChanged);

    fresh->setArtistMode(QStringLiteral("artists"));
    QTest::qWait(50);
    QCOMPARE(m_mock->requestCount(), before);
    // …and the chips still move, because the value did.
    QCOMPARE(spy.count(), 1);
    QCOMPARE(fresh->artistMode(), QStringLiteral("artists"));

    // The preference is honoured by the first real load: /Artists, and not the
    // album-artist endpoint at all.
    const int albumArtists = requestsFor(QStringLiteral("/Artists/AlbumArtists"));
    fresh->loadArtists();
    QTRY_VERIFY_WITH_TIMEOUT(!fresh->loading(), 5000);
    QCOMPARE(requestsFor(QStringLiteral("/Artists/AlbumArtists")), albumArtists);
    QVERIFY(requestsFor(QStringLiteral("/Artists")) > 0);
    delete fresh;
}

// ── The Playlists tab (MUSIC.md §3) ─────────────────────────────────────────
// The open question of that section was whether Emby exposes a playlist's media
// type on the list payload. Measured against the live 4.9.5.0 server: it does
// not — not on /Items, not on the item detail payload, and `Fields=MediaType`
// does not add it. `MediaTypes=Audio` is a trap rather than an answer: asked
// alongside IncludeItemTypes=Playlist it DISCARDS the type constraint and
// returns the whole library, and Audio and Video answer identically.
//
// ParentId does the job instead, in one request. Emby resolves a library id to
// that library's content type and matches each playlist's own media type
// against it — proven with an audio, a video and an untyped playlist, all three
// stored outside every library folder under /config/data/userplaylists: the
// music library's id returned the audio pair, the movie library's the video one.
//
// So the fallback §3 allowed for — asking /Playlists/{id}/Items for one item and
// caching its type — is not built, and this test is what says so: the tab costs
// exactly one request and never touches /Playlists/{id}/Items.
void MusicQueryTest::playlistsAreScopedToTheLibraryNotProbedOneByOne()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"pl1\",\"Name\":\"Road Trip\","
                                       "\"Type\":\"Playlist\"}],\"TotalRecordCount\":1564}"));

    m_music->setTab(QStringLiteral("playlists"));
    m_music->loadPlaylists();
    settle();

    const QUrlQuery query = lastItemsQuery();
    QCOMPARE(query.queryItemValue(QStringLiteral("IncludeItemTypes")),
             QStringLiteral("Playlist"));
    QCOMPARE(query.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
    // The whole audio scoping, and the only thing that separates this from the
    // user's film playlists.
    QCOMPARE(query.queryItemValue(QStringLiteral("ParentId")), kMusicLibrary);
    // MediaTypes must never be sent: it silently drops IncludeItemTypes.
    QVERIFY(!query.hasQueryItem(QStringLiteral("MediaTypes")));

    QCOMPARE(m_music->playlists()->rowCount(), 1);
    // Its own model, never PlaylistController's — that one still has to offer
    // film playlists to a picker raised from a film.
    QVERIFY(m_music->playlists() != m_music->songs());
    QVERIFY(m_music->canLoadMorePlaylists());

    // Not one request per playlist. The N+1 fallback is the thing this design
    // avoids, so its absence is asserted rather than assumed.
    QCOMPARE(requestsFor(QStringLiteral("/Playlists/pl1/Items")), 0);
    QCOMPARE(requestsFor(QStringLiteral("/Users/%1/Items").arg(kUserId)), 1);

    m_music->loadMorePlaylists();
    settle();
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("StartIndex")), QStringLiteral("1"));

    // The sort set is the measured one: a playlist has no release year, and
    // "Track number" belongs to a song.
    QStringList keys;
    for (const QVariant &entry : m_music->availableSorts())
        keys.append(entry.toMap().value(QStringLiteral("key")).toString());
    QVERIFY(keys.contains(QStringLiteral("DateCreated")));
    QVERIFY(keys.contains(QStringLiteral("Runtime")));
    QVERIFY(!keys.contains(QStringLiteral("ProductionYear")));

    // Shared filters reach it. GenreIds is asserted here on purpose: measured
    // on the live server, Emby
    // aggregates a playlist's genres from its members and publishes them, and
    // GenreIds narrows this query the way it narrows albums — 191 of 1,564
    // playlists for the Rock id, against 627 of 5,037 albums. Dropping it would
    // make a lit genre chip silently mean nothing on this one tab.
    m_music->setNameStartsWith(QStringLiteral("W"));
    m_music->setGenreIds({QStringLiteral("1932975")});
    settle();
    const QUrlQuery filtered = lastItemsQuery();
    QCOMPARE(filtered.queryItemValue(QStringLiteral("NameStartsWith")), QStringLiteral("W"));
    QCOMPARE(filtered.queryItemValue(QStringLiteral("GenreIds")), QStringLiteral("1932975"));

    m_music->setFavoritesOnly(true);
    settle();
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("Filters")),
             QStringLiteral("IsFavorite"));

    // Random is a single-page sort here too: Emby reshuffles per request, so a
    // second page would repeat rows the first already showed.
    m_music->setSort(QStringLiteral("Random"), false);
    settle();
    QVERIFY(!m_music->canLoadMorePlaylists());
}

// With no library there is no ParentId, and with no ParentId there is nothing
// telling an audio playlist from a video one. An unscoped fetch would fill a
// heading that says "music" with the user's film lists, which is precisely the
// failure MUSIC.md §3 set out to avoid — so it asks for nothing instead.
void MusicQueryTest::anUnscopedControllerFetchesNoPlaylists()
{
    auto *unscoped = new MusicController(m_client, this);
    const int before = m_mock->requestCount();
    unscoped->setTab(QStringLiteral("playlists"));
    unscoped->loadPlaylists();
    QTest::qWait(80);
    QCOMPARE(m_mock->requestCount(), before);
    QCOMPARE(unscoped->playlists()->rowCount(), 0);
    QVERIFY(!unscoped->loading());
    delete unscoped;
}

// A playlist made from a track has to turn up in the tab whose job is to list
// it. PlaylistController refreshes its own list and cannot know about this one,
// so the two are joined by a signal (wired in Application) rather than by a
// page remembering to relay it.
void MusicQueryTest::createdPlaylistsReappearInTheMusicTab()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"pl1\",\"Name\":\"Road Trip\","
                                       "\"Type\":\"Playlist\"}],\"TotalRecordCount\":1}"));
    m_music->setTab(QStringLiteral("playlists"));
    m_music->loadPlaylists();
    settle();
    QCOMPARE(m_music->playlists()->rowCount(), 1);
    const int before = requestsFor(QStringLiteral("/Users/%1/Items").arg(kUserId));

    // On screen: refetch now, or the playlist the user just made is missing
    // from the grid they made it in front of.
    m_music->invalidatePlaylists();
    settle();
    QCOMPARE(requestsFor(QStringLiteral("/Users/%1/Items").arg(kUserId)), before + 1);

    // Not on screen: empty it and let the tab's own "load if empty" path pay
    // for the request when it is next looked at — one request instead of one
    // per playlist created while browsing albums.
    m_music->setTab(QStringLiteral("albums"));
    settle();
    const int afterAlbums = requestsFor(QStringLiteral("/Users/%1/Items").arg(kUserId));
    m_music->invalidatePlaylists();
    QTest::qWait(80);
    QCOMPARE(requestsFor(QStringLiteral("/Users/%1/Items").arg(kUserId)), afterAlbums);
    QCOMPARE(m_music->playlists()->rowCount(), 0);
}

// ── `loading` must never outlive the request that raised it ─────────────────
// invalidatePlaylists() retires the playlist page in flight and only refetches
// when the Playlists tab is the one on screen. From any other tab that leaves a
// request that has been made stale and no replacement for it: the reply returns
// above the flag, and nothing else ever lowers it.
//
// The cost is not a spinner. `loading` is the guard on every loadMore* in this
// controller and on every ensure*() in MusicPage, so a stranded flag kills
// scroll paging on all four tabs for the rest of the session, and only a sort or
// filter change brings it back.
//
// The route delay is what makes this deterministic: without it the playlist page
// resolves before there is anything to strand and the test passes against the
// bug.
void MusicQueryTest::invalidatingPlaylistsFromAnotherTabDoesNotStrandLoading()
{
    const auto itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"1\",\"Name\":\"Threnody\","
                                       "\"Type\":\"Audio\"}],\"TotalRecordCount\":56283}"));

    // Songs first, so its model has rows and ensureCurrentTab() issues nothing
    // when the user comes back to it — the whole point of the sequence.
    m_music->setTab(QStringLiteral("songs"));
    m_music->loadSongs();
    settle();
    QCOMPARE(m_music->songs()->rowCount(), 1);

    // Playlists, on a library big enough that the page is still on the wire.
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 400);
    m_music->setTab(QStringLiteral("playlists"));
    QVERIFY(m_music->loading());
    const int inFlight = requestsFor(itemsPath);

    // Back to Songs. Its model has rows, so nothing is requested and nothing new
    // will clear the flag.
    m_music->setTab(QStringLiteral("songs"));
    QCOMPARE(requestsFor(itemsPath), inFlight);
    QVERIFY(m_music->loading());

    // Create a playlist from a track row: PlaylistController emits
    // playlistsMutated and Application routes it here.
    m_music->invalidatePlaylists();
    // Immediately, not eventually: the retired request is the only thing that
    // could have lowered this, and it is not coming back.
    QVERIFY(!m_music->loading());
    QCOMPARE(m_music->playlists()->rowCount(), 0);

    // The stale reply lands a moment later and must not raise it again either.
    QTest::qWait(600);
    QVERIFY(!m_music->loading());

    // And paging is alive: this is what the strand actually broke.
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 0);
    QVERIFY(m_music->canLoadMoreSongs());
    const int before = requestsFor(itemsPath);
    m_music->loadMoreSongs();
    settle();
    QCOMPARE(requestsFor(itemsPath), before + 1);
    QCOMPARE(lastItemsQuery().queryItemValue(QStringLiteral("StartIndex")), QStringLiteral("1"));
}

// The same shape at fetchPlaylists()' other early return. With no library there
// is no ParentId and nothing to ask for — but the generation has already been
// bumped by then, so the return has to answer for the flag as well.
//
// Reachable without a delayed playlist route at all: an unscoped controller
// (which is a legitimate state — an empty library id means "every music
// library") with a songs page in flight, and any filter change. applyQueryChange
// retires all four lists, which makes the songs reply stale, and then asks the
// playlists tab for a page it cannot fetch.
void MusicQueryTest::aPlaylistFetchWithNoLibraryDoesNotStrandLoading()
{
    const auto itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 400);

    auto *unscoped = new MusicController(m_client, this);
    unscoped->setTab(QStringLiteral("songs"));
    unscoped->loadSongs();
    QVERIFY(unscoped->loading());

    unscoped->setTab(QStringLiteral("playlists"));
    // A genre picked while the songs page is still on the wire.
    unscoped->setGenreIds({QStringLiteral("1932901")});
    QVERIFY(!unscoped->loading());

    // The retired songs reply arrives and changes nothing.
    QTest::qWait(600);
    QVERIFY(!unscoped->loading());
    QCOMPARE(unscoped->playlists()->rowCount(), 0);
    delete unscoped;
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 0);
}

void MusicQueryTest::albumDetailStatusIsIsolatedFromListRequests()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);

    // A successful library-list request is already in flight when the album
    // detail fails. Its later success must not erase the detail-owned error.
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200, kEmptyPage);
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 350);
    m_music->loadAlbums();
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(itemsPath), 1, 5000);

    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 500, QByteArrayLiteral("{}"));
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 0);
    m_music->openAlbum(QStringLiteral("album-failed"), QStringLiteral("Failed Album"));
    QCOMPARE(m_music->detailKind(), QStringLiteral("album"));
    QCOMPARE(m_music->detailId(), QStringLiteral("album-failed"));
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->detailLoading(), 5000);
    QVERIFY(!m_music->detailErrorMessage().isEmpty());
    const QString detailFailure = m_music->detailErrorMessage();
    QVERIFY(m_music->loading());
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->loading(), 5000);
    QCOMPARE(m_music->detailErrorMessage(), detailFailure);

    // Reverse the interleaving: an empty successful detail remains an honest
    // empty result even when an unrelated list failure lands afterwards.
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 500, QByteArrayLiteral("{}"));
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 350);
    m_music->loadAlbums();
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(itemsPath), 3, 5000);

    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200, kEmptyPage);
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 0);
    m_music->openAlbum(QStringLiteral("album-empty"), QStringLiteral("Empty Album"));
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->detailLoading(), 5000);
    QCOMPARE(m_music->detailId(), QStringLiteral("album-empty"));
    QVERIFY(m_music->detailErrorMessage().isEmpty());
    QCOMPARE(m_music->tracks()->rowCount(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->loading(), 5000);
    QVERIFY(!m_music->errorMessage().isEmpty());
    QVERIFY(m_music->detailErrorMessage().isEmpty());
}

void MusicQueryTest::albumDetailStatusRejectsRetiredReplies()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 500, QByteArrayLiteral("{}"));
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 350);
    m_music->openAlbum(QStringLiteral("album-a"), QStringLiteral("Album A"));
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(itemsPath), 1, 5000);

    const QByteArray albumB = QByteArrayLiteral(
        "{\"Items\":[{\"Id\":\"track-b\",\"Name\":\"Track B\",\"Type\":\"Audio\"}],"
        "\"TotalRecordCount\":1}");
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200, albumB);
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 0);
    m_music->openAlbum(QStringLiteral("album-b"), QStringLiteral("Album B"));
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->detailLoading(), 5000);
    QCOMPARE(m_music->detailKind(), QStringLiteral("album"));
    QCOMPARE(m_music->detailId(), QStringLiteral("album-b"));
    QVERIFY(m_music->detailErrorMessage().isEmpty());
    QCOMPARE(m_music->tracks()->rowCount(), 1);

    QTest::qWait(450);
    QCOMPARE(m_music->detailId(), QStringLiteral("album-b"));
    QVERIFY(m_music->detailErrorMessage().isEmpty());
    QCOMPARE(m_music->tracks()->get(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("track-b"));
}

void MusicQueryTest::artistDetailStatusIsOwnedAndIsolated()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);

    // Both artist-detail requests fail promptly while a successful list request
    // remains delayed. The list completion cannot clear the discography error.
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200, kEmptyPage);
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 350);
    m_music->loadAlbums();
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(itemsPath), 1, 5000);
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 500, QByteArrayLiteral("{}"));
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 0);
    m_music->openArtist(QStringLiteral("artist-failed"), QStringLiteral("Failed Artist"));
    QCOMPARE(m_music->detailKind(), QStringLiteral("artist"));
    QCOMPARE(m_music->detailId(), QStringLiteral("artist-failed"));
    QVERIFY(m_music->artistAlbumsLoading());
    QVERIFY(m_music->artistTracksLoading());
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->detailLoading(), 5000);
    QVERIFY(!m_music->artistAlbumsLoading());
    QVERIFY(!m_music->artistTracksLoading());
    const QString detailFailure = m_music->detailErrorMessage();
    QVERIFY(!detailFailure.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->loading(), 5000);
    QCOMPARE(m_music->detailErrorMessage(), detailFailure);

    // Retarget the artist lane while A is delayed. Neither of A's replies may
    // mutate B's empty successful state.
    const QByteArray staleArtistA = QByteArrayLiteral(
        "{\"Items\":[{\"Id\":\"stale-a\",\"Name\":\"Stale A\",\"Type\":\"Audio\"}],"
        "\"TotalRecordCount\":1}");
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200, staleArtistA);
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 350);
    const int beforeA = requestsFor(itemsPath);
    m_music->openArtist(QStringLiteral("artist-a"), QStringLiteral("Artist A"));
    QVERIFY(m_music->artistAlbumsLoading());
    QVERIFY(m_music->artistTracksLoading());
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(itemsPath), beforeA + 2, 5000);
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200, kEmptyPage);
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsPath, 0);
    m_music->openArtist(QStringLiteral("artist-b"), QStringLiteral("Artist B"));
    QVERIFY(m_music->artistAlbumsLoading());
    QVERIFY(m_music->artistTracksLoading());
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->detailLoading(), 5000);
    QVERIFY(!m_music->artistAlbumsLoading());
    QVERIFY(!m_music->artistTracksLoading());
    QCOMPARE(m_music->detailKind(), QStringLiteral("artist"));
    QCOMPARE(m_music->detailId(), QStringLiteral("artist-b"));
    QVERIFY(m_music->detailErrorMessage().isEmpty());
    QTest::qWait(450);
    QCOMPARE(m_music->detailId(), QStringLiteral("artist-b"));
    QVERIFY(m_music->detailErrorMessage().isEmpty());
    QCOMPARE(m_music->artistAlbums()->rowCount(), 0);
    QCOMPARE(m_music->artistTracks()->rowCount(), 0);
}

void MusicQueryTest::detailOwnersCanBeReacquiredAfterCrossNavigation()
{
    const QString itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), itemsPath, 200, kEmptyPage);

    // AlbumPage and ArtistPage share one detail lane. A covered page restores
    // itself by issuing the same open verb when it becomes visible again; the
    // controller must allow that A -> B -> A sequence even though its legacy
    // albumId/artistId scalar for A was never cleared by opening B.
    m_music->openArtist(QStringLiteral("artist-a"), QStringLiteral("Artist A"));
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->detailLoading(), 5000);
    QCOMPARE(m_music->detailKind(), QStringLiteral("artist"));
    QCOMPARE(m_music->detailId(), QStringLiteral("artist-a"));

    m_music->openAlbum(QStringLiteral("album-b"), QStringLiteral("Album B"));
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->detailLoading(), 5000);
    QCOMPARE(m_music->detailKind(), QStringLiteral("album"));
    QCOMPARE(m_music->detailId(), QStringLiteral("album-b"));

    m_music->openArtist(QStringLiteral("artist-a"), QStringLiteral("Artist A"));
    QCOMPARE(m_music->detailKind(), QStringLiteral("artist"));
    QCOMPARE(m_music->detailId(), QStringLiteral("artist-a"));
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->detailLoading(), 5000);

    m_music->openAlbum(QStringLiteral("album-b"), QStringLiteral("Album B"));
    QCOMPARE(m_music->detailKind(), QStringLiteral("album"));
    QCOMPARE(m_music->detailId(), QStringLiteral("album-b"));
    QTRY_VERIFY_WITH_TIMEOUT(!m_music->detailLoading(), 5000);
}

void MusicQueryTest::sessionResetClearsScopeAndAllowsSameLibraryForNextUser()
{
    const auto userB = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    const QString itemsA = QStringLiteral("/Users/%1/Items").arg(kUserId);
    const QString itemsB = QStringLiteral("/Users/%1/Items").arg(userB);
    m_mock->addRoute(QStringLiteral("GET"), itemsA, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"audio-a\",\"Name\":\"A "
                                       "Audio\",\"Type\":\"Audio\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/MusicGenres"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"genre-a\",\"Name\":\"A "
                                       "Genre\",\"Type\":\"MusicGenre\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsA, 350);
    m_mock->setRouteDelay(QStringLiteral("GET"), QStringLiteral("/MusicGenres"), 350);

    m_music->setArtistMode(QStringLiteral("artists"));
    m_music->setNameStartsWith(QStringLiteral("A"));
    m_music->setGenreIds({QStringLiteral("genre-a")});
    m_music->setFavoritesOnly(true);
    m_music->setTab(QStringLiteral("songs"));
    m_music->loadSongs();
    m_music->openAlbum(QStringLiteral("shared-album"), QStringLiteral("A Album"));
    m_music->loadGenres();
    QTRY_VERIFY_WITH_TIMEOUT(requestsFor(itemsA) >= 2, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(requestsFor(QStringLiteral("/MusicGenres")), 1, 5000);
    QVERIFY(m_music->loading());
    QCOMPARE(m_music->albumId(), QStringLiteral("shared-album"));

    m_music->resetSessionState();
    QVERIFY(!m_music->loading());
    QVERIFY(m_music->libraryId().isEmpty());
    QVERIFY(m_music->albumId().isEmpty());
    QVERIFY(m_music->artistId().isEmpty());
    QVERIFY(m_music->detailKind().isEmpty());
    QVERIFY(m_music->detailId().isEmpty());
    QVERIFY(!m_music->detailLoading());
    QVERIFY(!m_music->artistAlbumsLoading());
    QVERIFY(!m_music->artistTracksLoading());
    QVERIFY(m_music->detailErrorMessage().isEmpty());
    QCOMPARE(m_music->tab(), QStringLiteral("albums"));
    QCOMPARE(m_music->artistMode(), QStringLiteral("albumArtists"));
    QCOMPARE(m_music->sortBy(), QStringLiteral("SortName"));
    QVERIFY(!m_music->sortDescending());
    QVERIFY(m_music->nameStartsWith().isEmpty());
    QVERIFY(m_music->genreIds().isEmpty());
    QVERIFY(!m_music->favoritesOnly());
    QVERIFY(m_music->genreOptions().isEmpty());
    QCOMPARE(m_music->albums()->rowCount(), 0);
    QCOMPARE(m_music->artists()->rowCount(), 0);
    QCOMPARE(m_music->songs()->rowCount(), 0);
    QCOMPARE(m_music->playlists()->rowCount(), 0);
    QCOMPARE(m_music->tracks()->rowCount(), 0);
    QCOMPARE(m_music->artistAlbums()->rowCount(), 0);
    QCOMPARE(m_music->artistTracks()->rowCount(), 0);

    m_client->setSession(kToken, userB);
    m_music->setLibrary(kMusicLibrary); // the same id must not hit A's old early return
    const int beforePreference = m_mock->requestCount();
    m_music->setTab(QStringLiteral("songs"));
    QTest::qWait(100);
    // resetSessionState() restores the pre-load state: a tab preference alone
    // does not start a request in the new account.
    QCOMPARE(m_mock->requestCount(), beforePreference);

    m_mock->addRoute(QStringLiteral("GET"), itemsB, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"album-b\",\"Name\":\"B "
                                       "Album\",\"Type\":\"MusicAlbum\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/MusicGenres"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"genre-b\",\"Name\":\"B "
                                       "Genre\",\"Type\":\"MusicGenre\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->setRouteDelay(QStringLiteral("GET"), QStringLiteral("/MusicGenres"), 0);
    m_music->setTab(QStringLiteral("albums"));
    m_music->loadAlbums();
    m_music->loadGenres();
    settle();
    QTRY_COMPARE_WITH_TIMEOUT(m_music->genreOptions().size(), 1, 5000);
    QCOMPARE(m_music->albums()->rowCount(), 1);
    QCOMPARE(m_music->albums()->get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("B Album"));
    QCOMPARE(m_music->genreOptions().first().toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("B Genre"));

    QTest::qWait(450);
    QVERIFY(!m_music->loading());
    QCOMPARE(m_music->albums()->get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("B Album"));
    QCOMPARE(m_music->genreOptions().first().toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("B Genre"));
}

QTEST_MAIN(MusicQueryTest)
#include "tst_music_query.moc"
