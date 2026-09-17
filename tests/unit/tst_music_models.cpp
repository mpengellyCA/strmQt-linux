#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

#include "app/models/MediaItemModel.h"
#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/ArtistGridModel.h"
#include "app/music/models/GenreBinModel.h"
#include "app/music/models/PlaylistGridModel.h"
#include "app/music/models/StationModel.h"
#include "app/music/models/TrackListModel.h"

using namespace strmqt;
using namespace strmqt::music;

namespace {

Album album(const QString &id, int year = 1973, ReleaseType type = ReleaseType::Album)
{
    Album a;
    a.id = id;
    a.title = QStringLiteral("Title ") + id;
    a.albumArtists = {{QStringLiteral("ar1"), QStringLiteral("Pink Floyd")}};
    a.year = year;
    a.trackCount = 9;
    a.runtimeMs = 43 * 60'000;
    a.releaseType = type;
    a.coverRef = {id, QStringLiteral("Primary"), QStringLiteral("tag")};
    return a;
}

QVariant role(const QAbstractItemModel &model, int row, const QByteArray &name)
{
    const auto names = model.roleNames();
    return model.data(model.index(row, 0), names.key(name));
}

Track track(const QString &id, int number, const QString &playlistItemId = QString())
{
    Track t;
    t.id = id;
    t.title = QStringLiteral("Get Lucky (feat. Pharrell Williams)");
    t.displayTitle = QStringLiteral("Get Lucky");
    t.featured = {{QString(), QStringLiteral("Pharrell Williams")}, {QStringLiteral("nr"), QStringLiteral("Nile Rodgers")}};
    t.artists = {{QStringLiteral("dp"), QStringLiteral("Daft Punk")}};
    t.albumArtists = t.artists;
    t.albumId = QStringLiteral("ram");
    t.albumTitle = QStringLiteral("Random Access Memories");
    t.discNumber = 1;
    t.trackNumber = number;
    t.runtimeMs = 369'000;
    t.format.badge = QStringLiteral("FLAC 24/88.2");
    t.format.isHiRes = true;
    t.coverRef = {QStringLiteral("ram"), QStringLiteral("Primary"), QStringLiteral("c")};
    t.playlistItemId = playlistItemId;
    return t;
}

} // namespace

class MusicModelsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { setEmbyImageSourceNamespace(QStringLiteral("t")); }
    void albumRolesAndGet();
    void pagingAndIdentity();
    void albumUserDataPatchesInPlace();
    void artistAndPlaylistRoles();
    void mapForItemMatchesMediaModelGet();
    void trackRolesServeMediaAndMusicNames();
    void trackUserDataPatchesBothViews();
    void playlistEntriesUsePlaylistIdentity();
    void genreBinsAndStations();
};

void MusicModelsTest::albumRolesAndGet()
{
    AlbumGridModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setItems({album("a1"), album("a2", 0, ReleaseType::EP)});
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(role(model, 0, "title").toString(), QStringLiteral("Title a1"));
    QCOMPARE(role(model, 0, "artist").toString(), QStringLiteral("Pink Floyd"));
    QCOMPARE(role(model, 0, "subtitle").toString(), QStringLiteral("Pink Floyd · 1973"));
    QCOMPARE(role(model, 1, "subtitle").toString(), QStringLiteral("Pink Floyd")); // no year
    QCOMPARE(role(model, 0, "releaseBadge").toString(), QString());
    QCOMPARE(role(model, 1, "releaseBadge").toString(), QStringLiteral("EP"));
    QCOMPARE(role(model, 0, "coverUrl").toString(), QStringLiteral("image://emby/t/a1/Primary/tag"));
    QCOMPARE(role(model, 0, "posterUrl"), role(model, 0, "coverUrl"));
    QCOMPARE(role(model, 0, "durationText").toString(), QStringLiteral("43 min"));
    QCOMPARE(role(model, 0, "type").toString(), QStringLiteral("MusicAlbum"));

    const QVariantMap map = model.get(0);
    QCOMPARE(map.value("itemId").toString(), QStringLiteral("a1"));
    QCOMPARE(map.value("type").toString(), QStringLiteral("MusicAlbum"));
    QCOMPARE(map.value("albumArtist").toString(), QStringLiteral("Pink Floyd")); // media role
    QCOMPARE(map.value("releaseType").toString(), QStringLiteral("Album"));     // music role
    QVERIFY(model.get(7).isEmpty());
}

void MusicModelsTest::pagingAndIdentity()
{
    AlbumGridModel model;
    QSignalSpy count(&model, &MusicModelBase::countChanged);
    QSignalSpy total(&model, &MusicModelBase::totalRecordCountChanged);
    model.setItems({album("a1"), album("a2")}, 5);
    QVERIFY(model.canLoadMore());
    QCOMPARE(model.totalRecordCount(), 5);
    model.appendItems({album("a3"), album("a1")}, 4); // under-reported: floored at 4 rows
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.totalRecordCount(), 4);
    QVERIFY(!model.canLoadMore());
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("i:a3")), 2);
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("i:a1")), 0); // first occurrence
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("i:zz")), -1);
    QCOMPARE(model.idAt(1), QStringLiteral("a2"));
    QVERIFY(count.count() >= 2);
    QVERIFY(total.count() >= 2);
    model.clear();
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.totalRecordCount(), 0);
}

void MusicModelsTest::albumUserDataPatchesInPlace()
{
    AlbumGridModel model;
    model.setItems({album("a1"), album("a2"), album("a1")});
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    UserDataPatch patch;
    patch.favourite = true;
    patch.playCount = 3;
    model.applyUserData(QStringLiteral("a1"), patch);
    QCOMPARE(changed.count(), 2); // both rows holding a1
    QVERIFY(role(model, 0, "favourite").toBool());
    QVERIFY(role(model, 2, "favorite").toBool());
    QCOMPARE(role(model, 0, "playCount").toInt(), 3);
    model.applyUserData(QStringLiteral("nope"), patch);
    QCOMPARE(changed.count(), 2);
}

void MusicModelsTest::artistAndPlaylistRoles()
{
    ArtistGridModel artists;
    Artist artist;
    artist.id = QStringLiteral("ar1");
    artist.name = QStringLiteral("Björk");
    artist.albumCount = 1;
    artist.coverRef = {QStringLiteral("ar1"), QStringLiteral("Primary"), QStringLiteral("p")};
    artists.setItems({artist});
    QCOMPARE(role(artists, 0, "subtitle").toString(), QStringLiteral("1 record"));
    QCOMPARE(role(artists, 0, "recordCount").toInt(), 1);
    QCOMPARE(artists.get(0).value("type").toString(), QStringLiteral("MusicArtist"));

    PlaylistGridModel playlists;
    Playlist playlist;
    playlist.id = QStringLiteral("pl1");
    playlist.name = QStringLiteral("Road trip");
    playlist.trackCount = 31;
    playlist.runtimeMs = 120 * 60'000;
    playlists.setItems({playlist});
    QCOMPARE(role(playlists, 0, "subtitle").toString(), QStringLiteral("31 tracks · 2 h"));
    QCOMPARE(playlists.get(0).value("type").toString(), QStringLiteral("Playlist"));
    QCOMPARE(playlists.get(0).value("childCount").toInt(), 31);
}

void MusicModelsTest::mapForItemMatchesMediaModelGet()
{
    MediaItem item;
    item.id = QStringLiteral("x");
    item.name = QStringLiteral("X");
    item.type = QStringLiteral("Audio");
    item.runtimeTicks = 1000 * kTicksPerMs;
    MediaItemModel model;
    model.setItems({item});
    QCOMPARE(MediaItemModel::mapForItem(item), model.get(0));
}

void MusicModelsTest::trackRolesServeMediaAndMusicNames()
{
    TrackListModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setItems({track("t1", 8)});
    QCOMPARE(role(model, 0, "name").toString(), QStringLiteral("Get Lucky (feat. Pharrell Williams)"));
    QCOMPARE(role(model, 0, "displayTitle").toString(), QStringLiteral("Get Lucky"));
    QCOMPARE(role(model, 0, "featuredText").toString(), QStringLiteral("feat. Pharrell Williams & Nile Rodgers"));
    QCOMPARE(role(model, 0, "artistText").toString(), QStringLiteral("Daft Punk"));
    QCOMPARE(role(model, 0, "runtimeMs").toLongLong(), Q_INT64_C(369000)); // media role
    QCOMPARE(role(model, 0, "indexNumber").toInt(), 8);                      // media role
    QCOMPARE(role(model, 0, "trackNumber").toInt(), 8);
    QCOMPARE(role(model, 0, "durationText").toString(), QStringLiteral("6:09"));
    QCOMPARE(role(model, 0, "formatBadge").toString(), QStringLiteral("FLAC 24/88.2"));
    QVERIFY(role(model, 0, "isHiRes").toBool());
    QCOMPARE(role(model, 0, "coverUrl").toString(), QStringLiteral("image://emby/t/ram/Primary/c"));
    QCOMPARE(role(model, 0, "posterUrl").toString(), role(model, 0, "coverUrl").toString());

    const QVariantList maps = model.mediaMaps();
    QCOMPARE(maps.size(), 1);
    QCOMPARE(maps.first().toMap().value("type").toString(), QStringLiteral("Audio"));
    QCOMPARE(maps.first().toMap().value("albumId").toString(), QStringLiteral("ram"));
}

void MusicModelsTest::trackUserDataPatchesBothViews()
{
    TrackListModel model;
    model.setItems({track("t1", 1), track("t2", 2)});
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    UserDataPatch patch;
    patch.favourite = true;
    patch.played = true;
    patch.playCount = 5;
    model.applyUserData(QStringLiteral("t2"), patch);
    QCOMPARE(changed.count(), 1);
    QVERIFY(role(model, 1, "favorite").toBool());   // media role
    QVERIFY(role(model, 1, "favourite").toBool());  // music role
    QVERIFY(role(model, 1, "played").toBool());
    QCOMPARE(role(model, 1, "playCount").toInt(), 5);
    QVERIFY(model.get(1).value("favorite").toBool());
    QVERIFY(model.at(1).favourite);
}

void MusicModelsTest::playlistEntriesUsePlaylistIdentity()
{
    TrackListModel model;
    model.setItems({track("same", 1, "e1"), track("same", 2, "e2")});
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("p:e2")), 1);
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("i:same")), -1);
    QCOMPARE(role(model, 1, "playlistItemId").toString(), QStringLiteral("e2"));
}

void MusicModelsTest::genreBinsAndStations()
{
    GenreBinModel genres;
    GenreBin bin;
    bin.id = QStringLiteral("g1");
    bin.name = QStringLiteral("Jazz");
    bin.recordCount = 42;
    bin.covers = {{QStringLiteral("a"), QStringLiteral("Primary"), QStringLiteral("1")},
                  {QStringLiteral("b"), QStringLiteral("Primary"), QStringLiteral("2")}};
    genres.setItems({bin}, 120);
    QCOMPARE(role(genres, 0, "subtitle").toString(), QStringLiteral("42 records"));
    QCOMPARE(role(genres, 0, "covers").toStringList().size(), 2);
    QCOMPARE(role(genres, 0, "coverUrl").toString(), QStringLiteral("image://emby/t/a/Primary/1"));
    QCOMPARE(genres.totalRecordCount(), 120);
    QCOMPARE(genres.get(0).value("type").toString(), QStringLiteral("MusicGenre"));

    StationModel stations;
    Station more{StationKind::MoreLike, QStringLiteral("More like Björk"), QStringLiteral("ar1"), {}};
    stations.setStations({Station{StationKind::HeavyRotation, QStringLiteral("Heavy rotation"), {}, {}}, more});
    QCOMPARE(stations.rowCount(), 2);
    QCOMPARE(role(stations, 1, "itemId").toString(), QStringLiteral("moreLike"));
    QCOMPARE(role(stations, 1, "label").toString(), QStringLiteral("More like Björk"));
    QCOMPARE(stations.indexOfNavigationIdentity(QStringLiteral("i:moreLike")), 1);
    QCOMPARE(stations.stationFor(QStringLiteral("moreLike"))->seedId, QStringLiteral("ar1"));
    QVERIFY(!stations.stationFor(QStringLiteral("bogus")).has_value());
    QVERIFY(StationModel::kindFromKey(QStringLiteral("deepCuts")) == StationKind::DeepCuts);
    QCOMPARE(stations.get(0).value("label").toString(), QStringLiteral("Heavy rotation"));
}

QTEST_GUILESS_MAIN(MusicModelsTest)
#include "tst_music_models.moc"
