#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/controllers/PlayerController.h"
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

// A real engine publishes the post-seek position asynchronously; the fake
// applies it inside seekTo(), which hides exactly the staleness the seek tests
// below are about.
class LazySeekBackend : public FakePlayerBackend
{
public:
    using FakePlayerBackend::FakePlayerBackend;
    void seekTo(qint64 positionMs) override { seeks.append(positionMs); }
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
    void brokenTailErrorIsCleanEnd();
    void midStreamErrorRefetchesTicket();
    void crashResumePersistsAndClears();
    void ladderDemotesOnStartupFailure();
    void allRungsFailingSurfacesError();
    void endReachedReportsFullRuntime();
    void stopReportsAndDeactivates();
    void seekAndPauseReportProgress();
    void seekAdoptsItsTargetBeforeTheEngineReportsIt();

    void demotionStaysWithinSelectedSource();
    void preferredSourceHonouredAtStart();
    void setPreferredSourceSwitchesVersionMidSession();
    void sourceSurfaceExposesStreams();
    void unplayableSourceSelectionIsIgnored();

private:
    void expectReport(const QString &path, const QString &method, bool paused = false);

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_controller = nullptr;
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

void PlayerControllerTest::seekAndPauseReportProgress()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);

    m_controller->seekRelative(30'000);
    QCOMPARE(m_backend->seeks.size(), 1);
    QCOMPARE(m_backend->seeks[0], Q_INT64_C(30000));
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
}

void PlayerControllerTest::brokenTailErrorIsCleanEnd()
{
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(8'184'000);
    m_backend->simulatePosition(8'182'000); // 2 s before EOF

    QSignalSpy stoppedSpy(m_controller, &PlayerController::stopped);
    m_backend->simulateError(QStringLiteral("demuxer: broken tail"));

    // Clean end: stopped emitted, NO user-facing error, played position pinned.
    QTRY_COMPARE(stoppedSpy.count(), 1);
    QVERIFY(m_controller->errorMessage().isEmpty());
    QTRY_VERIFY(
        !m_mock->lastRequestFor(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped"))
             .method.isEmpty());
    const QJsonObject body =
        QJsonDocument::fromJson(m_mock
                                    ->lastRequestFor(QStringLiteral("POST"),
                                                     QStringLiteral("/Sessions/Playing/Stopped"))
                                    .body)
            .object();
    QCOMPARE(static_cast<qint64>(body.value(QLatin1String("PositionTicks")).toDouble()),
             Q_INT64_C(8184000) * kTicksPerMs);
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

QTEST_GUILESS_MAIN(PlayerControllerTest)
#include "tst_player_controller.moc"
