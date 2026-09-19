#include <QtTest>

#include "playback/mpv/MpvPlayer.h"
#include "playback/mpv/MpvVideoItem.h"

#include <QOpenGLContext>
#include <QProcess>
#include <QQuickWindow>
#include <QSGNode>
#include <QTcpServer>
#include <QThread>

#include <atomic>
#include <memory>
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif
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

const QUrl kMissingMedia = QUrl::fromLocalFile(QStringLiteral("/strmqt-test-missing-media"));

QString stringProperty(mpv_handle *handle, const char *name)
{
    char *raw = mpv_get_property_string(handle, name);
    const QString result = QString::fromUtf8(raw ? raw : "");
    mpv_free(raw);
    return result;
}

// Loads a file that cannot open and waits for mpv to say so, which leaves the
// core idle exactly as an ended or failed playback does.
bool failLoad(MpvPlayer &player, PlayerBackend::LoadId loadId)
{
    QSignalSpy errors(&player, &PlayerBackend::errorOccurred);
    player.load(kMissingMedia, 0, loadId);
    return QTest::qWaitFor([&] {
        for (const QList<QVariant> &error : errors)
            if (error.at(1).value<PlayerBackend::LoadId>() == loadId)
                return true;
        return false;
    });
}

} // namespace

class MpvVideoItemTest : public QObject
{
    Q_OBJECT

private slots:
    void coreInitializesOnFirstLoad();
    void deferredSettingsApplyOnFirstLoad();
    void deferredSettingsNormalizeBeforeFirstLoad();
    void bundledScriptsAreNotLoaded();
    void playerDetachSynchronizesBeforeOwnerDestruction();
    void offThreadFrameNotificationRedrawsTheItem();
    void idleCoreIsReleased();
    void coreInUseIsKept();
    void recreatedCoreKeepsOptionsSettingsAndEvents();
    void releaseWaitsForEveryHolder();
    void releaseWaitsForTheRenderer();
    void hiddenPlaneStillLetsGo();
    void destroyingAHeldPlayerIsLoud();
    void destructionEmitsNoSettingChanges();
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

void MpvVideoItemTest::deferredSettingsNormalizeBeforeFirstLoad()
{
    MpvPlayer player;
    player.setVolume(999);
    player.setPlaybackSpeed(99.0);
    player.setSubtitleStyle({}, 999, QStringLiteral("not-a-color"), 999, 999);
    player.setReplayGain(QStringLiteral("unknown"));
    QCOMPARE(player.volume(), 130);
    QCOMPARE(player.playbackSpeed(), 4.0);

    player.load(QUrl::fromLocalFile(QStringLiteral("/strmqt-test-missing-media")), 0, 1);
    mpv_handle *handle = player.handle();
    QVERIFY(handle != nullptr);

    double value = 0.0;
    QCOMPARE(mpv_get_property(handle, "volume", MPV_FORMAT_DOUBLE, &value), 0);
    QCOMPARE(value, 130.0);
    QCOMPARE(mpv_get_property(handle, "speed", MPV_FORMAT_DOUBLE, &value), 0);
    QCOMPARE(value, 4.0);
    QCOMPARE(mpv_get_property(handle, "sub-scale", MPV_FORMAT_DOUBLE, &value), 0);
    QCOMPARE(value, 3.0);
    QCOMPARE(mpv_get_property(handle, "sub-border-size", MPV_FORMAT_DOUBLE, &value), 0);
    QCOMPARE(value, 0.0);

    int64_t position = 0;
    QCOMPARE(mpv_get_property(handle, "sub-pos", MPV_FORMAT_INT64, &position), 0);
    QCOMPARE(position, int64_t{150});
    const auto stringProperty = [handle](const char *name) {
        char *raw = mpv_get_property_string(handle, name);
        const QString result = QString::fromUtf8(raw ? raw : "");
        mpv_free(raw);
        return result;
    };
    QCOMPARE(stringProperty("sub-color"), QStringLiteral("#FFFFFFFF"));
    QCOMPARE(stringProperty("sub-back-color"), QStringLiteral("#FF000000"));
    QCOMPARE(stringProperty("replaygain"), QStringLiteral("no"));
    player.stop();
}

// mpv's bundled Lua scripts (ytdl_hook, stats, select, positioning, ...) each
// start a VM on a thread of their own at mpv_initialize. Nothing in an embedded
// Emby player drives them, so the core must come up without any.
void MpvVideoItemTest::bundledScriptsAreNotLoaded()
{
    MpvPlayer player;
    player.load(QUrl::fromLocalFile(QStringLiteral("/strmqt-test-missing-media")), 0, 1);
    mpv_handle *handle = player.handle();
    QVERIFY(handle != nullptr);

    const auto stringProperty = [handle](const char *name) {
        char *raw = mpv_get_property_string(handle, name);
        const QString result = QString::fromUtf8(raw ? raw : "");
        mpv_free(raw);
        return result;
    };
    QCOMPARE(stringProperty("load-scripts"), QStringLiteral("no"));
    QCOMPARE(stringProperty("ytdl"), QStringLiteral("no"));

#ifdef Q_OS_LINUX
    // The observable cost: mpv names each script's thread "lua/<script>".
    QStringList luaThreads;
    const QDir tasks(QStringLiteral("/proc/self/task"));
    for (const QString &tid : tasks.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile comm(tasks.filePath(tid + QStringLiteral("/comm")));
        if (comm.open(QIODevice::ReadOnly)) {
            const QString name = QString::fromUtf8(comm.readAll()).trimmed();
            if (name.startsWith(QLatin1String("lua/")))
                luaThreads << name;
        }
    }
    QVERIFY2(luaThreads.isEmpty(), qPrintable(luaThreads.join(QLatin1Char(','))));
#endif
    player.stop();
}

void MpvVideoItemTest::playerDetachSynchronizesBeforeOwnerDestruction()
{
    // Owned from before the window, so a failed check that returns early still
    // destroys the renderer first; the test itself destroys it at the end.
    auto owned = std::make_unique<MpvPlayer>();
    MpvPlayer *player = owned.get();
    QQuickWindow window;
    window.resize(320, 180);
    MpvVideoItem item(window.contentItem());
    item.setSize(window.size());
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
    owned.reset();
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

// mpv 0.41 keeps every demuxer packet it ever cached until its core is
// destroyed, so a stopped, ended or failed player gives its core back.
void MpvVideoItemTest::idleCoreIsReleased()
{
    MpvPlayer player;
    player.setCoreIdleReleaseDelayForTests(20);
    QVERIFY(failLoad(player, 1));
    QTRY_VERIFY2(!player.hasCore(), "a failed playback left its mpv core alive");
    QCOMPARE(player.handle(), nullptr);

    player.load(kMissingMedia, 0, 2);
    QVERIFY(player.hasCore());
    player.stop();
    QTRY_VERIFY2(!player.hasCore(), "a stopped player kept its mpv core");
}

// A pending release must not take the core from a load that follows it.
void MpvVideoItemTest::coreInUseIsKept()
{
    // Accepts and never answers, so the next load stays in Loading.
    QTcpServer silent;
    QVERIFY(silent.listen(QHostAddress::LocalHost));

    MpvPlayer player;
    player.setCoreIdleReleaseDelayForTests(20);
    QVERIFY(failLoad(player, 1));
    mpv_handle *core = player.handle();
    QVERIFY(core != nullptr);

    player.load(QUrl(QStringLiteral("http://127.0.0.1:%1/stalled.mkv").arg(silent.serverPort())),
                0, 2);
    QTest::qWait(200);
    QCOMPARE(player.state(), PlayerBackend::State::Loading);
    QVERIFY2(player.hasCore(), "the core was destroyed under a load in progress");
    QCOMPARE(player.handle(), core);
    player.stop();
    QTRY_VERIFY(!player.hasCore());
}

// The replacement core is the same player to everything above it: same options,
// the settings the old core ended with, and a working event channel.
void MpvVideoItemTest::recreatedCoreKeepsOptionsSettingsAndEvents()
{
    MpvPlayer player;
    player.setCoreIdleReleaseDelayForTests(20);
    QSignalSpy handleChanges(&player, &MpvPlayer::renderHandleChanged);
    QVERIFY(failLoad(player, 1));
    // Changed on the live core after stop(): mpv keeps the value, but no
    // property event for it reaches the player any more.
    player.stop();
    player.setPlaybackSpeed(1.5);
    player.setVolume(61);
    QTRY_VERIFY(!player.hasCore());
    player.setMuted(true);

    QVERIFY2(failLoad(player, 2), "the recreated core does not deliver events");
    mpv_handle *handle = player.handle();
    QVERIFY(handle != nullptr);
    QCOMPARE(stringProperty(handle, "load-scripts"), QStringLiteral("no"));
    QCOMPARE(stringProperty(handle, "ytdl"), QStringLiteral("no"));
    QCOMPARE(stringProperty(handle, "vo"), QStringLiteral("libmpv"));
    QCOMPARE(stringProperty(handle, "hwdec"), QStringLiteral("auto-safe"));
    QCOMPARE(stringProperty(handle, "demuxer-max-bytes"), QStringLiteral("268435456"));
    QCOMPARE(stringProperty(handle, "keep-open"), QStringLiteral("no"));
    QCOMPARE(stringProperty(handle, "speed"), QStringLiteral("1.500000"));
    QCOMPARE(stringProperty(handle, "volume"), QStringLiteral("61.000000"));
    QCOMPARE(stringProperty(handle, "mute"), QStringLiteral("yes"));
    QCOMPARE(player.playbackSpeed(), 1.5);
    // Created, released, created again: every video item was told each time.
    QVERIFY(handleChanges.size() >= 3);
    player.stop();
}

// mpv requires every render context gone before its core is destroyed. The
// player half of that contract, with the test standing in for a renderer.
void MpvVideoItemTest::releaseWaitsForEveryHolder()
{
    MpvPlayer player;
    player.setCoreIdleReleaseDelayForTests(20);
    QSignalSpy handleChanges(&player, &MpvPlayer::renderHandleChanged);
    player.load(kMissingMedia, 0, 1);
    QVERIFY(player.handle() != nullptr);
    const std::shared_ptr<mpvdetail::RenderLink> link = player.renderLink();
    link->acquire();

    QTRY_COMPARE(player.handle(), nullptr);
    const qsizetype releaseNotices = handleChanges.size();
    QTest::qWait(100);
    QVERIFY2(player.hasCore(), "the core was destroyed while a renderer still held it");

    link->release();
    QTRY_VERIFY2(!player.hasCore(), "the last holder let go but the core stayed");
    QCOMPARE(handleChanges.size(), releaseNotices);

    // A load while the release still waits keeps the core and hands it back.
    player.load(kMissingMedia, 0, 2);
    mpv_handle *core = player.handle();
    QVERIFY(core != nullptr);
    link->acquire();
    QTRY_COMPARE(player.handle(), nullptr);
    player.load(kMissingMedia, 0, 3);
    QCOMPARE(player.handle(), core);
    link->release();
    player.stop();
    QTRY_VERIFY(!player.hasCore());
}

// The same contract through the real renderer, which only exists when the
// scene graph renders with OpenGL: run it on a GL platform (see the commit).
void MpvVideoItemTest::releaseWaitsForTheRenderer()
{
    // Declared first, so destroyed last: a failed check returns early, and the
    // window has to take its renderer down before the player goes.
    MpvPlayer player;
    player.setCoreIdleReleaseDelayForTests(20);
    QQuickWindow window;
    window.resize(320, 180);
    MpvVideoItem item(window.contentItem());
    item.setSize(window.size());
    item.setPlayerObject(&player);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    if (window.rendererInterface()->graphicsApi() != QSGRendererInterface::OpenGL)
        QSKIP("MpvVideoItem renders only with the OpenGL scene graph");

    // Long enough to be seen held: this load stays open until stop().
    QTcpServer silent;
    QVERIFY(silent.listen(QHostAddress::LocalHost));
    player.load(QUrl(QStringLiteral("http://127.0.0.1:%1/stalled.mkv").arg(silent.serverPort())),
                0, 1);
    QTRY_COMPARE(player.renderLink()->holders(), 1);

    // On screen: the next synchronize() frees the context and lets go.
    player.stop();
    QTRY_VERIFY2(!player.hasCore(), "the renderer let go but the core stayed");
    QCOMPARE(player.renderLink()->holders(), 0);

    // Stopped, then the window goes away before the release: an unexposed
    // window never synchronizes, and the core must be released regardless.
    // That release may take the scene graph but never the graphics context,
    // even from a window that does not keep it persistent, and it hands the
    // window's own settings back unchanged.
    window.setPersistentGraphics(false);
    auto *gl = static_cast<QOpenGLContext *>(window.rendererInterface()->getResource(
        &window, QSGRendererInterface::OpenGLContextResource));
    QVERIFY(gl);
    auto glDestroyed = std::make_shared<std::atomic<bool>>(false);
    QObject::connect(
        gl, &QOpenGLContext::aboutToBeDestroyed, gl, [glDestroyed] { *glDestroyed = true; },
        Qt::DirectConnection);
    player.load(QUrl(QStringLiteral("http://127.0.0.1:%1/stalled.mkv").arg(silent.serverPort())),
                0, 2);
    QTRY_COMPARE(player.renderLink()->holders(), 1);
    player.stop();
    window.hide();
    QTRY_VERIFY2(!player.hasCore(), "an unexposed window kept the idle core");
    QCOMPARE(player.renderLink()->holders(), 0);
    QVERIFY2(!glDestroyed->load(), "releasing the core destroyed the window's GL context");
    QVERIFY(!window.isPersistentGraphics());
    QVERIFY(window.isPersistentSceneGraph());
    window.setPersistentGraphics(true);

    // Shown again, the rebuilt scene attaches to the next core.
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    player.load(QUrl(QStringLiteral("http://127.0.0.1:%1/stalled.mkv").arg(silent.serverPort())),
                0, 3);
    QTRY_COMPARE(player.renderLink()->holders(), 1);

    // Minimised rather than hidden, where the platform stops exposing it.
    window.showMinimized();
    if (QTest::qWaitFor([&] { return !window.isExposed(); }, 2000)) {
        player.stop();
        QTRY_VERIFY2(!player.hasCore(), "a minimised window kept the idle core");
        QCOMPARE(player.renderLink()->holders(), 0);
    } else {
        qInfo("this platform keeps a minimised window exposed; nothing to test");
        player.stop();
        QTRY_VERIFY(!player.hasCore());
    }
    item.setPlayerObject(nullptr);
}

// Main.qml parks the one video plane in the picture-in-picture frame, which is
// hidden once playback stops: exactly when the core is released. A hidden
// item must still re-synchronize, or the core would never be let go.
void MpvVideoItemTest::hiddenPlaneStillLetsGo()
{
    // Before the window, as in releaseWaitsForTheRenderer().
    MpvPlayer player;
    player.setCoreIdleReleaseDelayForTests(20);
    QQuickWindow window;
    window.resize(320, 180);
    QQuickItem frame(window.contentItem());
    frame.setSize(window.size());
    MpvVideoItem item(&frame);
    item.setSize(window.size());
    item.setPlayerObject(&player);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    if (window.rendererInterface()->graphicsApi() != QSGRendererInterface::OpenGL)
        QSKIP("MpvVideoItem renders only with the OpenGL scene graph");

    QTcpServer silent;
    QVERIFY(silent.listen(QHostAddress::LocalHost));
    player.load(QUrl(QStringLiteral("http://127.0.0.1:%1/stalled.mkv").arg(silent.serverPort())),
                0, 1);
    QTRY_COMPARE(player.renderLink()->holders(), 1);

    frame.setVisible(false);
    player.stop();
    QTRY_VERIFY2(!player.hasCore(), "a renderer under a hidden parent never let go of the core");
    QCOMPARE(player.renderLink()->holders(), 0);

    item.setVisible(false);
    player.load(QUrl(QStringLiteral("http://127.0.0.1:%1/stalled.mkv").arg(silent.serverPort())),
                0, 2);
    QTRY_COMPARE(player.renderLink()->holders(), 1);
    player.stop();
    QTRY_VERIFY2(!player.hasCore(), "a hidden renderer never let go of the core");
    item.setPlayerObject(nullptr);
}

// A player destroyed under a renderer that still holds its core would take a
// live render context down with it, which mpv answers with an abort of its
// own, far from the cause. The player has to say so first, and loudly. Run in
// a child process, because in a debug build "loudly" includes Q_ASSERT.
void MpvVideoItemTest::destroyingAHeldPlayerIsLoud()
{
    if (qEnvironmentVariableIsSet("STRMQT_TEST_HELD_PLAYER")) {
#ifdef Q_OS_LINUX
        // The abort is the expected outcome: keep it out of the core dump store.
        prctl(PR_SET_DUMPABLE, 0);
#endif
        auto *player = new MpvPlayer;
        player->load(kMissingMedia, 0, 1);
        QVERIFY(player->hasCore());
        player->renderLink()->acquire(); // what a renderer with a context does
        delete player;
        return;
    }

    QProcess child;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("STRMQT_TEST_HELD_PLAYER"), QStringLiteral("1"));
    env.insert(QStringLiteral("QTEST_DISABLE_STACK_DUMP"), QStringLiteral("1"));
    child.setProcessEnvironment(env);
    child.start(QCoreApplication::applicationFilePath(),
                {QStringLiteral("destroyingAHeldPlayerIsLoud")});
    QVERIFY(child.waitForFinished(60000));
    const QString output = QString::fromLocal8Bit(child.readAllStandardError())
        + QString::fromLocal8Bit(child.readAllStandardOutput());
    QVERIFY2(output.contains(QLatin1String("still hold its mpv core")), qPrintable(output));
#ifndef QT_NO_DEBUG
    QVERIFY2(child.exitStatus() == QProcess::CrashExit || child.exitCode() != 0,
             "a debug build let a held player be destroyed without asserting");
#endif
}

// Reading the core's settings back is for the next core. A player on its way
// out has none, and must not tell its listeners about changes mid-destruction.
void MpvVideoItemTest::destructionEmitsNoSettingChanges()
{
    auto *player = new MpvPlayer;
    QVERIFY(failLoad(*player, 1));
    player->stop();
    // Set on the live core after stop(): the mirror still says 1.0.
    player->setPlaybackSpeed(1.5);
    player->setAudioDelayMs(40);
    QSignalSpy speed(player, &PlayerBackend::playbackSpeedChanged);
    QSignalSpy audioDelay(player, &PlayerBackend::audioDelayChanged);
    delete player;
    QCOMPARE(speed.count(), 0);
    QCOMPARE(audioDelay.count(), 0);
}

QTEST_MAIN(MpvVideoItemTest)
#include "tst_mpv_video_item.moc"
