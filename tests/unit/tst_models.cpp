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
    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(insertedSpy.count(), 0);
    QCOMPARE(removedSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(resetSpy.count(), 0);
}

QTEST_GUILESS_MAIN(ModelsTest)
#include "tst_models.moc"
