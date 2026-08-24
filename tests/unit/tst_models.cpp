#include <QSignalSpy>
#include <QtTest>

#include "app/models/HomeRailModel.h"
#include "app/models/LibraryListModel.h"
#include "app/models/MediaItemModel.h"

using namespace strmqt;

namespace {

MediaItem makeEpisode()
{
    MediaItem item;
    item.id = QStringLiteral("402014");
    item.name = QStringLiteral("Ozymandias");
    item.type = QStringLiteral("Episode");
    item.seriesName = QStringLiteral("Breaking Bad");
    item.indexNumber = 14;
    item.parentIndexNumber = 5;
    item.runtimeTicks = 28470000000;
    item.playbackPositionTicks = 12000000000;
    item.primaryImageTag = QStringLiteral("tag123");
    return item;
}

} // namespace

class ModelsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void episodeRoles();
    void appendAndReset();
    void setItemsNotifiesOnlyChangedProperties();
    void getReturnsAllRoles();
    void userDataUpdateEmitsDataChanged();
    void userDataUpdateUsesMaintainedIndex();
    void navigationIdentityIndexTracksPlaylistDuplicates();
    void libraryModelRoles();
    void setLibrariesNotifiesOnlyChangedCount();
    void homeRailDescriptorsAreIncremental();
};

void ModelsTest::initTestCase()
{
    setEmbyImageSourceNamespace(QStringLiteral("test-session"));
}

void ModelsTest::episodeRoles()
{
    MediaItemModel model;
    model.setItems({makeEpisode()});

    const QModelIndex index = model.index(0);
    QCOMPARE(model.data(index, MediaItemModel::LabelRole).toString(),
             QStringLiteral("Breaking Bad — S5E14 — Ozymandias"));
    QCOMPARE(model.data(index, MediaItemModel::PosterUrlRole).toString(),
             QStringLiteral("image://emby/test-session/402014/Primary/tag123"));
    QCOMPARE(model.data(index, MediaItemModel::BackdropUrlRole).toString(), QString());
    QVERIFY(model.data(index, MediaItemModel::ResumableRole).toBool());
    const double progress = model.data(index, MediaItemModel::ProgressRole).toDouble();
    QVERIFY(progress > 0.40 && progress < 0.45);
}

void ModelsTest::appendAndReset()
{
    MediaItemModel model;
    QSignalSpy countSpy(&model, &MediaItemModel::countChanged);

    model.setItems({makeEpisode()}, 50);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.totalRecordCount(), 50);
    QCOMPARE(countSpy.count(), 1);

    MediaItem second = makeEpisode();
    second.id = QStringLiteral("402015");
    model.appendItems({second});
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.totalRecordCount(), 50);

    // A page carries its own count, and the library it came from can have grown
    // since the first request. The newer count wins, or canLoadMore() stops at
    // a total the server has already moved past.
    MediaItem third = makeEpisode();
    third.id = QStringLiteral("402016");
    QSignalSpy totalSpy(&model, &MediaItemModel::totalRecordCountChanged);
    model.appendItems({third}, 51);
    QCOMPARE(model.totalRecordCount(), 51);
    QCOMPARE(totalSpy.count(), 1);

    // With no count — and on /Persons and /Genres, which report 0 while
    // returning rows — the total may still never sit below the rows now held:
    // canLoadMore() would read as "nothing more" with a page already appended.
    model.setItems({makeEpisode()}, 0);
    model.appendItems({second});
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.totalRecordCount(), 2);
    model.appendItems({third}, 0);
    QCOMPARE(model.totalRecordCount(), 3);

    model.clear();
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.totalRecordCount(), 0);
}

void ModelsTest::setItemsNotifiesOnlyChangedProperties()
{
    MediaItemModel model;
    QSignalSpy countSpy(&model, &MediaItemModel::countChanged);
    QSignalSpy totalSpy(&model, &MediaItemModel::totalRecordCountChanged);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    model.setItems({makeEpisode()}, 50);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(totalSpy.count(), 1);
    QCOMPARE(resetSpy.count(), 1);

    countSpy.clear();
    totalSpy.clear();
    resetSpy.clear();
    MediaItem replacement = makeEpisode();
    replacement.id = QStringLiteral("replacement");
    model.setItems({replacement}, 50);
    QCOMPARE(resetSpy.count(), 1); // same-cardinality content still replaced
    QCOMPARE(countSpy.count(), 0);
    QCOMPARE(totalSpy.count(), 0);

    model.setItems({replacement, makeEpisode()}, 50);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(totalSpy.count(), 0);

    countSpy.clear();
    model.setItems({replacement, makeEpisode()}, 51);
    QCOMPARE(countSpy.count(), 0);
    QCOMPARE(totalSpy.count(), 1);
}

void ModelsTest::getReturnsAllRoles()
{
    MediaItemModel model;
    model.setItems({makeEpisode()});

    const QVariantMap map = model.get(0);
    QCOMPARE(map.value(QStringLiteral("itemId")).toString(), QStringLiteral("402014"));
    QCOMPARE(map.value(QStringLiteral("seriesName")).toString(), QStringLiteral("Breaking Bad"));
    QVERIFY(map.contains(QStringLiteral("posterUrl")));
    QVERIFY(map.contains(QStringLiteral("overview")));
    QVERIFY(model.get(99).isEmpty());
}

void ModelsTest::navigationIdentityIndexTracksPlaylistDuplicates()
{
    MediaItem first = makeEpisode();
    first.id = QStringLiteral("duplicate-media");
    first.playlistItemId = QStringLiteral("entry-a");
    MediaItem second = first;
    second.playlistItemId = QStringLiteral("entry-b");
    MediaItem ordinary = makeEpisode();
    ordinary.id = QStringLiteral("ordinary-media");

    MediaItemModel model;
    model.setItems({first, second, ordinary});
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("p:entry-a")), 0);
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("p:entry-b")), 1);
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("i:ordinary-media")), 2);
    // Playlist rows are entry-addressed; their duplicated media id must not
    // collapse the two occurrences into one navigation identity.
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("i:duplicate-media")), -1);

    model.setItems({second, ordinary, first});
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("p:entry-a")), 2);
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("p:entry-b")), 0);
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("i:ordinary-media")), 1);

    MediaItem appended = makeEpisode();
    appended.id = QStringLiteral("appended-media");
    model.appendItems({appended});
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("i:appended-media")), 3);
    model.clear();
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("p:entry-a")), -1);
}

void ModelsTest::userDataUpdateEmitsDataChanged()
{
    MediaItemModel model;
    model.setItems({makeEpisode()});
    QSignalSpy spy(&model, &MediaItemModel::dataChanged);

    model.updateUserData(QStringLiteral("402014"), true, true, 420000000, 7);
    QCOMPARE(spy.count(), 1);
    QVERIFY(model.data(model.index(0), MediaItemModel::PlayedRole).toBool());
    QVERIFY(model.data(model.index(0), MediaItemModel::FavoriteRole).toBool());
    QCOMPARE(model.data(model.index(0), MediaItemModel::PositionMsRole).toLongLong(), 42000);
    QCOMPARE(model.data(model.index(0), MediaItemModel::PlayCountRole).toInt(), 7);

    model.updateUserData(QStringLiteral("does-not-exist"), false, false);
    QCOMPARE(spy.count(), 1);
}

void ModelsTest::userDataUpdateUsesMaintainedIndex()
{
    MediaItem first = makeEpisode();
    first.id = QStringLiteral("shared-id");
    MediaItem second = first;
    MediaItem unrelated = first;
    unrelated.id = QStringLiteral("other-id");

    MediaItemModel model;
    model.setItems({first, unrelated});
    model.appendItems({second});
    QSignalSpy spy(&model, &MediaItemModel::dataChanged);

    model.updateUserData(QStringLiteral("shared-id"), true, true, 420000000, 7);
    QCOMPARE(spy.count(), 2);
    for (const int row : {0, 2}) {
        QVERIFY(model.data(model.index(row), MediaItemModel::PlayedRole).toBool());
        QVERIFY(model.data(model.index(row), MediaItemModel::FavoriteRole).toBool());
        QCOMPARE(model.data(model.index(row), MediaItemModel::PlayCountRole).toInt(), 7);
    }
    QVERIFY(!model.data(model.index(1), MediaItemModel::PlayedRole).toBool());

    // A reset must discard old row locations as well as old item storage.
    model.setItems({unrelated});
    spy.clear();
    model.updateUserData(QStringLiteral("shared-id"), false, false);
    QCOMPARE(spy.count(), 0);
}

void ModelsTest::libraryModelRoles()
{
    Library movies;
    movies.id = QStringLiteral("4");
    movies.name = QStringLiteral("Movies");
    movies.collectionType = QStringLiteral("movies");
    movies.primaryImageTag = QStringLiteral("imgtag");

    LibraryListModel model;
    model.setLibraries({movies});
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), LibraryListModel::NameRole).toString(),
             QStringLiteral("Movies"));
    QCOMPARE(model.data(model.index(0), LibraryListModel::ImageUrlRole).toString(),
             QStringLiteral("image://emby/test-session/4/Primary/imgtag"));
    QCOMPARE(model.get(0).value(QStringLiteral("libraryId")).toString(),
             QStringLiteral("4"));
    QVERIFY(model.get(1).isEmpty());
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("i:4")), 0);
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("i:missing")), -1);
}

void ModelsTest::setLibrariesNotifiesOnlyChangedCount()
{
    Library movies;
    movies.id = QStringLiteral("4");
    movies.name = QStringLiteral("Movies");

    Library television = movies;
    television.id = QStringLiteral("5");
    television.name = QStringLiteral("Television");

    LibraryListModel model;
    QSignalSpy countSpy(&model, &LibraryListModel::countChanged);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    model.setLibraries({movies});
    QCOMPARE(countSpy.count(), 1);

    countSpy.clear();
    resetSpy.clear();
    model.setLibraries({television});
    QCOMPARE(resetSpy.count(), 1); // replacement remains visible through modelReset
    QCOMPARE(countSpy.count(), 0);

    model.setLibraries({movies, television});
    QCOMPARE(countSpy.count(), 1);
}

void ModelsTest::homeRailDescriptorsAreIncremental()
{
    QObject resume;
    QObject nextUp;
    HomeRailModel model;
    QList<HomeRailModel::Descriptor> descriptors = {
        {QStringLiteral("resume"), QStringLiteral("Continue Watching"), &resume, false, true, {}},
        {QStringLiteral("next-up"), QStringLiteral("Next Up"), &nextUp, false, true, {}},
    };

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removedSpy(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy movedSpy(&model, &QAbstractItemModel::rowsMoved);
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

    model.setDescriptors(descriptors);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.indexOfKey(QStringLiteral("resume")), 0);
    QCOMPARE(model.indexOfKey(QStringLiteral("next-up")), 1);
    QCOMPARE(model.indexOfKey(QStringLiteral("missing")), -1);
    QCOMPARE(resetSpy.count(), 0);
    QVERIFY(!insertedSpy.isEmpty());

    insertedSpy.clear();
    model.setDescriptors(descriptors);
    QCOMPARE(insertedSpy.count(), 0);
    QCOMPARE(removedSpy.count(), 0);
    QCOMPARE(movedSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(resetSpy.count(), 0);

    descriptors[0].title = QStringLiteral("Keep Watching");
    model.setDescriptors(descriptors);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(insertedSpy.count(), 0);
    QCOMPARE(removedSpy.count(), 0);

    changedSpy.clear();
    descriptors.move(1, 0);
    model.setDescriptors(descriptors);
    QCOMPARE(model.indexOfKey(QStringLiteral("next-up")), 0);
    QCOMPARE(model.indexOfKey(QStringLiteral("resume")), 1);
    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(insertedSpy.count(), 0);
    QCOMPARE(removedSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(resetSpy.count(), 0);
}

QTEST_GUILESS_MAIN(ModelsTest)
#include "tst_models.moc"
