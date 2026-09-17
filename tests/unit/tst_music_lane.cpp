#include <QSignalSpy>
#include <QtTest>

#include "app/controllers/music/MusicLane.h"
#include "app/music/models/AlbumGridModel.h"

using namespace strmqt::music;

namespace {

QList<Album> albums(int count)
{
    QList<Album> list;
    for (int i = 0; i < count; ++i) {
        Album album;
        album.id = QStringLiteral("al%1").arg(i);
        album.title = QStringLiteral("Album %1").arg(i);
        list.append(album);
    }
    return list;
}

} // namespace

class MusicLaneTest : public QObject
{
    Q_OBJECT

private slots:
    void startsIdleAndEmpty();
    void beginSucceedFailFollowTheGeneration();
    void emptyTracksTheModel();
    void refreshKeepsContentOnFailure();
    void refreshOfAFailedLaneShowsLoading();
    void retryAsksTheOwner();
    void resetForgetsEverything();
};

void MusicLaneTest::startsIdleAndEmpty()
{
    AlbumGridModel model;
    MusicLane lane(&model);
    QCOMPARE(lane.model(), &model);
    QCOMPARE(lane.typedModel(), &model);
    QVERIFY(!lane.loading());
    QVERIFY(lane.error().isEmpty());
    QVERIFY(!lane.ready());
    QVERIFY(lane.empty());
}

void MusicLaneTest::beginSucceedFailFollowTheGeneration()
{
    AlbumGridModel model;
    MusicLane lane(&model);
    QSignalSpy changed(&lane, &MusicLane::stateChanged);

    const quint64 first = lane.begin();
    QVERIFY(lane.loading());
    QVERIFY(!lane.empty());
    QVERIFY(lane.isCurrent(first));
    QVERIFY(changed.count() >= 1);

    const quint64 second = lane.begin();
    QVERIFY(second > first);
    QVERIFY(!lane.isCurrent(first));

    lane.fail(first, QStringLiteral("stale")); // superseded: ignored
    QVERIFY(lane.loading());
    QVERIFY(lane.error().isEmpty());

    lane.fail(second, QString());
    QVERIFY(!lane.loading());
    QCOMPARE(lane.error(), QStringLiteral("Couldn't load"));
    QVERIFY(!lane.empty()); // an error is shown, not hidden
    QVERIFY(!lane.ready());

    const quint64 third = lane.begin();
    QVERIFY(lane.error().isEmpty());
    model.setItems(albums(2));
    lane.succeed(second); // superseded: ignored
    QVERIFY(lane.loading());
    lane.succeed(third);
    QVERIFY(!lane.loading());
    QVERIFY(lane.ready());
    QVERIFY(!lane.empty());
}

void MusicLaneTest::emptyTracksTheModel()
{
    AlbumGridModel model;
    MusicLane lane(&model);
    lane.succeed(lane.begin());
    QVERIFY(lane.ready());
    QVERIFY(lane.empty());

    QSignalSpy changed(&lane, &MusicLane::stateChanged);
    model.setItems(albums(1));
    QVERIFY(changed.count() >= 1);
    QVERIFY(!lane.empty());
    model.clear();
    QVERIFY(lane.empty());
}

void MusicLaneTest::refreshKeepsContentOnFailure()
{
    AlbumGridModel model;
    MusicLane lane(&model);
    model.setItems(albums(3));
    lane.succeed(lane.begin());

    const quint64 refresh = lane.beginRefresh();
    QVERIFY(!lane.loading()); // quiet: the shelf stays on screen
    QVERIFY(lane.isCurrent(refresh));
    lane.fail(refresh, QStringLiteral("HTTP 500"));
    QVERIFY(lane.error().isEmpty());
    QVERIFY(!lane.loading());
    QVERIFY(lane.ready());
    QCOMPARE(model.count(), 3);

    lane.succeed(lane.beginRefresh());
    QVERIFY(lane.ready());
}

void MusicLaneTest::refreshOfAFailedLaneShowsLoading()
{
    AlbumGridModel model;
    MusicLane lane(&model);
    lane.fail(lane.begin(), QStringLiteral("HTTP 500"));
    const quint64 refresh = lane.beginRefresh();
    QVERIFY(lane.loading());
    QVERIFY(lane.error().isEmpty());
    lane.fail(refresh, QStringLiteral("HTTP 502"));
    QCOMPARE(lane.error(), QStringLiteral("HTTP 502"));

    MusicLane fresh(&model);
    fresh.beginRefresh();
    QVERIFY(fresh.loading()); // never loaded: a refresh is a first load
}

void MusicLaneTest::retryAsksTheOwner()
{
    AlbumGridModel model;
    MusicLane lane(&model);
    QSignalSpy retry(&lane, &MusicLane::retryRequested);
    QMetaObject::invokeMethod(&lane, "retry");
    QCOMPARE(retry.count(), 1);
}

void MusicLaneTest::resetForgetsEverything()
{
    AlbumGridModel model;
    MusicLane lane(&model);
    const quint64 generation = lane.begin();
    lane.reset();
    QVERIFY(!lane.isCurrent(generation));
    QVERIFY(!lane.loading());
    QVERIFY(!lane.ready());
    QVERIFY(lane.error().isEmpty());
    lane.succeed(generation);
    QVERIFY(!lane.ready());
}

QTEST_GUILESS_MAIN(MusicLaneTest)
#include "tst_music_lane.moc"
