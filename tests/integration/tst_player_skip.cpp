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

private:
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

QTEST_GUILESS_MAIN(PlayerSkipTest)
#include "tst_player_skip.moc"
