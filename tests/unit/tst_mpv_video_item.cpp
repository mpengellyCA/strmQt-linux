#include <QtTest>

#include "playback/mpv/MpvPlayer.h"
#include "playback/mpv/MpvVideoItem.h"

#include <QQuickWindow>
#include <QSGNode>
#include <QThread>

#include <atomic>
#include <mpv/client.h>
#include <utility>

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
    void coreInitializesOnFirstLoad();
    void deferredSettingsApplyOnFirstLoad();
    void playerDetachSynchronizesBeforeOwnerDestruction();
    void offThreadFrameNotificationRedrawsTheItem();
};

void MpvVideoItemTest::coreInitializesOnFirstLoad()
{
    MpvPlayer player;
    QVERIFY2(player.handle() == nullptr, "constructing the app initialized libmpv eagerly");

    player.load(QUrl::fromLocalFile(QStringLiteral("/strmqt-test-missing-media")), 0, 1);
    QVERIFY2(player.handle() != nullptr, "the first playback intent did not initialize libmpv");
    player.stop();
}

void MpvVideoItemTest::deferredSettingsApplyOnFirstLoad()
{
    MpvPlayer player;
    player.setVolume(73);
    player.setMuted(true);
    player.setPlaybackSpeed(1.75);
    player.setAudioDelayMs(125);
    player.setSubtitleDelayMs(-250);
    player.setSubtitleStyle(QStringLiteral("Noto Sans"), 135, QStringLiteral("#12abef"), 25,
                            123);
    player.setReplayGain(QStringLiteral("album"));

    QCOMPARE(player.handle(), nullptr);
    QCOMPARE(player.volume(), 73);
    QCOMPARE(player.muted(), true);
    QCOMPARE(player.playbackSpeed(), 1.75);
    QCOMPARE(player.audioDelayMs(), 125);
    QCOMPARE(player.subtitleDelayMs(), -250);

    player.load(QUrl::fromLocalFile(QStringLiteral("/strmqt-test-missing-media")), 0, 1);
    mpv_handle *handle = player.handle();
    QVERIFY(handle != nullptr);

    const auto doubleProperty = [handle](const char *name) {
        double value = 0.0;
        const int result = mpv_get_property(handle, name, MPV_FORMAT_DOUBLE, &value);
        return std::pair{result, value};
    };
    const auto intProperty = [handle](const char *name) {
        int64_t value = 0;
        const int result = mpv_get_property(handle, name, MPV_FORMAT_INT64, &value);
        return std::pair{result, value};
    };
    const auto flagProperty = [handle](const char *name) {
        int value = 0;
        const int result = mpv_get_property(handle, name, MPV_FORMAT_FLAG, &value);
        return std::pair{result, value != 0};
    };
    const auto stringProperty = [handle](const char *name) {
        char *raw = mpv_get_property_string(handle, name);
        const QString value = QString::fromUtf8(raw ? raw : "");
        mpv_free(raw);
        return value;
    };

    const auto volume = doubleProperty("volume");
    QCOMPARE(volume.first, 0);
    QCOMPARE(volume.second, 73.0);
    const auto mute = flagProperty("mute");
    QCOMPARE(mute.first, 0);
    QCOMPARE(mute.second, true);
    const auto speed = doubleProperty("speed");
    QCOMPARE(speed.first, 0);
    QCOMPARE(speed.second, 1.75);
    const auto audioDelay = doubleProperty("audio-delay");
    QCOMPARE(audioDelay.first, 0);
    QCOMPARE(audioDelay.second, 0.125);
    const auto subtitleDelay = doubleProperty("sub-delay");
    QCOMPARE(subtitleDelay.first, 0);
    QCOMPARE(subtitleDelay.second, -0.25);
    QCOMPARE(stringProperty("sub-font"), QStringLiteral("Noto Sans"));
    const auto subtitleScale = doubleProperty("sub-scale");
    QCOMPARE(subtitleScale.first, 0);
    QVERIFY(qAbs(subtitleScale.second - 1.35) < 0.000001);
    QCOMPARE(stringProperty("sub-color"), QStringLiteral("#FF12ABEF"));
    QCOMPARE(stringProperty("sub-back-color"), QStringLiteral("#3F000000"));
    const auto subtitleBorder = doubleProperty("sub-border-size");
    QCOMPARE(subtitleBorder.first, 0);
    QCOMPARE(subtitleBorder.second, 3.0);
    const auto subtitlePosition = intProperty("sub-pos");
    QCOMPARE(subtitlePosition.first, 0);
    QCOMPARE(subtitlePosition.second, int64_t{123});
    QCOMPARE(stringProperty("replaygain"), QStringLiteral("album"));
    player.stop();
}

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
    player->load(QUrl::fromLocalFile(QStringLiteral("/strmqt-test-missing-media")), 0, 1);
    QVERIFY2(player->handle() != nullptr, "detach test did not exercise a live mpv core");
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
