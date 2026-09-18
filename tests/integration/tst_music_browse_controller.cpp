#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

#include <algorithm>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "app/controllers/PlaylistController.h"
#include "app/controllers/music/MusicBrowseController.h"
#include "app/controllers/music/MusicLane.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicQueryTranslator.h"
#include "app/music/MusicRepository.h"
#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/ArtistGridModel.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"
#include "server/emby/MusicServerCapabilities.h"

using namespace strmqt;
using namespace strmqt::music;

namespace {

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
const auto kLibrary = QStringLiteral("1868998");
const auto kAlbumArtistsPath = QStringLiteral("/Artists/AlbumArtists");

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

QString itemsPath() { return QStringLiteral("/Users/%1/Items").arg(kUserId); }

QJsonObject albumJson(const QString &id)
{
    return {{"Id", id}, {"Name", "Album " + id}, {"Type", "MusicAlbum"},
            {"AlbumArtists", QJsonArray{QJsonObject{{"Id", "ar1"}, {"Name", "Artist ar1"}}}},
            {"ChildCount", 10}, {"ImageTags", QJsonObject{{"Primary", "tag-" + id}}}};
}

QJsonObject artistJson(const QString &id)
{
    return {{"Id", id}, {"Name", "Artist " + id}, {"Type", "MusicArtist"}};
}

QJsonObject playlistJson(const QString &id, const QString &name)
{
    return {{"Id", id}, {"Name", name}, {"Type", "Playlist"}, {"ChildCount", 4}};
}

QJsonObject trackJson(const QString &id, const QString &albumId, int number)
{
    return {{"Id", id}, {"Name", "Track " + id}, {"Type", "Audio"}, {"AlbumId", albumId},
            {"Album", "Album " + albumId}, {"ParentIndexNumber", 1}, {"IndexNumber", number},
            {"RunTimeTicks", 4 * 60 * 10'000'000LL}};
}

QByteArray page(const QJsonArray &items, int total = -1)
{
    return QJsonDocument(QJsonObject{{"Items", items}, {"TotalRecordCount", total < 0 ? items.size() : total}})
        .toJson(QJsonDocument::Compact);
}

QJsonArray albums(const QString &prefix, int count)
{
    QJsonArray out;
    for (int i = 0; i < count; ++i)
        out.append(albumJson(prefix + QString::number(i)));
    return out;
}

QStringList modelIds(MusicModelBase *model)
{
    QStringList ids;
    for (int row = 0; row < model->count(); ++row)
        ids.append(model->idAt(row));
    return ids;
}

QStringList queueIds(PlayQueue *queue)
{
    QStringList ids;
    for (int row = 0; row < queue->rowCount(); ++row)
        ids.append(queue->itemAt(row).value(QStringLiteral("itemId")).toString());
    return ids;
}

using Q = QList<QPair<QString, QString>>;

} // namespace

class MusicBrowseControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void sendsTheTranslatedQuery();
    void remembersSortPerSection();
    void filterChangeRefetchesVisibleAndInvalidatesHidden();
    void dropsRepliesForASupersededQuery();
    void loadMoreAppendsTheNextPage();
    void openGenreSwitchesToAlbumsWithOnlyThatGenre();
    void countTextShowsUnfilteredArrowFiltered();
    void formatAvailabilityFollowsCapabilities();
    void playAndShuffleUseTheScopeLabel();
    void genreOptionsCarryCountsFromAllGenres();
    void lettersAndSectionsCycle();
    void routeStateRoundTrips();
    void libraryRetargetDropsTheInFlightPage();
    void createdPlaylistsReappearInThePlaylistsSection();
    void sessionResetClearsScopeAndAllowsSameLibraryForNextUser();

private:
    PlayQueue *queue() const { return m_player->queue(); }
    int requestsTo(const QString &path, const Q &required = {}) const;
    QUrlQuery lastItems() const { return QUrlQuery(m_mock->lastRequestFor("GET", itemsPath()).query); }

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_player = nullptr;
    ItemActions *m_actions = nullptr;
    MusicRepository *m_repo = nullptr;
    MusicPlayback *m_playback = nullptr;
    MusicBrowseController *m_ctl = nullptr;
};

int MusicBrowseControllerTest::requestsTo(const QString &path, const Q &required) const
{
    int count = 0;
    for (const auto &request : m_mock->requests()) {
        if (request.path != path)
            continue;
        const QUrlQuery query(request.query);
        const bool matches = std::all_of(required.cbegin(), required.cend(), [&](const auto &pair) {
            return query.queryItemValue(pair.first) == pair.second;
        });
        if (matches)
            ++count;
    }
    return count;
}

void MusicBrowseControllerTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    for (const char *id : {"t1", "t2"}) {
        QVERIFY(m_mock->addRouteFromFile("POST", QStringLiteral("/Items/%1/PlaybackInfo").arg(id),
                                         fixturePath(QStringLiteral("playback_info.json"))));
    }
    m_mock->addRoute("POST", "/Sessions/Playing", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Progress", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Stopped", 204, {});
    m_mock->addRoute("GET", itemsPath(), 200, page(albums("a", 3), 3));
    m_mock->addRoute("GET", kAlbumArtistsPath, 200, page({artistJson("ar1"), artistJson("ar2")}));
    m_mock->addRoute("GET", "/Artists", 200, page({artistJson("ar1"), artistJson("ar2"), artistJson("ar3")}));

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
    m_playback = new MusicPlayback(m_repo, m_actions, this);
    m_ctl = new MusicBrowseController(m_repo, m_playback, this);
}

void MusicBrowseControllerTest::cleanup()
{
    delete m_ctl;
    delete m_playback;
    delete m_repo;
    delete m_actions;
    delete m_player;
    delete m_settings;
    delete m_dir;
    delete m_backend;
    delete m_client;
    delete m_mock;
    m_ctl = nullptr;
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

void MusicBrowseControllerTest::sendsTheTranslatedQuery()
{
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QCOMPARE(m_ctl->libraryId(), kLibrary);
    QCOMPARE(m_ctl->section(), QStringLiteral("albums"));
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());
    QCOMPARE(m_ctl->albums()->count(), 3);

    QUrlQuery sent = lastItems();
    QCOMPARE(sent.queryItemValue("ParentId"), kLibrary);
    QCOMPARE(sent.queryItemValue("IncludeItemTypes"), QStringLiteral("MusicAlbum"));
    QCOMPARE(sent.queryItemValue("SortBy"), QStringLiteral("SortName"));
    QCOMPARE(sent.queryItemValue("SortOrder"), QStringLiteral("Ascending"));
    QCOMPARE(sent.queryItemValue("StartIndex"), QStringLiteral("0"));
    QCOMPARE(sent.queryItemValue("Limit"), QStringLiteral("100"));

    m_ctl->setGenres({QStringLiteral("g1")});
    m_ctl->setLetter(QStringLiteral("B"));
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());
    sent = lastItems();
    QCOMPARE(sent.queryItemValue("GenreIds"), QStringLiteral("g1"));
    QCOMPARE(sent.queryItemValue("NameStartsWithOrGreater"), QStringLiteral("B"));
    QCOMPARE(sent.queryItemValue("NameLessThan"), QStringLiteral("C"));
    QVERIFY(m_ctl->filtered());
    QCOMPARE(m_ctl->activeFilterCount(), 1);
}

void MusicBrowseControllerTest::remembersSortPerSection()
{
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());

    m_ctl->setSortKey(QStringLiteral("year"));
    QVERIFY(m_ctl->sortDescending()); // the key's default direction
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());
    QUrlQuery sent = lastItems();
    QCOMPARE(sent.queryItemValue("SortBy"), QStringLiteral("ProductionYear,PremiereDate,SortName"));
    QCOMPARE(sent.queryItemValue("SortOrder"), QStringLiteral("Descending"));

    m_ctl->setSection(QStringLiteral("songs"));
    QCOMPARE(m_ctl->sortKey(), QStringLiteral("name"));
    QVERIFY(!m_ctl->sortDescending());
    m_ctl->setSortKey(QStringLiteral("duration"));
    QCOMPARE(m_ctl->sortLabel(), QStringLiteral("Duration"));
    QTRY_VERIFY(!m_ctl->songsLane()->loading());

    const int before = requestsTo(itemsPath());
    m_ctl->setSection(QStringLiteral("albums"));
    QCOMPARE(m_ctl->sortKey(), QStringLiteral("year"));
    QVERIFY(m_ctl->sortDescending());
    QCOMPARE(m_ctl->sortLabel(), QStringLiteral("Year"));
    QCOMPARE(requestsTo(itemsPath()), before); // still fresh: no refetch
    m_ctl->setSection(QStringLiteral("songs"));
    QCOMPARE(m_ctl->sortKey(), QStringLiteral("duration"));
}

void MusicBrowseControllerTest::filterChangeRefetchesVisibleAndInvalidatesHidden()
{
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());
    m_ctl->setSection(QStringLiteral("artists"));
    QTRY_COMPARE(m_ctl->artists()->count(), 2);
    m_ctl->setSection(QStringLiteral("albums"));

    const int items = requestsTo(itemsPath());
    const int artists = requestsTo(kAlbumArtistsPath);
    m_ctl->setFavouritesOnly(true);
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());

    QCOMPARE(requestsTo(itemsPath()), items + 1); // visible only; the unfiltered count is already known
    QCOMPARE(lastItems().queryItemValue("Filters"), QStringLiteral("IsFavorite"));
    QCOMPARE(requestsTo(kAlbumArtistsPath), artists); // hidden: not fetched
    QCOMPARE(m_ctl->artists()->count(), 0);            // …but invalidated

    m_ctl->setSection(QStringLiteral("artists"));
    QTRY_COMPARE(requestsTo(kAlbumArtistsPath), artists + 1);
    QTRY_COMPARE(m_ctl->artists()->count(), 2);
}

void MusicBrowseControllerTest::dropsRepliesForASupersededQuery()
{
    m_mock->addRoute("GET", itemsPath(), 200, page({albumJson("n1")}));
    m_mock->setRouteDelay("GET", itemsPath(), 400);
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"SortBy", "ProductionYear,PremiereDate,SortName"}}, 200,
                          page({albumJson("y1")}));

    QStringList seen;
    const auto connection = connect(m_ctl->albums(), &QAbstractItemModel::modelReset, this,
                                    [&] { seen += modelIds(m_ctl->albums()); });
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    m_ctl->setSortKey(QStringLiteral("year")); // before the name reply lands

    QTRY_COMPARE(modelIds(m_ctl->albums()), QStringList{QStringLiteral("y1")});
    QTest::qWait(700); // the delayed name reply has arrived by now
    QCOMPARE(modelIds(m_ctl->albums()), QStringList{QStringLiteral("y1")});
    QVERIFY(!seen.contains(QStringLiteral("n1")));
    QVERIFY(!m_ctl->albumsLane()->loading());
    QVERIFY(m_ctl->albumsLane()->error().isEmpty());
    disconnect(connection);
}

void MusicBrowseControllerTest::loadMoreAppendsTheNextPage()
{
    m_mock->addRoute("GET", itemsPath(), 200, page(albums("a", 100), 250));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "MusicAlbum"}, {"StartIndex", "100"}}, 200,
                          page(albums("b", 100), 250));

    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QTRY_COMPARE(m_ctl->albums()->count(), 100);
    QVERIFY(m_ctl->albums()->canLoadMore());

    m_ctl->loadMore();
    m_ctl->loadMore(); // ignored while the first is in flight
    QTRY_COMPARE(m_ctl->albums()->count(), 200);
    QCOMPARE(m_ctl->albums()->idAt(100), QStringLiteral("b0"));
    QCOMPARE(requestsTo(itemsPath(), Q{{"StartIndex", "100"}}), 1);
    QCOMPARE(m_ctl->resultCount(), 250);
}

void MusicBrowseControllerTest::openGenreSwitchesToAlbumsWithOnlyThatGenre()
{
    m_ctl->open(kLibrary, QStringLiteral("songs"));
    m_ctl->setFavouritesOnly(true);
    m_ctl->setDecade(1970);
    m_ctl->setSection(QStringLiteral("albums"));
    m_ctl->setLetter(QStringLiteral("M"));
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());

    m_ctl->openGenre(QStringLiteral("g7"), QStringLiteral("Jazz"));
    QCOMPARE(m_ctl->section(), QStringLiteral("albums"));
    QCOMPARE(m_ctl->genreIds(), QStringList{QStringLiteral("g7")});
    QCOMPARE(m_ctl->decade(), 0);
    QVERIFY(!m_ctl->favouritesOnly());
    QCOMPARE(m_ctl->letter(), QString());
    QCOMPARE(m_ctl->genrePillText(), QStringLiteral("Genre: Jazz"));

    QTRY_VERIFY(!m_ctl->albumsLane()->loading());
    const QUrlQuery sent = lastItems();
    QCOMPARE(sent.queryItemValue("GenreIds"), QStringLiteral("g7"));
    QVERIFY(!sent.hasQueryItem("Filters"));
    QVERIFY(!sent.hasQueryItem("NameStartsWithOrGreater"));
}

void MusicBrowseControllerTest::countTextShowsUnfilteredArrowFiltered()
{
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "MusicAlbum"}, {"Filters", "IsFavorite"}}, 200,
                          page(albums("f", 3), 38));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "MusicAlbum"}, {"Limit", "1"}}, 200,
                          page(albums("u", 1), 1204));

    // Opened already narrowed (a restored route), so the unfiltered total is unknown.
    m_ctl->restore(kLibrary, QStringLiteral("albums"),
                   QStringLiteral(R"({"v":1,"fav":true,"s":[{"k":"year","d":true,"l":""}]})"));
    QVERIFY(m_ctl->favouritesOnly());
    QCOMPARE(m_ctl->sortKey(), QStringLiteral("year"));

    QTRY_COMPARE(m_ctl->countText(), QStringLiteral("1,204 → 38 RECORDS · SORT: YEAR ↓"));
    QCOMPARE(m_ctl->resultCount(), 38);
    QCOMPARE(m_ctl->unfilteredCount(), 1204);
    QCOMPARE(requestsTo(itemsPath(), Q{{"Limit", "1"}}), 1);

    m_ctl->setFavouritesOnly(false);
    QTRY_COMPARE(m_ctl->countText(), QStringLiteral("3 RECORDS · SORT: YEAR ↓"));
    m_ctl->setSortKey(QStringLiteral("random"));
    QTRY_COMPARE(m_ctl->countText(), QStringLiteral("3 RECORDS · SORT: RANDOM"));
    QCOMPARE(requestsTo(itemsPath(), Q{{"Limit", "1"}}), 1);
}

void MusicBrowseControllerTest::formatAvailabilityFollowsCapabilities()
{
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    const QList<QPair<QString, Section>> sections{{"albums", Section::Albums},
                                                  {"artists", Section::Artists},
                                                  {"songs", Section::Songs},
                                                  {"genres", Section::Genres},
                                                  {"playlists", Section::Playlists}};
    for (const auto &[key, section] : sections) {
        m_ctl->setSection(key);
        QCOMPARE(m_ctl->section(), key);
        QCOMPARE(m_ctl->formatAvailable(), MusicQueryTranslator::formatFilterable(section));
        QCOMPARE(m_ctl->formatOptions().size(), MusicQueryTranslator::formatOptions(section).size());
        const bool filterable = section == Section::Albums || section == Section::Artists
                                || section == Section::Songs;
        QCOMPARE(m_ctl->filtersAvailable(), filterable);
        QCOMPARE(m_ctl->decadeAvailable(), section == Section::Albums || section == Section::Songs);
    }

    const bool anyFormat = MusicQueryTranslator::formatFilterable(Section::Albums)
                           || MusicQueryTranslator::formatFilterable(Section::Songs);
    m_ctl->setFormat(QStringLiteral("lossless"));
    QCOMPARE(m_ctl->format(), anyFormat ? QStringLiteral("lossless") : QStringLiteral("any"));
    QCOMPARE(m_ctl->formatPillText(), anyFormat ? QStringLiteral("Format: Lossless") : QStringLiteral("Format"));
}

void MusicBrowseControllerTest::playAndShuffleUseTheScopeLabel()
{
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "Audio"}, {"GenreIds", "g1"}}, 200,
                          page({trackJson("t1", "al", 1), trackJson("t2", "al", 2)}));

    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QCOMPARE(m_ctl->scopeLabel(), QStringLiteral("All music"));
    m_ctl->openGenre(QStringLiteral("g1"), QStringLiteral("Jazz"));
    m_ctl->setFavouritesOnly(true);
    QCOMPARE(m_ctl->scopeLabel(), QStringLiteral("Jazz · Favourites"));

    m_ctl->playFiltered();
    QTRY_COMPARE(queueIds(queue()), (QStringList{"t1", "t2"}));
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Jazz · Favourites"));

    m_ctl->shuffleFiltered();
    QTRY_COMPARE(queue()->sourceLabel(), QStringLiteral("Shuffle · Jazz · Favourites"));
    QCOMPARE(queue()->rowCount(), 2);
    QVERIFY(requestsTo(itemsPath(), Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "Random"}}) >= 1);
}

void MusicBrowseControllerTest::genreOptionsCarryCountsFromAllGenres()
{
    QJsonArray genres{QJsonObject{{"Id", "g1"}, {"Name", "Jazz"}, {"AlbumCount", 3}},
                      QJsonObject{{"Id", "g2"}, {"Name", "Rock"}, {"AlbumCount", 40}},
                      QJsonObject{{"Id", "g3"}, {"Name", "Folk"}, {"AlbumCount", 12}}};
    m_mock->addRoute("GET", "/MusicGenres", 200, page(genres));
    if (!emby::caps::kGenreItemCounts) {
        QJsonArray walk;
        auto tagged = [](const QString &id, const QString &genreId, const QString &name) {
            QJsonObject album = albumJson(id);
            album.insert("GenreItems", QJsonArray{QJsonObject{{"Id", genreId}, {"Name", name}}});
            return album;
        };
        for (int i = 0; i < 40; ++i)
            walk.append(tagged("r" + QString::number(i), "g2", "Rock"));
        for (int i = 0; i < 12; ++i)
            walk.append(tagged("f" + QString::number(i), "g3", "Folk"));
        for (int i = 0; i < 3; ++i)
            walk.append(tagged("j" + QString::number(i), "g1", "Jazz"));
        m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "MusicAlbum"}, {"Fields", "Genres"}}, 200,
                              page(walk));
    }

    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QTRY_COMPARE(m_ctl->genreOptions().size(), 3);
    QVERIFY(!m_ctl->genreOptionsLoading());
    QVariantMap first = m_ctl->genreOptions().first().toMap();
    QCOMPARE(first.value("id").toString(), QStringLiteral("g2"));
    QCOMPARE(first.value("name").toString(), QStringLiteral("Rock"));
    QCOMPARE(first.value("count").toInt(), 40);
    QCOMPARE(first.value("subtitle").toString(), QStringLiteral("40 records"));
    QVERIFY(!first.value("selected").toBool());

    m_ctl->setGenres({QStringLiteral("g2")});
    QVERIFY(m_ctl->genreOptions().first().toMap().value("selected").toBool());
    QCOMPARE(m_ctl->genrePillText(), QStringLiteral("Genre: Rock"));

    m_ctl->setGenres({QStringLiteral("g1"), QStringLiteral("g2"), QStringLiteral("g1")});
    QCOMPARE(m_ctl->genreIds(), (QStringList{"g1", "g2"}));
    QCOMPARE(m_ctl->genrePillText(), QStringLiteral("Genres: 2"));
    QCOMPARE(m_ctl->activeFilterCount(), 1);
    m_ctl->toggleGenre(QStringLiteral("g1"));
    QCOMPARE(m_ctl->genreIds(), QStringList{QStringLiteral("g2")});
}

void MusicBrowseControllerTest::lettersAndSectionsCycle()
{
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QVERIFY(m_ctl->letterStripVisible());
    QCOMPARE(m_ctl->letters().size(), 27);
    QCOMPARE(m_ctl->letters().first(), QStringLiteral("#"));

    QVERIFY(m_ctl->jumpLetter(1));
    QCOMPARE(m_ctl->letter(), QStringLiteral("A"));
    QVERIFY(m_ctl->jumpLetter(-1));
    QCOMPARE(m_ctl->letter(), QStringLiteral("#"));
    QVERIFY(m_ctl->jumpLetter(-1)); // clamped, still handled
    QCOMPARE(m_ctl->letter(), QStringLiteral("#"));
    m_ctl->setLetter(QString());
    QVERIFY(m_ctl->jumpLetter(-1));
    QCOMPARE(m_ctl->letter(), QStringLiteral("Z"));
    m_ctl->toggleLetter(QStringLiteral("Z"));
    QCOMPARE(m_ctl->letter(), QString());

    m_ctl->setSortKey(QStringLiteral("year"));
    QVERIFY(!m_ctl->letterStripVisible());
    QVERIFY(!m_ctl->jumpLetter(1));
    m_ctl->setLetter(QStringLiteral("B"));
    QCOMPARE(m_ctl->letter(), QString()); // refused off a name sort

    QCOMPARE(m_ctl->cycleSection(1), QStringLiteral("artists"));
    QCOMPARE(m_ctl->section(), QStringLiteral("artists"));
    QCOMPARE(m_ctl->cycleSection(-1), QStringLiteral("albums"));
    QCOMPARE(m_ctl->cycleSection(-1), QStringLiteral("home"));
    QCOMPARE(m_ctl->section(), QStringLiteral("albums")); // Home is a route, not a section
    m_ctl->setSection(QStringLiteral("playlists"));
    QCOMPARE(m_ctl->cycleSection(1), QStringLiteral("home"));
    QCOMPARE(m_ctl->section(), QStringLiteral("playlists"));

    m_ctl->setSection(QStringLiteral("genres"));
    QVERIFY(!m_ctl->letterStripVisible());
    QCOMPARE(m_ctl->sortKey(), QStringLiteral("size"));
    QVERIFY(m_ctl->sortDescending());
    m_ctl->setSection(QStringLiteral("nonsense"));
    QCOMPARE(m_ctl->section(), QStringLiteral("genres"));
}

void MusicBrowseControllerTest::routeStateRoundTrips()
{
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    m_ctl->setSortKey(QStringLiteral("year"));
    m_ctl->setSortDescending(false);
    m_ctl->setDecade(1990);
    m_ctl->setSection(QStringLiteral("artists"));
    m_ctl->setLetter(QStringLiteral("M"));
    m_ctl->setArtistMode(QStringLiteral("everyone"));
    m_ctl->setGenres({QStringLiteral("g1"), QStringLiteral("g2")});
    m_ctl->setUnplayedOnly(true);
    const QString state = m_ctl->routeState();
    QVERIFY(QJsonDocument::fromJson(state.toUtf8()).isObject());

    MusicBrowseController other(m_repo, m_playback);
    other.restore(kLibrary, QStringLiteral("artists"), state);
    QCOMPARE(other.routeState(), state);
    QCOMPARE(other.section(), QStringLiteral("artists"));
    QCOMPARE(other.letter(), QStringLiteral("M"));
    QCOMPARE(other.artistMode(), QStringLiteral("everyone"));
    QCOMPARE(other.decadePillText(), QStringLiteral("Decade: 90s"));
    other.setSection(QStringLiteral("albums"));
    QCOMPARE(other.sortKey(), QStringLiteral("year"));
    QVERIFY(!other.sortDescending());
    QCOMPARE(other.decade(), 1990);

    other.restore(kLibrary, QStringLiteral("songs"), QStringLiteral("not json"));
    QCOMPARE(other.section(), QStringLiteral("songs"));
    QCOMPARE(other.routeState(), state); // unreadable state keeps the live query
}

// Ported here when MusicController went (Task 10): tst_content_controllers'
// musicRetargetDropsTheInFlightPage covered this for the old controller, and no
// browse test covered a LIBRARY retarget (the epoch), only a superseded query
// (the lane generation). Clearing the models is not enough on its own: the reply
// already in flight put the old library's albums straight back, under the new
// library's name.
void MusicBrowseControllerTest::libraryRetargetDropsTheInFlightPage()
{
    const auto otherLibrary = QStringLiteral("2000001");
    // The plain route is the delayed one and answers the first library; the
    // query route wins for the second and answers at once. So the OLD reply is
    // the one that lands last, which is the ordering this guards. A query route
    // cannot be the delayed one — MockEmbyServer builds it with delayMs 0 and
    // setRouteDelay never reaches it (measured).
    m_mock->addRoute("GET", itemsPath(), 200, page({albumJson("old")}, 1));
    m_mock->setRouteDelay("GET", itemsPath(), 300);
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", otherLibrary}}, 200,
                          page({albumJson("new")}, 1));

    // Every id the model has ever published, so a row that appears and is then
    // replaced still fails: the old library's albums must never be visible at
    // all, not merely be gone by the end.
    QStringList seen;
    const auto connection = connect(m_ctl->albums(), &QAbstractItemModel::modelReset, this,
                                    [&] { seen += modelIds(m_ctl->albums()); });

    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QTRY_VERIFY(requestsTo(itemsPath(), Q{{"ParentId", kLibrary}}) >= 1);
    QVERIFY(m_ctl->albumsLane()->loading());

    m_ctl->open(otherLibrary, QStringLiteral("albums"));
    QCOMPARE(m_ctl->libraryId(), otherLibrary);
    QCOMPARE(m_ctl->albums()->count(), 0);

    QTRY_COMPARE(modelIds(m_ctl->albums()), QStringList{QStringLiteral("new")});
    QTest::qWait(450); // the first library's reply has certainly landed by now
    // Delivered, not cancelled: without this the assertion below could pass
    // because nothing ever came back.
    QCOMPARE(m_mock->abortedResponseCount(itemsPath()), 0);
    QCOMPARE(modelIds(m_ctl->albums()), QStringList{QStringLiteral("new")});
    QVERIFY(!seen.contains(QStringLiteral("old")));
    QVERIFY(!m_ctl->albumsLane()->loading());
    disconnect(connection);
}

// Ported here when MusicController went (Task 10): tst_music_query's
// createdPlaylistsReappearInTheMusicTab was the only cover for this. A playlist
// made from a track has to turn up in the section whose job is to list it.
// PlaylistController refreshes its own list and cannot know about this one, so
// Application joins the two with a signal rather than asking a page to relay it
// — and the connection made below is the one Application makes.
void MusicBrowseControllerTest::createdPlaylistsReappearInThePlaylistsSection()
{
    // ParentId is what tells the browse section's audio-scoped query apart from
    // PlaylistController's own unscoped walk over the same REST path.
    const Q browseQuery{{"ParentId", kLibrary}, {"IncludeItemTypes", "Playlist"}};
    m_mock->addQueryRoute("GET", itemsPath(), browseQuery, 200,
                          page({playlistJson("pl1", "Road Trip")}));
    m_mock->addRoute("POST", "/Playlists", 200,
                     QByteArrayLiteral("{\"Id\":\"pl2\",\"ItemAddedCount\":1}"));

    PlaylistController playlists(m_client);
    QSignalSpy mutated(&playlists, &PlaylistController::playlistsMutated);
    connect(&playlists, &PlaylistController::playlistsMutated, m_ctl,
            [this] { m_ctl->notePlaylistsMutated(); });

    m_ctl->open(kLibrary, QStringLiteral("playlists"));
    QTRY_COMPARE(modelIds(m_ctl->playlists()), QStringList{QStringLiteral("pl1")});
    const int before = requestsTo(itemsPath(), browseQuery);
    // Fully loaded — one row of a total of one. This is the state a paging
    // retry cannot serve, and the state the section is in most of the time.
    QVERIFY(!m_ctl->playlists()->canLoadMore());

    // On screen: refetch now, or the playlist the user just made is missing from
    // the grid they made it in front of.
    m_mock->addQueryRoute("GET", itemsPath(), browseQuery, 200,
                          page({playlistJson("pl1", "Road Trip"), playlistJson("pl2", "New One")}));
    playlists.create(QStringLiteral("New One"), {QStringLiteral("t1")});
    QTRY_COMPARE(mutated.count(), 1);
    QTRY_COMPARE(requestsTo(itemsPath(), browseQuery), before + 1);
    QTRY_COMPARE(modelIds(m_ctl->playlists()), (QStringList{QStringLiteral("pl1"), QStringLiteral("pl2")}));

    // Not on screen: empty it and let the section's own load-when-visible path
    // pay for the request when it is next looked at — one request instead of one
    // per playlist created while browsing albums.
    m_ctl->setSection(QStringLiteral("albums"));
    QTRY_VERIFY(!m_ctl->albumsLane()->loading());
    const int hidden = requestsTo(itemsPath(), browseQuery);
    playlists.create(QStringLiteral("Another"), {QStringLiteral("t1")});
    QTRY_COMPARE(mutated.count(), 2);
    QTest::qWait(120);
    QCOMPARE(requestsTo(itemsPath(), browseQuery), hidden);
    QCOMPARE(m_ctl->playlists()->count(), 0);

    // …and paid on return, so the rows that come back are the new set.
    m_ctl->setSection(QStringLiteral("playlists"));
    QTRY_COMPARE(requestsTo(itemsPath(), browseQuery), hidden + 1);
    QTRY_COMPARE(modelIds(m_ctl->playlists()), (QStringList{QStringLiteral("pl1"), QStringLiteral("pl2")}));
}

// Ported here when MusicController went (Task 10): tst_music_query's
// sessionResetClearsScopeAndAllowsSameLibraryForNextUser was the only cover for
// resetSessionState. Everything the old identity scoped goes, in-flight replies
// for it land on a dead epoch, and the next account may open the same library id.
void MusicBrowseControllerTest::sessionResetClearsScopeAndAllowsSameLibraryForNextUser()
{
    const auto userB = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    const QString itemsB = QStringLiteral("/Users/%1/Items").arg(userB);
    m_mock->addRoute("GET", "/MusicGenres", 200,
                     page({QJsonObject{{"Id", "g-a"}, {"Name", "A Genre"}, {"AlbumCount", 3}}}));
    m_mock->setRouteDelay("GET", itemsPath(), 350);
    m_mock->setRouteDelay("GET", kAlbumArtistsPath, 350);
    m_mock->setRouteDelay("GET", "/MusicGenres", 350);

    m_ctl->open(kLibrary, QStringLiteral("albums"));
    m_ctl->setSortKey(QStringLiteral("year"));
    m_ctl->setSection(QStringLiteral("songs"));
    m_ctl->setLetter(QStringLiteral("A"));
    m_ctl->setGenres({QStringLiteral("g-a")});
    m_ctl->setDecade(1990);
    m_ctl->setFavouritesOnly(true);
    m_ctl->setUnplayedOnly(true);
    m_ctl->setArtistMode(QStringLiteral("everyone"));
    QVERIFY(m_ctl->songsLane()->loading());
    QCOMPARE(m_ctl->libraryId(), kLibrary);
    QVERIFY(m_ctl->genreOptionsLoading());

    m_ctl->resetSessionState();
    QVERIFY(m_ctl->libraryId().isEmpty());
    QCOMPARE(m_ctl->section(), QStringLiteral("albums"));
    QVERIFY(!m_ctl->songsLane()->loading());
    QVERIFY(!m_ctl->albumsLane()->loading());
    QVERIFY(!m_ctl->filtered());
    QCOMPARE(m_ctl->activeFilterCount(), 0);
    QVERIFY(m_ctl->genreIds().isEmpty());
    QCOMPARE(m_ctl->decade(), 0);
    QVERIFY(!m_ctl->favouritesOnly());
    QVERIFY(!m_ctl->unplayedOnly());
    QCOMPARE(m_ctl->artistMode(), QStringLiteral("albumArtists"));
    QCOMPARE(m_ctl->letter(), QString());
    QCOMPARE(m_ctl->sortKey(), QStringLiteral("name"));
    QVERIFY(!m_ctl->sortDescending());
    QVERIFY(m_ctl->genreOptions().isEmpty());
    QVERIFY(!m_ctl->genreOptionsLoading());
    for (MusicModelBase *model : m_ctl->models())
        QCOMPARE(model->count(), 0);

    // The old account's replies land now. They carry a dead epoch, so no row and
    // no genre option from the previous user may appear.
    QTest::qWait(500);
    for (MusicModelBase *model : m_ctl->models())
        QCOMPARE(model->count(), 0);
    QVERIFY(m_ctl->genreOptions().isEmpty());

    // A section preference alone starts no request: reset restored the state the
    // controller had before anything was opened.
    const int quiet = m_mock->requestCount();
    m_ctl->setSection(QStringLiteral("artists"));
    QTest::qWait(120);
    QCOMPARE(m_mock->requestCount(), quiet);

    // The same library id for the next user must not hit the old early return.
    m_client->setSession(kToken, userB);
    m_mock->setRouteDelay("GET", "/MusicGenres", 0);
    m_mock->addRoute("GET", itemsB, 200, page({albumJson("b1")}, 1));
    m_ctl->open(kLibrary, QStringLiteral("albums"));
    QTRY_COMPARE(modelIds(m_ctl->albums()), QStringList{QStringLiteral("b1")});
    // Limit picks the page request out of the count probe on the same path.
    QCOMPARE(requestsTo(itemsB, Q{{"ParentId", kLibrary}, {"IncludeItemTypes", "MusicAlbum"},
                                 {"Limit", "100"}}),
             1);
}

QTEST_GUILESS_MAIN(MusicBrowseControllerTest)
#include "tst_music_browse_controller.moc"
