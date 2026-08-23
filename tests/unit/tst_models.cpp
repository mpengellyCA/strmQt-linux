#include <QSignalSpy>
#include <QtTest>

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
    void episodeRoles();
    void appendAndReset();
    void getReturnsAllRoles();
    void userDataUpdateEmitsDataChanged();
    void libraryModelRoles();
};

void ModelsTest::episodeRoles()
{
    MediaItemModel model;
    model.setItems({makeEpisode()});

    const QModelIndex index = model.index(0);
    QCOMPARE(model.data(index, MediaItemModel::LabelRole).toString(),
             QStringLiteral("Breaking Bad — S5E14 — Ozymandias"));
    QCOMPARE(model.data(index, MediaItemModel::PosterUrlRole).toString(),
             QStringLiteral("image://emby/402014/Primary/tag123"));
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

    model.clear();
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.totalRecordCount(), 0);
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

    model.updateUserData(QStringLiteral("402014"), true, true);
    QCOMPARE(spy.count(), 1);
    QVERIFY(model.data(model.index(0), MediaItemModel::PlayedRole).toBool());
    QVERIFY(model.data(model.index(0), MediaItemModel::FavoriteRole).toBool());

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
             QStringLiteral("image://emby/4/Primary/imgtag"));
}

QTEST_GUILESS_MAIN(ModelsTest)
#include "tst_models.moc"
