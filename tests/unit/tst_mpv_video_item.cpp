#include <QtTest>

#include "playback/mpv/MpvPlayer.h"
#include "playback/mpv/MpvVideoItem.h"

#include <QQuickWindow>

using namespace strmqt;

class MpvVideoItemTest : public QObject
{
    Q_OBJECT

private slots:
    void playerDetachSynchronizesBeforeOwnerDestruction();
};

void MpvVideoItemTest::playerDetachSynchronizesBeforeOwnerDestruction()
{
    QQuickWindow window;
    window.resize(320, 180);
    MpvVideoItem item(window.contentItem());
    item.setSize(window.size());
    auto *player = new MpvPlayer;
    QSignalSpy playerSpy(&item, &MpvVideoItem::playerChanged);
    QSignalSpy synchronizedSpy(&window, &QQuickWindow::afterSynchronizing);
    item.setPlayerObject(player);
    QCOMPARE(item.playerObject(), player);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(!synchronizedSpy.isEmpty());

    const qsizetype priorSynchronizations = synchronizedSpy.size();
    item.setPlayerObject(nullptr);
    QCOMPARE(item.playerObject(), nullptr);
    QCOMPARE(playerSpy.count(), 2);
    // The scene graph's synchronization pass retires the renderer's copied mpv
    // handle before the application tears down the owning backend.
    window.update();
    QTRY_VERIFY(synchronizedSpy.size() > priorSynchronizations);
    delete player;
}

QTEST_MAIN(MpvVideoItemTest)
#include "tst_mpv_video_item.moc"
