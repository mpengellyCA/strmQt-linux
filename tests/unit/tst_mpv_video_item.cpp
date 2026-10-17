#include <QtTest>

#include "playback/mpv/MpvPlayer.h"
#include "playback/mpv/MpvVideoItem.h"
#include "playback/upscale/VideoProcessing.h"

#include <QOpenGLContext>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QMutex>
#include <QPointer>
#include <QProcess>
#include <QQuickWindow>
#include <QSGNode>
#include <QSGRendererInterface>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <algorithm>
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

// The renderer cases need an OpenGL scene graph, which the offscreen platform
// the ordinary ctest entry uses does not give a QQuickWindow. A second entry
// (tst_mpv_video_item_gl, tests/CMakeLists.txt) runs them on the session's own
// display: there, where CI exports STRMQT_REQUIRE_GL=1, no GL is a failure.
#define SKIP_UNLESS_OPENGL_SCENE_GRAPH(window)                                                     \
    do {                                                                                           \
        if ((window).rendererInterface()->graphicsApi() != QSGRendererInterface::OpenGL) {         \
            if (qEnvironmentVariableIsSet("STRMQT_TEST_NATIVE_GL") &&                              \
                qEnvironmentVariableIsSet("STRMQT_REQUIRE_GL"))                                    \
                QFAIL("no OpenGL scene graph on the session's display");                           \
            QSKIP("MpvVideoItem renders only with the OpenGL scene graph");                       \
        }                                                                                          \
    } while (false)

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

// A short clip mpv can keep open with no render context: without one it gives
// up on the picture, and a file with nothing left to play is a failed load. The
// silent audio track is what stays. mpeg4 and PCM are ffmpeg's own encoders.
// Empty when there is no ffmpeg to make it with.
QString generateClip(QTemporaryDir &dir, const QString &name = QStringLiteral("clip.avi"))
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return {};
    const QString path = dir.filePath(name);
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                           QStringLiteral("testsrc2=duration=20:size=64x64:rate=10"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                           QStringLiteral("anullsrc=r=8000:cl=mono"), QStringLiteral("-shortest"),
                           QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-c:a"),
                           QStringLiteral("pcm_s16le"), path});
    if (!process.waitForFinished(30000) || process.exitCode() != 0)
        return {};
    return path;
}

// Brings the core up (with a load that fails, which also leaves the event
// queue empty) and points its audio at mpv's null device, so a clip plays in
// real time without opening a sound device on the machine running the tests.
bool startCoreWithoutSound(MpvPlayer &player, PlayerBackend::LoadId loadId)
{
    return failLoad(player, loadId) && player.handle() != nullptr &&
           mpv_set_property_string(player.handle(), "ao", "null") >= 0;
}

// A subtitle file named so that mpv's own sidecar matching (same base name as
// the media) cannot be what loaded it.
QString writeSubtitle(QTemporaryDir &dir, const QString &name, const QString &line)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(QStringLiteral("1\n00:00:00,000 --> 00:00:19,000\n%1\n").arg(line).toUtf8());
    return path;
}

// Everything the player said about load ids, in the order it said it.
struct LoadEvents
{
    explicit LoadEvents(MpvPlayer &player)
        : states(&player, &PlayerBackend::stateChanged),
          errors(&player, &PlayerBackend::errorOccurred)
    {
    }

    bool sawState(PlayerBackend::State state, PlayerBackend::LoadId loadId) const
    {
        for (const QList<QVariant> &event : states) {
            if (event.at(0).value<PlayerBackend::State>() == state &&
                event.at(1).value<PlayerBackend::LoadId>() == loadId)
                return true;
        }
        return false;
    }
    QList<PlayerBackend::LoadId> idsWithState(PlayerBackend::State state) const
    {
        QList<PlayerBackend::LoadId> ids;
        for (const QList<QVariant> &event : states) {
            if (event.at(0).value<PlayerBackend::State>() == state)
                ids.append(event.at(1).value<PlayerBackend::LoadId>());
        }
        return ids;
    }
    QList<PlayerBackend::LoadId> errorIds() const
    {
        QList<PlayerBackend::LoadId> ids;
        for (const QList<QVariant> &event : errors)
            ids.append(event.at(1).value<PlayerBackend::LoadId>());
        return ids;
    }

    QSignalSpy states;
    QSignalSpy errors;
};

// Long enough for mpv's own thread to start a missing local file, fail it and
// queue both events — while this thread, which is the one that drains them,
// does nothing. That is the state every attribution test below needs: events
// of one load sitting in the queue when the next load() is issued.
void letMpvRunUndrained()
{
    QThread::msleep(150);
}

// A sine tone of `seconds`, FLAC like a ripped album, at `sampleRate`. Empty
// when there is no ffmpeg to make it with.
QString generateTone(QTemporaryDir &dir, const QString &name, int sampleRate, double seconds = 2.0)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return {};
    const QString path = dir.filePath(name);
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                           QStringLiteral("sine=frequency=440:sample_rate=%1:duration=%2")
                               .arg(sampleRate)
                               .arg(seconds),
                           QStringLiteral("-c:a"), QStringLiteral("flac"), path});
    if (!process.waitForFinished(30000) || process.exitCode() != 0)
        return {};
    return path;
}

// A second client on the player's core: what mpv itself did, read without
// going through MpvPlayer's own reading of the event stream — each audio
// output it opened (its "AO: [...]" line), each file it started, the playhead
// with the wall-clock time it was seen at. Destroy it before the player: a core
// is not torn down while another client holds it.
class CoreObserver
{
public:
    struct Sample
    {
        int file = 0; // 1 for the first file started after construction
        qint64 wallMs = 0;
        double position = 0.0;
    };

    explicit CoreObserver(mpv_handle *core) : m_handle(mpv_create_client(core, "test-observer"))
    {
        mpv_request_log_messages(m_handle, "v");
        mpv_observe_property(m_handle, 0, "time-pos", MPV_FORMAT_DOUBLE);
        m_clock.start();
        m_timer.setInterval(1);
        QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this] { drain(); });
        m_timer.start();
    }
    ~CoreObserver()
    {
        m_timer.stop();
        mpv_destroy(m_handle);
    }

    int audioOutputsOpened() const { return m_aoLines.size(); }
    const QStringList &audioOutputs() const { return m_aoLines; }
    int filesStarted() const { return m_files; }
    int prefetchedOpens() const { return m_prefetchedOpens; }
    // Times the audio output ran dry: an audible gap, whatever the playheads say.
    int underruns() const { return m_underruns; }
    const QList<Sample> &samples() const { return m_samples; }

    void drain()
    {
        while (true) {
            mpv_event *event = mpv_wait_event(m_handle, 0);
            if (event->event_id == MPV_EVENT_NONE)
                return;
            if (event->event_id == MPV_EVENT_START_FILE) {
                ++m_files;
            } else if (event->event_id == MPV_EVENT_LOG_MESSAGE) {
                const auto *msg = static_cast<mpv_event_log_message *>(event->data);
                const QString text = QString::fromUtf8(msg->text).trimmed();
                if (qstrcmp(msg->prefix, "cplayer") == 0 && text.startsWith(QLatin1String("AO: [")))
                    m_aoLines.append(text);
                if (text.contains(QLatin1String("Using prefetched")))
                    ++m_prefetchedOpens;
                if (text.contains(QLatin1String("underrun"), Qt::CaseInsensitive))
                    ++m_underruns;
            } else if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
                const auto *change = static_cast<mpv_event_property *>(event->data);
                if (change->format == MPV_FORMAT_DOUBLE)
                    m_samples.append({m_files, m_clock.elapsed(), *static_cast<double *>(change->data)});
            }
        }
    }

    // How much longer the change from `file` to the next took than the audio
    // in it: wall time between a playhead sample half a second before the end
    // of `file` and one half a second into the next, minus the second of
    // media between them. mpv's null device keeps a real clock, so a stall at
    // the change — a reopened output, a stream opened late — adds to it.
    // Across a gapless change it reads negative by about the depth of the
    // output's buffer: the next file's playhead starts while the last of the
    // previous file is still queued in front of it. Polled, so good to a few
    // milliseconds. NaN when either sample is missing.
    double gapMs(int file, double fileSeconds) const
    {
        const Sample *before = nullptr;
        const Sample *after = nullptr;
        for (const Sample &sample : m_samples) {
            if (sample.file == file && !before && sample.position >= fileSeconds - 0.5)
                before = &sample;
            else if (sample.file == file + 1 && !after && sample.position >= 0.5)
                after = &sample;
        }
        if (!before || !after)
            return qQNaN();
        const double media = (fileSeconds - before->position) + after->position;
        return double(after->wallMs - before->wallMs) - media * 1000.0;
    }

private:
    mpv_handle *m_handle = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QStringList m_aoLines;
    int m_files = 0;
    int m_prefetchedOpens = 0;
    int m_underruns = 0;
    QList<Sample> m_samples;
};

} // namespace

class MpvVideoItemTest : public QObject
{
    Q_OBJECT

private slots:
    void coreInitializesOnFirstLoad();
    void deferredSettingsApplyOnFirstLoad();
    void deferredSettingsNormalizeBeforeFirstLoad();
    void bundledScriptsAreNotLoaded();
    void gaplessOptionsAreSetOnTheCore();
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
    void videoProcessingAppliesOnFirstLoad();
    void videoProcessingSurvivesACoreRelease();
    void outputPixelSizeFollowsTheItem();
    void framesAreHandedOverWhenDue();
    void renderingAFrameDoesNotWaitForItsDisplayTime();
    void corePropertiesAreDeliveredWithNoFileLoaded();
    void corePropertiesAreDeliveredWhileALoadIsStarting();
    void aReplacedLoadsFailureIsNotTheNextLoads();
    void rapidAlternatingLoadsEndWithOnlyTheLastPlaying();
    void stopThenLoadAttributesToTheNewLoad();
    void emptySubtitleFontRestoresMpvsDefault();
    void externalSubtitlesJoinTheTrackListInOrder();
    void externalSubtitlesBelongToTheirLoad();
    void anUnreadableExternalSubtitleCostsOnlyItself();
    void externalSubtitleIsFetchedFromItsUrl();
    void aPrefetchedTrackFollowsWithoutALoad();
    void aWithdrawnPrefetchIsNotPlayed();
    void aPrefetchTooLateForTheEndIsAnOrdinaryEnd();
    void aSubtitleAddAndAPrefetchNeverOverlap();
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
// What the gapless advance rests on, as the core reports it. prefetch-playlist
// is off in mpv by default, and without it the next file is only opened after
// the current one has played out — the gap itself. gapless-audio "yes" would
// resample a 96 kHz record to whatever rate played first.
void MpvVideoItemTest::gaplessOptionsAreSetOnTheCore()
{
    MpvPlayer player;
    player.load(kMissingMedia, 0, 1);
    mpv_handle *handle = player.handle();
    QVERIFY(handle != nullptr);
    QCOMPARE(stringProperty(handle, "prefetch-playlist"), QStringLiteral("yes"));
    QCOMPARE(stringProperty(handle, "gapless-audio"), QStringLiteral("weak"));
}

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
    SKIP_UNLESS_OPENGL_SCENE_GRAPH(window);

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
    SKIP_UNLESS_OPENGL_SCENE_GRAPH(window);

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

void MpvVideoItemTest::videoProcessingAppliesOnFirstLoad()
{
    MpvPlayer player;
    QVERIFY(player.supportsVideoShaders());
    const VideoProcessing chain{{QStringLiteral("/strmqt-test/a.glsl")},
                                QStringLiteral("ewa_lanczossharp"),
                                QStringLiteral("ewa_lanczossharp")};
    player.setVideoProcessing(chain); // before the core exists
    player.load(kMissingMedia, 0, 1);
    mpv_handle *handle = player.handle();
    QVERIFY(handle != nullptr);
    QCOMPARE(stringProperty(handle, "glsl-shaders"), QStringLiteral("/strmqt-test/a.glsl"));
    QCOMPARE(stringProperty(handle, "scale"), QStringLiteral("ewa_lanczossharp"));
    QCOMPARE(stringProperty(handle, "cscale"), QStringLiteral("ewa_lanczossharp"));

    // Empty filters mean mpv's own defaults, not "leave the last ones".
    player.setVideoProcessing({});
    QCOMPARE(stringProperty(handle, "glsl-shaders"), QString());
    QCOMPARE(stringProperty(handle, "scale"), stringProperty(handle, "option-info/scale/default-value"));
    QCOMPARE(stringProperty(handle, "cscale"), stringProperty(handle, "option-info/cscale/default-value"));
    player.stop();
}

void MpvVideoItemTest::videoProcessingSurvivesACoreRelease()
{
    MpvPlayer player;
    player.setCoreIdleReleaseDelayForTests(20);
    const VideoProcessing chain{{QStringLiteral("/strmqt-test/b.glsl")}, QString(), QString()};
    QVERIFY(failLoad(player, 1));
    player.setVideoProcessing(chain);
    player.stop();
    QTRY_VERIFY(!player.hasCore());

    QVERIFY(failLoad(player, 2));
    QCOMPARE(stringProperty(player.handle(), "glsl-shaders"), QStringLiteral("/strmqt-test/b.glsl"));
    player.stop();
}

void MpvVideoItemTest::outputPixelSizeFollowsTheItem()
{
    QQuickWindow window;
    window.resize(800, 600);
    auto *item = new MpvVideoItem(window.contentItem());
    QSignalSpy changed(item, &MpvVideoItem::outputPixelSizeChanged);
    item->setSize(QSizeF(640, 360));
    const qreal dpr = window.effectiveDevicePixelRatio();
    QCOMPARE(item->outputPixelSize(), QSize(qRound(640 * dpr), qRound(360 * dpr)));
    QVERIFY(changed.count() >= 1);
    const int before = changed.count();
    item->setSize(QSizeF(640, 360));
    QCOMPARE(changed.count(), before); // no change, no signal
    item->setSize(QSizeF(1280, 720));
    QCOMPARE(item->outputPixelSize(), QSize(qRound(1280 * dpr), qRound(720 * dpr)));
}

// The renderer never waits for a frame's display time, so mpv must not hand
// frames over ahead of it: with mpv's default offset every frame would be shown
// up to 50 ms early. The offset is per core, and the player replaces its core.
void MpvVideoItemTest::framesAreHandedOverWhenDue()
{
    MpvPlayer player;
    player.setCoreIdleReleaseDelayForTests(20);
    MpvVideoItem item;
    item.setPlayerObject(&player);

    player.load(kMissingMedia, 0, 1);
    QVERIFY(player.handle() != nullptr);
    QCOMPARE(stringProperty(player.handle(), "video-timing-offset"), QStringLiteral("0.000000"));

    player.stop();
    QTRY_VERIFY(!player.hasCore());
    player.load(kMissingMedia, 0, 2);
    QVERIFY(player.handle() != nullptr);
    QCOMPARE(stringProperty(player.handle(), "video-timing-offset"), QStringLiteral("0.000000"));
    player.stop();
    item.setPlayerObject(nullptr);

    // A player whose core is already up when the plane takes it.
    MpvPlayer running;
    running.load(kMissingMedia, 0, 1);
    QVERIFY(running.handle() != nullptr);
    QVERIFY(stringProperty(running.handle(), "video-timing-offset") != QStringLiteral("0.000000"));
    MpvVideoItem late;
    late.setPlayerObject(&running);
    QCOMPARE(stringProperty(running.handle(), "video-timing-offset"), QStringLiteral("0.000000"));
    running.stop();
    late.setPlayerObject(nullptr);
}

// The renderer runs on the scene graph's render thread, where a wait stalls
// the whole window: every animation, scroll and focus transition drops to the
// video's frame rate. Real frames through the real renderer, with the time
// the scene graph spends rendering each pass measured from outside.
//
// At 10 fps mpv's default hand-over is 50 ms before each frame is due, so a
// renderer that sat that out (its own sleep, or mpv's blocking wait) spends
// about 50 ms in every pass that draws a frame. One that does not spends as
// long as a 320x180 frame takes to draw.
void MpvVideoItemTest::renderingAFrameDoesNotWaitForItsDisplayTime()
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        QSKIP("ffmpeg is required to generate the test clip");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString clip = dir.filePath(QStringLiteral("clip10fps.avi"));
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                           QStringLiteral("testsrc2=duration=6:size=320x180:rate=10"),
                           QStringLiteral("-c:v"), QStringLiteral("mpeg4"), clip});
    QVERIFY(process.waitForFinished(30000));
    QCOMPARE(process.exitCode(), 0);

    // Before the window, as in releaseWaitsForTheRenderer().
    MpvPlayer player;
    QQuickWindow window;
    window.resize(320, 180);
    CountingVideoItem item(window.contentItem());
    item.setSize(window.size());
    item.setPlayerObject(&player);

    // Connected before the window is shown, so before the framebuffer node
    // connects its own render to beforeRendering: the clock starts first.
    QMutex mutex;
    QElapsedTimer passClock;
    QList<qint64> passMs;
    bool measuring = false;
    connect(
        &window, &QQuickWindow::beforeRendering, &window, [&] { passClock.start(); },
        Qt::DirectConnection);
    connect(
        &window, &QQuickWindow::afterRendering, &window,
        [&] {
            const QMutexLocker locker(&mutex);
            if (measuring && passClock.isValid())
                passMs.append(passClock.elapsed());
        },
        Qt::DirectConnection);

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    SKIP_UNLESS_OPENGL_SCENE_GRAPH(window);

    // mpv opens its video output when the first frame is decoded and gives up
    // on video if no render context exists by then. A local clip gets there
    // before the renderer's first pass, so a load that never answers brings
    // the core and the context up first.
    QTcpServer silent;
    QVERIFY(silent.listen(QHostAddress::LocalHost));
    player.load(QUrl(QStringLiteral("http://127.0.0.1:%1/stalled.mkv").arg(silent.serverPort())),
                0, 1);
    QTRY_VERIFY(!item.gpuRenderer().isEmpty());

    player.load(QUrl::fromLocalFile(clip), 0, 2);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), PlayerBackend::State::Playing, 15000);
    QTRY_VERIFY(item.paintNodeUpdates > 0);
    {
        const QMutexLocker locker(&mutex);
        measuring = true;
    }
    // Nothing else in this window changes, so each pass from here on is one
    // video frame asking to be drawn.
    const int framesWanted = 20;
    const int baseline = item.paintNodeUpdates;
    QTRY_VERIFY_WITH_TIMEOUT(item.paintNodeUpdates >= baseline + framesWanted, 15000);

    QList<qint64> passes;
    {
        const QMutexLocker locker(&mutex);
        measuring = false;
        passes = passMs;
    }
    player.stop();
    item.setPlayerObject(nullptr);

    QVERIFY2(passes.size() >= framesWanted / 2,
             qPrintable(QStringLiteral("only %1 passes measured").arg(passes.size())));
    std::sort(passes.begin(), passes.end());
    const qint64 median = passes.at(passes.size() / 2);
    // The median, so that one pass held up by a busy machine does not decide it.
    QVERIFY2(median < 25,
             qPrintable(QStringLiteral("the render thread spent a median of %1 ms per video frame "
                                       "(slowest %2 ms, %3 passes): it is waiting for frame times")
                            .arg(median)
                            .arg(passes.last())
                            .arg(passes.size())));
}

// ── Wave 1 regressions (commit 07ccbd0) ───────────────────────────────────────

// volume, mute, speed and the two delays are the core's, not a file's: they
// can be changed with nothing loaded, and the change has to reach whoever
// mirrors them. They used to sit behind the per-load guard, which is shut
// whenever the load the events talk about is not the one last asked for —
// after every stop().
void MpvVideoItemTest::corePropertiesAreDeliveredWithNoFileLoaded()
{
    MpvPlayer player;
    QVERIFY(failLoad(player, 1));
    player.stop();
    QCOMPARE(player.state(), PlayerBackend::State::Idle);
    QVERIFY(player.hasCore());

    QSignalSpy volume(&player, &PlayerBackend::volumeChanged);
    QSignalSpy muted(&player, &PlayerBackend::mutedChanged);
    QSignalSpy speed(&player, &PlayerBackend::playbackSpeedChanged);
    QSignalSpy audioDelay(&player, &PlayerBackend::audioDelayChanged);
    QSignalSpy subtitleDelay(&player, &PlayerBackend::subtitleDelayChanged);

    player.setVolume(42);
    player.setMuted(true);
    player.setPlaybackSpeed(1.5);
    player.setAudioDelayMs(120);
    player.setSubtitleDelayMs(-80);

    QTRY_VERIFY(!volume.isEmpty());
    QCOMPARE(volume.constLast().at(0).toInt(), 42);
    QTRY_VERIFY(!muted.isEmpty());
    QCOMPARE(muted.constLast().at(0).toBool(), true);
    // These three are mirrored: the getter is only as good as the event.
    QTRY_COMPARE(player.playbackSpeed(), 1.5);
    QTRY_COMPARE(player.audioDelayMs(), 120);
    QTRY_COMPARE(player.subtitleDelayMs(), -80);
    QVERIFY(!speed.isEmpty());
    QVERIFY(!audioDelay.isEmpty());
    QVERIFY(!subtitleDelay.isEmpty());
    player.stop();
}

// The same guard is also shut from load() until that load's START_FILE has
// been drained. That window does not reproduce from outside: mpv reports
// property changes only once its other events are out, so START_FILE always
// arrives first, and this test passes against the old guard as well. What it
// does pin is the everyday case around it — settings changed just before and
// during a load that never gets anywhere (a stalled stream) still arrive.
void MpvVideoItemTest::corePropertiesAreDeliveredWhileALoadIsStarting()
{
    // Accepts and never answers: the load stays in Loading.
    QTcpServer silent;
    QVERIFY(silent.listen(QHostAddress::LocalHost));
    const QUrl stalled(QStringLiteral("http://127.0.0.1:%1/stalled.mkv").arg(silent.serverPort()));

    MpvPlayer player;
    QVERIFY(failLoad(player, 1));
    QSignalSpy volume(&player, &PlayerBackend::volumeChanged);
    QSignalSpy muted(&player, &PlayerBackend::mutedChanged);

    // Changed and given time to be queued before the load is even issued, so
    // the change is ahead of START_FILE in the queue whatever mpv's timing.
    player.setVolume(37);
    player.setMuted(true);
    player.setPlaybackSpeed(2.0);
    player.setAudioDelayMs(60);
    player.setSubtitleDelayMs(40);
    letMpvRunUndrained();
    player.load(stalled, 0, 2);

    QTRY_VERIFY(!volume.isEmpty());
    QCOMPARE(volume.constLast().at(0).toInt(), 37);
    QTRY_VERIFY(!muted.isEmpty());
    QCOMPARE(muted.constLast().at(0).toBool(), true);
    QTRY_COMPARE(player.playbackSpeed(), 2.0);
    QTRY_COMPARE(player.audioDelayMs(), 60);
    QTRY_COMPARE(player.subtitleDelayMs(), 40);
    QCOMPARE(player.state(), PlayerBackend::State::Loading);

    // And once more with the load under way and nothing loaded yet.
    volume.clear();
    player.setVolume(64);
    QTRY_VERIFY(!volume.isEmpty());
    QCOMPARE(volume.constLast().at(0).toInt(), 64);
    player.stop();
}

// load(A) fails inside mpv before anyone drains its events, and load(B) is
// issued on top. START_FILE and END_FILE for A are then read with B as the
// current load. They used to be stamped with "the pending load", which by then
// was B: a healthy B was reported failed, and PlayerController demoted or gave
// up on a stream that was fine.
void MpvVideoItemTest::aReplacedLoadsFailureIsNotTheNextLoads()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString clip = generateClip(dir);
    if (clip.isEmpty())
        QSKIP("ffmpeg is required to generate the test clip");

    MpvPlayer player;
    QVERIFY(startCoreWithoutSound(player, 1));
    LoadEvents events(player);

    player.load(kMissingMedia, 0, 2);
    letMpvRunUndrained();
    player.load(QUrl::fromLocalFile(clip), 0, 3, true);

    QTRY_VERIFY_WITH_TIMEOUT(events.sawState(PlayerBackend::State::Paused, 3), 15000);
    QCOMPARE(player.state(), PlayerBackend::State::Paused);
    QTest::qWait(100);
    QVERIFY2(!events.errorIds().contains(3), "the replaced load's failure was reported for its successor");
    QVERIFY(!events.sawState(PlayerBackend::State::Error, 3));
    // The failure is the missing file's, and it is said to be the missing file's.
    QCOMPARE(events.errorIds(), QList<PlayerBackend::LoadId>{2});
    QVERIFY(!events.sawState(PlayerBackend::State::Paused, 2));
    QVERIFY(!events.sawState(PlayerBackend::State::Playing, 2));
    QCOMPARE(player.state(), PlayerBackend::State::Paused);
    player.stop();
}

// The same, at the pace of a held "next" key: missing and playable files in
// turn, some given time to start and fail and some replaced at once. Whatever
// mpv got round to, a failure belongs to a missing file, a ready state to a
// playable one, and the player ends on the last load and nothing else.
void MpvVideoItemTest::rapidAlternatingLoadsEndWithOnlyTheLastPlaying()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString clip = generateClip(dir);
    if (clip.isEmpty())
        QSKIP("ffmpeg is required to generate the test clip");

    MpvPlayer player;
    QVERIFY(startCoreWithoutSound(player, 1));
    LoadEvents events(player);

    // Odd ids are missing files, even ids the clip.
    constexpr PlayerBackend::LoadId kFirst = 11;
    constexpr PlayerBackend::LoadId kLast = 40;
    for (PlayerBackend::LoadId id = kFirst; id <= kLast; ++id) {
        const bool missing = id % 2 == 1;
        player.load(missing ? kMissingMedia : QUrl::fromLocalFile(clip), 0, id, true);
        if (id % 3 == 0)
            letMpvRunUndrained(); // let this one get as far as it can, undrained
        else if (id % 5 == 0)
            QTest::qWait(20); // and drain in the middle of some
    }

    QTRY_VERIFY_WITH_TIMEOUT(events.sawState(PlayerBackend::State::Paused, kLast), 15000);
    QTest::qWait(150);
    QCOMPARE(player.state(), PlayerBackend::State::Paused);

    for (const PlayerBackend::LoadId id : events.errorIds())
        QVERIFY2(id % 2 == 1, qPrintable(QStringLiteral("a failure was reported for clip load %1").arg(id)));
    for (const PlayerBackend::State ready :
         {PlayerBackend::State::Playing, PlayerBackend::State::Paused}) {
        for (const PlayerBackend::LoadId id : events.idsWithState(ready))
            QVERIFY2(id % 2 == 0,
                     qPrintable(QStringLiteral("missing-file load %1 was reported ready").arg(id)));
    }
    QVERIFY(!events.errorIds().contains(kLast));
    QVERIFY(!events.sawState(PlayerBackend::State::Error, kLast));
    // Ready once, for the last load, and it is still what the player says.
    QCOMPARE(events.idsWithState(PlayerBackend::State::Paused).count(kLast), 1);
    QCOMPARE(events.idsWithState(PlayerBackend::State::Paused).constLast(), kLast);
    QCOMPARE(player.positionMs(), Q_INT64_C(0));
    player.stop();
}

// stop() between the two changes nothing about whose events are in the queue.
void MpvVideoItemTest::stopThenLoadAttributesToTheNewLoad()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString clip = generateClip(dir);
    if (clip.isEmpty())
        QSKIP("ffmpeg is required to generate the test clip");

    MpvPlayer player;
    QVERIFY(startCoreWithoutSound(player, 1));
    LoadEvents events(player);

    player.load(kMissingMedia, 0, 2);
    letMpvRunUndrained();
    player.stop();
    QCOMPARE(player.state(), PlayerBackend::State::Idle);
    player.load(QUrl::fromLocalFile(clip), 0, 3, true);

    QTRY_VERIFY_WITH_TIMEOUT(events.sawState(PlayerBackend::State::Paused, 3), 15000);
    QTest::qWait(100);
    QVERIFY2(!events.errorIds().contains(3), "a stopped load's failure was reported for the next load");
    QVERIFY(!events.sawState(PlayerBackend::State::Error, 3));
    QCOMPARE(player.state(), PlayerBackend::State::Paused);

    // The other way round: a playing file stopped, and a missing one loaded.
    // The failure is the new load's, and nothing of the old file's follows it.
    player.setPaused(false);
    QTRY_VERIFY(events.sawState(PlayerBackend::State::Playing, 3));
    QTRY_VERIFY(player.positionMs() > 0);
    player.stop();
    QCOMPARE(player.positionMs(), Q_INT64_C(0));
    player.load(kMissingMedia, 0, 4);
    QTRY_VERIFY(events.errorIds().contains(4));
    QCOMPARE(player.state(), PlayerBackend::State::Error);
    QCOMPARE(player.positionMs(), Q_INT64_C(0));
    QCOMPARE(events.errorIds(), (QList<PlayerBackend::LoadId>{2, 4}));
    player.stop();
}

// "Default" in the subtitle font picker is the empty family. It used to be
// skipped rather than written, so once a face had been chosen there was no
// way back to mpv's own.
void MpvVideoItemTest::emptySubtitleFontRestoresMpvsDefault()
{
    MpvPlayer player;
    player.load(kMissingMedia, 0, 1);
    mpv_handle *handle = player.handle();
    QVERIFY(handle != nullptr);
    const QString defaultFont = stringProperty(handle, "sub-font");
    QVERIFY(!defaultFont.isEmpty());
    QCOMPARE(defaultFont, stringProperty(handle, "option-info/sub-font/default-value"));

    player.setSubtitleStyle(QStringLiteral("Noto Serif"), 100, QStringLiteral("#FFFFFF"), 0, 100);
    QCOMPARE(stringProperty(handle, "sub-font"), QStringLiteral("Noto Serif"));
    player.setSubtitleStyle(QString(), 100, QStringLiteral("#FFFFFF"), 0, 100);
    QCOMPARE(stringProperty(handle, "sub-font"), defaultFont);
    player.setSubtitleStyle(QStringLiteral("Noto Sans Mono"), 100, QStringLiteral("#FFFFFF"), 0, 100);
    QCOMPARE(stringProperty(handle, "sub-font"), QStringLiteral("Noto Sans Mono"));

    // A core that starts with a face already chosen still knows mpv's own.
    player.setCoreIdleReleaseDelayForTests(20);
    player.stop();
    QTRY_VERIFY(!player.hasCore());
    player.load(kMissingMedia, 0, 2);
    handle = player.handle();
    QVERIFY(handle != nullptr);
    QCOMPARE(stringProperty(handle, "sub-font"), QStringLiteral("Noto Sans Mono"));
    player.setSubtitleStyle(QString(), 100, QStringLiteral("#FFFFFF"), 0, 100);
    QCOMPARE(stringProperty(handle, "sub-font"), defaultFont);
    player.stop();
}

// ── External subtitles (PlayerBackend::addExternalSubtitle) ───────────────────

// What the track panel and PlayerController rely on: an attached subtitle is
// an ordinary entry of subtitleTracks() with an id setSubtitleTrack() takes,
// carrying the title and language it was attached with; `select` decides
// whether it is shown; and ids follow the order of the calls.
void MpvVideoItemTest::externalSubtitlesJoinTheTrackListInOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString clip = generateClip(dir);
    if (clip.isEmpty())
        QSKIP("ffmpeg is required to generate the test clip");
    const QString dutch = writeSubtitle(dir, QStringLiteral("sidecar-nl.srt"), QStringLiteral("Hallo"));
    const QString english = writeSubtitle(dir, QStringLiteral("sidecar-en.srt"), QStringLiteral("Hello"));
    const QString german = writeSubtitle(dir, QStringLiteral("sidecar-de.srt"), QStringLiteral("Hallo!"));
    QVERIFY(!dutch.isEmpty() && !english.isEmpty() && !german.isEmpty());

    MpvPlayer player;
    // Before there is a core, and before the file is up: ignored, not queued
    // for some later file.
    player.addExternalSubtitle(QUrl::fromLocalFile(dutch), QStringLiteral("Early"), {}, true);
    QVERIFY(startCoreWithoutSound(player, 9));
    player.addExternalSubtitle(QUrl::fromLocalFile(dutch), QStringLiteral("Early"), {}, true);
    QSignalSpy errors(&player, &PlayerBackend::errorOccurred);
    player.load(QUrl::fromLocalFile(clip), 0, 1, true);
    player.addExternalSubtitle(QUrl::fromLocalFile(dutch), QStringLiteral("Early"), {}, true);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), PlayerBackend::State::Paused, 15000);
    QTest::qWait(100);
    QVERIFY2(player.subtitleTracks().isEmpty(), "the clip has no subtitle of its own");
    QCOMPARE(player.currentSubtitleTrackId(), -1);

    player.addExternalSubtitle(QUrl::fromLocalFile(dutch), QStringLiteral("Dutch (SRT)"),
                               QStringLiteral("nld"), true);
    player.addExternalSubtitle(QUrl::fromLocalFile(english), QStringLiteral("English (SRT)"),
                               QStringLiteral("eng"), false);
    player.addExternalSubtitle(QUrl::fromLocalFile(german), QString(), QString(), false);
    QTRY_COMPARE(player.subtitleTracks().size(), 3);

    const QVariantMap first = player.subtitleTracks().at(0).toMap();
    const QVariantMap second = player.subtitleTracks().at(1).toMap();
    const QVariantMap third = player.subtitleTracks().at(2).toMap();
    QCOMPARE(first.value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(first.value(QStringLiteral("title")).toString(), QStringLiteral("Dutch (SRT)"));
    QCOMPARE(first.value(QStringLiteral("language")).toString(), QStringLiteral("nld"));
    QCOMPARE(first.value(QStringLiteral("isExternal")).toBool(), true);
    QCOMPARE(second.value(QStringLiteral("id")).toInt(), 2);
    QCOMPARE(second.value(QStringLiteral("title")).toString(), QStringLiteral("English (SRT)"));
    QCOMPARE(second.value(QStringLiteral("language")).toString(), QStringLiteral("eng"));
    QCOMPARE(second.value(QStringLiteral("isExternal")).toBool(), true);
    QCOMPARE(third.value(QStringLiteral("id")).toInt(), 3);
    // Neither title nor language: not the file name (see the leak check below).
    QCOMPARE(third.value(QStringLiteral("title")).toString(), QStringLiteral("External"));

    // The one asked for is shown; the others are only offered.
    QTRY_COMPARE(player.currentSubtitleTrackId(), 1);
    QCOMPARE(player.subtitleTracks().at(0).toMap().value(QStringLiteral("selected")).toBool(), true);
    QCOMPARE(player.subtitleTracks().at(1).toMap().value(QStringLiteral("selected")).toBool(), false);
    QCOMPARE(player.subtitleTracks().at(2).toMap().value(QStringLiteral("selected")).toBool(), false);

    // The list is published to QML and to the web remote. Where a subtitle was
    // fetched from is not in it: on a server that is a URL with the token.
    for (const QVariant &track : player.subtitleTracks()) {
        const QByteArray published =
            QJsonDocument::fromVariant(track.toMap()).toJson(QJsonDocument::Compact);
        QVERIFY2(!published.contains("sidecar-"), published.constData());
        QVERIFY2(!published.contains(dir.path().toUtf8()), published.constData());
    }

    // Picked in the track panel like any other track, and turned off like one.
    player.setSubtitleTrack(2);
    QTRY_COMPARE(player.currentSubtitleTrackId(), 2);
    player.setSubtitleTrack(-1);
    QTRY_COMPARE(player.currentSubtitleTrackId(), -1);

    // Attached again for the same file, it is the same track, not a second one.
    player.addExternalSubtitle(QUrl::fromLocalFile(english), QStringLiteral("English (SRT)"),
                               QStringLiteral("eng"), true);
    QTRY_COMPARE(player.currentSubtitleTrackId(), 2);
    QCOMPARE(player.subtitleTracks().size(), 3);

    QCOMPARE(player.state(), PlayerBackend::State::Paused);
    QVERIFY(errors.isEmpty());
    player.stop();
}

// A retry, a demotion and a reload are each a new file to mpv, which starts
// without the last one's subtitles — and without any still on their way to it.
void MpvVideoItemTest::externalSubtitlesBelongToTheirLoad()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString clip = generateClip(dir);
    if (clip.isEmpty())
        QSKIP("ffmpeg is required to generate the test clip");
    const QString dutch = writeSubtitle(dir, QStringLiteral("sidecar-nl.srt"), QStringLiteral("Hallo"));
    const QString english = writeSubtitle(dir, QStringLiteral("sidecar-en.srt"), QStringLiteral("Hello"));

    MpvPlayer player;
    QVERIFY(startCoreWithoutSound(player, 9));
    player.load(QUrl::fromLocalFile(clip), 0, 1, true);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), PlayerBackend::State::Paused, 15000);
    player.addExternalSubtitle(QUrl::fromLocalFile(dutch), QStringLiteral("Dutch"), {}, true);
    QTRY_COMPARE(player.subtitleTracks().size(), 1);

    // Queued for load 1, then load 2 arrives before mpv has heard of them.
    player.addExternalSubtitle(QUrl::fromLocalFile(english), QStringLiteral("English"), {}, true);
    player.addExternalSubtitle(QUrl::fromLocalFile(dutch), QStringLiteral("Dutch again"), {}, true);
    player.load(QUrl::fromLocalFile(clip), 0, 2, true);
    QVERIFY(player.subtitleTracks().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), PlayerBackend::State::Paused, 15000);
    QTest::qWait(300);
    QVERIFY2(player.subtitleTracks().isEmpty(),
             "a subtitle attached to the previous load turned up in the next one");
    QCOMPARE(player.currentSubtitleTrackId(), -1);

    // Attached again, they are there again, numbered from one.
    player.addExternalSubtitle(QUrl::fromLocalFile(english), QStringLiteral("English"), {}, false);
    player.addExternalSubtitle(QUrl::fromLocalFile(dutch), QStringLiteral("Dutch"), {}, true);
    QTRY_COMPARE(player.subtitleTracks().size(), 2);
    QCOMPARE(player.subtitleTracks().at(0).toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("English"));
    QCOMPARE(player.subtitleTracks().at(0).toMap().value(QStringLiteral("id")).toInt(), 1);
    QTRY_COMPARE(player.currentSubtitleTrackId(), 2);

    player.stop();
    QVERIFY(player.subtitleTracks().isEmpty());
    player.addExternalSubtitle(QUrl::fromLocalFile(dutch), QStringLiteral("After stop"), {}, true);
    QTest::qWait(200);
    QVERIFY(player.subtitleTracks().isEmpty());
}

// The server can name a subtitle it then fails to serve. That costs the one
// subtitle: playback carries on and the ones behind it in the queue still load.
void MpvVideoItemTest::anUnreadableExternalSubtitleCostsOnlyItself()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString clip = generateClip(dir);
    if (clip.isEmpty())
        QSKIP("ffmpeg is required to generate the test clip");
    const QString english = writeSubtitle(dir, QStringLiteral("sidecar-en.srt"), QStringLiteral("Hello"));

    MpvPlayer player;
    QVERIFY(startCoreWithoutSound(player, 9));
    LoadEvents events(player);
    player.load(QUrl::fromLocalFile(clip), 0, 1, true);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), PlayerBackend::State::Paused, 15000);

    player.addExternalSubtitle(QUrl::fromLocalFile(dir.filePath(QStringLiteral("gone.srt"))),
                               QStringLiteral("Gone"), {}, true);
    player.addExternalSubtitle(QUrl::fromLocalFile(english), QStringLiteral("English"),
                               QStringLiteral("eng"), false);
    QTRY_COMPARE(player.subtitleTracks().size(), 1);
    QCOMPARE(player.subtitleTracks().at(0).toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("English"));
    QCOMPARE(player.currentSubtitleTrackId(), -1);
    QTest::qWait(100);
    QCOMPARE(player.state(), PlayerBackend::State::Paused);
    QVERIFY(events.errorIds().isEmpty());
    player.stop();
}

// The shape PlayerController hands over: an http URL on the server, with the
// stream path Emby uses and the token in the query. mpv has to ask for exactly
// that, and what it is given has to become the subtitle.
void MpvVideoItemTest::externalSubtitleIsFetchedFromItsUrl()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString clip = generateClip(dir);
    if (clip.isEmpty())
        QSKIP("ffmpeg is required to generate the test clip");

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QByteArrayList requestLines;
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (QTcpSocket *socket = server.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &requestLines] {
                if (!socket->canReadLine())
                    return;
                requestLines.append(socket->readLine().trimmed());
                socket->readAll();
                const QByteArray body = "1\n00:00:00,000 --> 00:00:19,000\nOver the wire\n";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/x-subrip\r\n"
                              "Connection: close\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    const QString path = QStringLiteral("/emby/Videos/7700/ms7700/Subtitles/3/0/Stream.srt");
    const QString query = QStringLiteral("api_key=not-a-real-token-fixture-only");
    QUrl url(QStringLiteral("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path));
    url.setQuery(query);

    MpvPlayer player;
    QVERIFY(startCoreWithoutSound(player, 9));
    player.load(QUrl::fromLocalFile(clip), 0, 1, true);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), PlayerBackend::State::Paused, 15000);
    player.addExternalSubtitle(url, QStringLiteral("Dutch (SRT)"), QStringLiteral("nld"), true);

    QTRY_COMPARE_WITH_TIMEOUT(player.subtitleTracks().size(), 1, 15000);
    QVERIFY(!requestLines.isEmpty());
    QCOMPARE(requestLines.constFirst(),
             QByteArray("GET " + path.toUtf8() + "?" + query.toUtf8() + " HTTP/1.1"));
    const QVariantMap track = player.subtitleTracks().at(0).toMap();
    QCOMPARE(track.value(QStringLiteral("title")).toString(), QStringLiteral("Dutch (SRT)"));
    QCOMPARE(track.value(QStringLiteral("codec")).toString(), QStringLiteral("subrip"));
    QTRY_COMPARE(player.currentSubtitleTrackId(), 1);

    // Untitled, mpv would name the track after the file it read — here
    // "Stream.srt?api_key=…". It is named after its language instead.
    QUrl untitled = url;
    untitled.setPath(QStringLiteral("/emby/Videos/7700/ms7700/Subtitles/4/0/Stream.srt"));
    player.addExternalSubtitle(untitled, QString(), QStringLiteral("eng"), false);
    QTRY_COMPARE_WITH_TIMEOUT(player.subtitleTracks().size(), 2, 15000);
    QCOMPARE(player.subtitleTracks().at(1).toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("eng"));
    QCOMPARE(player.currentSubtitleTrackId(), 1);

    // The token went to the server and nowhere a listener of the list sees.
    for (const QVariant &entry : player.subtitleTracks()) {
        const QByteArray published =
            QJsonDocument::fromVariant(entry.toMap()).toJson(QJsonDocument::Compact);
        QVERIFY2(!published.contains("api_key"), published.constData());
        QVERIFY2(!published.contains("not-a-real-token"), published.constData());
        QVERIFY2(!published.contains("Stream.srt"), published.constData());
    }
    player.stop();
}

// ── Gapless advance (PlayerBackend::prefetchNext) ─────────────────────────────

// libmpv 0.41 hangs for good when the file is changed under a running sub-add,
// and with a prefetch queued mpv changes file by itself, at a moment nothing
// here chooses. So the two never overlap: a prefetch is refused while a
// sub-add runs or waits, and a sub-add withdraws a prefetch already queued.
void MpvVideoItemTest::aSubtitleAddAndAPrefetchNeverOverlap()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString clip = generateClip(dir);
    const QString next = generateClip(dir, QStringLiteral("next.avi"));
    if (clip.isEmpty() || next.isEmpty())
        QSKIP("ffmpeg is required to generate the test clip");

    // Answers each subtitle request only when told to.
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QList<QPointer<QTcpSocket>> held;
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (QTcpSocket *socket = server.nextPendingConnection()) {
            held.append(socket);
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    const auto answerAll = [&held] {
        const QByteArray body = "1\n00:00:00,000 --> 00:00:19,000\nHeld\n";
        for (const QPointer<QTcpSocket> &socket : std::as_const(held)) {
            if (!socket)
                continue;
            socket->readAll();
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/x-subrip\r\n"
                          "Connection: close\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->disconnectFromHost();
        }
        held.clear();
    };
    const auto subtitleUrl = [&server](int index) {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/Subtitles/%2/Stream.srt")
                        .arg(server.serverPort())
                        .arg(index));
    };

    MpvPlayer player;
    player.setCoreIdleReleaseDelayForTests(60'000);
    QVERIFY(startCoreWithoutSound(player, 1));
    player.load(QUrl::fromLocalFile(clip), 0, 2, true);
    QTRY_COMPARE_WITH_TIMEOUT(player.state(), PlayerBackend::State::Paused, 15000);
    const auto playlistCount = [&player] {
        int64_t count = -1;
        mpv_get_property(player.handle(), "playlist-count", MPV_FORMAT_INT64, &count);
        return count;
    };

    // A sub-add on the wire: no prefetch.
    player.addExternalSubtitle(subtitleUrl(1), QStringLiteral("One"), QStringLiteral("eng"),
                               false);
    QTRY_COMPARE_WITH_TIMEOUT(held.size(), 1, 15000);
    QVERIFY(!player.prefetchNext(QUrl::fromLocalFile(next), 0, 3));
    QCOMPARE(playlistCount(), 1);

    // Landed: a prefetch is welcome again.
    answerAll();
    QTRY_COMPARE_WITH_TIMEOUT(player.subtitleTracks().size(), 1, 15000);
    QVERIFY(player.prefetchNext(QUrl::fromLocalFile(next), 0, 3));
    QCOMPARE(playlistCount(), 2);

    // A subtitle asked for now withdraws the queued file before it runs.
    player.addExternalSubtitle(subtitleUrl(2), QStringLiteral("Two"), QStringLiteral("eng"),
                               false);
    QCOMPARE(playlistCount(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(held.size(), 1, 15000);
    QVERIFY(!player.prefetchNext(QUrl::fromLocalFile(next), 0, 4));
    answerAll();
    QTRY_COMPARE_WITH_TIMEOUT(player.subtitleTracks().size(), 2, 15000);
    player.stop();
}

// mpv goes on to the appended entry by itself: under the prefetch's load id,
// with no load() and no end reported for the first file, from the start of
// the file whatever the first one was resumed at, on the audio output that was
// already open. A record in another sample rate gets an output of its own.
void MpvVideoItemTest::aPrefetchedTrackFollowsWithoutALoad()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString one = generateTone(dir, QStringLiteral("one.flac"), 44100);
    const QString two = generateTone(dir, QStringLiteral("two.flac"), 44100);
    const QString other = generateTone(dir, QStringLiteral("other.flac"), 48000);
    if (one.isEmpty() || two.isEmpty() || other.isEmpty())
        QSKIP("ffmpeg is required to generate the test tones");

    MpvPlayer player;
    player.setCoreIdleReleaseDelayForTests(60'000);
    QVERIFY(startCoreWithoutSound(player, 1));
    CoreObserver observer(player.handle());
    LoadEvents events(player);
    QSignalSpy ends(&player, &PlayerBackend::endReached);
    QSignalSpy handovers(&player, &PlayerBackend::prefetchStarted);
    QSignalSpy positions(&player, &PlayerBackend::positionChanged);

    // Resumed half a second in: a `start` that must not carry over.
    player.load(QUrl::fromLocalFile(one), 500, 2);
    QTRY_VERIFY_WITH_TIMEOUT(events.sawState(PlayerBackend::State::Playing, 2), 15000);
    QVERIFY(player.prefetchNext(QUrl::fromLocalFile(two), 0, 3));

    QTRY_COMPARE_WITH_TIMEOUT(handovers.count(), 1, 15000);
    QCOMPARE(handovers.at(0).at(0).value<PlayerBackend::LoadId>(), PlayerBackend::LoadId(2));
    QCOMPARE(handovers.at(0).at(1).value<PlayerBackend::LoadId>(), PlayerBackend::LoadId(3));
    QTRY_VERIFY_WITH_TIMEOUT(events.sawState(PlayerBackend::State::Playing, 3), 15000);
    QTRY_VERIFY(player.positionMs() > 0);
    QVERIFY2(ends.isEmpty(), "the first file's end was reported as well as the handover");
    QVERIFY(!events.sawState(PlayerBackend::State::Ended, 2));
    QVERIFY(events.errorIds().isEmpty());
    QCOMPARE(player.state(), PlayerBackend::State::Playing);

    observer.drain();
    QCOMPARE(observer.filesStarted(), 2);
    QCOMPARE(observer.prefetchedOpens(), 1); // opened while the first played out
    QCOMPARE(observer.audioOutputsOpened(), 1); // the same output throughout
    // From its own start, not from the first file's resume point.
    const auto firstOfSecond = std::find_if(observer.samples().cbegin(), observer.samples().cend(),
                                            [](const CoreObserver::Sample &s) { return s.file == 2; });
    QVERIFY(firstOfSecond != observer.samples().cend());
    QVERIFY2(firstOfSecond->position < 0.4, qPrintable(QString::number(firstOfSecond->position)));
    // No position of the second file was published under the first's id.
    for (const QList<QVariant> &event : positions) {
        if (event.at(1).value<PlayerBackend::LoadId>() == 2)
            QVERIFY(event.at(0).toLongLong() <= 2100);
    }

    // Another sample rate: weak gapless reopens the output at the new rate
    // rather than resampling the record to the first one's.
    QVERIFY(player.prefetchNext(QUrl::fromLocalFile(other), 0, 4));
    QTRY_COMPARE_WITH_TIMEOUT(handovers.count(), 2, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(events.sawState(PlayerBackend::State::Playing, 4), 15000);
    QTRY_COMPARE_WITH_TIMEOUT(observer.audioOutputsOpened(), 2, 5000);
    QVERIFY2(observer.audioOutputs().constLast().contains(QLatin1String("48000Hz")),
             qPrintable(observer.audioOutputs().constLast()));

    // The last file ends the ordinary way.
    QTRY_COMPARE_WITH_TIMEOUT(ends.count(), 1, 15000);
    QCOMPARE(ends.at(0).at(0).value<PlayerBackend::LoadId>(), PlayerBackend::LoadId(4));
    QCOMPARE(player.state(), PlayerBackend::State::Ended);
    QVERIFY(events.errorIds().isEmpty());

    // For comparison, the ordinary advance: a load() when the end is heard.
    connect(&player, &PlayerBackend::endReached, &player, [&](PlayerBackend::LoadId id) {
        if (id == 5)
            player.load(QUrl::fromLocalFile(two), 0, 6);
    });
    const int filesBefore = observer.filesStarted();
    player.load(QUrl::fromLocalFile(one), 0, 5);
    QTRY_VERIFY_WITH_TIMEOUT(events.sawState(PlayerBackend::State::Playing, 6), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(player.positionMs() > 700, 5000);
    observer.drain();
    // Measured, not asserted: the null device's clock is real, but a test
    // machine's scheduling is not.
    qInfo("change of file, measured over the playheads: gapless same format %.1f ms, "
          "gapless 44.1 -> 48 kHz %.1f ms, ordinary advance (endReached -> load) %.1f ms; "
          "%d audio outputs opened in all, %d underruns",
          observer.gapMs(1, 2.0), observer.gapMs(2, 2.0), observer.gapMs(filesBefore + 1, 2.0),
          observer.audioOutputsOpened(), observer.underruns());
    player.stop();
}

// Taken back before the end, it is not played, and the end is an ordinary one.
void MpvVideoItemTest::aWithdrawnPrefetchIsNotPlayed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString one = generateTone(dir, QStringLiteral("one.flac"), 44100, 1.0);
    const QString two = generateTone(dir, QStringLiteral("two.flac"), 44100, 1.0);
    if (one.isEmpty() || two.isEmpty())
        QSKIP("ffmpeg is required to generate the test tones");

    MpvPlayer player;
    player.setCoreIdleReleaseDelayForTests(60'000);
    QVERIFY(startCoreWithoutSound(player, 1));
    CoreObserver observer(player.handle());
    LoadEvents events(player);
    QSignalSpy ends(&player, &PlayerBackend::endReached);
    QSignalSpy handovers(&player, &PlayerBackend::prefetchStarted);

    player.load(QUrl::fromLocalFile(one), 0, 2);
    QTRY_VERIFY_WITH_TIMEOUT(events.sawState(PlayerBackend::State::Playing, 2), 15000);
    QVERIFY(player.prefetchNext(QUrl::fromLocalFile(two), 0, 3));
    // Replaced by another before it is withdrawn: only one is ever queued.
    QVERIFY(player.prefetchNext(QUrl::fromLocalFile(two), 0, 4));
    player.cancelPrefetch();

    QTRY_COMPARE_WITH_TIMEOUT(ends.count(), 1, 15000);
    QCOMPARE(ends.at(0).at(0).value<PlayerBackend::LoadId>(), PlayerBackend::LoadId(2));
    QCOMPARE(player.state(), PlayerBackend::State::Ended);
    QTest::qWait(300);
    QVERIFY(handovers.isEmpty());
    for (const QList<QVariant> &event : events.states)
        QVERIFY(event.at(1).value<PlayerBackend::LoadId>() <= 2);
    observer.drain();
    QCOMPARE(observer.filesStarted(), 1);
    QVERIFY(events.errorIds().isEmpty());

    // A load() drops a prefetch the same way: the new file plays, the
    // prefetched one never does.
    player.load(QUrl::fromLocalFile(one), 0, 5);
    QTRY_VERIFY_WITH_TIMEOUT(events.sawState(PlayerBackend::State::Playing, 5), 15000);
    QVERIFY(player.prefetchNext(QUrl::fromLocalFile(two), 0, 6));
    player.load(QUrl::fromLocalFile(one), 0, 7);
    QTRY_COMPARE_WITH_TIMEOUT(ends.count(), 2, 15000);
    QCOMPARE(ends.at(1).at(0).value<PlayerBackend::LoadId>(), PlayerBackend::LoadId(7));
    QTest::qWait(300);
    QVERIFY(handovers.isEmpty());
    QVERIFY(!events.sawState(PlayerBackend::State::Loading, 6));
    player.stop();
}

// The file ended while nobody was draining, and the entry was appended to a
// core that had already gone idle: mpv does not start it, and the end the
// player held back for it is reported after all.
void MpvVideoItemTest::aPrefetchTooLateForTheEndIsAnOrdinaryEnd()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString one = generateTone(dir, QStringLiteral("one.flac"), 44100, 0.5);
    const QString two = generateTone(dir, QStringLiteral("two.flac"), 44100, 0.5);
    if (one.isEmpty() || two.isEmpty())
        QSKIP("ffmpeg is required to generate the test tones");

    MpvPlayer player;
    player.setCoreIdleReleaseDelayForTests(60'000);
    QVERIFY(startCoreWithoutSound(player, 1));
    CoreObserver observer(player.handle());
    LoadEvents events(player);
    QSignalSpy ends(&player, &PlayerBackend::endReached);
    QSignalSpy handovers(&player, &PlayerBackend::prefetchStarted);

    player.load(QUrl::fromLocalFile(one), 0, 2);
    QTRY_VERIFY_WITH_TIMEOUT(events.sawState(PlayerBackend::State::Playing, 2), 15000);
    QThread::msleep(1500); // the tone plays out and mpv goes idle, undrained
    QCOMPARE(player.state(), PlayerBackend::State::Playing); // as far as anyone has heard
    QVERIFY(player.prefetchNext(QUrl::fromLocalFile(two), 0, 3));

    QTRY_COMPARE_WITH_TIMEOUT(ends.count(), 1, 5000);
    QCOMPARE(ends.at(0).at(0).value<PlayerBackend::LoadId>(), PlayerBackend::LoadId(2));
    QCOMPARE(player.state(), PlayerBackend::State::Ended);
    QTest::qWait(300);
    QVERIFY(handovers.isEmpty());
    QVERIFY(!events.sawState(PlayerBackend::State::Loading, 3));
    observer.drain();
    QCOMPARE(observer.filesStarted(), 1);

    // ...and the core is free for the ordinary advance.
    player.load(QUrl::fromLocalFile(two), 0, 4);
    QTRY_VERIFY_WITH_TIMEOUT(events.sawState(PlayerBackend::State::Playing, 4), 15000);
    QVERIFY(events.errorIds().isEmpty());
    player.stop();
}

int main(int argc, char *argv[])
{
    // The GL entry: the session's display when it has one. Without one (a
    // package build's ctest) it stays offscreen and the renderer cases skip.
    if (qEnvironmentVariableIsSet("STRMQT_TEST_NATIVE_GL") &&
        (qEnvironmentVariableIsSet("WAYLAND_DISPLAY") || qEnvironmentVariableIsSet("DISPLAY")))
        qunsetenv("QT_QPA_PLATFORM");
    QGuiApplication app(argc, argv);
    QTEST_SET_MAIN_SOURCE_PATH
    MpvVideoItemTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_mpv_video_item.moc"
