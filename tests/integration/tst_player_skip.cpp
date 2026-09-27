// A remote's ⏭ / ⏮: chapters first, then the queue (PlayerController::
// skipForward / skipBack). The same rule answers MPRIS Next/Previous, whose
// CanGoNext / CanGoPrevious read canSkipForward / canSkipBack.

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/controllers/PlayerController.h"
#include "app/models/MediaItemModel.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;

namespace {

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");

QVariantMap itemMap(const QString &id, const QString &type = QStringLiteral("Episode"))
{
    QVariantMap map;
    map.insert(QStringLiteral("itemId"), id);
    map.insert(QStringLiteral("name"), id);
    map.insert(QStringLiteral("label"), id);
    map.insert(QStringLiteral("type"), type);
    map.insert(QStringLiteral("runtimeMs"), 60'000);
    return map;
}

// Details with chapters at 0 s, 10 s and 20 s.
QByteArray detailsWithChapters(const QString &id, const QString &type)
{
    return QStringLiteral(R"json({
        "Id": "%1", "Name": "%1", "Type": "%2",
        "Chapters": [
            {"Name": "One", "StartPositionTicks": 0},
            {"Name": "Two", "StartPositionTicks": 100000000},
            {"Name": "Three", "StartPositionTicks": 200000000}
        ]
    })json")
        .arg(id, type)
        .toUtf8();
}

} // namespace

class PlayerSkipTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void chaptersComeBeforeTheQueue();
    void theLastChapterWithNothingQueuedIsTheEnd();
    void backRestartsAChapterThenStepsBack();
    void backFromTheFirstChapterIsTheQueuesRule();
    void videoWithoutChaptersStepsByItem();
    void musicStepsByTrackEvenWithChapters();
    void skipStateFollowsTheChapterUnderThePlayhead();
    void musicCanSkipBackOnceARestartWouldHappen();

    // ⏭ held: tap on release, hold to fast-forward.
    void aTapSkipsOnTheRelease();
    void aHoldSeeksWhereTheEngineHasNoSpeed();
    void aHoldUsesTheEnginesSpeedFirst();
    void theUsersMuteSurvivesAHold();
    void aHoldEndsWithTheSession();
    // Fixes after review.
    void aHoldDoesNotTripTheStallWatchdog();
    void aTranscodeHoldNeverSeeks();
    void aTranscodeHoldWithoutSpeedControlIsInert();
    void unknownLengthNeverSeeks();
    void speedReadoutsKeepTheUsersSpeed();
    void aStutteringRemoteSkipsOnce();
    void aHoldCutShortLendsTheNextItemNoGrace();

private:
    // Presses ⏭, then ticks the hold's timer at each of `ticks` (ms since the
    // press).
    void hold(const QList<qint64> &ticks);
    int progressReports() const;

    void addChapters(const QString &id, const QString &type = QStringLiteral("Episode"));
    // Starts the queue at `index` and waits for the item to be playing with its
    // chapters (when it has any) in place.
    void start(const QVariantList &items, int index, int expectedChapters);

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_controller = nullptr;
    qint64 m_now = 0;
};

void PlayerSkipTest::init()
{
    setEmbyImageSourceNamespace(QStringLiteral("test-session"));
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    for (const QString &id : {QStringLiteral("301001"), QStringLiteral("301002"),
                              QStringLiteral("301003")}) {
        QVERIFY(m_mock->addRouteFromFile(QStringLiteral("POST"),
                                         QStringLiteral("/Items/%1/PlaybackInfo").arg(id),
                                         fixturePath(QStringLiteral("playback_info.json"))));
    }
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing"), 204, {});
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Progress"), 204, {});
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped"), 204, {});

    m_client = new emby::EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setDeviceId(QStringLiteral("test-device"));
    m_client->setSession(kToken, kUserId);

    m_backend = new FakePlayerBackend(this);
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_settings = new Settings(m_dir->filePath(QStringLiteral("settings.ini")), this);
    m_controller = new PlayerController(m_client, m_backend, m_settings, this);
    m_controller->setTimingForTests(20, 2, 10);
    m_now = 0;
    m_controller->setSkipHoldClockForTests([this] { return m_now; });
}

void PlayerSkipTest::cleanup()
{
    delete m_controller;
    delete m_settings;
    delete m_dir;
    delete m_backend;
    delete m_client;
    delete m_mock;
    m_controller = nullptr;
    m_settings = nullptr;
    m_dir = nullptr;
    m_backend = nullptr;
    m_client = nullptr;
    m_mock = nullptr;
}

void PlayerSkipTest::addChapters(const QString &id, const QString &type)
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/%2").arg(kUserId, id),
                     200, detailsWithChapters(id, type));
}

void PlayerSkipTest::start(const QVariantList &items, int index, int expectedChapters)
{
    const qsizetype loadsBefore = m_backend->loadedUrls.size();
    m_controller->playQueue(items, index);
    QTRY_COMPARE(m_backend->loadedUrls.size(), loadsBefore + 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(60'000);
    if (expectedChapters > 0)
        QTRY_COMPARE(m_controller->chapters().size(), expectedChapters);
}

void PlayerSkipTest::chaptersComeBeforeTheQueue()
{
    addChapters(QStringLiteral("301001"));
    start({itemMap(QStringLiteral("301001")), itemMap(QStringLiteral("301002"))}, 0, 3);
    m_backend->simulatePosition(1'000);
    QCOMPARE(m_controller->currentChapter(), 0);
    QVERIFY(m_controller->canSkipForward());

    m_controller->skipForward();
    QCOMPARE(m_backend->seeks.constLast(), Q_INT64_C(10'000));
    QCOMPARE(m_controller->currentChapter(), 1);
    m_controller->skipForward();
    QCOMPARE(m_backend->seeks.constLast(), Q_INT64_C(20'000));
    QCOMPARE(m_controller->currentChapter(), 2);
    QCOMPARE(m_backend->loadedUrls.size(), 1);

    // Past the last chapter, ⏭ is the next episode.
    QVERIFY(m_controller->canSkipForward());
    m_controller->skipForward();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->queue()->currentIndex(), 1);
}

void PlayerSkipTest::theLastChapterWithNothingQueuedIsTheEnd()
{
    addChapters(QStringLiteral("301001"));
    start({itemMap(QStringLiteral("301001"))}, 0, 3);
    m_backend->simulatePosition(25'000);
    QCOMPARE(m_controller->currentChapter(), 2);
    QVERIFY(!m_controller->canSkipForward());

    const qsizetype seeks = m_backend->seeks.size();
    m_controller->skipForward();
    // Nothing: not a seek to the end, not a stop.
    QCOMPARE(m_backend->seeks.size(), seeks);
    QVERIFY(m_controller->active());
    QCOMPARE(m_backend->stopCalls, 0);
}

void PlayerSkipTest::backRestartsAChapterThenStepsBack()
{
    addChapters(QStringLiteral("301001"));
    start({itemMap(QStringLiteral("301001"))}, 0, 3);
    m_backend->simulatePosition(25'000);
    QVERIFY(m_controller->canSkipBack());

    // 5 s into chapter three: restart it.
    m_controller->skipBack();
    QCOMPARE(m_backend->seeks.constLast(), Q_INT64_C(20'000));
    // At its start now, so the next press steps back a chapter — without
    // waiting for the engine to report the restart.
    m_controller->skipBack();
    QCOMPARE(m_backend->seeks.constLast(), Q_INT64_C(10'000));
    // Within the restart window (2 s in) is still "step back".
    m_backend->simulatePosition(12'000);
    m_controller->skipBack();
    QCOMPARE(m_backend->seeks.constLast(), Q_INT64_C(0));
}

void PlayerSkipTest::backFromTheFirstChapterIsTheQueuesRule()
{
    addChapters(QStringLiteral("301002"));
    start({itemMap(QStringLiteral("301001")), itemMap(QStringLiteral("301002"))}, 1, 3);
    m_backend->simulatePosition(1'000);
    QCOMPARE(m_controller->currentChapter(), 0);

    // Nothing earlier in this item: the previous episode.
    m_controller->skipBack();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->queue()->currentIndex(), 0);
}

void PlayerSkipTest::videoWithoutChaptersStepsByItem()
{
    start({itemMap(QStringLiteral("301001")), itemMap(QStringLiteral("301002")),
           itemMap(QStringLiteral("301003"))},
          1, 0);
    QVERIFY(m_controller->chapters().isEmpty());
    QVERIFY(m_controller->canSkipForward());
    QVERIFY(m_controller->canSkipBack());

    m_controller->skipForward();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->queue()->currentIndex(), 2);
    QVERIFY(!m_controller->canSkipForward());

    // More than 5 s in, ⏮ restarts; then it steps back.
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(8'000);
    m_controller->skipBack();
    QCOMPARE(m_backend->seeks.constLast(), Q_INT64_C(0));
    QCOMPARE(m_controller->queue()->currentIndex(), 2);
    m_controller->skipBack();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 3);
    QCOMPARE(m_controller->queue()->currentIndex(), 1);
}

void PlayerSkipTest::musicStepsByTrackEvenWithChapters()
{
    const QString audio = QStringLiteral("Audio");
    addChapters(QStringLiteral("301001"), audio);
    start({itemMap(QStringLiteral("301001"), audio), itemMap(QStringLiteral("301002"), audio)},
          0, 3);
    QVERIFY(m_controller->isAudio());
    m_backend->simulatePosition(1'000);

    m_controller->skipForward();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->queue()->currentIndex(), 1);
}

void PlayerSkipTest::skipStateFollowsTheChapterUnderThePlayhead()
{
    QSignalSpy skipState(m_controller, &PlayerController::skipStateChanged);
    addChapters(QStringLiteral("301001"));
    start({itemMap(QStringLiteral("301001"))}, 0, 3);
    QVERIFY(skipState.count() > 0);
    QVERIFY(m_controller->canSkipForward());
    QVERIFY(m_controller->canSkipBack());

    skipState.clear();
    m_backend->simulatePosition(25'000);
    QVERIFY(skipState.count() > 0);
    QVERIFY(!m_controller->canSkipForward());

    m_controller->stop();
    QVERIFY(!m_controller->canSkipForward());
    QVERIFY(!m_controller->canSkipBack());
}

void PlayerSkipTest::musicCanSkipBackOnceARestartWouldHappen()
{
    const QString audio = QStringLiteral("Audio");
    start({itemMap(QStringLiteral("301001"), audio)}, 0, 0);
    QVERIFY(m_controller->isAudio());
    QVERIFY(!m_controller->hasPrevious());
    m_backend->simulatePosition(1'000);
    // The first track, just started: ⏮ has nothing to do.
    QVERIFY(!m_controller->canSkipBack());

    // 5 s in, ⏮ restarts the track — MPRIS CanGoPrevious must say so, and
    // hear about it when the threshold is crossed.
    QSignalSpy skipState(m_controller, &PlayerController::skipStateChanged);
    m_backend->simulatePosition(5'000);
    QVERIFY(m_controller->canSkipBack());
    QCOMPARE(skipState.count(), 1);
    m_backend->simulatePosition(6'000);
    QCOMPARE(skipState.count(), 1); // not on every tick

    m_controller->skipBack();
    QCOMPARE(m_backend->seeks.constLast(), Q_INT64_C(0));
    QVERIFY(!m_controller->canSkipBack());
    QCOMPARE(skipState.count(), 2);

    m_controller->stop();
    QVERIFY(!m_controller->canSkipBack());
}

void PlayerSkipTest::hold(const QList<qint64> &ticks)
{
    const qint64 pressedAt = m_now;
    m_controller->skipForwardPressed();
    QVERIFY(m_controller->skipHoldTimerActiveForTests());
    for (const qint64 tick : ticks) {
        m_now = pressedAt + tick;
        m_controller->tickSkipHoldForTests();
    }
}

int PlayerSkipTest::progressReports() const
{
    int count = 0;
    for (const MockEmbyServer::ReceivedRequest &request : m_mock->requests()) {
        if (request.method == QLatin1String("POST")
            && request.path == QLatin1String("/Sessions/Playing/Progress"))
            ++count;
    }
    return count;
}

void PlayerSkipTest::aTapSkipsOnTheRelease()
{
    addChapters(QStringLiteral("301001"));
    start({itemMap(QStringLiteral("301001"))}, 0, 3);
    m_backend->simulatePosition(1'000);

    const qsizetype seeks = m_backend->seeks.size();
    hold({100, 200, 300});
    // Nothing yet: until the release it may still be a hold.
    QCOMPARE(m_backend->seeks.size(), seeks);
    QCOMPARE(m_controller->fastForwardRate(), 0.0);
    m_now += 50;
    m_controller->skipForwardReleased();
    QCOMPARE(m_backend->seeks.constLast(), Q_INT64_C(10'000));
    QVERIFY(!m_controller->skipHoldTimerActiveForTests());
    QVERIFY(m_backend->speedRequests.isEmpty());
}

// VLC and Qt Multimedia have no speed control: the whole rate is seeking.
void PlayerSkipTest::aHoldSeeksWhereTheEngineHasNoSpeed()
{
    addChapters(QStringLiteral("301001"));
    start({itemMap(QStringLiteral("301001"))}, 0, 3);
    m_backend->simulateDuration(600'000);
    // An ordinary seek reports at once — the baseline the hold is held to.
    m_controller->seekTo(1'000);
    QTRY_VERIFY(progressReports() > 0);
    const int reportsBefore = progressReports();
    const qsizetype seeksBefore = m_backend->seeks.size();
    QSignalSpy rate(m_controller, &PlayerController::fastForwardRateChanged);

    hold({100, 200, 300, 400});
    QCOMPARE(m_controller->fastForwardRate(), 2.0);
    QCOMPARE(rate.count(), 1);
    // Silenced by volume, since this engine has no mute of its own.
    QCOMPARE(m_backend->volumeRequests.constLast(), 0);
    QCOMPARE(m_controller->muted(), false);

    hold({}); // a second press while held changes nothing
    m_now = 900;
    m_controller->tickSkipHoldForTests();
    // 500 ms at 2×, of which the engine played 1×: a 500 ms jump.
    QCOMPARE(m_backend->seeks.constLast(), Q_INT64_C(1'500));
    m_now = 1'400;
    m_controller->tickSkipHoldForTests();
    QCOMPARE(m_controller->fastForwardRate(), 4.0);
    QCOMPARE(m_backend->seeks.constLast(), Q_INT64_C(3'000)); // +3 × 500
    m_now = 4'400;
    m_controller->tickSkipHoldForTests();
    QCOMPARE(m_controller->fastForwardRate(), 32.0);
    QCOMPARE(m_backend->seeks.constLast(), Q_INT64_C(3'000 + 31 * 3'000));
    // Seeks while held are not each reported.
    QCOMPARE(progressReports(), reportsBefore);

    m_now = 4'500;
    m_controller->skipForwardReleased();
    QCOMPARE(m_controller->fastForwardRate(), 0.0);
    QCOMPARE(m_backend->volumeRequests.constLast(), m_controller->volume());
    QVERIFY(m_backend->speedRequests.isEmpty());
    // No chapter skip on the release of a hold — the last seek is the hold's.
    QCOMPARE(m_backend->seeks.size(), seeksBefore + 3);
    QCOMPARE(m_controller->queue()->currentIndex(), 0);
    // One report, with the position the hold reached.
    QTRY_COMPARE(progressReports(), reportsBefore + 1);
}

// mpv: real playback at up to 4×, seeking only for what is beyond it, and the
// speed the user had comes back on release.
void PlayerSkipTest::aHoldUsesTheEnginesSpeedFirst()
{
    m_backend->maxSpeed = 4.0;
    m_backend->muteSupported = true;
    start({itemMap(QStringLiteral("301001"))}, 0, 0);
    m_backend->simulateDuration(600'000);
    m_backend->simulatePosition(1'000);
    m_controller->setPlaybackSpeed(1.5);

    hold({400});
    QCOMPARE(m_backend->speedRequests.constLast(), 2.0);
    QCOMPARE(m_backend->muteRequests.constLast(), true);
    const qsizetype seeks = m_backend->seeks.size();
    m_now = 900;
    m_controller->tickSkipHoldForTests();
    QCOMPARE(m_backend->seeks.size(), seeks); // the engine is doing all of 2×
    m_now = 1'400;
    m_controller->tickSkipHoldForTests();
    QCOMPARE(m_backend->speedRequests.constLast(), 4.0);
    m_now = 2'400;
    m_controller->tickSkipHoldForTests();
    QCOMPARE(m_controller->fastForwardRate(), 8.0);
    QCOMPARE(m_backend->speedRequests.constLast(), 4.0); // the engine's ceiling
    // 8× for 1000 ms since the last step, the engine covering 4×.
    QCOMPARE(m_backend->seeks.constLast(), Q_INT64_C(1'000 + 4 * 1'000));

    m_controller->skipForwardReleased();
    QCOMPARE(m_backend->speedRequests.constLast(), 1.5);
    QCOMPARE(m_backend->muteRequests.constLast(), false);
    QCOMPARE(m_controller->muted(), false);
}

void PlayerSkipTest::theUsersMuteSurvivesAHold()
{
    m_backend->muteSupported = true;
    start({itemMap(QStringLiteral("301001"))}, 0, 0);
    m_controller->setMuted(true);

    hold({400});
    // The engine's echo of the hold's own mute is not adopted as the user's…
    emit m_backend->mutedChanged(true);
    m_controller->skipForwardReleased();
    // …and the release puts back the user's mute, not "unmuted".
    QCOMPARE(m_controller->muted(), true);
    QCOMPARE(m_backend->muteRequests.constLast(), true);

    m_controller->setMuted(false);
    m_now += 1'000; // a fresh press, well clear of the release debounce
    hold({400});
    emit m_backend->mutedChanged(true);
    QCOMPARE(m_controller->muted(), false);
    m_controller->skipForwardReleased();
    QCOMPARE(m_backend->muteRequests.constLast(), false);
}

void PlayerSkipTest::aHoldEndsWithTheSession()
{
    m_backend->maxSpeed = 4.0;
    start({itemMap(QStringLiteral("301001")), itemMap(QStringLiteral("301002"))}, 0, 0);
    hold({400, 1'400});
    QCOMPARE(m_controller->fastForwardRate(), 4.0);

    m_controller->stop();
    QCOMPARE(m_controller->fastForwardRate(), 0.0);
    QCOMPARE(m_backend->speedRequests.constLast(), 1.0);
    QVERIFY(!m_controller->skipHoldTimerActiveForTests());

    // The key's release, arriving after, is nobody's: no skip, no restart.
    const qsizetype loads = m_backend->loadedUrls.size();
    m_controller->skipForwardReleased();
    QCOMPARE(m_backend->loadedUrls.size(), loads);
}

// The watchdog ticks every 20 ms here and escalates after two still ticks
// (setTimingForTests). A hold whose seeks leave the position frozen must not
// be read as a stall — no nudge-seek, no reload, no demotion — and once the
// hold (and its grace) is over, a real freeze still is.
void PlayerSkipTest::aHoldDoesNotTripTheStallWatchdog()
{
    start({itemMap(QStringLiteral("301001"))}, 0, 0);
    m_backend->simulateDuration(600'000);
    m_backend->simulatePosition(1'000);
    hold({400});
    QCOMPARE(m_controller->fastForwardRate(), 2.0);
    const qsizetype seeks = m_backend->seeks.size();
    const qsizetype loads = m_backend->loadedUrls.size();
    const QString method = m_controller->streamMethod();

    QTest::qWait(300); // fifteen watchdog ticks, position never moves
    QCOMPARE(m_backend->seeks.size(), seeks);
    QCOMPARE(m_backend->loadedUrls.size(), loads);
    QCOMPARE(m_controller->streamMethod(), method);
    QVERIFY(m_controller->active());

    // The control: the same freeze after the hold does escalate.
    m_now += 100;
    m_controller->skipForwardReleased();
    QTRY_VERIFY(m_backend->seeks.size() > seeks || m_backend->loadedUrls.size() > loads);
}

void PlayerSkipTest::aTranscodeHoldNeverSeeks()
{
    QVERIFY(m_mock->addRouteFromFile(
        QStringLiteral("POST"), QStringLiteral("/Items/301005/PlaybackInfo"),
        fixturePath(QStringLiteral("playback_info_transcode_only.json"))));
    m_backend->maxSpeed = 4.0;
    start({itemMap(QStringLiteral("301005"))}, 0, 0);
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("Transcode"));
    m_backend->simulateDuration(600'000);
    m_backend->simulatePosition(1'000);
    const qsizetype seeks = m_backend->seeks.size();

    hold({400, 900, 1'400, 2'400, 3'400, 4'400, 4'900});
    // Capped at what the engine plays; the ramp's 32× is not claimed.
    QCOMPARE(m_backend->speedRequests.constLast(), 4.0);
    QCOMPARE(m_controller->fastForwardRate(), 4.0);
    QCOMPARE(m_backend->seeks.size(), seeks);
    m_controller->skipForwardReleased();
    QCOMPARE(m_backend->speedRequests.constLast(), 1.0);
}

void PlayerSkipTest::aTranscodeHoldWithoutSpeedControlIsInert()
{
    QVERIFY(m_mock->addRouteFromFile(
        QStringLiteral("POST"), QStringLiteral("/Items/301005/PlaybackInfo"),
        fixturePath(QStringLiteral("playback_info_transcode_only.json"))));
    start({itemMap(QStringLiteral("301005"))}, 0, 0);
    const qsizetype seeks = m_backend->seeks.size();
    const qsizetype volumes = m_backend->volumeRequests.size();

    hold({400, 1'400, 2'400});
    // Nothing it could do faster: no silence, no rate, no seek…
    QCOMPARE(m_controller->fastForwardRate(), 0.0);
    QCOMPARE(m_backend->volumeRequests.size(), volumes);
    QCOMPARE(m_backend->seeks.size(), seeks);
    // …and letting go is still not a skip.
    m_controller->skipForwardReleased();
    QCOMPARE(m_backend->seeks.size(), seeks);
}

// A raw URL (no ticket, no length reported): nothing to seek ahead into.
void PlayerSkipTest::unknownLengthNeverSeeks()
{
    m_backend->maxSpeed = 4.0;
    m_controller->playUrl(QUrl(QStringLiteral("http://127.0.0.1:1/live.ts")),
                          QStringLiteral("Live"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    QTRY_VERIFY(m_controller->active());
    QVERIFY(m_controller->durationMs() <= 0);
    m_backend->simulatePosition(1'000);
    const qsizetype seeks = m_backend->seeks.size();

    hold({400, 1'400, 2'400, 2'900});
    QCOMPARE(m_controller->fastForwardRate(), 4.0);
    QCOMPARE(m_backend->seeks.size(), seeks);
    m_controller->skipForwardReleased();
}

// MPRIS Rate, the web remote and the playback-settings panel read
// playbackSpeed / playbackSpeedChanged; a hold is not a speed setting.
void PlayerSkipTest::speedReadoutsKeepTheUsersSpeed()
{
    m_backend->maxSpeed = 4.0;
    start({itemMap(QStringLiteral("301001"))}, 0, 0);
    m_controller->setPlaybackSpeed(1.25);
    QCOMPARE(m_controller->playbackSpeed(), 1.25);
    QSignalSpy speed(m_controller, &PlayerController::playbackSpeedChanged);

    hold({400, 1'400});
    QCOMPARE(m_backend->playbackSpeed(), 4.0);
    QCOMPARE(m_controller->playbackSpeed(), 1.25);
    QCOMPARE(speed.count(), 0);

    m_controller->skipForwardReleased();
    QCOMPARE(m_controller->playbackSpeed(), 1.25);
    QVERIFY(speed.count() >= 1);
}

// A remote that sends press/release pairs every 40 ms while ⏭ is held gives
// one chapter, not one per pair.
void PlayerSkipTest::aStutteringRemoteSkipsOnce()
{
    addChapters(QStringLiteral("301001"));
    start({itemMap(QStringLiteral("301001"))}, 0, 3);
    m_backend->simulatePosition(1'000);
    const qsizetype seeks = m_backend->seeks.size();

    for (qint64 t = 0; t < 1'000; t += 80) {
        m_now = t;
        m_controller->skipForwardPressed();
        m_now = t + 40;
        m_controller->skipForwardReleased();
    }
    QCOMPARE(m_backend->seeks.size(), seeks + 1);
    QCOMPARE(m_controller->currentChapter(), 1);
}

// A hold cut short by an item change leaves nothing behind: the next item's
// watchdog still escalates a frozen position. (The grace is two 20 ms ticks,
// so this pins the behaviour, not the exact tick count.)
void PlayerSkipTest::aHoldCutShortLendsTheNextItemNoGrace()
{
    start({itemMap(QStringLiteral("301001")), itemMap(QStringLiteral("301002"))}, 0, 0);
    m_backend->simulateDuration(600'000);
    m_backend->simulatePosition(1'000);
    hold({400});
    QCOMPARE(m_controller->fastForwardRate(), 2.0);

    const qsizetype loads = m_backend->loadedUrls.size();
    m_controller->skipForward(); // the next item, mid-hold
    QTRY_VERIFY(m_backend->loadedUrls.size() > loads);
    QCOMPARE(m_controller->fastForwardRate(), 0.0);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(600'000);
    m_backend->simulatePosition(5'000);

    const qsizetype seeks = m_backend->seeks.size();
    const qsizetype reloads = m_backend->loadedUrls.size();
    QTRY_VERIFY(m_backend->seeks.size() > seeks || m_backend->loadedUrls.size() > reloads);
}

QTEST_GUILESS_MAIN(PlayerSkipTest)
#include "tst_player_skip.moc"
