#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/PlayQueue.h"
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

QVariantMap itemMap(const QString &id, const QString &name)
{
    QVariantMap map;
    map.insert(QStringLiteral("itemId"), id);
    map.insert(QStringLiteral("name"), name);
    map.insert(QStringLiteral("label"), name);
    map.insert(QStringLiteral("type"), QStringLiteral("Episode"));
    map.insert(QStringLiteral("runtimeMs"), 60'000);
    return map;
}

} // namespace

// Auto-advance, Up Next and prev/next where they actually live: on top of the
// existing robustness layer (ladder, watchdog, broken-tail EOF, reporting).
class QueuePlaybackTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void playQueueStartsTheFirstItem();
    void cleanEndAdvancesAndOnlyExhaustionStops();
    void brokenTailAdvancesInsteadOfStopping();
    void upNextAppearsInTheTailWindow();
    void cancelUpNextSuppressesOnlyThisItem();
    void playNextReportsStoppedAtTheRealPosition();
    void playPreviousRestartsThenStepsBack();
    void jumpingInTheQueuePanelChangesItem();
    void removingTheCurrentItemContinuesWithTheNext();
    void repeatOneReplaysTheSameItem();
    void aBarePlayItemIsAOneItemQueue();
    void queueEditsAfterStopDoNotStartPlayback();
    void changingItemClosesTheOutgoingSession();

private:
    QVariantList threeItems() const;
    int requestCount(const QString &method, const QString &path) const;

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_controller = nullptr;
};

void QueuePlaybackTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    // Three queueable items; one ticket fixture answers for all of them.
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

void QueuePlaybackTest::cleanup()
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

QVariantList QueuePlaybackTest::threeItems() const
{
    return {itemMap(QStringLiteral("301001"), QStringLiteral("Episode One")),
            itemMap(QStringLiteral("301002"), QStringLiteral("Episode Two")),
            itemMap(QStringLiteral("301003"), QStringLiteral("Episode Three"))};
}

int QueuePlaybackTest::requestCount(const QString &method, const QString &path) const
{
    int count = 0;
    for (const MockEmbyServer::ReceivedRequest &request : m_mock->requests()) {
        if (request.method == method.toUpper() && request.path == path)
            ++count;
    }
    return count;
}

void QueuePlaybackTest::playQueueStartsTheFirstItem()
{
    m_controller->playQueue(threeItems(), 0);
    QCOMPARE(m_controller->queue()->rowCount(), 3);
    QCOMPARE(m_controller->queue()->currentIndex(), 0);
    QVERIFY(m_controller->hasNext());
    QVERIFY(!m_controller->hasPrevious());
    QCOMPARE(m_controller->nextItem().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301002"));
    QCOMPARE(m_controller->title(), QStringLiteral("Episode One"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    QVERIFY(m_controller->active());

    // Starting in the middle is the "play from this episode on" case.
    m_controller->playQueue(threeItems(), 2);
    QCOMPARE(m_controller->queue()->currentIndex(), 2);
    QCOMPARE(m_controller->title(), QStringLiteral("Episode Three"));
    QVERIFY(!m_controller->hasNext());
    QVERIFY(m_controller->hasPrevious());
    QVERIFY(m_controller->nextItem().isEmpty());
}

// The heart of it: a clean end is a queue event now. Only an exhausted queue
// still ends the session.
void QueuePlaybackTest::cleanEndAdvancesAndOnlyExhaustionStops()
{
    QSignalSpy stoppedSpy(m_controller, &PlayerController::stopped);
    m_controller->playQueue(threeItems(), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(60'000);
    m_backend->simulatePosition(59'000);

    m_backend->simulateEnd();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(stoppedSpy.count(), 0); // the session did NOT end
    QVERIFY(m_controller->active());
    QCOMPARE(m_controller->queue()->currentIndex(), 1);
    QCOMPARE(m_controller->title(), QStringLiteral("Episode Two"));
    // The finished item was still reported played, at its full runtime.
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped")),
                 1);
    const QJsonObject stopped =
        QJsonDocument::fromJson(m_mock
                                    ->lastRequestFor(QStringLiteral("POST"),
                                                     QStringLiteral("/Sessions/Playing/Stopped"))
                                    .body)
            .object();
    QCOMPARE(stopped.value(QLatin1String("ItemId")).toString(), QStringLiteral("301001"));
    QCOMPARE(static_cast<qint64>(stopped.value(QLatin1String("PositionTicks")).toDouble()),
             Q_INT64_C(60000) * kTicksPerMs);
    // And the new item reports a start of its own.
    m_backend->simulateState(PlayerBackend::State::Playing);
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing")), 2);

    m_backend->simulateEnd();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 3);
    QCOMPARE(stoppedSpy.count(), 0);
    QCOMPARE(m_controller->title(), QStringLiteral("Episode Three"));

    // Last item: now the queue is exhausted and the session really ends.
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateEnd();
    QTRY_COMPARE(stoppedSpy.count(), 1);
    QVERIFY(!m_controller->active());
    QCOMPARE(m_backend->loadedUrls.size(), 3);
}

// The broken-tail rule (an error within the tail epsilon of EOF is a clean end)
// has to reach the queue too, or half the auto-advances in a real library would
// silently stop instead.
void QueuePlaybackTest::brokenTailAdvancesInsteadOfStopping()
{
    QSignalSpy stoppedSpy(m_controller, &PlayerController::stopped);
    m_controller->playQueue(threeItems(), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(8'184'000);
    m_backend->simulatePosition(8'182'000); // 2 s from EOF

    m_backend->simulateError(QStringLiteral("demuxer: broken tail"));
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(stoppedSpy.count(), 0);
    QCOMPARE(m_controller->queue()->currentIndex(), 1);
    QVERIFY(m_controller->errorMessage().isEmpty()); // still not a user-facing failure
}

void QueuePlaybackTest::upNextAppearsInTheTailWindow()
{
    QSignalSpy upNextSpy(m_controller, &PlayerController::upNextChanged);
    m_controller->playQueue(threeItems(), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(60'000);

    m_backend->simulatePosition(20'000);
    QVERIFY(!m_controller->upNextVisible());

    m_backend->simulatePosition(35'000); // 25 s left
    QVERIFY(m_controller->upNextVisible());
    QCOMPARE(m_controller->upNextSecondsRemaining(), 25);
    QVERIFY(upNextSpy.count() > 0);
    QCOMPARE(m_controller->nextItem().value(QStringLiteral("name")).toString(),
             QStringLiteral("Episode Two"));

    m_backend->simulatePosition(58'500);
    QCOMPARE(m_controller->upNextSecondsRemaining(), 2);

    // Seeking back out of the window puts the card away again.
    m_backend->simulatePosition(10'000);
    QVERIFY(!m_controller->upNextVisible());

    // The last item has nothing to offer, so no card.
    m_controller->playQueue(threeItems(), 2);
    QTRY_VERIFY(m_backend->loadedUrls.size() >= 2);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(60'000);
    m_backend->simulatePosition(50'000);
    QVERIFY(!m_controller->upNextVisible());
}

void QueuePlaybackTest::cancelUpNextSuppressesOnlyThisItem()
{
    QSignalSpy stoppedSpy(m_controller, &PlayerController::stopped);
    m_controller->playQueue(threeItems(), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(60'000);
    m_backend->simulatePosition(40'000);
    QVERIFY(m_controller->upNextVisible());

    m_controller->cancelUpNext();
    QVERIFY(!m_controller->upNextVisible());
    m_backend->simulatePosition(55'000);
    QVERIFY(!m_controller->upNextVisible()); // it stays dismissed

    // A cancelled card means this item ends the session, queue or no queue.
    m_backend->simulateEnd();
    QTRY_COMPARE(stoppedSpy.count(), 1);
    QVERIFY(!m_controller->active());
    QCOMPARE(m_backend->loadedUrls.size(), 1);

    // ...and only this item: the next one auto-advances normally again.
    m_controller->playNext();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->title(), QStringLiteral("Episode Two"));
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(60'000);
    m_backend->simulatePosition(40'000);
    QVERIFY(m_controller->upNextVisible());
    m_backend->simulateEnd();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 3);
    QCOMPARE(stoppedSpy.count(), 1);
    QCOMPARE(m_controller->title(), QStringLiteral("Episode Three"));
}

void QueuePlaybackTest::playNextReportsStoppedAtTheRealPosition()
{
    m_controller->playQueue(threeItems(), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(60'000);
    m_backend->simulatePosition(12'000);

    m_controller->playNext();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->title(), QStringLiteral("Episode Two"));

    // A skip must not tell the server the episode was watched to the end.
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped")),
                 1);
    const QJsonObject stopped =
        QJsonDocument::fromJson(m_mock
                                    ->lastRequestFor(QStringLiteral("POST"),
                                                     QStringLiteral("/Sessions/Playing/Stopped"))
                                    .body)
            .object();
    QCOMPARE(static_cast<qint64>(stopped.value(QLatin1String("PositionTicks")).toDouble()),
             Q_INT64_C(12000) * kTicksPerMs);

    // At the end of the queue there is nothing to skip to.
    m_controller->playQueue(threeItems(), 2);
    QTRY_VERIFY(m_backend->loadedUrls.size() >= 3);
    const int loads = static_cast<int>(m_backend->loadedUrls.size());
    m_controller->playNext();
    QCOMPARE(m_backend->loadedUrls.size(), loads);
    QVERIFY(m_controller->active());
}

void QueuePlaybackTest::playPreviousRestartsThenStepsBack()
{
    m_controller->playQueue(threeItems(), 1);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(60'000);
    m_backend->simulatePosition(20'000);

    // More than 5 s in: "previous" restarts what is playing.
    m_controller->playPrevious();
    QCOMPARE(m_backend->loadedUrls.size(), 1);
    QVERIFY(m_backend->seeks.contains(Q_INT64_C(0)));
    QCOMPARE(m_controller->title(), QStringLiteral("Episode Two"));

    // The seek left the position at 0, so now it really steps back.
    m_controller->playPrevious();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->title(), QStringLiteral("Episode One"));
    QCOMPARE(m_controller->queue()->currentIndex(), 0);

    // At the head of the queue it degrades to a restart.
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulatePosition(1'000);
    m_controller->playPrevious();
    QCOMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->queue()->currentIndex(), 0);
}

void QueuePlaybackTest::jumpingInTheQueuePanelChangesItem()
{
    m_controller->playQueue(threeItems(), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);

    m_controller->queue()->jumpTo(2);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->title(), QStringLiteral("Episode Three"));
    QVERIFY(m_controller->active());
}

void QueuePlaybackTest::removingTheCurrentItemContinuesWithTheNext()
{
    QSignalSpy stoppedSpy(m_controller, &PlayerController::stopped);
    m_controller->playQueue(threeItems(), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);

    m_controller->queue()->removeAt(0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->title(), QStringLiteral("Episode Two"));
    QCOMPARE(stoppedSpy.count(), 0);
    m_backend->simulateState(PlayerBackend::State::Playing);

    // Removing a row above the cursor must not restart anything.
    m_controller->queue()->addToQueue(itemMap(QStringLiteral("301001"), QStringLiteral("Extra")));
    m_controller->queue()->jumpTo(1);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 3);
    m_controller->queue()->removeAt(0);
    QCOMPARE(m_controller->queue()->currentIndex(), 0);
    QTest::qWait(30);
    QCOMPARE(m_backend->loadedUrls.size(), 3);

    // Emptying the queue ends the session.
    m_controller->queue()->removeAt(1);
    m_controller->queue()->removeAt(0);
    QTRY_COMPARE(stoppedSpy.count(), 1);
    QVERIFY(!m_controller->active());
}

void QueuePlaybackTest::repeatOneReplaysTheSameItem()
{
    QSignalSpy stoppedSpy(m_controller, &PlayerController::stopped);
    m_controller->playQueue(threeItems(), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_controller->queue()->setRepeatMode(PlayQueue::RepeatOne);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(60'000);
    m_backend->simulatePosition(59'000);

    m_backend->simulateEnd();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->queue()->currentIndex(), 0);
    QCOMPARE(m_controller->title(), QStringLiteral("Episode One"));
    QCOMPARE(stoppedSpy.count(), 0);
    // RepeatOne re-plays, so the Up Next card has nothing to advertise.
    QVERIFY(m_controller->nextItem().isEmpty());
}

// A plain play verb still behaves exactly as it did — and leaves a queue of one
// behind, so "play next" from a card lands after what is on screen.
void QueuePlaybackTest::aBarePlayItemIsAOneItemQueue()
{
    QSignalSpy stoppedSpy(m_controller, &PlayerController::stopped);
    m_controller->playItem(QStringLiteral("301001"), QStringLiteral("The Matrix"), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    QCOMPARE(m_controller->queue()->rowCount(), 1);
    QCOMPARE(m_controller->queue()->currentIndex(), 0);
    QVERIFY(!m_controller->hasNext());

    m_backend->simulateState(PlayerBackend::State::Playing);
    m_controller->queue()->playNext(itemMap(QStringLiteral("301002"), QStringLiteral("Next Up")));
    QCOMPARE(m_controller->queue()->rowCount(), 2);
    QCOMPARE(m_controller->queue()->currentIndex(), 0);
    QVERIFY(m_controller->hasNext());
    // Enqueuing must not interrupt what is playing.
    QTest::qWait(30);
    QCOMPARE(m_backend->loadedUrls.size(), 1);

    m_backend->simulateDuration(60'000);
    m_backend->simulatePosition(59'000);
    m_backend->simulateEnd();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->title(), QStringLiteral("Next Up"));
    QCOMPARE(stoppedSpy.count(), 0);
}

// stop() leaves the queue standing — the panel still lists it and the mini
// player still shows what was playing. Every edit made to it from there is an
// edit, not a play verb: none of them may restart the session.
void QueuePlaybackTest::queueEditsAfterStopDoNotStartPlayback()
{
    m_controller->playQueue(threeItems(), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);

    m_controller->stop();
    QVERIFY(!m_controller->active());
    QCOMPARE(m_controller->queue()->rowCount(), 3); // the queue survives the stop

    // Enqueuing after the cursor.
    m_controller->queue()->playNext(itemMap(QStringLiteral("301003"), QStringLiteral("Later")));
    // Appending at the tail.
    m_controller->queue()->addToQueue(itemMap(QStringLiteral("301002"), QStringLiteral("Last")));
    // Removing a row above the cursor — here the cursor is row 0, so remove
    // one below it and then the row that was playing itself.
    m_controller->queue()->removeAt(3);
    // The row that was playing: the next one is promoted, which while a session
    // is running means "playback continues with it" and while one is not means
    // nothing at all.
    m_controller->queue()->removeAt(0);

    QTest::qWait(50);
    QCOMPARE(m_backend->loadedUrls.size(), 1);
    QVERIFY(!m_controller->active());

    // An explicit verb still works on the same queue.
    m_controller->playNext();
    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QVERIFY(m_controller->active());
}

// Every path that swaps the item under the playhead has to close the server
// session for the outgoing one first. The queue-driven paths (a jumpTo, the
// current row being removed, a remote PlayNow) used not to, so the item stayed
// open on the server and the 10 s progress timer kept running across the swap —
// reporting the NEW item id against the OLD play session.
void QueuePlaybackTest::changingItemClosesTheOutgoingSession()
{
    m_controller->playQueue(threeItems(), 0);
    QTRY_COMPARE(m_backend->loadedUrls.size(), 1);
    m_backend->simulateState(PlayerBackend::State::Playing);
    m_backend->simulateDuration(60'000);
    m_backend->simulatePosition(12'000);
    QCOMPARE(requestCount(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped")), 0);

    m_controller->queue()->jumpTo(2);
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped")),
                 1);
    const QJsonObject stopped =
        QJsonDocument::fromJson(m_mock
                                    ->lastRequestFor(QStringLiteral("POST"),
                                                     QStringLiteral("/Sessions/Playing/Stopped"))
                                    .body)
            .object();
    // The outgoing item, at the position it was actually at — not the incoming
    // one, and not its runtime.
    QCOMPARE(stopped.value(QLatin1String("ItemId")).toString(), QStringLiteral("301001"));
    QCOMPARE(static_cast<qint64>(stopped.value(QLatin1String("PositionTicks")).toDouble()),
             Q_INT64_C(12000) * kTicksPerMs);

    QTRY_COMPARE(m_backend->loadedUrls.size(), 2);
    QCOMPARE(m_controller->title(), QStringLiteral("Episode Three"));
    m_backend->simulateState(PlayerBackend::State::Playing);

    // Removing the row that is playing is the same swap by another route.
    m_backend->simulatePosition(3'000);
    m_controller->queue()->removeAt(m_controller->queue()->currentIndex());
    QTRY_COMPARE(requestCount(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped")),
                 2);
    QCOMPARE(QJsonDocument::fromJson(m_mock
                                         ->lastRequestFor(QStringLiteral("POST"),
                                                          QStringLiteral("/Sessions/Playing/Stopped"))
                                         .body)
                 .object()
                 .value(QLatin1String("ItemId"))
                 .toString(),
             QStringLiteral("301003"));
}

QTEST_GUILESS_MAIN(QueuePlaybackTest)
#include "tst_queue_playback.moc"
