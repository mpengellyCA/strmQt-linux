#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

#include "app/models/MediaItemModel.h"
#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/ArtistGridModel.h"
#include "app/music/models/PlaylistGridModel.h"

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

QTEST_GUILESS_MAIN(MusicModelsTest)
#include "tst_music_models.moc"
