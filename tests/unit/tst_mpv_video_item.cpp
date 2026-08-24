#include <QtTest>

#include "playback/mpv/MpvPlayer.h"
#include "playback/mpv/MpvVideoItem.h"

#include <QQuickWindow>
#include <QSGNode>
#include <QThread>

#include <atomic>

using namespace strmqt;

namespace {

// updatePaintNode() runs only for an item the scene graph considers dirty, so
// counting it is the difference between "a frame was drawn" and "this item was
// asked to redraw". QQuickWindow::update() alone does the former; only an item
// update does the latter, and only the latter re-renders the mpv framebuffer.
class CountingVideoItem : public MpvVideoItem
{
public:
    using MpvVideoItem::MpvVideoItem;

    std::atomic<int> paintNodeUpdates{0};

protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *data) override
    {
        ++paintNodeUpdates;
        return MpvVideoItem::updatePaintNode(node, data);
    }
};

} // namespace

class MpvVideoItemTest : public QObject
{
    Q_OBJECT

private slots:
    void playerDetachSynchronizesBeforeOwnerDestruction();
    void offThreadFrameNotificationRedrawsTheItem();
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

// mpv notifies about new frames from its own thread, and in simple-control mode
// that notification is the ONLY thing that asks for another pass over the
// framebuffer. Scheduling a window frame instead leaves the node's render
// pending flag clear, so the picture freezes on its last drawn frame while
// audio, timeline and OSD keep running.
void MpvVideoItemTest::offThreadFrameNotificationRedrawsTheItem()
{
    QQuickWindow window;
    window.resize(320, 180);
    CountingVideoItem item(window.contentItem());
    item.setSize(window.size());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(item.paintNodeUpdates > 0);

    // Let the scene settle so the baseline is not a frame that was already in
    // flight for another reason.
    QTest::qWait(50);
    const int baseline = item.paintNodeUpdates;

    // A QThread object belongs to the thread that created it, so the request
    // has to be raised through a worker that actually lives on the other side.
    QThread notifier;
    QObject worker;
    worker.moveToThread(&notifier);
    notifier.start();
    QVERIFY(notifier.isRunning());
    QThread *raisedOn = nullptr;
    QMetaObject::invokeMethod(
        &worker,
        [&] {
            raisedOn = QThread::currentThread();
            item.requestRedrawForTests();
        },
        Qt::BlockingQueuedConnection);
    QCOMPARE(raisedOn, &notifier);

    QTRY_VERIFY(item.paintNodeUpdates > baseline);
    notifier.quit();
    QVERIFY(notifier.wait(5000));
}

QTEST_MAIN(MpvVideoItemTest)
#include "tst_mpv_video_item.moc"
