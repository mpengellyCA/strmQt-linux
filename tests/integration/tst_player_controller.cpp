#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/controllers/PlayerController.h"
#include "core/Settings.h"
#include "playback/upscale/UpscaleCatalog.h"
#include "playback/upscale/UpscaleGovernor.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;

namespace {

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");

// A real engine publishes the post-seek position asynchronously; the fake
// applies it inside seekTo(), which hides exactly the staleness the seek tests
// below are about.
class LazySeekBackend : public FakePlayerBackend
{
public:
    using FakePlayerBackend::FakePlayerBackend;
    void seekTo(qint64 positionMs) override { seeks.append(positionMs); }
};

// Real engines may publish the outgoing load id with their stop transition.
// PlayerController must not rely on that stale event to clear new-session
// state after it has invalidated the old load.
class OldLoadIdStopBackend : public FakePlayerBackend
{
public:
    using FakePlayerBackend::FakePlayerBackend;

    void stop() override
    {
        ++stopCalls;
        Q_ASSERT(!loadedIds.isEmpty());
        simulateState(State::Idle, loadedIds.constLast());
    }
};

} // namespace

class PlayerControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void directPlayStartsAndReports();
    void watchdogNudgesReloadsThenDemotes();
    void reloadThenErrorPreservesResumePosition();
    void errorNearTailIsNotAConfirmedEnd();
    void midStreamErrorRefetchesTicket();
    void recoveryBurstStartsOnlyOneRefresh();
    void crashResumePersistsAndClears();
    void ladderDemotesOnStartupFailure();
    void transcodeResumeStartsTheEngineAtTheResumePoint();
    void allRungsFailingSurfacesError();
    void endReachedReportsFullRuntime();
    void stopReportsAndDeactivates();
    void controlledShutdownDrainsAndClearsResume();
    void staleEventsCannotControlAReplacement();
    void failedHandoffIsIdleAndPreservesCrashResume();
    void stopWhileResolvingCancelsTheLoad();
    void engineReadyTracksAcceptedBackendState();
    void rawUrlHandoffClearsEngineReadyWhileResolving();
    void seekAndPauseReportProgress();
    void seekAdoptsItsTargetBeforeTheEngineReportsIt();
    void setPausedIsAbsoluteAndSeeksAnnounceThemselves();
    void trackPersistenceWaitsForBackendReadback();
    void externalSubtitlesAreAttachedOncePerLoad();
    void directPlaySkipsSubtitlesTheFileAlreadyCarries();
    void rememberedChoiceOutranksTheServerDefaultSubtitle();
    void rememberedExternalSubtitleWaitsForItsTrack();
    void pickingASubtitleCancelsTheWaitForARememberedOne();
    void playbackSettingsFollowBackendReadback();
    void screenshotSuccessRequiresBackendConfirmation();
    void positionSnapshotsBoundQmlChurn();
    void chaptersAreOrderedByStart();

    void replayGainReachesTheEngineOnLoadAndOnChange();

    void demotionStaysWithinSelectedSource();
    void preferredSourceHonouredAtStart();
    void setPreferredSourceSwitchesVersionMidSession();
    void reloadStreamRefetchesTicketInPlace();
    void sourceSurfaceExposesStreams();
    void unplayableSourceSelectionIsIgnored();
    void upscaleFollowsLoadResizeAndOverride();
    void overrideResetsOnTheNextItem();
    void backendWithoutShadersIsUnavailable();
    void softwareRendererIsUnavailable();
    void animeGenresPickTheAnimationLadder();
    void episodeWithoutGenresAsksItsSeries();
    void shaderFailureSkipsTheEntry();
    void pipShrinkDropsChainAndRestoresIt();
    void audioAfterFilmClearsTheChain();
    void resizeStormPushesOnce();
    void seekStartsTheGovernorGrace();
    void watchdogNudgeStartsTheGovernorGrace();
    void playbackSpeedNeverStepsTheCeiling();

    void gaplessPrefetchWaitsForTheTailOfAMusicTrack();
    void gaplessPrefetchOnlyForMusicIntoMusicInADeliberateQueue();
    void gaplessHandoverMatchesAnOrdinaryAdvance();
    void gaplessPrefetchFollowsWhatIsNext();
    void gaplessPrefetchIsDroppedWithTheTrack();
    void lateNextTrackTicketIsIgnored();
    void failedGaplessPrefetchFallsBack();
    void engineGoingOnToAnUnwantedTrackIsReplaced();

private:
    void expectReport(const QString &path, const QString &method, bool paused = false);
    void startSidecarItem();
    void startUpscaling();
    // Gapless helpers. Track n of an album is item "50100n", with its own
    // ticket (PlaySessionId "ps50100n") and URLs naming it.
    void addAudioItem(const QString &id);
    QVariantMap audioEntry(const QString &id, const QString &name) const;
    QVariantList album(int tracks);
    // Plays `tracks` of the album on a prefetching engine, the first one
    // Playing at 10 s of 200 s. The watchdog is parked: these tests move the
    // playhead by hand, and a reload it started would be a load of its own.
    void startGaplessAlbum(int tracks);
    // Into the last 15 s and until the engine holds the next track.
    void reachQueuedPrefetch();
    int requestCount(const QString &method, const QString &path) const;
    int playbackInfoRequests(const QString &itemId) const;
    QJsonObject lastBody(const QString &path) const;

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_controller = nullptr;
    qint64 m_upscaleNow = 0;
};

void PlayerControllerTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("POST"),
                                     QStringLiteral("/Items/301001/PlaybackInfo"),
                                     fixturePath(QStringLiteral("playback_info.json"))));
    QVERIFY(m_mock->addRouteFromFile(
        QStringLiteral("POST"), QStringLiteral("/Items/4242/PlaybackInfo"),
        fixturePath(QStringLiteral("playback_info_multi_version.json"))));
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing"), 204, {});
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Progress"), 204, {});
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped"), 204, {});

    m_client = new emby::EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setDeviceId(QStringLiteral("test-device"));
    m_client->setSession(kToken, kUserId);

    m_backend = new FakePlayerBackend(this);
    m_dir = new QTemporaryDir;
    m_settings = new Settings(m_dir->filePath(QStringLiteral("settings.ini")), this);
    m_settings->setServerUrl(m_mock->baseUrl());
    m_settings->setUserId(kUserId);
    m_controller = new PlayerController(m_client, m_backend, m_settings, this);
    m_controller->setTimingForTests(20, 2, 10);
}

void PlayerControllerTest::cleanup()
{
    delete m_controller;
    delete m_settings;
    delete m_dir;
    m_settings = nullptr;
    m_dir = nullptr;
    delete m_backend;
    delete m_client;
    delete m_mock;
    m_controller = nullptr;
    m_backend = nullptr;
    m_client = nullptr;
    m_mock = nullptr;
}

void PlayerControllerTest::expectReport(const QString &path, const QString &method, bool paused)
{
    const auto request = m_mock->lastRequestFor(QStringLiteral("POST"), path);
    QVERIFY2(!request.method.isEmpty(), qPrintable(QStringLiteral("no request to ") + path));
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    QCOMPARE(body.value(QLatin1String("ItemId")).toString(), QStringLiteral("301001"));
    QCOMPARE(body.value(QLatin1String("PlaySessionId")).toString(), QStringLiteral("ps0011"));
    QCOMPARE(body.value(QLatin1String("PlayMethod")).toString(), method);
    QCOMPARE(body.value(QLatin1String("IsPaused")).toBool(), paused);
}

void PlayerControllerTest::screenshotSuccessRequiresBackendConfirmation()
{
    m_controller->setScreenshotDirectoryForTests(m_dir->filePath(QStringLiteral("screenshots")));
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);

    QSignalSpy saved(m_controller, &PlayerController::screenshotSaved);
    QSignalSpy failed(m_controller, &PlayerController::screenshotFailed);
    const QString accepted = m_controller->takeScreenshot();
    QVERIFY(!accepted.isEmpty());
    QCOMPARE(saved.count(), 1);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(m_backend->screenshots.last(), accepted);

    m_backend->screenshotSucceeds = false;
    const QString rejected = m_controller->takeScreenshot();
    QVERIFY(rejected.isEmpty());
    QCOMPARE(saved.count(), 1);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(m_backend->screenshots.size(), 2);
}

void PlayerControllerTest::chaptersAreOrderedByStart()
{
    // Emby lists chapters in whatever order they were stored. The current
    // chapter lookup, chapter stepping and the phone remote all assume start
    // order, so the controller sorts them once on arrival.
    const QByteArray details = R"json({
        "Id": "301001",
        "Name": "The Matrix",
        "Type": "Movie",
        "Chapters": [
            {"Name": "Second scene", "StartPositionTicks": 20000000},
            {"Name": "Opening", "StartPositionTicks": 0},
            {"Name": "First scene", "StartPositionTicks": 5000000}
        ]
    })json";
    m_mock->addRoute(QStringLiteral("GET"),
                     QStringLiteral("/Users/%1/Items/301001").arg(kUserId), 200, details);

    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    QTRY_COMPARE(m_controller->chapters().size(), 3);
    const QVariantList chapters = m_controller->chapters();
    QCOMPARE(chapters.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Opening"));
    QCOMPARE(chapters.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("First scene"));
    QCOMPARE(chapters.at(2).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Second scene"));
    QCOMPARE(m_controller->currentChapter(), 0);

    m_backend->simulatePosition(600);
    QCOMPARE(m_controller->currentChapter(), 1);
    m_backend->simulatePosition(2500);
    QCOMPARE(m_controller->currentChapter(), 2);
}

void PlayerControllerTest::positionSnapshotsBoundQmlChurn()
{
    const QByteArray details = R"json({
        "Id": "301001",
        "Name": "The Matrix",
        "Type": "Movie",
        "Chapters": [
            {"Name": "Opening", "StartPositionTicks": 0},
            {"Name": "First scene", "StartPositionTicks": 5000000},
            {"Name": "Second scene", "StartPositionTicks": 20000000}
        ]
    })json";
    m_mock->addRoute(QStringLiteral("GET"),
                     QStringLiteral("/Users/%1/Items/301001").arg(kUserId), 200, details);

    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    QTRY_COMPARE(m_controller->chapters().size(), 3);
    QCOMPARE(m_controller->currentChapter(), 0);

    QSignalSpy rawPosition(m_controller, &PlayerController::positionChanged);
    QSignalSpy wholeSeconds(m_controller, &PlayerController::positionSecondsChanged);
    QSignalSpy chapter(m_controller, &PlayerController::currentChapterChanged);
    QSignalSpy buffered(m_controller, &PlayerController::bufferedEndChanged);

    // The smooth playhead remains frame-rate capable, while text and internal
    // chapter work stay quiet inside a whole second / 250 ms sample window.
    m_backend->simulatePosition(100);
    m_backend->simulatePosition(200);
    m_backend->simulatePosition(249);
    QCOMPARE(rawPosition.count(), 3);
    QCOMPARE(wholeSeconds.count(), 0);
    QCOMPARE(chapter.count(), 0);

    m_backend->simulatePosition(260);
    m_backend->simulatePosition(490);
    m_backend->simulatePosition(510);
    QCOMPARE(m_controller->currentChapter(), 1);
    QCOMPARE(chapter.count(), 1);

    m_backend->simulatePosition(1001);
    m_backend->simulatePosition(1010);
    m_backend->simulatePosition(1090);
    QCOMPARE(m_controller->positionSeconds(), Q_INT64_C(1));
    QCOMPARE(wholeSeconds.count(), 1);

    // The buffered fill snapshots an absolute endpoint only when the backend's
    // buffer estimate changes. Raw playhead frames no longer drag it forward.
    m_backend->simulateBufferedMs(5000);
    QCOMPARE(m_controller->bufferedEndMs(), Q_INT64_C(6090));
    QCOMPARE(buffered.count(), 1);
    m_backend->simulatePosition(1200);
    QCOMPARE(m_controller->bufferedEndMs(), Q_INT64_C(6090));
    QCOMPARE(buffered.count(), 1);
    // mpv suppresses bufferedMsChanged while the relative amount stays within
    // its epsilon. The controller's coarse position sample must still advance
    // the absolute endpoint without inventing frame-rate QML work.
    m_backend->simulatePosition(1300);
    QCOMPARE(m_controller->bufferedEndMs(), Q_INT64_C(6300));
    QCOMPARE(buffered.count(), 2);

    // A seek is a forced sample, including with a backend that keeps reporting
    // the same relative buffer-ahead value.
    m_controller->seekTo(10'000);
    QCOMPARE(m_controller->bufferedEndMs(), Q_INT64_C(15'000));
    QCOMPARE(buffered.count(), 3);
}

void PlayerControllerTest::directPlayStartsAndReports()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QVERIFY(m_controller->active());
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    QVERIFY(m_backend->loadedUrls[0].path().endsWith(QStringLiteral("stream.mkv")));
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("DirectPlay"));

    m_backend->simulateState(PlayerBackend::State::Playing);
    QTRY_VERIFY(!m_controller->busy());
    QTRY_VERIFY(!m_mock->lastRequestFor(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing"))
                     .method.isEmpty());
    expectReport(QStringLiteral("/Sessions/Playing"), QStringLiteral("DirectPlay"));
}

void PlayerControllerTest::ladderDemotesOnStartupFailure()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 60'000);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    QCOMPARE(m_backend->loadedStarts[0], Q_INT64_C(60000));

    m_backend->simulateError(QStringLiteral("network stream error"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QVERIFY(m_backend->loadedUrls[1].path().endsWith(QStringLiteral("stream.mkv")));
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("DirectStream"));
    QVERIFY(m_controller->active());

    m_backend->simulateError(QStringLiteral("still failing"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 3);
    QVERIFY(m_backend->loadedUrls[2].path().endsWith(QStringLiteral("master.m3u8")));
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("Transcode"));
}

// A Transcode rung is resumed the way the other two are: the engine is told
// where to start. Emby is given StartTimeTicks in the PlaybackInfo request, but
// the HLS TranscodingUrl it answers with carries no offset of its own and
// describes the whole runtime on the item's own timeline — the recorded
// fixtures show the first, and seeking and position reports on a transcode
// rely on the second. Withholding the start from the engine would therefore
// play a resumed transcode from 0:00 (AUDIT.md §4.4 proposed exactly that).
void PlayerControllerTest::transcodeResumeStartsTheEngineAtTheResumePoint()
{
    QVERIFY(m_mock->addRouteFromFile(
        QStringLiteral("POST"), QStringLiteral("/Items/999/PlaybackInfo"),
        fixturePath(QStringLiteral("playback_info_transcode_only.json"))));
    m_controller->setTimingForTests(20, 1000, 10); // not about the watchdog

    m_controller->playItem(QStringLiteral("999"), QStringLiteral("Transcode only"), 60'000);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("Transcode"));
    QVERIFY(m_backend->loadedUrls[0].path().endsWith(QStringLiteral("master.m3u8")));

    // The server was told the resume point...
    const QJsonObject asked =
        QJsonDocument::fromJson(m_mock
                                    ->lastRequestFor(QStringLiteral("POST"),
                                                     QStringLiteral("/Items/999/PlaybackInfo"))
                                    .body)
            .object();
    QCOMPARE(static_cast<qint64>(asked.value(QLatin1String("StartTimeTicks")).toDouble()),
             Q_INT64_C(600000000));
    // ...its URL does not start there by itself...
    QVERIFY(!m_backend->loadedUrls[0].query().contains(QLatin1String("StartTimeTicks"),
                                                       Qt::CaseInsensitive));
    // ...so the engine is.
    QCOMPARE(m_backend->loadedStarts[0], Q_INT64_C(60000));

    // And the engine's position is the item's position: no offset is added to
    // what a transcode reports.
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(61'000);
    m_controller->togglePause();
    QTRY_VERIFY([this] {
        const auto request = m_mock->lastRequestFor(QStringLiteral("POST"),
                                                    QStringLiteral("/Sessions/Playing/Progress"));
        const QJsonObject body = QJsonDocument::fromJson(request.body).object();
        return body.value(QLatin1String("IsPaused")).toBool() &&
               static_cast<qint64>(body.value(QLatin1String("PositionTicks")).toDouble()) ==
                   Q_INT64_C(610000000);
    }());
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("Transcode"));
}

void PlayerControllerTest::allRungsFailingSurfacesError()
{
    QSignalSpy stoppedSpy(m_controller, &PlayerController::stopped);
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);

    m_backend->simulateError(QStringLiteral("e1"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    m_backend->simulateError(QStringLiteral("e2"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 3);
    m_backend->simulateError(QStringLiteral("e3"));

    QTRY_VERIFY(!m_controller->active());
    QCOMPARE(stoppedSpy.count(), 1);
    QVERIFY(m_controller->errorMessage().contains(QStringLiteral("e3")));
}

void PlayerControllerTest::endReachedReportsFullRuntime()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(8'184'000);
    m_backend->simulatePosition(8'100'000);

    QSignalSpy stoppedSpy(m_controller, &PlayerController::stopped);
    m_backend->simulateEnd();
    QTRY_COMPARE(stoppedSpy.count(), 1);
    QVERIFY(!m_controller->active());

    // The stopped report is an async POST; wait for it to reach the mock.
    QTRY_VERIFY(
        !m_mock->lastRequestFor(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped"))
             .method.isEmpty());
    const auto request =
        m_mock->lastRequestFor(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped"));
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    // Position must be pinned to the full runtime so the server marks it played.
    QCOMPARE(static_cast<qint64>(body.value(QLatin1String("PositionTicks")).toDouble()),
             Q_INT64_C(8184000) * kTicksPerMs);
}

void PlayerControllerTest::stopReportsAndDeactivates()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(120'000);

    m_controller->stop();
    QCOMPARE(m_backend->stopCalls, 1);
    QVERIFY(!m_controller->active());
    QTRY_VERIFY(
        !m_mock->lastRequestFor(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped"))
             .method.isEmpty());
    expectReport(QStringLiteral("/Sessions/Playing/Stopped"), QStringLiteral("DirectPlay"));
}

void PlayerControllerTest::controlledShutdownDrainsAndClearsResume()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(300'000);

    // Enter a watchdog reload: m_started is temporarily false, but the server
    // session opened by the original rung still needs its final stop report.
    QTRY_VERIFY(m_backend->seeks.contains(Q_INT64_C(301000)));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);

    const QFuture<Result<bool>> drain = m_controller->shutdownForApplicationExit();
    QVERIFY(drain.isValid());
    QTRY_VERIFY(drain.isFinished());
    QVERIFY(!m_controller->active());
    QCOMPARE(m_backend->state(), PlayerBackend::State::Idle);
    QVERIFY(m_controller->crashResumeInfo().isEmpty());

    QTRY_VERIFY(
        !m_mock->lastRequestFor(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped"))
             .method.isEmpty());
    expectReport(QStringLiteral("/Sessions/Playing/Stopped"), QStringLiteral("DirectPlay"));
    const QJsonObject body = QJsonDocument::fromJson(
                                 m_mock
                                     ->lastRequestFor(
                                         QStringLiteral("POST"),
                                         QStringLiteral("/Sessions/Playing/Stopped"))
                                     .body)
                                 .object();
    QCOMPARE(static_cast<qint64>(body.value(QLatin1String("PositionTicks")).toDouble()),
             Q_INT64_C(301000) * kTicksPerMs);

    QFile durable(m_dir->filePath(QStringLiteral("settings.ini")));
    QVERIFY(durable.open(QIODevice::ReadOnly));
    QVERIFY(!durable.readAll().contains("positionMs="));
}

void PlayerControllerTest::staleEventsCannotControlAReplacement()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0,
                           -1, QStringLiteral("Movie"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    const PlayerBackend::LoadId oldLoad = m_backend->loadedIds.constFirst();
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(8'184'000);
    m_backend->simulatePosition(120'000);

    m_mock->setRouteDelay(QStringLiteral("POST"), QStringLiteral("/Items/4242/PlaybackInfo"),
                          250);
    QSignalSpy stoppedSpy(m_controller, &PlayerController::stopped);
    m_controller->playItem(QStringLiteral("4242"), QStringLiteral("Dune"), 0, -1,
                           QStringLiteral("Movie"));

    // Replacement resolution owns no backend load. The outgoing engine and all
    // of its item-scoped observables are already quiescent.
    QVERIFY(m_controller->active());
    QVERIFY(m_controller->busy());
    QCOMPARE(m_backend->state(), PlayerBackend::State::Idle);
    QCOMPARE(m_controller->durationMs(), Q_INT64_C(0));
    QCOMPARE(m_controller->positionMs(), Q_INT64_C(0));
    QCOMPARE(m_controller->sourceCount(), 0);

    // Events queued by the outgoing load cannot advance, recover, or repopulate
    // the pending item's timeline.
    m_backend->simulatePosition(777'000, oldLoad);
    m_backend->simulateDuration(999'000, oldLoad);
    m_backend->simulateState(PlayerBackend::State::Playing, oldLoad);
    m_backend->simulateError(QStringLiteral("late A error"), oldLoad);
    m_backend->simulateEnd(oldLoad);
    QCOMPARE(stoppedSpy.count(), 0);
    QVERIFY(m_controller->active());
    QVERIFY(m_controller->busy());
    QCOMPARE(m_controller->positionMs(), Q_INT64_C(0));
    QCOMPARE(m_controller->durationMs(), Q_INT64_C(0));

    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    const PlayerBackend::LoadId newLoad = m_backend->loadedIds.constLast();
    QVERIFY(newLoad != oldLoad);
    m_backend->simulateEnd(oldLoad);
    QCOMPARE(stoppedSpy.count(), 0);
    QVERIFY(m_controller->active());
    QCOMPARE(m_backend->loadedUrls.size(), 2);
}

void PlayerControllerTest::failedHandoffIsIdleAndPreservesCrashResume()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(300'000);
    QCOMPARE(m_controller->crashResumeInfo().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301001"));

    // No PlaybackInfo route exists for this id, so the handoff fails after the
    // outgoing load was stopped.
    m_controller->playItem(QStringLiteral("missing"), QStringLiteral("Missing"), 0);
    QTRY_VERIFY(!m_controller->active());
    QCOMPARE(m_backend->state(), PlayerBackend::State::Idle);
    QVERIFY(!m_controller->busy());
    QCOMPARE(m_controller->sourceCount(), 0);
    QCOMPARE(m_controller->durationMs(), Q_INT64_C(0));
    QCOMPARE(m_controller->crashResumeInfo().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301001"));
}

void PlayerControllerTest::stopWhileResolvingCancelsTheLoad()
{
    m_mock->setRouteDelay(QStringLiteral("POST"), QStringLiteral("/Items/301001/PlaybackInfo"),
                          200);
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QVERIFY(m_controller->active());
    QVERIFY(m_controller->busy());

    m_controller->stop();
    QVERIFY(!m_controller->active());
    QVERIFY(!m_controller->busy());
    QCOMPARE(m_backend->state(), PlayerBackend::State::Idle);
    QTest::qWait(250);
    QCOMPARE(m_backend->loadedUrls.size(), 0);
}

void PlayerControllerTest::engineReadyTracksAcceptedBackendState()
{
    QSignalSpy readyChanged(m_controller, &PlayerController::engineReadyChanged);
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);

    QVERIFY(m_controller->active());
    QVERIFY(!m_controller->engineReady());
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    QVERIFY(!m_controller->engineReady()); // engine is still Loading

    m_backend->simulateState(PlayerBackend::State::Playing);
    QVERIFY(m_controller->engineReady());
    QCOMPARE(readyChanged.count(), 1);

    // An engine error is no longer ready even while the controller keeps the
    // active session alive to fetch a recovery ticket.
    m_mock->setRouteDelay(QStringLiteral("POST"), QStringLiteral("/Items/301001/PlaybackInfo"),
                          200);
    m_backend->simulateError(QStringLiteral("network stream error"));
    QVERIFY(m_controller->active());
    QVERIFY(!m_controller->engineReady());
    QCOMPARE(readyChanged.count(), 2);

    QTRY_COMPARE_WITH_TIMEOUT(m_backend->loadedUrls.size(), 2, 5000);
    QVERIFY(!m_controller->engineReady());
    m_backend->simulateState(PlayerBackend::State::Paused);
    QVERIFY(m_controller->engineReady());
    QCOMPARE(readyChanged.count(), 3);

    m_controller->stop();
    QVERIFY(!m_controller->engineReady());
    QCOMPARE(readyChanged.count(), 4);
}

void PlayerControllerTest::rawUrlHandoffClearsEngineReadyWhileResolving()
{
    OldLoadIdStopBackend backend;
    PlayerController controller(m_client, &backend, m_settings);

    controller.playUrl(QUrl(QStringLiteral("file:///tmp/raw-handoff.mkv")),
                       QStringLiteral("Raw URL"));
    QCOMPARE(backend.loadedUrls.size(), 1);
    const PlayerBackend::LoadId rawLoadId = backend.loadedIds.constLast();
    backend.simulateState(PlayerBackend::State::Playing, rawLoadId);
    QVERIFY(controller.engineReady());

    m_mock->setRouteDelay(QStringLiteral("POST"), QStringLiteral("/Items/301001/PlaybackInfo"),
                          200);
    QSignalSpy readyChanged(&controller, &PlayerController::engineReadyChanged);
    controller.playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);

    QVERIFY(controller.active());
    QVERIFY(controller.busy());
    QVERIFY(!controller.engineReady());
    QCOMPARE(readyChanged.count(), 1);
    QCOMPARE(backend.stopCalls, 1);

    // The old load's stop was rejected, and the delayed server ticket must not
    // leave the controller advertising the raw URL as engine-ready.
    QTest::qWait(100);
    QCOMPARE(backend.loadedUrls.size(), 1);
    QVERIFY(!controller.engineReady());

    QTRY_COMPARE_WITH_TIMEOUT(backend.loadedUrls.size(), 2, 5000);
    QVERIFY(!controller.engineReady()); // accepted engine state is still Loading
    const PlayerBackend::LoadId serverLoadId = backend.loadedIds.constLast();
    QVERIFY(serverLoadId != rawLoadId);
    backend.simulateState(PlayerBackend::State::Paused, serverLoadId);
    QVERIFY(controller.engineReady());
    QCOMPARE(readyChanged.count(), 2);
}

void PlayerControllerTest::seekAndPauseReportProgress()
{
    // The fixture's watchdog nudges after 40 ms of a frozen position; on a slow
    // runner the progress round trip below outlasts that and the nudge moves
    // the persisted playhead to 31000. This test is not about the watchdog.
    m_controller->setTimingForTests(20, 1000, 10);
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);

    m_controller->seekRelative(30'000);
    QCOMPARE(m_backend->seeks.size(), 1);
    QCOMPARE(m_backend->seeks[0], Q_INT64_C(30000));
    m_backend->simulatePosition(30'000);
    QTRY_VERIFY(
        !m_mock
             ->lastRequestFor(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Progress"))
             .method.isEmpty());

    m_controller->togglePause();
    QVERIFY(m_controller->paused());
    QTRY_VERIFY([this] {
        const auto request = m_mock->lastRequestFor(QStringLiteral("POST"),
                                                    QStringLiteral("/Sessions/Playing/Progress"));
        return QJsonDocument::fromJson(request.body)
            .object()
            .value(QLatin1String("IsPaused"))
            .toBool();
    }());

    QFile durable(m_dir->filePath(QStringLiteral("settings.ini")));
    QVERIFY(durable.open(QIODevice::ReadOnly));
    QVERIFY(durable.readAll().contains("positionMs=30000"));
}

// The controller's idea of "where we are" has to move with the seek, not wait
// for the engine to confirm it. Two things ride on that: the progress report
// sent immediately after a seek, and seekRelative(), which computes its target
// from that same value — a held skip key or a gamepad shoulder repeats every
// ~38 ms, far faster than an engine answers, so a stale base makes every step
// after the first a no-op.
void PlayerControllerTest::seekAdoptsItsTargetBeforeTheEngineReportsIt()
{
    LazySeekBackend backend;
    PlayerController controller(m_client, &backend, m_settings);
    controller.playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(backend.loadedUrls.size(), 1);
    backend.simulateState(PlayerBackend::State::Playing);
    backend.simulatePosition(30'000);

    controller.seekRelative(10'000);
    controller.seekRelative(10'000);
    QCOMPARE(backend.seeks.size(), 2);
    QCOMPARE(backend.seeks.at(0), Q_INT64_C(40'000));
    QCOMPARE(backend.seeks.at(1), Q_INT64_C(50'000)); // not 40 s twice

    QTRY_COMPARE(
        static_cast<qint64>(
            QJsonDocument::fromJson(m_mock
                                        ->lastRequestFor(QStringLiteral("POST"),
                                                         QStringLiteral("/Sessions/Playing/Progress"))
                                        .body)
                .object()
                .value(QLatin1String("PositionTicks"))
                .toDouble()),
        Q_INT64_C(50'000) * kTicksPerMs);

    // Clamping is still the floor: seeking below zero lands at zero, and the
    // next relative step counts from there.
    controller.seekRelative(-90'000);
    QCOMPARE(backend.seeks.at(2), Q_INT64_C(0));
    controller.seekRelative(5'000);
    QCOMPARE(backend.seeks.at(3), Q_INT64_C(5'000));
}

// Two things MPRIS needs that a toggle-only, signal-less controller cannot give
// it. Play, Pause and PlayPause are three separate verbs on the bus and only the
// last one toggles — `playerctl play` and XF86AudioPlay send Play, so routing it
// through togglePause() paused a track that was already playing. And a client
// extrapolates Position between polls, so it has to be told when the playhead
// jumped; the signal fires for seeks made in the app as much as for ones that
// arrived over D-Bus, since the client has no other way to hear about those.
void PlayerControllerTest::setPausedIsAbsoluteAndSeeksAnnounceThemselves()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);

    m_controller->setPaused(false); // Play, while already playing
    QVERIFY(!m_controller->paused());
    m_controller->setPaused(true);
    QVERIFY(m_controller->paused());
    m_controller->setPaused(true); // Pause, while already paused
    QVERIFY(m_controller->paused());
    m_controller->setPaused(false);
    QVERIFY(!m_controller->paused());
    // The toggle is still a toggle: QML and the input map use it.
    m_controller->togglePause();
    QVERIFY(m_controller->paused());
    m_controller->togglePause();
    QVERIFY(!m_controller->paused());

    QSignalSpy seeked(m_controller, &PlayerController::seeked);
    m_backend->simulateDuration(240'000);
    m_backend->simulatePosition(30'000);
    m_controller->seekTo(90'000);
    QCOMPARE(seeked.count(), 1);
    QCOMPARE(seeked.takeFirst().at(0).toLongLong(), Q_INT64_C(90'000));

    // Relative steps and chapter jumps funnel through seekTo(), so they announce
    // themselves too — and what is announced is the CLAMPED target the engine was
    // actually given, not the argument.
    m_controller->seekRelative(-500'000);
    QCOMPARE(seeked.count(), 1);
    QCOMPARE(seeked.takeFirst().at(0).toLongLong(), Q_INT64_C(0));
}

void PlayerControllerTest::watchdogNudgesReloadsThenDemotes()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(100'000);

    // Position frozen (not paused, not buffering) → step 1: nudge seek +1 s.
    QTRY_VERIFY(m_backend->seeks.contains(Q_INT64_C(101000)));

    // Still frozen → step 2: reload the same rung at the last position.
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QVERIFY(m_backend->loadedUrls[1].path().endsWith(QStringLiteral("stream.mkv")));
    QCOMPARE(m_backend->loadedStarts[1], Q_INT64_C(101000));
    m_backend->simulateState(PlayerBackend::State::Playing);

    // Still frozen → step 3: demote to DirectStream.
    QTRY_COMPARE(m_backend->loadedUrls.size(), 3);
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("DirectStream"));
    QVERIFY(m_controller->active());

    // Buffering must NOT count as a stall: no further escalation while starved.
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateBuffering(true);
    QTest::qWait(200);
    QCOMPARE(m_backend->loadedUrls.size(), 3);

    // Once starvation clears, exhaust the final rung. The watchdog closes the
    // reporting session before finishing the failure; the crash-resume record
    // must still carry the last real position durably.
    m_backend->simulateBuffering(false);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 4);
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("Transcode"));
    m_backend->simulateState(PlayerBackend::State::Playing);
    QTRY_VERIFY(!m_controller->active());

    QFile durable(m_dir->filePath(QStringLiteral("settings.ini")));
    QVERIFY(durable.open(QIODevice::ReadOnly));
    QVERIFY(durable.readAll().contains("positionMs=101000"));
}

void PlayerControllerTest::reloadThenErrorPreservesResumePosition()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(100'000);

    // Let the watchdog nudge once and then reload the current rung. The fake
    // mirrors the engines' load contract: the new load publishes its requested
    // position directly rather than an accepted synthetic zero.
    QTRY_VERIFY(m_backend->seeks.contains(Q_INT64_C(101000)));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_backend->loadedStarts.at(1), Q_INT64_C(101000));

    // Failure before the reloaded rung becomes ready demotes at that same
    // position. Before the fix, MpvPlayer's new-load zero made this restart at
    // the beginning.
    m_backend->simulateError(QStringLiteral("reload failed before ready"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 3);
    QCOMPARE(m_backend->loadedStarts.at(2), Q_INT64_C(101000));
}

void PlayerControllerTest::errorNearTailIsNotAConfirmedEnd()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(8'184'000);
    m_backend->simulatePosition(8'182'000); // 2 s before EOF

    QSignalSpy stoppedSpy(m_controller, &PlayerController::stopped);
    m_backend->simulateError(QStringLiteral("tcp: connection reset near tail"));

    // Position is not an EOF reason. The ordinary mid-stream recovery path
    // keeps the item and resume point alive; it neither reports the runtime as
    // watched nor advances/ends the session.
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_backend->loadedStarts.constLast(), Q_INT64_C(8182000));
    QCOMPARE(stoppedSpy.count(), 0);
    QVERIFY(m_controller->active());
    QVERIFY(m_controller->errorMessage().isEmpty());
    QVERIFY(m_mock
                ->lastRequestFor(QStringLiteral("POST"),
                                 QStringLiteral("/Sessions/Playing/Stopped"))
                .method.isEmpty());
}

void PlayerControllerTest::midStreamErrorRefetchesTicket()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(8'184'000);
    m_backend->simulatePosition(500'000); // mid-stream, far from the tail

    const int ticketRequestsBefore = static_cast<int>(m_mock->requests().size());
    m_backend->simulateError(QStringLiteral("tcp: connection reset"));

    // A fresh PlaybackInfo is fetched and the same rung reloads at 500 s.
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_backend->loadedStarts[1], Q_INT64_C(500000));
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("DirectPlay"));
    QVERIFY(m_mock->requests().size() > ticketRequestsBefore);
    QVERIFY(m_controller->active());

    // Exhausting retries surfaces the error and ends the session.
    QSignalSpy stoppedSpy(m_controller, &PlayerController::stopped);
    m_backend->simulateError(QStringLiteral("reset 2"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 3);
    m_backend->simulateError(QStringLiteral("reset 3"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 4);
    m_backend->simulateError(QStringLiteral("reset final"));
    QTRY_COMPARE(stoppedSpy.count(), 1);
    QVERIFY(m_controller->errorMessage().contains(QStringLiteral("reset final")));

    QFile durable(m_dir->filePath(QStringLiteral("settings.ini")));
    QVERIFY(durable.open(QIODevice::ReadOnly));
    QVERIFY(durable.readAll().contains("positionMs=500000"));
}

void PlayerControllerTest::recoveryBurstStartsOnlyOneRefresh()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(500'000);

    m_mock->setRouteDelay(QStringLiteral("POST"), QStringLiteral("/Items/301001/PlaybackInfo"),
                          200);
    const PlayerBackend::LoadId failedLoad = m_backend->loadedIds.constLast();
    m_backend->simulateError(QStringLiteral("first"), failedLoad);
    m_backend->simulateError(QStringLiteral("duplicate"), failedLoad);
    m_backend->simulateError(QStringLiteral("duplicate again"), failedLoad);

    // The watchdog is stopped throughout backoff/readback, and the error burst
    // owns only one refresh token.
    QTest::qWait(80);
    QCOMPARE(m_backend->loadedUrls.size(), 1);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    int playbackInfoRequests = 0;
    for (const auto &request : m_mock->requests()) {
        if (request.method == QLatin1String("POST") &&
            request.path == QLatin1String("/Items/301001/PlaybackInfo"))
            ++playbackInfoRequests;
    }
    QCOMPARE(playbackInfoRequests, 2); // initial resolve plus one recovery
}

void PlayerControllerTest::trackPersistenceWaitsForBackendReadback()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateTracks(
        {FakePlayerBackend::makeTrack(1, QStringLiteral("Main"), {}, {}, true),
         FakePlayerBackend::makeTrack(2, QStringLiteral("Commentary"))},
        {});
    const QString sourceId =
        m_controller->currentSource().value(QStringLiteral("id")).toString();
    QVERIFY(!m_settings->hasRememberedTracks(QStringLiteral("301001"), sourceId));

    m_backend->deferTrackReadback = true;
    m_controller->setAudioTrack(2);
    QCOMPARE(m_backend->currentAudioTrackId(), 1);
    QVERIFY(!m_settings->hasRememberedTracks(QStringLiteral("301001"), sourceId));

    // An unrelated track-list republication before the selection takes effect
    // must not retire the request: the readback that follows is still the one
    // that decides what gets remembered.
    m_backend->republishTracksUnchanged();
    QVERIFY(!m_settings->hasRememberedTracks(QStringLiteral("301001"), sourceId));

    m_backend->confirmAudioTrack(2);
    QVERIFY(m_settings->hasRememberedTracks(QStringLiteral("301001"), sourceId));
    QCOMPARE(m_settings->recalledTracks(QStringLiteral("301001"), sourceId).first, 2);

    QSignalSpy toast(m_controller, &PlayerController::trackChanged);
    m_backend->simulateTrackDescription(QStringLiteral("Audio: Commentary"));
    QCOMPARE(toast.count(), 1);
    QCOMPARE(toast.takeFirst().constFirst().toString(), QStringLiteral("Audio: Commentary"));
}

// ── External (sidecar) subtitles ──────────────────────────────────────────────
// EmbyClient's device profile declares "Method": "External" for text
// subtitles, so the server no longer puts a sidecar subtitle into the stream:
// it names a URL per stream and the client has to hand each one to the engine.
// Item 7700 is a film with an in-file subrip (2), two sidecars (3 srt — the
// server's default for this user — and 4 ass), an in-file PGS (5), and an
// in-file ass the server marked External as well (6).

namespace {
const auto kSidecarItem = QStringLiteral("7700");
const auto kSidecarSource = QStringLiteral("ms7700");

QStringList subtitlePaths(const QList<FakePlayerBackend::ExternalSubtitle> &subtitles,
                          PlayerBackend::LoadId loadId)
{
    QStringList paths;
    for (const FakePlayerBackend::ExternalSubtitle &subtitle : subtitles) {
        if (subtitle.loadId == loadId)
            paths.append(subtitle.url.path());
    }
    return paths;
}
} // namespace

void PlayerControllerTest::startSidecarItem()
{
    QVERIFY(m_mock->addRouteFromFile(
        QStringLiteral("POST"), QStringLiteral("/Items/7700/PlaybackInfo"),
        fixturePath(QStringLiteral("playback_info_external_subtitle.json"))));
    m_controller->playItem(kSidecarItem, QStringLiteral("Sidecar"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("DirectPlay"));
}

void PlayerControllerTest::externalSubtitlesAreAttachedOncePerLoad()
{
    startSidecarItem();
    // An engine can only add a subtitle to a file it has open.
    QVERIFY(m_backend->externalSubtitles.isEmpty());
    m_backend->simulateTracks({FakePlayerBackend::makeTrack(1, QStringLiteral("English"), {}, {}, true)},
                              {FakePlayerBackend::makeTrack(1, QStringLiteral("English"))});
    QVERIFY(m_backend->externalSubtitles.isEmpty());

    m_backend->simulateState(PlayerBackend::State::Playing);
    QCOMPARE(m_backend->externalSubtitles.size(), 2);
    const PlayerBackend::LoadId first = m_backend->loadedIds.at(0);

    // Stream 3: the server's default, so it is shown — the subtitle a viewer
    // used to get burned into a transcode. Its DeliveryUrl came with a key.
    const FakePlayerBackend::ExternalSubtitle &dutch = m_backend->externalSubtitles.at(0);
    QCOMPARE(dutch.loadId, first);
    QCOMPARE(dutch.url.scheme(), m_mock->baseUrl().scheme());
    QCOMPARE(dutch.url.host(), m_mock->baseUrl().host());
    QCOMPARE(dutch.url.port(), m_mock->baseUrl().port());
    QCOMPARE(dutch.url.path(), QStringLiteral("/Videos/7700/ms7700/Subtitles/3/0/Stream.srt"));
    QCOMPARE(QUrlQuery(dutch.url).allQueryItemValues(QStringLiteral("api_key")),
             QStringList{kToken});
    QCOMPARE(dutch.title, QStringLiteral("Dutch (SRT)"));
    QCOMPARE(dutch.language, QStringLiteral("nld"));
    QVERIFY(dutch.select);

    // Stream 4: offered, not shown. Its DeliveryUrl had no key; it has ours.
    const FakePlayerBackend::ExternalSubtitle &english = m_backend->externalSubtitles.at(1);
    QCOMPARE(english.url.path(), QStringLiteral("/Videos/7700/ms7700/Subtitles/4/0/Stream.ass"));
    QCOMPARE(QUrlQuery(english.url).allQueryItemValues(QStringLiteral("api_key")),
             QStringList{kToken});
    QCOMPARE(english.title, QStringLiteral("English (ASS)"));
    QCOMPARE(english.language, QStringLiteral("eng"));
    QVERIFY(!english.select);

    // The default arriving selected is the server's choice, not the user's:
    // nothing is remembered for it.
    m_backend->simulateTracks(
        {FakePlayerBackend::makeTrack(1, QStringLiteral("English"), {}, {}, true)},
        {FakePlayerBackend::makeTrack(1, QStringLiteral("English")),
         FakePlayerBackend::makeTrack(2, QStringLiteral("Dutch (SRT)"), {}, {}, true)});
    QVERIFY(!m_settings->hasRememberedTracks(kSidecarItem, kSidecarSource));
    QVERIFY(m_backend->subtitleTrackRequests.isEmpty());

    // Pausing, resuming and buffering are the same file: nothing is added again.
    m_backend->simulateState(PlayerBackend::State::Paused);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateBuffering(true);
    m_backend->simulateBuffering(false);
    QCOMPARE(m_backend->externalSubtitles.size(), 2);

    // The user picking it in the track panel is an ordinary engine track id.
    m_controller->setSubtitleTrack(1);
    QCOMPARE(m_backend->subtitleTrackRequests, QList<int>{1});
    QCOMPARE(m_settings->recalledTracks(kSidecarItem, kSidecarSource).second, 1);

    // A reload is a new file to the engine, which starts without them.
    m_controller->reloadStream();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    const PlayerBackend::LoadId second = m_backend->loadedIds.at(1);
    QVERIFY(second != first);
    QVERIFY(subtitlePaths(m_backend->externalSubtitles, second).isEmpty());
    m_backend->simulateState(PlayerBackend::State::Playing);
    QCOMPARE(subtitlePaths(m_backend->externalSubtitles, second),
             (QStringList{QStringLiteral("/Videos/7700/ms7700/Subtitles/3/0/Stream.srt"),
                          QStringLiteral("/Videos/7700/ms7700/Subtitles/4/0/Stream.ass")}));
    // By now the user has chosen for this item, so the server's default is no
    // longer forced on top of that choice.
    for (const FakePlayerBackend::ExternalSubtitle &subtitle : m_backend->externalSubtitles) {
        if (subtitle.loadId == second)
            QVERIFY(!subtitle.select);
    }
    QCOMPARE(m_backend->externalSubtitles.size(), 4);

    // Nothing leaks into the next thing played.
    m_controller->playUrl(QUrl(QStringLiteral("file:///tmp/no-ticket.mkv")), QStringLiteral("Raw"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 3);
    m_backend->simulateState(PlayerBackend::State::Playing);
    QCOMPARE(m_backend->externalSubtitles.size(), 4);
}

// Direct play opens the original file, and stream 6 is in it: mpv lists it by
// itself, so attaching the server's copy would show it twice. A remux or a
// transcode is the server's own output, and there an External delivery is the
// only place the subtitle exists.
void PlayerControllerTest::directPlaySkipsSubtitlesTheFileAlreadyCarries()
{
    startSidecarItem();
    // The rung fails before it starts, twice: DirectPlay → DirectStream →
    // Transcode. Nothing was attached to a file that never opened.
    m_backend->simulateError(QStringLiteral("network stream error"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("DirectStream"));
    QVERIFY(m_backend->externalSubtitles.isEmpty());

    const QStringList all{QStringLiteral("/Videos/7700/ms7700/Subtitles/3/0/Stream.srt"),
                          QStringLiteral("/Videos/7700/ms7700/Subtitles/4/0/Stream.ass"),
                          QStringLiteral("/Videos/7700/ms7700/Subtitles/6/0/Stream.ass")};
    m_backend->simulateState(PlayerBackend::State::Playing);
    QCOMPARE(subtitlePaths(m_backend->externalSubtitles, m_backend->loadedIds.at(1)), all);
    // Still the server's default that is shown, on every rung.
    QVERIFY(m_backend->externalSubtitles.at(0).select);
    QVERIFY(!m_backend->externalSubtitles.at(1).select);
    QVERIFY(!m_backend->externalSubtitles.at(2).select);

    // A stall: nudge, then the watchdog reloads the same rung, then demotes.
    // Each of those is a new file to the engine and gets its subtitles again.
    m_backend->simulatePosition(100'000);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 3);
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("DirectStream"));
    const PlayerBackend::LoadId reload = m_backend->loadedIds.at(2);
    QVERIFY(subtitlePaths(m_backend->externalSubtitles, reload).isEmpty());
    m_backend->simulateState(PlayerBackend::State::Playing);
    QCOMPARE(subtitlePaths(m_backend->externalSubtitles, reload), all);

    QTRY_COMPARE(m_backend->loadedUrls.size(), 4);
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("Transcode"));
    const PlayerBackend::LoadId transcode = m_backend->loadedIds.at(3);
    QVERIFY(subtitlePaths(m_backend->externalSubtitles, transcode).isEmpty());
    m_backend->simulateBuffering(true); // hold the watchdog off the last rung
    m_backend->simulateState(PlayerBackend::State::Playing);
    QCOMPARE(subtitlePaths(m_backend->externalSubtitles, transcode), all);
    QCOMPARE(m_backend->externalSubtitles.size(), 9);
    // Never the embedded-and-Embed ones, on any rung.
    for (const FakePlayerBackend::ExternalSubtitle &subtitle : m_backend->externalSubtitles) {
        QVERIFY(!subtitle.url.path().contains(QStringLiteral("/Subtitles/2/")));
        QVERIFY(!subtitle.url.path().contains(QStringLiteral("/Subtitles/5/")));
    }
}

// "Off" is a choice. A viewer who turned subtitles off for this film does not
// get the server's default turned back on by the attach.
void PlayerControllerTest::rememberedChoiceOutranksTheServerDefaultSubtitle()
{
    m_settings->rememberTracks(kSidecarItem, kSidecarSource, 1, -1);
    startSidecarItem();
    m_backend->simulateState(PlayerBackend::State::Playing);
    QCOMPARE(m_backend->externalSubtitles.size(), 2);
    QVERIFY(!m_backend->externalSubtitles.at(0).select);
    QVERIFY(!m_backend->externalSubtitles.at(1).select);

    // The engine came up with its own default on; the remembered "off" wins.
    m_backend->simulateTracks(
        {FakePlayerBackend::makeTrack(1, QStringLiteral("English"), {}, {}, true)},
        {FakePlayerBackend::makeTrack(1, QStringLiteral("English"), {}, {}, true)});
    QCOMPARE(m_backend->subtitleTrackRequests, QList<int>{-1});
    // The sidecars joining the list later change nothing.
    m_backend->simulateTracks(
        {FakePlayerBackend::makeTrack(1, QStringLiteral("English"), {}, {}, true)},
        {FakePlayerBackend::makeTrack(1, QStringLiteral("English")),
         FakePlayerBackend::makeTrack(2, QStringLiteral("Dutch (SRT)")),
         FakePlayerBackend::makeTrack(3, QStringLiteral("English (ASS)"))});
    QCOMPARE(m_backend->subtitleTrackRequests, QList<int>{-1});
    QCOMPARE(m_settings->recalledTracks(kSidecarItem, kSidecarSource).second, -1);
}

// A remembered sidecar is not in the engine's list when the list is first
// published: it is fetched after the file is up. Its choice has to survive
// until the track arrives, and be applied exactly once.
void PlayerControllerTest::rememberedExternalSubtitleWaitsForItsTrack()
{
    m_settings->rememberTracks(kSidecarItem, kSidecarSource, 1, 3);
    startSidecarItem();
    const QVariantList audio{FakePlayerBackend::makeTrack(1, QStringLiteral("English"), {}, {}, true)};
    // mpv publishes the container's tracks before it reports Playing.
    m_backend->simulateTracks(audio, {FakePlayerBackend::makeTrack(1, QStringLiteral("English"))});
    QVERIFY(m_backend->subtitleTrackRequests.isEmpty());
    m_backend->simulateState(PlayerBackend::State::Playing);
    QVERIFY(!m_backend->externalSubtitles.at(0).select);

    m_backend->simulateTracks(audio,
                              {FakePlayerBackend::makeTrack(1, QStringLiteral("English")),
                               FakePlayerBackend::makeTrack(2, QStringLiteral("Dutch (SRT)"))});
    QVERIFY(m_backend->subtitleTrackRequests.isEmpty());
    m_backend->simulateTracks(audio,
                              {FakePlayerBackend::makeTrack(1, QStringLiteral("English")),
                               FakePlayerBackend::makeTrack(2, QStringLiteral("Dutch (SRT)")),
                               FakePlayerBackend::makeTrack(3, QStringLiteral("English (ASS)"))});
    QCOMPARE(m_backend->subtitleTrackRequests, QList<int>{3});
    QCOMPARE(m_backend->currentSubtitleTrackId(), 3);
    m_backend->republishTracksUnchanged();
    QCOMPARE(m_backend->subtitleTrackRequests, QList<int>{3});
    QCOMPARE(m_settings->recalledTracks(kSidecarItem, kSidecarSource), qMakePair(1, 3));

    // An item with nothing external does not wait for a track that will never
    // come: a remembered id its list lacks is dropped, as before.
    m_settings->rememberTracks(QStringLiteral("301001"), QStringLiteral("ms301001"), 1, 9);
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    m_backend->simulateState(PlayerBackend::State::Playing);
    QCOMPARE(m_backend->externalSubtitles.size(), 2); // the first item's, none new
    m_backend->simulateTracks(audio, {FakePlayerBackend::makeTrack(1, QStringLiteral("English"))});
    m_backend->simulateTracks(audio,
                              {FakePlayerBackend::makeTrack(1, QStringLiteral("English")),
                               FakePlayerBackend::makeTrack(9, QStringLiteral("Late"))});
    QCOMPARE(m_backend->subtitleTrackRequests, QList<int>{3});
}

void PlayerControllerTest::pickingASubtitleCancelsTheWaitForARememberedOne()
{
    m_settings->rememberTracks(kSidecarItem, kSidecarSource, 1, 3);
    startSidecarItem();
    const QVariantList audio{FakePlayerBackend::makeTrack(1, QStringLiteral("English"), {}, {}, true)};
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateTracks(audio, {FakePlayerBackend::makeTrack(1, QStringLiteral("English"))});

    m_controller->setSubtitleTrack(1);
    QCOMPARE(m_backend->subtitleTrackRequests, QList<int>{1});
    m_backend->simulateTracks(audio,
                              {FakePlayerBackend::makeTrack(1, QStringLiteral("English"), {}, {}, true),
                               FakePlayerBackend::makeTrack(2, QStringLiteral("Dutch (SRT)")),
                               FakePlayerBackend::makeTrack(3, QStringLiteral("English (ASS)"))});
    QCOMPARE(m_backend->subtitleTrackRequests, QList<int>{1});
    QCOMPARE(m_backend->currentSubtitleTrackId(), 1);
    QCOMPARE(m_settings->recalledTracks(kSidecarItem, kSidecarSource).second, 1);
}

void PlayerControllerTest::playbackSettingsFollowBackendReadback()
{
    m_controller->playUrl(QUrl(QStringLiteral("file:///tmp/readback-test.mkv")),
                          QStringLiteral("Readback"));
    m_backend->deferPlaybackSettingsReadback = true;
    QSignalSpy speedSpy(m_controller, &PlayerController::playbackSpeedChanged);
    QSignalSpy audioSpy(m_controller, &PlayerController::audioDelayChanged);
    QSignalSpy subtitleSpy(m_controller, &PlayerController::subtitleDelayChanged);

    m_controller->setPlaybackSpeed(1.5);
    m_controller->setAudioDelayMs(125);
    m_controller->setSubtitleDelayMs(-250);
    QCOMPARE(m_controller->playbackSpeed(), 1.0);
    QCOMPARE(m_controller->audioDelayMs(), 0);
    QCOMPARE(m_controller->subtitleDelayMs(), 0);
    QCOMPARE(speedSpy.count(), 0);
    QCOMPARE(audioSpy.count(), 0);
    QCOMPARE(subtitleSpy.count(), 0);

    m_backend->confirmPlaybackSpeed(1.5);
    m_backend->confirmAudioDelay(125);
    m_backend->confirmSubtitleDelay(-250);
    QCOMPARE(m_controller->playbackSpeed(), 1.5);
    QCOMPARE(m_controller->audioDelayMs(), 125);
    QCOMPARE(m_controller->subtitleDelayMs(), -250);
    QCOMPARE(speedSpy.count(), 1);
    QCOMPARE(audioSpy.count(), 1);
    QCOMPARE(subtitleSpy.count(), 1);
}

void PlayerControllerTest::crashResumePersistsAndClears()
{
    QVERIFY(m_controller->crashResumeInfo().isEmpty());

    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(300'000);

    // The record is written at start (and every 5 s after).
    const QVariantMap info = m_controller->crashResumeInfo();
    QCOMPARE(info.value(QStringLiteral("itemId")).toString(), QStringLiteral("301001"));
    QCOMPARE(info.value(QStringLiteral("title")).toString(), QStringLiteral("The Matrix"));

    // Clean stop clears it — no bogus prompt next launch.
    m_controller->stop();
    QVERIFY(m_controller->crashResumeInfo().isEmpty());
}

// ── Multi-version (MediaSource) behaviour ─────────────────────────────────────

// MUSIC.md §6.3. Two things have to hold, and neither is free: the preference
// has to reach a *running* engine (a normalisation you only hear on the next
// track is a control that appears not to work), and it has to be re-asserted on
// every load, because an engine is entitled to rebuild its audio chain per file.
void PlayerControllerTest::replayGainReachesTheEngineOnLoadAndOnChange()
{
    // Constructed with the stored value already in hand, so the first file is
    // normalised rather than the second.
    QCOMPARE(m_backend->replayGainModes, QStringList{QStringLiteral("off")});

    m_backend->replayGainModes.clear();
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    QCOMPARE(m_backend->replayGainModes, QStringList{QStringLiteral("off")});
    m_backend->simulateState(PlayerBackend::State::Playing);

    // Changed mid-playback: pushed at once, not deferred to the next item.
    m_backend->replayGainModes.clear();
    m_settings->setReplayGainMode(QStringLiteral("album"));
    QCOMPARE(m_backend->replayGainModes, QStringList{QStringLiteral("album")});

    // And every subsequent load carries it, including a ladder demotion — which
    // reloads through the same path and must not drop back to the engine default.
    m_backend->replayGainModes.clear();
    m_backend->simulateError(QStringLiteral("network stream error"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_backend->replayGainModes, QStringList{QStringLiteral("album")});
}

void PlayerControllerTest::demotionStaysWithinSelectedSource()
{
    QSignalSpy stoppedSpy(m_controller, &PlayerController::stopped);
    m_controller->playItem(QStringLiteral("4242"), QStringLiteral("Dune"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    QCOMPARE(m_controller->sourceCount(), 2);
    QCOMPARE(m_controller->sourceIndex(), 0);
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("DirectPlay"));
    QVERIFY(m_backend->loadedUrls[0].query().contains(QStringLiteral("ms4242uhd")));

    // Every demotion must stay on the 4K source, never hop to the 1080p one.
    m_backend->simulateError(QStringLiteral("e1"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("DirectStream"));
    QCOMPARE(m_controller->sourceIndex(), 0);
    QVERIFY(m_backend->loadedUrls[1].query().contains(QStringLiteral("ms4242uhd")));

    m_backend->simulateError(QStringLiteral("e2"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 3);
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("Transcode"));
    QCOMPARE(m_controller->sourceIndex(), 0);
    QVERIFY(m_backend->loadedUrls[2].query().contains(QStringLiteral("ms4242uhd")));

    // Source 0's ladder is exhausted: the session ends rather than silently
    // continuing into source 1 (the latent bug this restructure removes).
    m_backend->simulateError(QStringLiteral("e3"));
    QTRY_VERIFY(!m_controller->active());
    QCOMPARE(stoppedSpy.count(), 1);
    QCOMPARE(m_backend->loadedUrls.size(), 3);
    for (const QUrl &url : m_backend->loadedUrls)
        QVERIFY(!url.query().contains(QStringLiteral("ms4242hd")));
}

void PlayerControllerTest::preferredSourceHonouredAtStart()
{
    m_controller->playItem(QStringLiteral("4242"), QStringLiteral("Dune"), 0, 1);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    QCOMPARE(m_controller->sourceIndex(), 1);
    QVERIFY(m_backend->loadedUrls[0].query().contains(QStringLiteral("ms4242hd")));

    // Source 1 has no DirectStream rung: DirectPlay demotes straight to Transcode.
    m_backend->simulateError(QStringLiteral("e1"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("Transcode"));
    QVERIFY(m_backend->loadedUrls[1].query().contains(QStringLiteral("ms4242hd")));
}

void PlayerControllerTest::setPreferredSourceSwitchesVersionMidSession()
{
    m_controller->playItem(QStringLiteral("4242"), QStringLiteral("Dune"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(240'000);

    QSignalSpy sourceSpy(m_controller, &PlayerController::sourceIndexChanged);
    m_controller->setPreferredSource(1);
    QCOMPARE(sourceSpy.count(), 1);
    QCOMPARE(m_controller->sourceIndex(), 1);
    // Reloads the new version at the current position, back at the top rung.
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_backend->loadedStarts[1], Q_INT64_C(240000));
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("DirectPlay"));
    QVERIFY(m_backend->loadedUrls[1].query().contains(QStringLiteral("ms4242hd")));

    // Reports now name the newly selected source.
    m_backend->simulateState(PlayerBackend::State::Playing);
    QTRY_VERIFY(!m_mock->lastRequestFor(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing"))
                     .method.isEmpty());
    m_controller->stop();
    QTRY_VERIFY(
        !m_mock->lastRequestFor(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped"))
             .method.isEmpty());
    const QJsonObject body =
        QJsonDocument::fromJson(m_mock
                                    ->lastRequestFor(QStringLiteral("POST"),
                                                     QStringLiteral("/Sessions/Playing/Stopped"))
                                    .body)
            .object();
    QCOMPARE(body.value(QLatin1String("MediaSourceId")).toString(), QStringLiteral("ms4242hd"));
}

void PlayerControllerTest::reloadStreamRefetchesTicketInPlace()
{
    m_controller->playItem(QStringLiteral("4242"), QStringLiteral("Dune"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_controller->setPreferredSource(1);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(310'000);

    const auto countTicketRequests = [this] {
        int count = 0;
        for (const auto &request : m_mock->requests()) {
            if (request.method == QLatin1String("POST") &&
                request.path == QLatin1String("/Items/4242/PlaybackInfo"))
                ++count;
        }
        return count;
    };
    const int ticketsBefore = countTicketRequests();
    const int queueCountBefore = m_controller->queue()->rowCount();

    // A quality change is decided when a stream starts; reloading asks the
    // server again, in place: same item, same version, same position, same queue.
    m_controller->reloadStream();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 3);
    QCOMPARE(countTicketRequests(), ticketsBefore + 1);
    QCOMPARE(m_backend->loadedStarts[2], Q_INT64_C(310000));
    QVERIFY(m_backend->loadedUrls[2].query().contains(QStringLiteral("ms4242hd")));
    QCOMPARE(m_controller->sourceIndex(), 1);
    QCOMPARE(m_controller->queue()->rowCount(), queueCountBefore);
    QVERIFY(m_controller->active());

    // With nothing playing it is a no-op rather than a start.
    m_controller->stop();
    const int loadsAfterStop = static_cast<int>(m_backend->loadedUrls.size());
    m_controller->reloadStream();
    QTest::qWait(50);
    QCOMPARE(static_cast<int>(m_backend->loadedUrls.size()), loadsAfterStop);
}

void PlayerControllerTest::sourceSurfaceExposesStreams()
{
    m_controller->playItem(QStringLiteral("4242"), QStringLiteral("Dune"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);

    const QVariantList sources = m_controller->sources();
    QCOMPARE(sources.size(), 2);
    QCOMPARE(sources[0].toMap().value(QStringLiteral("displayName")).toString(),
             QStringLiteral("4K Remux"));
    QCOMPARE(sources[0].toMap().value(QStringLiteral("index")).toInt(), 0);
    QVERIFY(sources[0].toMap().value(QStringLiteral("playable")).toBool());
    QCOMPARE(sources[1].toMap().value(QStringLiteral("resolutionLabel")).toString(),
             QStringLiteral("1080p"));

    QCOMPARE(m_controller->currentSource().value(QStringLiteral("id")).toString(),
             QStringLiteral("ms4242uhd"));
    QVERIFY(m_controller->currentSource().value(QStringLiteral("isHdr")).toBool());
    QCOMPARE(m_controller->videoStream().value(QStringLiteral("codec")).toString(),
             QStringLiteral("hevc"));
    QCOMPARE(m_controller->audioStreams().size(), 2);
    QCOMPARE(m_controller->subtitleStreams().size(), 2);

    // The single-source item exposes exactly one version.
    m_controller->stop();
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_controller->sourceCount(), 1);
    QCOMPARE(m_controller->sourceIndex(), 0);
    QCOMPARE(m_controller->audioStreams().size(), 1);
    QVERIFY(m_controller->subtitleStreams().isEmpty());
}

void PlayerControllerTest::unplayableSourceSelectionIsIgnored()
{
    m_controller->playItem(QStringLiteral("4242"), QStringLiteral("Dune"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);

    // Out-of-range picks must not disturb a running session.
    m_controller->setPreferredSource(9);
    m_controller->setPreferredSource(-1);
    QCOMPARE(m_controller->sourceIndex(), 0);
    QCOMPARE(m_backend->loadedUrls.size(), 1);
    QVERIFY(m_controller->active());
}

// Shader support on, the real catalog, a 1440p window on a known GPU, and the
// governor on a test clock well past its first grace period.
void PlayerControllerTest::startUpscaling()
{
    m_backend->videoShadersSupported = true;
    m_controller->setUpscaleCatalogForTests(
        UpscaleCatalog::load(QStringLiteral(STRMQT_SOURCE_DIR "/shaders")));
    m_controller->setUpscaleDebounceForTests(10);
    m_upscaleNow = 0;
    m_controller->upscaleGovernorForTests()->setClockForTests([this] { return m_upscaleNow; });
    m_controller->setGpuRenderer(QStringLiteral("Test GPU"));
    m_controller->setVideoOutputSize(2560, 1440);
}

void PlayerControllerTest::upscaleFollowsLoadResizeAndOverride()
{
    startUpscaling();
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    QVERIFY(m_controller->upscaleAvailable());

    m_backend->simulateVideoFormat(QSize(1280, 720));
    QTRY_VERIFY(!m_backend->processingRequests.isEmpty());
    QVERIFY(m_backend->processingRequests.last().shaderPaths.constFirst().endsWith(
        QStringLiteral("FSRCNNX_x2_16-0-4-1.glsl")));
    QCOMPARE(m_controller->upscaleActive(), QStringLiteral("FSRCNNX 16"));
    QCOMPARE(m_controller->upscaleReason(), QStringLiteral("FSRCNNX 16 · 720p → 1440p"));

    // An identical decision pushes nothing.
    const qsizetype pushes = m_backend->processingRequests.size();
    m_backend->simulateVideoFormat(QSize(1280, 720));
    QCOMPARE(m_backend->processingRequests.size(), pushes);

    // A resize, after the debounce: 1.1× is FSR, no doubler.
    m_controller->setVideoOutputSize(1408, 792);
    QCOMPARE(m_backend->processingRequests.size(), pushes);
    QTRY_COMPARE(m_backend->processingRequests.size(), pushes + 1);
    QVERIFY(m_backend->processingRequests.last().shaderPaths.constFirst().endsWith(
        QStringLiteral("FSR.glsl")));

    // An override applies at once, no debounce.
    QSignalSpy changed(m_controller, &PlayerController::upscaleChanged);
    m_controller->setUpscaleMode(QStringLiteral("nis"));
    QCOMPARE(m_backend->processingRequests.size(), pushes + 2);
    QVERIFY(m_backend->processingRequests.last().shaderPaths.constFirst().endsWith(
        QStringLiteral("NVScaler.glsl")));
    QCOMPARE(m_controller->upscaleMode(), QStringLiteral("nis"));
    QVERIFY(changed.count() >= 1);

    m_controller->setUpscaleMode(QStringLiteral("off"));
    QVERIFY(m_backend->processingRequests.last().isDefault());
    QCOMPARE(m_controller->upscaleActive(), QStringLiteral("Off"));
}

void PlayerControllerTest::overrideResetsOnTheNextItem()
{
    startUpscaling();
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_controller->setUpscaleMode(QStringLiteral("nis"));
    QCOMPARE(m_controller->upscaleMode(), QStringLiteral("nis"));

    m_controller->playItem(QStringLiteral("4242"), QStringLiteral("Two Versions"), 0);
    QCOMPARE(m_controller->upscaleMode(), QString());
}

void PlayerControllerTest::backendWithoutShadersIsUnavailable()
{
    startUpscaling();
    m_backend->videoShadersSupported = false;
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateVideoFormat(QSize(1280, 720));
    QTest::qWait(50);
    QVERIFY(!m_controller->upscaleAvailable());
    QCOMPARE(m_controller->upscaleReason(), QStringLiteral("Not available with FAKE"));
    QVERIFY(m_backend->processingRequests.isEmpty());
}

void PlayerControllerTest::softwareRendererIsUnavailable()
{
    startUpscaling();
    m_controller->setGpuRenderer(QStringLiteral("llvmpipe (LLVM 19.1.7, 256 bits)"));
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateVideoFormat(QSize(1280, 720));
    QVERIFY(!m_controller->upscaleAvailable());
    QCOMPARE(m_controller->upscaleReason(), QStringLiteral("Needs a GPU"));
    QVERIFY(m_backend->processingRequests.isEmpty());
}

void PlayerControllerTest::animeGenresPickTheAnimationLadder()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/301001").arg(kUserId),
                     200, R"json({"Id": "301001", "Name": "Akira", "Type": "Movie",
                                  "Genres": ["Action", "anime"]})json");
    startUpscaling();
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("Akira"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateVideoFormat(QSize(1280, 720));
    QTRY_COMPARE(m_controller->upscaleActive(), QStringLiteral("Anime4K CNN VL"));
    QCOMPARE(m_controller->upscaleReason(),
             QStringLiteral("Anime4K CNN VL · animation · 720p → 1440p"));
    const QVariantList choices = m_controller->upscaleChoices();
    QCOMPARE(choices.first().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("anime4k-vl"));
    for (const QVariant &choice : choices)
        QVERIFY(!choice.toMap().value(QStringLiteral("id")).toString().startsWith(u"fsrcnnx"));
}

void PlayerControllerTest::episodeWithoutGenresAsksItsSeries()
{
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/301001").arg(kUserId),
                     200, R"json({"Id": "301001", "Name": "Episode 1", "Type": "Episode",
                                  "SeriesId": "9001", "Genres": []})json");
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/9001").arg(kUserId),
                     200, R"json({"Id": "9001", "Name": "A Series", "Type": "Series",
                                  "Genres": ["Animation"]})json");
    startUpscaling();
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("Episode 1"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateVideoFormat(QSize(1280, 720));
    QTRY_COMPARE(m_controller->upscaleActive(), QStringLiteral("Anime4K CNN VL"));
    QVERIFY(!m_mock->lastRequestFor(QStringLiteral("GET"),
                                    QStringLiteral("/Users/%1/Items/9001").arg(kUserId))
                 .method.isEmpty());
}

void PlayerControllerTest::shaderFailureSkipsTheEntry()
{
    startUpscaling();
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateVideoFormat(QSize(1280, 720));
    QTRY_COMPARE(m_controller->upscaleActive(), QStringLiteral("FSRCNNX 16"));

    m_backend->simulateShaderFailure(m_backend->processingRequests.last().shaderPaths.constFirst());
    QCOMPARE(m_controller->upscaleActive(), QStringLiteral("FSRCNNX 8"));
    QCOMPARE(m_controller->upscaleReason(),
             QStringLiteral("FSRCNNX 16 failed to load — using FSRCNNX 8 · 720p → 1440p"));
    QVERIFY(m_backend->processingRequests.last().shaderPaths.constFirst().endsWith(
        QStringLiteral("FSRCNNX_x2_8-0-4-1.glsl")));
    for (const QVariant &choice : m_controller->upscaleChoices())
        QVERIFY(choice.toMap().value(QStringLiteral("id")).toString() != QLatin1String("fsrcnnx16"));
}

void PlayerControllerTest::pipShrinkDropsChainAndRestoresIt()
{
    startUpscaling();
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateVideoFormat(QSize(1280, 720));
    QTRY_COMPARE(m_controller->upscaleActive(), QStringLiteral("FSRCNNX 16"));

    m_controller->setVideoOutputSize(360, 203); // the picture-in-picture frame
    QTRY_VERIFY(m_backend->processingRequests.last().isDefault());
    QCOMPARE(m_controller->upscaleActive(), QStringLiteral("Off"));

    m_controller->setVideoOutputSize(2560, 1440); // back to the player page
    QTRY_COMPARE(m_controller->upscaleActive(), QStringLiteral("FSRCNNX 16"));
    QVERIFY(!m_backend->processingRequests.last().isDefault());
}

void PlayerControllerTest::audioAfterFilmClearsTheChain()
{
    startUpscaling();
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateVideoFormat(QSize(1280, 720));
    QTRY_COMPARE(m_controller->upscaleActive(), QStringLiteral("FSRCNNX 16"));

    // The details reply must agree, or fetchChapters() resets the item type.
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/301001").arg(kUserId),
                     200, R"json({"Id": "301001", "Name": "A Song", "Type": "Audio"})json");
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("A Song"), 0, -1,
                           QStringLiteral("Audio"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QVERIFY(m_controller->isAudio());
    QVERIFY(m_backend->processingRequests.last().isDefault());
    // mpv shows embedded cover art as a video stream; it is not upscaled.
    m_backend->simulateVideoFormat(QSize(600, 600));
    QTest::qWait(50);
    QVERIFY(m_backend->processingRequests.last().isDefault());
}

void PlayerControllerTest::resizeStormPushesOnce()
{
    startUpscaling();
    m_controller->setUpscaleDebounceForTests(100);
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateVideoFormat(QSize(1280, 720));
    QTRY_COMPARE(m_controller->upscaleActive(), QStringLiteral("FSRCNNX 16"));
    const qsizetype pushes = m_backend->processingRequests.size();

    for (int width = 2400; width >= 1400; width -= 100)
        m_controller->setVideoOutputSize(width, width * 9 / 16);
    // Nothing during the storm: only the debounce may push, not the sizes
    // passing through (the chain dedupe alone would also give one push here).
    QCOMPARE(m_backend->processingRequests.size(), pushes);
    QTRY_COMPARE(m_backend->processingRequests.size(), pushes + 1);
    QTest::qWait(250);
    QCOMPARE(m_backend->processingRequests.size(), pushes + 1);
    QVERIFY(m_backend->processingRequests.last().shaderPaths.constFirst().endsWith(
        QStringLiteral("FSR.glsl"))); // 720p in 1400×787: 1.09×
}

void PlayerControllerTest::seekStartsTheGovernorGrace()
{
    startUpscaling();
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(600'000);
    m_backend->simulateVideoFormat(QSize(1280, 720));
    QTRY_COMPARE(m_controller->upscaleActive(), QStringLiteral("FSRCNNX 16"));

    // Frames render throughout, so a summary comes every 500 ms.
    const auto rendering = [this](qint64 from, qint64 to) {
        for (m_upscaleNow = from; m_upscaleNow <= to; m_upscaleNow += 500)
            m_controller->reportGpuTiming(5.0, 4.0, 12);
        m_upscaleNow = to;
    };
    rendering(9'000, 10'000); // well past every earlier grace period
    m_backend->simulateFrameDrops(0);
    m_controller->seekTo(300'000);
    rendering(10'500, 10'500);
    m_backend->simulateFrameDrops(4); // the seek's transient
    QCOMPARE(m_controller->upscaleCeiling(), 4);
    rendering(11'000, 13'000);
    m_backend->simulateFrameDrops(5); // a real drop
    QCOMPARE(m_controller->upscaleCeiling(), 3);
    QCOMPARE(m_controller->upscaleActive(), QStringLiteral("FSRCNNX 8"));
}

// The watchdog's nudge is a seek like any other: its transient is not the GPU's.
void PlayerControllerTest::watchdogNudgeStartsTheGovernorGrace()
{
    startUpscaling();
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateVideoFormat(QSize(1280, 720));
    QTRY_COMPARE(m_controller->upscaleActive(), QStringLiteral("FSRCNNX 16"));

    m_upscaleNow = 10'000; // well past every earlier grace period
    m_controller->reportGpuTiming(5.0, 4.0, 12);
    m_backend->simulateFrameDrops(0);
    m_backend->simulatePosition(100'000); // then frozen: the watchdog nudges
    QTRY_VERIFY(m_backend->seeks.contains(Q_INT64_C(101000)));
    m_upscaleNow = 10'500;
    m_controller->reportGpuTiming(5.0, 4.0, 12);
    m_backend->simulateFrameDrops(4); // the nudge's transient
    QCOMPARE(m_controller->upscaleCeiling(), 4);
}

// Frames dropped at 2–4× are the speed's, not the GPU's: neither a fast-forward
// hold nor a user speed may lower the ceiling saved for this GPU.
void PlayerControllerTest::playbackSpeedNeverStepsTheCeiling()
{
    startUpscaling();
    m_backend->maxSpeed = 4.0;
    m_controller->setSkipHoldClockForTests([this] { return m_upscaleNow; });
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(600'000);
    m_backend->simulateVideoFormat(QSize(1280, 720));
    QTRY_COMPARE(m_controller->upscaleActive(), QStringLiteral("FSRCNNX 16"));
    const auto overBudget = [this](qint64 from, qint64 to) {
        for (m_upscaleNow = from; m_upscaleNow <= to; m_upscaleNow += 500)
            m_controller->reportGpuTiming(100.0, 90.0, 12);
    };

    // A fast-forward hold: the engine plays at 2×, no seek, no grace period.
    m_upscaleNow = 10'000;
    m_backend->simulateFrameDrops(0);
    m_controller->skipForwardPressed();
    m_upscaleNow = 10'400;
    m_controller->tickSkipHoldForTests();
    QCOMPARE(m_backend->playbackSpeed(), 2.0);
    // Frames render (summaries keep coming), so only the hold keeps the drop
    // from counting.
    overBudget(13'000, 17'000);
    m_backend->simulateFrameDrops(3);
    QCOMPARE(m_controller->upscaleCeiling(), 4);
    m_controller->skipForwardReleased();
    QCOMPARE(m_backend->playbackSpeed(), 1.0);

    // A user speed of 2×.
    m_upscaleNow = 17'500;
    m_controller->setPlaybackSpeed(2.0);
    overBudget(20'000, 24'000);
    m_backend->simulateFrameDrops(6);
    QCOMPARE(m_controller->upscaleCeiling(), 4);

    // Back at 1×: the change is a transient, and after it a drop steps as usual.
    // Frames keep rendering throughout, so summaries keep coming.
    m_upscaleNow = 25'000;
    m_controller->reportGpuTiming(5.0, 4.0, 12);
    m_controller->setPlaybackSpeed(1.0);
    m_upscaleNow = 26'000;
    m_controller->reportGpuTiming(5.0, 4.0, 12);
    m_backend->simulateFrameDrops(7);
    QCOMPARE(m_controller->upscaleCeiling(), 4);
    m_upscaleNow = 27'500;
    m_controller->reportGpuTiming(5.0, 4.0, 12);
    m_backend->simulateFrameDrops(8);
    QCOMPARE(m_controller->upscaleCeiling(), 3);
    QCOMPARE(m_controller->upscaleActive(), QStringLiteral("FSRCNNX 8"));
}

// ── Gapless advance ───────────────────────────────────────────────────────────

void PlayerControllerTest::addAudioItem(const QString &id)
{
    QFile fixture(fixturePath(QStringLiteral("playback_info_audio.json")));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    QByteArray body = fixture.readAll();
    body.replace("301004", id.toUtf8());
    body.replace("ps0014", "ps" + id.toUtf8());
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Items/%1/PlaybackInfo").arg(id), 200,
                     body);
}

QVariantMap PlayerControllerTest::audioEntry(const QString &id, const QString &name) const
{
    return {{QStringLiteral("itemId"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("label"), name},
            {QStringLiteral("type"), QStringLiteral("Audio")},
            {QStringLiteral("runtimeMs"), 200'000}};
}

QVariantList PlayerControllerTest::album(int tracks)
{
    QVariantList entries;
    for (int n = 1; n <= tracks; ++n) {
        const QString id = QStringLiteral("50100%1").arg(n);
        addAudioItem(id);
        entries.append(audioEntry(id, QStringLiteral("Track %1").arg(n)));
    }
    return entries;
}

void PlayerControllerTest::startGaplessAlbum(int tracks)
{
    m_backend->prefetchSupported = true;
    m_controller->setTimingForTests(60'000, 2, 10);
    const qsizetype loadsBefore = m_backend->loadedUrls.size();
    m_controller->playQueue(album(tracks), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), loadsBefore + 1);
    QVERIFY(m_backend->loadedUrls.constLast().toString().contains(QStringLiteral("501001")));
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(200'000);
    m_backend->simulatePosition(10'000);
    QVERIFY(m_controller->isAudio());
}

void PlayerControllerTest::reachQueuedPrefetch()
{
    m_backend->simulatePosition(190'000);
    QTRY_VERIFY(m_backend->queuedPrefetch().loadId != 0);
}

int PlayerControllerTest::requestCount(const QString &method, const QString &path) const
{
    int count = 0;
    for (const MockEmbyServer::ReceivedRequest &request : m_mock->requests()) {
        if (request.method == method && request.path == path)
            ++count;
    }
    return count;
}

int PlayerControllerTest::playbackInfoRequests(const QString &itemId) const
{
    return requestCount(QStringLiteral("POST"), QStringLiteral("/Items/%1/PlaybackInfo").arg(itemId));
}

QJsonObject PlayerControllerTest::lastBody(const QString &path) const
{
    return QJsonDocument::fromJson(m_mock->lastRequestFor(QStringLiteral("POST"), path).body)
        .object();
}

// Nothing is asked for until the last 15 s, then the next track is asked for
// once and handed to the engine; nothing about the playing track changes.
void PlayerControllerTest::gaplessPrefetchWaitsForTheTailOfAMusicTrack()
{
    startGaplessAlbum(3);
    m_backend->simulatePosition(184'000); // 16 s left
    QTest::qWait(50);
    QCOMPARE(playbackInfoRequests(QStringLiteral("501002")), 0);
    QVERIFY(m_backend->prefetchRequests.isEmpty());

    m_backend->simulatePosition(186'000); // 14 s left
    QTRY_COMPARE(m_backend->prefetchRequests.size(), 1);
    QCOMPARE(playbackInfoRequests(QStringLiteral("501002")), 1);
    const FakePlayerBackend::Prefetch request = m_backend->prefetchRequests.constFirst();
    QVERIFY(request.url.toString().contains(QStringLiteral("501002")));
    QCOMPARE(request.startMs, Q_INT64_C(0));
    QVERIFY(request.loadId != 0);
    QVERIFY(!m_backend->loadedIds.contains(request.loadId));
    QCOMPARE(m_backend->queuedPrefetch().loadId, request.loadId);

    // Once per track, however many ticks follow.
    m_backend->simulatePosition(190'000);
    m_backend->simulatePosition(195'000);
    m_backend->simulateDuration(200'000);
    QTest::qWait(50);
    QCOMPARE(playbackInfoRequests(QStringLiteral("501002")), 1);
    QCOMPARE(m_backend->prefetchRequests.size(), 1);
    QCOMPARE(m_backend->loadedUrls.size(), 1);
    QCOMPARE(m_controller->title(), QStringLiteral("Track 1"));
    QCOMPARE(m_controller->queue()->currentIndex(), 0);

    // A track shorter than the lead time is inside it as soon as it plays.
    QVariantList shortTracks = album(2);
    m_controller->playQueue(shortTracks, 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_backend->queuedPrefetch().loadId, PlayerBackend::LoadId(0));
    m_backend->simulateDuration(9'000);
    QTest::qWait(30);
    QCOMPARE(m_backend->prefetchRequests.size(), 1); // not before it is playing
    m_backend->simulateState(PlayerBackend::State::Playing);
    QTRY_COMPARE(m_backend->prefetchRequests.size(), 2);
    QVERIFY(m_backend->prefetchRequests.constLast().url.toString().contains(QStringLiteral("501002")));
}

void PlayerControllerTest::gaplessPrefetchOnlyForMusicIntoMusicInADeliberateQueue()
{
    m_backend->prefetchSupported = true;
    m_controller->setTimingForTests(60'000, 2, 10);
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("POST"),
                                     QStringLiteral("/Items/301002/PlaybackInfo"),
                                     fixturePath(QStringLiteral("playback_info.json"))));
    const auto playTail = [this](const QVariantList &queue) {
        const qsizetype loads = m_backend->loadedUrls.size();
        m_controller->playQueue(queue, 0);
        QTRY_COMPARE(m_backend->loadedUrls.size(), loads + 1);
        m_backend->simulateState(PlayerBackend::State::Playing);
        m_backend->simulateDuration(200'000);
        m_backend->simulatePosition(190'000);
        QTest::qWait(50);
    };
    const QVariantMap episodeOne{{QStringLiteral("itemId"), QStringLiteral("301001")},
                                 {QStringLiteral("name"), QStringLiteral("Episode One")},
                                 {QStringLiteral("type"), QStringLiteral("Episode")}};
    QVariantMap episodeTwo = episodeOne;
    episodeTwo[QStringLiteral("itemId")] = QStringLiteral("301002");

    // An engine that cannot is not asked, and costs no request.
    m_backend->prefetchSupported = false;
    playTail(album(2));
    QCOMPARE(playbackInfoRequests(QStringLiteral("501002")), 0);
    m_backend->prefetchSupported = true;

    // One item played on its own: there is no next track.
    playTail({audioEntry(QStringLiteral("501001"), QStringLiteral("Track 1"))});
    // Repeat-one: the next track is this one again.
    {
        const qsizetype loads = m_backend->loadedUrls.size();
        m_controller->playQueue(album(2), 0);
        m_controller->queue()->setRepeatMode(PlayQueue::RepeatOne);
        QTRY_COMPARE(m_backend->loadedUrls.size(), loads + 1);
        m_backend->simulateState(PlayerBackend::State::Playing);
        m_backend->simulateDuration(200'000);
        m_backend->simulatePosition(190'000);
        QTest::qWait(50);
        m_controller->queue()->setRepeatMode(PlayQueue::RepeatOff);
        QTRY_COMPARE(m_backend->prefetchRequests.size(), 1); // and it is off again
        m_controller->stop();
    }
    // A record into a film; a film into a film.
    playTail({audioEntry(QStringLiteral("501001"), QStringLiteral("Track 1")), episodeOne});
    playTail({episodeOne, episodeTwo});
    QCOMPARE(playbackInfoRequests(QStringLiteral("301002")), 0);
    QCOMPARE(m_backend->prefetchRequests.size(), 1);

    // Not while ⏭ is held: the hold seeks and runs the engine at 4×.
    qint64 now = 0;
    m_controller->setSkipHoldClockForTests([&now] { return now; });
    m_backend->maxSpeed = 4.0;
    playTail(album(2)); // already in the tail: asked for
    QTRY_VERIFY(m_backend->queuedPrefetch().loadId != 0);
    const int cancels = m_backend->prefetchCancels;
    m_controller->skipForwardPressed();
    now = 500;
    m_controller->tickSkipHoldForTests();
    QVERIFY(m_controller->fastForwardRate() > 0.0);
    QCOMPARE(m_backend->prefetchCancels, cancels + 1);
    QCOMPARE(m_backend->queuedPrefetch().loadId, PlayerBackend::LoadId(0));
    m_backend->simulatePosition(191'000);
    QTest::qWait(50);
    QCOMPARE(m_backend->queuedPrefetch().loadId, PlayerBackend::LoadId(0));
    const qsizetype asked = m_backend->prefetchRequests.size();
    now = 600;
    m_controller->skipForwardReleased();
    QCOMPARE(m_controller->fastForwardRate(), 0.0);
    QTRY_COMPARE(m_backend->prefetchRequests.size(), asked + 1);

    // A dismissed Up Next card means this track ends the session.
    m_controller->cancelUpNext();
    QCOMPARE(m_backend->queuedPrefetch().loadId, PlayerBackend::LoadId(0));
    QSignalSpy stopped(m_controller, &PlayerController::stopped);
    m_backend->simulateEnd();
    QCOMPARE(stopped.count(), 1);
}

// The engine moving on by itself leaves the controller, the queue and the
// server exactly where an end followed by an ordinary advance would — with no
// PlaybackInfo and no load at the change.
void PlayerControllerTest::gaplessHandoverMatchesAnOrdinaryAdvance()
{
    startGaplessAlbum(3);
    const PlayerBackend::LoadId first = m_backend->loadedIds.constLast();
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing")), 1);
    reachQueuedPrefetch();
    const PlayerBackend::LoadId second = m_backend->queuedPrefetch().loadId;
    const int infoRequests = playbackInfoRequests(QStringLiteral("501002"));

    QSignalSpy stopped(m_controller, &PlayerController::stopped);
    QSignalSpy itemStarted(m_controller, &PlayerController::itemStarted);
    QSignalSpy cursor(m_controller->queue(), &PlayQueue::currentItemChanged);
    QSignalSpy queueState(m_controller, &PlayerController::queueStateChanged);
    m_backend->simulatePosition(199'900);
    m_backend->simulateEnd();

    QCOMPARE(m_backend->loadedUrls.size(), 1);
    QCOMPARE(m_backend->stopCalls, 0);
    QCOMPARE(playbackInfoRequests(QStringLiteral("501002")), infoRequests);
    QCOMPARE(stopped.count(), 0);
    QCOMPARE(itemStarted.count(), 0); // an advance, not a choice
    QVERIFY(m_controller->active());
    QCOMPARE(m_controller->queue()->currentIndex(), 1);
    QCOMPARE(cursor.count(), 1);
    QVERIFY(queueState.count() > 0);
    QCOMPARE(m_controller->title(), QStringLiteral("Track 2"));
    QCOMPARE(m_controller->streamMethod(), QStringLiteral("DirectPlay"));
    QCOMPARE(m_controller->sourceCount(), 1);
    QVERIFY(m_controller->busy());
    QVERIFY(!m_controller->engineReady());
    QVERIFY(m_controller->isAudio());
    QCOMPARE(m_controller->positionMs(), Q_INT64_C(0));

    // The finished track was reported stopped at its full runtime, so it is
    // marked played, under its own ticket.
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped")), 1);
    QJsonObject body = lastBody(QStringLiteral("/Sessions/Playing/Stopped"));
    QCOMPARE(body.value(QLatin1String("ItemId")).toString(), QStringLiteral("501001"));
    QCOMPARE(body.value(QLatin1String("PlaySessionId")).toString(), QStringLiteral("ps501001"));
    QCOMPARE(static_cast<qint64>(body.value(QLatin1String("PositionTicks")).toDouble()),
             Q_INT64_C(200000) * kTicksPerMs);

    // The engine's next events are the new track's, under the prefetch's id.
    m_backend->simulateState(PlayerBackend::State::Playing);
    QVERIFY(!m_controller->busy());
    QVERIFY(m_controller->engineReady());
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing")), 2);
    body = lastBody(QStringLiteral("/Sessions/Playing"));
    QCOMPARE(body.value(QLatin1String("ItemId")).toString(), QStringLiteral("501002"));
    QCOMPARE(body.value(QLatin1String("PlaySessionId")).toString(), QStringLiteral("ps501002"));
    QCOMPARE(body.value(QLatin1String("PlayMethod")).toString(), QStringLiteral("DirectPlay"));
    QCOMPARE(m_controller->crashResumeInfo().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("501002"));
    m_backend->simulateDuration(200'000, second);
    m_backend->simulatePosition(5'000, second);
    QCOMPARE(m_controller->positionSeconds(), Q_INT64_C(5));
    m_backend->simulatePosition(150'000, first); // the finished load is not listened to
    QCOMPARE(m_controller->positionSeconds(), Q_INT64_C(5));
    QCOMPARE(m_backend->stopCalls, 0);

    // And it chains: track 2's tail queues track 3, whose end ends the album.
    reachQueuedPrefetch();
    QVERIFY(m_backend->queuedPrefetch().url.toString().contains(QStringLiteral("501003")));
    m_backend->simulateEnd();
    QCOMPARE(m_controller->title(), QStringLiteral("Track 3"));
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(200'000);
    m_backend->simulatePosition(195'000);
    QTest::qWait(50);
    QCOMPARE(m_backend->queuedPrefetch().loadId, PlayerBackend::LoadId(0));
    m_backend->simulateEnd();
    QCOMPARE(stopped.count(), 1);
    QVERIFY(!m_controller->active());
    QCOMPARE(m_backend->loadedUrls.size(), 1);
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped")), 3);
    QCOMPARE(requestCount(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing")), 3);
}

// Whatever happens to the queue, what the engine holds is what comes next.
void PlayerControllerTest::gaplessPrefetchFollowsWhatIsNext()
{
    startGaplessAlbum(4);
    reachQueuedPrefetch();
    PlayQueue *queue = m_controller->queue();
    const auto queuedIsNext = [this, queue] {
        const QString next = queue->itemAt(queue->nextIndex()).value(QStringLiteral("itemId")).toString();
        return m_backend->queuedPrefetch().loadId != 0 &&
               m_backend->queuedPrefetch().url.toString().contains(next);
    };
    QVERIFY(queuedIsNext());

    queue->moveItem(3, 1); // Track 4 is next now
    QCOMPARE(m_backend->prefetchCancels, 1);
    QTRY_VERIFY(queuedIsNext());
    QVERIFY(m_backend->queuedPrefetch().url.toString().contains(QStringLiteral("501004")));

    queue->removeAt(1); // and gone again: Track 2
    QCOMPARE(m_backend->prefetchCancels, 2);
    QTRY_VERIFY(queuedIsNext());
    QVERIFY(m_backend->queuedPrefetch().url.toString().contains(QStringLiteral("501002")));

    // Track 3 played next — the same item as the copy later on, another entry.
    queue->playNext(QVariant(audioEntry(QStringLiteral("501003"), QStringLiteral("Track 3"))));
    QCOMPARE(m_backend->prefetchCancels, 3);
    QTRY_VERIFY(queuedIsNext());
    QVERIFY(m_backend->queuedPrefetch().url.toString().contains(QStringLiteral("501003")));

    // An edit further down changes nothing.
    queue->removeAt(queue->rowCount() - 1);
    QCOMPARE(m_backend->prefetchCancels, 3);

    // Repeat-one: dropped, and not asked for again until it is off.
    const qsizetype asked = m_backend->prefetchRequests.size();
    queue->setRepeatMode(PlayQueue::RepeatOne);
    QCOMPARE(m_backend->prefetchCancels, 4);
    QTest::qWait(50);
    QCOMPARE(m_backend->prefetchRequests.size(), asked);
    queue->setRepeatMode(PlayQueue::RepeatOff);
    QTRY_VERIFY(queuedIsNext());

    // Shuffled: whatever the deal put next is what is held.
    queue->setShuffled(true);
    QTRY_VERIFY(queuedIsNext());
    queue->setShuffled(false);
    QTRY_VERIFY(queuedIsNext());

    // Nothing of this reloaded the track playing.
    QCOMPARE(m_backend->loadedUrls.size(), 1);
    QCOMPARE(m_controller->title(), QStringLiteral("Track 1"));
}

// Every user action that ends or replaces the playing track takes the
// prefetch with it.
void PlayerControllerTest::gaplessPrefetchIsDroppedWithTheTrack()
{
    const PlayerBackend::LoadId none = 0;

    // A seek back out of the lead time drops it; coming back asks again.
    startGaplessAlbum(3);
    reachQueuedPrefetch();
    m_controller->seekTo(60'000);
    QCOMPARE(m_backend->queuedPrefetch().loadId, none);
    QCOMPARE(m_backend->prefetchCancels, 1);
    reachQueuedPrefetch();
    QCOMPARE(playbackInfoRequests(QStringLiteral("501002")), 2);

    // A skip: the next track starts the ordinary way.
    m_controller->playNext();
    QCOMPARE(m_backend->queuedPrefetch().loadId, none);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QVERIFY(m_backend->loadedUrls.constLast().toString().contains(QStringLiteral("501002")));

    // A stop.
    startGaplessAlbum(3);
    reachQueuedPrefetch();
    m_controller->stop();
    QCOMPARE(m_backend->queuedPrefetch().loadId, none);

    // A film chosen over the record.
    startGaplessAlbum(3);
    reachQueuedPrefetch();
    const qsizetype loads = m_backend->loadedUrls.size();
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0, -1,
                           QStringLiteral("Movie"));
    QCOMPARE(m_backend->queuedPrefetch().loadId, none);
    QTRY_COMPARE(m_backend->loadedUrls.size(), loads + 1);
    m_controller->stop();
    QTRY_COMPARE(m_controller->queue()->currentItem().value(QStringLiteral("itemId")).toString(),
                 QStringLiteral("501001")); // the record is back, and waits paused
    m_controller->stop();

    // A failure of the playing track: its recovery reloads it, and the
    // prefetch is asked for again once it plays.
    startGaplessAlbum(3);
    reachQueuedPrefetch();
    const qsizetype beforeError = m_backend->loadedUrls.size();
    m_backend->simulateError(QStringLiteral("connection reset"));
    QCOMPARE(m_backend->queuedPrefetch().loadId, none);
    QTRY_COMPARE(m_backend->loadedUrls.size(), beforeError + 1);
    QVERIFY(m_backend->loadedUrls.constLast().toString().contains(QStringLiteral("501001")));
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(200'000);
    reachQueuedPrefetch();

    // A sign-out.
    m_controller->shutdownForSessionBoundary();
    QCOMPARE(m_backend->queuedPrefetch().loadId, none);
}

// The next track's ticket lands after the user has already moved on.
void PlayerControllerTest::lateNextTrackTicketIsIgnored()
{
    startGaplessAlbum(3);
    m_mock->setRouteDelay(QStringLiteral("POST"), QStringLiteral("/Items/501002/PlaybackInfo"), 300);
    m_backend->simulatePosition(190'000);
    QTRY_COMPARE(playbackInfoRequests(QStringLiteral("501002")), 1);
    m_controller->playNext();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QTest::qWait(400);
    QVERIFY(m_backend->prefetchRequests.isEmpty());
    QCOMPARE(m_controller->title(), QStringLiteral("Track 2"));

    // ...and after the track has ended: the ordinary advance has it.
    startGaplessAlbum(3);
    m_backend->simulatePosition(190'000);
    QTRY_COMPARE(playbackInfoRequests(QStringLiteral("501002")), 3);
    m_backend->simulateEnd();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 4);
    QVERIFY(m_backend->loadedUrls.constLast().toString().contains(QStringLiteral("501002")));
    QTest::qWait(400);
    QVERIFY(m_backend->prefetchRequests.isEmpty());
    QCOMPARE(m_controller->queue()->currentIndex(), 1);
}

void PlayerControllerTest::failedGaplessPrefetchFallsBack()
{
    // The engine will not take it: asked once, and the end advances as ever.
    m_backend->prefetchAccepted = false;
    startGaplessAlbum(3);
    m_backend->simulatePosition(190'000);
    QTRY_COMPARE(m_backend->prefetchRequests.size(), 1);
    m_backend->simulatePosition(192'000);
    m_backend->simulatePosition(194'000);
    QTest::qWait(50);
    QCOMPARE(m_backend->prefetchRequests.size(), 1);
    QCOMPARE(playbackInfoRequests(QStringLiteral("501002")), 1);
    m_backend->simulateEnd();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QVERIFY(m_backend->loadedUrls.constLast().toString().contains(QStringLiteral("501002")));
    QCOMPARE(m_controller->title(), QStringLiteral("Track 2"));
    m_controller->stop();

    // The ticket request fails: not retried, and the end advances as ever.
    m_backend->prefetchAccepted = true;
    m_backend->prefetchRequests.clear();
    startGaplessAlbum(3);
    m_mock->enqueueRoute(QStringLiteral("POST"), QStringLiteral("/Items/501002/PlaybackInfo"), 500,
                         "{}");
    const int asked = playbackInfoRequests(QStringLiteral("501002"));
    m_backend->simulatePosition(190'000);
    QTRY_COMPARE(playbackInfoRequests(QStringLiteral("501002")), asked + 1);
    m_backend->simulatePosition(194'000);
    QTest::qWait(100);
    QCOMPARE(playbackInfoRequests(QStringLiteral("501002")), asked + 1);
    QVERIFY(m_backend->prefetchRequests.isEmpty());
    const qsizetype loads = m_backend->loadedUrls.size();
    m_backend->simulateEnd();
    QTRY_COMPARE(m_backend->loadedUrls.size(), loads + 1);
    QCOMPARE(m_controller->title(), QStringLiteral("Track 2"));
    m_controller->stop();

    // The queued file does not open: the handover has happened, and the new
    // track's own ladder takes over exactly as for a load that failed.
    startGaplessAlbum(3);
    reachQueuedPrefetch();
    const PlayerBackend::LoadId second = m_backend->queuedPrefetch().loadId;
    const qsizetype before = m_backend->loadedUrls.size();
    QSignalSpy stopped(m_controller, &PlayerController::stopped);
    m_backend->simulateEnd();
    m_backend->simulateError(QStringLiteral("cannot open"), second);
    QTRY_COMPARE(m_backend->loadedUrls.size(), before + 1);
    QVERIFY(m_backend->loadedUrls.constLast().toString().contains(QStringLiteral("501002")));
    QCOMPARE(m_controller->queue()->currentIndex(), 1);
    QCOMPARE(stopped.count(), 0);
    QVERIFY(m_controller->streamMethod() != QStringLiteral("DirectPlay"));
}

namespace {
// An engine that had already moved on by the time it was told not to: the
// cancel reaches it after mpv started the entry.
class LateCancelBackend : public FakePlayerBackend
{
public:
    using FakePlayerBackend::FakePlayerBackend;
    void cancelPrefetch() override { ++prefetchCancels; }
};
} // namespace

void PlayerControllerTest::engineGoingOnToAnUnwantedTrackIsReplaced()
{
    LateCancelBackend backend;
    backend.prefetchSupported = true;
    PlayerController controller(m_client, &backend, m_settings);
    controller.setTimingForTests(60'000, 2, 10);
    controller.playQueue(album(3), 0);
    QTRY_COMPARE(backend.loadedUrls.size(), 1);
    backend.simulateState(PlayerBackend::State::Playing);
    backend.simulateDuration(200'000);
    backend.simulatePosition(190'000);
    QTRY_VERIFY(backend.queuedPrefetch().loadId != 0);

    // Track 3 is next now; the engine still holds Track 2 and plays it.
    m_mock->setRouteDelay(QStringLiteral("POST"), QStringLiteral("/Items/501003/PlaybackInfo"), 200);
    controller.queue()->moveItem(2, 1);
    QCOMPARE(backend.prefetchCancels, 1);
    backend.simulateEnd();

    // Treated as the end it was: the ordinary advance, into Track 3.
    QTRY_COMPARE(backend.loadedUrls.size(), 2);
    QVERIFY(backend.loadedUrls.constLast().toString().contains(QStringLiteral("501003")));
    QCOMPARE(controller.title(), QStringLiteral("Track 3"));
    QCOMPARE(controller.queue()->currentIndex(), 1);
    QVERIFY(backend.stopCalls >= 1);
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped")), 1);
    const QJsonObject body = lastBody(QStringLiteral("/Sessions/Playing/Stopped"));
    QCOMPARE(body.value(QLatin1String("ItemId")).toString(), QStringLiteral("501001"));
    QCOMPARE(static_cast<qint64>(body.value(QLatin1String("PositionTicks")).toDouble()),
             Q_INT64_C(200000) * kTicksPerMs);
}

QTEST_GUILESS_MAIN(PlayerControllerTest)
#include "tst_player_controller.moc"
