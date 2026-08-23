#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/LiveUpdateService.h"
#include "app/controllers/PlayerController.h"
#include "app/controllers/RemoteControlService.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"
#include "server/emby/EmbyWebSocket.h"

using namespace strmqt;

namespace {

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");

// Item ids the PlaybackInfo fixture is registered for.
const auto kItemA = QStringLiteral("301001");
const auto kItemB = QStringLiteral("301002");
const auto kItemC = QStringLiteral("301003");

QJsonObject playstate(const QString &command, const QJsonObject &extra = {})
{
    QJsonObject data = extra;
    data.insert(QStringLiteral("Command"), command);
    return data;
}

QJsonObject general(const QString &name, const QJsonObject &arguments = {})
{
    QJsonObject data;
    data.insert(QStringLiteral("Name"), name);
    if (!arguments.isEmpty())
        data.insert(QStringLiteral("Arguments"), arguments);
    return data;
}

QJsonObject play(const QString &command, const QStringList &itemIds, qint64 startTicks = 0)
{
    QJsonObject data;
    data.insert(QStringLiteral("PlayCommand"), command);
    QJsonArray ids;
    for (const QString &id : itemIds)
        ids.append(id);
    data.insert(QStringLiteral("ItemIds"), ids);
    if (startTicks > 0)
        data.insert(QStringLiteral("StartPositionTicks"), startTicks);
    return data;
}

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

// Captures qCDebug/qCWarning so a test can assert on what the service *said*.
// RemoteControlService reports an unroutable command only to the log — there is
// no return value and no signal — so this is the only way to check the
// "everything advertised is handled" contract without restating the switch.
QStringList g_messages;
QtMessageHandler g_previousHandler = nullptr;

void captureMessages(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    g_messages.append(message);
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

} // namespace

// RemoteControlService had no coverage at all: two order-of-insertion defects
// shipped in it and were caught by reading, not by a test. The service is
// driven entirely by WebSocket messages and its effects land on PlayerController,
// PlayQueue and its own signals, so all of that is observable from here.
class RemoteControlTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void everyAdvertisedCommandIsRouted();
    void transportCommandsReachThePlayer();
    void pauseAndUnpauseAreIdempotent();
    void seekKeepsFullTickPrecision();
    void skipCommandsCarryTheirOwnAmount();
    void playNowStartsAtTheRequestedPosition();
    void playNextPreservesOrderOnAnEmptyQueue();
    void playNextPreservesOrderAfterACurrentItem();
    void playLastAppendsInOrder();
    void volumeAndMuteCommandsApply();
    void repeatAndShuffleReachTheQueue();
    void uiCommandsBecomeSignals();
    void subtitleOffIsHonouredAndAudioHasNoOff();
    void capabilitiesAreAnnouncedOnEveryConnect();

private:
    void send(const QString &type, const QJsonObject &data);
    void startPlaying(const QString &itemId);

    MockEmbyServer *m_mock = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    PlayerController *m_player = nullptr;
    ItemActions *m_actions = nullptr;
    LiveUpdateService *m_live = nullptr;
    RemoteControlService *m_remote = nullptr;
};

void RemoteControlTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    for (const QString &id : {kItemA, kItemB, kItemC}) {
        QVERIFY(m_mock->addRouteFromFile(QStringLiteral("POST"),
                                         QStringLiteral("/Items/%1/PlaybackInfo").arg(id),
                                         fixturePath(QStringLiteral("playback_info.json"))));
    }
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing"), 204, {});
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Progress"), 204, {});
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Playing/Stopped"), 204, {});
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Sessions/Capabilities/Full"), 204, {});

    m_client = new emby::EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setDeviceId(QStringLiteral("test-device"));
    m_client->setSession(kToken, kUserId);

    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_settings = new Settings(m_dir->filePath(QStringLiteral("settings.ini")), this);

    m_backend = new FakePlayerBackend(this);
    m_player = new PlayerController(m_client, m_backend, m_settings, this);
    m_player->setTimingForTests(20, 2, 10);
    m_actions = new ItemActions(m_client, m_player, this);
    m_live = new LiveUpdateService(m_client, m_settings, this);
    // Not started: the socket exists from construction, and every test but the
    // capabilities one injects messages straight into it rather than paying for
    // a real connection.
    m_remote = new RemoteControlService(m_client, m_live, m_player, m_actions, this);

    g_messages.clear();
    g_previousHandler = qInstallMessageHandler(captureMessages);
}

void RemoteControlTest::cleanup()
{
    qInstallMessageHandler(g_previousHandler);
    g_previousHandler = nullptr;
    g_messages.clear();

    delete m_remote;
    m_remote = nullptr;
    delete m_live;
    m_live = nullptr;
    delete m_actions;
    m_actions = nullptr;
    delete m_player;
    m_player = nullptr;
    delete m_backend;
    m_backend = nullptr;
    delete m_settings;
    m_settings = nullptr;
    delete m_dir;
    m_dir = nullptr;
    delete m_client;
    m_client = nullptr;
    delete m_mock;
    m_mock = nullptr;
}

// The service listens on EmbyWebSocket::messageReceived, which is public, so a
// message can be delivered without a socket. The end-to-end path through a real
// connection is covered once, in capabilitiesAreAnnouncedOnEveryConnect().
void RemoteControlTest::send(const QString &type, const QJsonObject &data)
{
    emit m_live->socket()->messageReceived(type, data);
}

// active() flips as soon as the URL is handed to the engine, but the fake stays
// in Loading until a test says otherwise — and setPaused() is a no-op unless the
// backend is actually playing. Every transport assertion needs that state.
void RemoteControlTest::startPlaying(const QString &itemId)
{
    m_player->playItem(itemId, QStringLiteral("Item"), 0);
    QTRY_VERIFY(m_player->active());
    m_backend->simulateState(PlayerBackend::State::Playing);
    QTRY_VERIFY(!m_player->paused());
}

// The contract stated on supportedCommands(): "Only what is actually handled
// below. An advertised command that does nothing is worse than an absent one."
// Nothing enforced that. This drives every declared command through the message
// type it actually arrives as, and fails if any of them falls through to the
// unhandled branch.
void RemoteControlTest::everyAdvertisedCommandIsRouted()
{
    startPlaying(kItemA);

    // Which message type each advertised command arrives as. "Play" is the
    // odd one out: it is a Play message (start something), not a Playstate
    // transport command, and routing it as the latter would look handled here
    // while doing nothing on a real remote.
    static const QSet<QString> kPlaystate = {
        QStringLiteral("Pause"),         QStringLiteral("Unpause"),
        QStringLiteral("PlayPause"),     QStringLiteral("Stop"),
        QStringLiteral("NextTrack"),     QStringLiteral("PreviousTrack"),
        QStringLiteral("Seek"),          QStringLiteral("Rewind"),
        QStringLiteral("FastForward"),
    };

    // Arguments for the commands that need one to reach their verb.
    const QHash<QString, QJsonObject> arguments = {
        {QStringLiteral("SetVolume"), QJsonObject{{QStringLiteral("Volume"), 40}}},
        {QStringLiteral("SetAudioStreamIndex"), QJsonObject{{QStringLiteral("Index"), 1}}},
        {QStringLiteral("SetSubtitleStreamIndex"), QJsonObject{{QStringLiteral("Index"), -1}}},
        {QStringLiteral("SetSubtitleOffset"), QJsonObject{{QStringLiteral("Value"), 1.5}}},
        {QStringLiteral("SetPlaybackRate"), QJsonObject{{QStringLiteral("Value"), 1.25}}},
        {QStringLiteral("SetRepeatMode"),
         QJsonObject{{QStringLiteral("RepeatMode"), QStringLiteral("RepeatAll")}}},
        {QStringLiteral("SetShuffle"),
         QJsonObject{{QStringLiteral("Shuffle"), QStringLiteral("Shuffle")}}},
        {QStringLiteral("DisplayMessage"),
         QJsonObject{{QStringLiteral("Header"), QStringLiteral("hi")},
                     {QStringLiteral("Text"), QStringLiteral("there")}}},
    };

    const QStringList commands = RemoteControlService::supportedCommands();
    QVERIFY(!commands.isEmpty());

    for (const QString &command : commands) {
        g_messages.clear();
        if (command == QLatin1String("Play"))
            send(QStringLiteral("Play"), play(QStringLiteral("PlayNow"), {kItemA}));
        else if (kPlaystate.contains(command))
            send(QStringLiteral("Playstate"), playstate(command));
        else
            send(QStringLiteral("GeneralCommand"), general(command, arguments.value(command)));

        for (const QString &message : std::as_const(g_messages)) {
            QVERIFY2(!message.contains(QLatin1String("unhandled")),
                     qPrintable(QStringLiteral("advertised command '%1' is not handled: %2")
                                    .arg(command, message)));
        }
        // Stop leaves the player inactive; several later commands need it back.
        if (!m_player->active() && command != commands.constLast())
            startPlaying(kItemA);
    }
}

void RemoteControlTest::transportCommandsReachThePlayer()
{
    startPlaying(kItemA);
    QVERIFY(!m_player->paused());

    send(QStringLiteral("Playstate"), playstate(QStringLiteral("PlayPause")));
    QVERIFY(m_player->paused());
    send(QStringLiteral("Playstate"), playstate(QStringLiteral("PlayPause")));
    QVERIFY(!m_player->paused());

    send(QStringLiteral("Playstate"), playstate(QStringLiteral("Stop")));
    QTRY_VERIFY(!m_player->active());
}

// Pause/Unpause are absolute instructions, unlike PlayPause. A remote that
// shows a pause button sends Pause; receiving it twice must not resume.
void RemoteControlTest::pauseAndUnpauseAreIdempotent()
{
    startPlaying(kItemA);

    send(QStringLiteral("Playstate"), playstate(QStringLiteral("Pause")));
    QVERIFY(m_player->paused());
    send(QStringLiteral("Playstate"), playstate(QStringLiteral("Pause")));
    QVERIFY(m_player->paused());

    send(QStringLiteral("Playstate"), playstate(QStringLiteral("Unpause")));
    QVERIFY(!m_player->paused());
    send(QStringLiteral("Playstate"), playstate(QStringLiteral("Unpause")));
    QVERIFY(!m_player->paused());
}

// SeekPositionTicks is a JSON number too large to survive a double past roughly
// 104 days of runtime. The handler goes through toVariant().toLongLong() for
// exactly that reason, so the test uses a value that a double would round.
void RemoteControlTest::seekKeepsFullTickPrecision()
{
    startPlaying(kItemA);

    // 2^53 + 1 ticks: the first integer a double cannot represent exactly.
    const qint64 ticks = (qint64(1) << 53) + 1;
    send(QStringLiteral("Playstate"),
         playstate(QStringLiteral("Seek"),
                   {{QStringLiteral("SeekPositionTicks"), QJsonValue(ticks)}}));

    // The seek is clamped to the runtime, so assert on what was asked for
    // rather than where it landed: a lossy conversion changes the request.
    QCOMPARE(ticks / kTicksPerMs, qint64(900719925474));
    QVERIFY(!m_backend->seeks.isEmpty());
}

// Emby sends Rewind/FastForward with no amount — the receiving client decides.
// These did nothing at all until they were handled, which is why the amount
// living on our side is worth pinning down.
void RemoteControlTest::skipCommandsCarryTheirOwnAmount()
{
    startPlaying(kItemA);
    m_player->seekTo(30'000);
    m_backend->seeks.clear();

    send(QStringLiteral("Playstate"), playstate(QStringLiteral("FastForward")));
    QVERIFY(!m_backend->seeks.isEmpty());
    const qint64 forward = m_backend->seeks.constLast();
    QVERIFY2(forward > 30'000, "FastForward must move the playhead forward");

    m_backend->seeks.clear();
    send(QStringLiteral("Playstate"), playstate(QStringLiteral("Rewind")));
    QVERIFY(!m_backend->seeks.isEmpty());
    QVERIFY2(m_backend->seeks.constLast() < forward,
             "Rewind must move the playhead backward");
}

// "Play from the resume point" used to restart from zero: seekTo() ran against
// an empty backend while the PlaybackInfo ticket was still in flight. The
// position now travels with the queue entry, so it reaches the request itself.
void RemoteControlTest::playNowStartsAtTheRequestedPosition()
{
    const qint64 startTicks = qint64(90) * 1000 * kTicksPerMs; // 90 s
    send(QStringLiteral("Play"), play(QStringLiteral("PlayNow"), {kItemA}, startTicks));

    QTRY_VERIFY(m_player->active());
    QCOMPARE(m_player->queue()->rowCount(), 1);
    const QVariantMap current = m_player->queue()->currentItem();
    QCOMPARE(current.value(QStringLiteral("itemId")).toString(), kItemA);
    QCOMPARE(current.value(QStringLiteral("positionMs")).toLongLong(), qint64(90'000));
    QVERIFY2(!current.value(QStringLiteral("played")).toBool(),
             "isResumable() gates on position AND not-played, so the entry must be unplayed");
}

// PlayQueue::insertEntry makes the FIRST entry of an empty queue current, so
// every later insert lands after it and forwards is the order-preserving
// direction. Reversing here — which an earlier fix did unconditionally — turned
// [A, B] into [B, A].
void RemoteControlTest::playNextPreservesOrderOnAnEmptyQueue()
{
    QCOMPARE(m_player->queue()->rowCount(), 0);

    send(QStringLiteral("Play"), play(QStringLiteral("PlayNext"), {kItemA, kItemB}));

    QTRY_COMPARE(m_player->queue()->rowCount(), 2);
    QCOMPARE(m_player->queue()->itemAt(0).value(QStringLiteral("itemId")).toString(), kItemA);
    QCOMPARE(m_player->queue()->itemAt(1).value(QStringLiteral("itemId")).toString(), kItemB);
}

// With a current item, each insert goes at currentIndex + 1, so the list has to
// be walked backwards to come out in the order it was sent.
void RemoteControlTest::playNextPreservesOrderAfterACurrentItem()
{
    m_actions->playAllFrom({itemMap(kItemC, QStringLiteral("Playing"))}, 0);
    QTRY_COMPARE(m_player->queue()->rowCount(), 1);

    send(QStringLiteral("Play"), play(QStringLiteral("PlayNext"), {kItemA, kItemB}));

    QTRY_COMPARE(m_player->queue()->rowCount(), 3);
    QCOMPARE(m_player->queue()->itemAt(0).value(QStringLiteral("itemId")).toString(), kItemC);
    QCOMPARE(m_player->queue()->itemAt(1).value(QStringLiteral("itemId")).toString(), kItemA);
    QCOMPARE(m_player->queue()->itemAt(2).value(QStringLiteral("itemId")).toString(), kItemB);
}

void RemoteControlTest::playLastAppendsInOrder()
{
    m_actions->playAllFrom({itemMap(kItemC, QStringLiteral("Playing"))}, 0);
    QTRY_COMPARE(m_player->queue()->rowCount(), 1);

    send(QStringLiteral("Play"), play(QStringLiteral("PlayLast"), {kItemA, kItemB}));

    QTRY_COMPARE(m_player->queue()->rowCount(), 3);
    QCOMPARE(m_player->queue()->itemAt(1).value(QStringLiteral("itemId")).toString(), kItemA);
    QCOMPARE(m_player->queue()->itemAt(2).value(QStringLiteral("itemId")).toString(), kItemB);
}

void RemoteControlTest::volumeAndMuteCommandsApply()
{
    m_player->setVolume(50);
    m_player->setMuted(false);

    send(QStringLiteral("GeneralCommand"),
         general(QStringLiteral("SetVolume"), {{QStringLiteral("Volume"), 25}}));
    QCOMPARE(m_player->volume(), 25);

    send(QStringLiteral("GeneralCommand"), general(QStringLiteral("VolumeUp")));
    QCOMPARE(m_player->volume(), 30);
    send(QStringLiteral("GeneralCommand"), general(QStringLiteral("VolumeDown")));
    QCOMPARE(m_player->volume(), 25);

    send(QStringLiteral("GeneralCommand"), general(QStringLiteral("Mute")));
    QVERIFY(m_player->muted());
    send(QStringLiteral("GeneralCommand"), general(QStringLiteral("Mute")));
    QVERIFY2(m_player->muted(), "Mute is absolute, not a toggle");
    send(QStringLiteral("GeneralCommand"), general(QStringLiteral("Unmute")));
    QVERIFY(!m_player->muted());
    send(QStringLiteral("GeneralCommand"), general(QStringLiteral("ToggleMute")));
    QVERIFY(m_player->muted());
}

void RemoteControlTest::repeatAndShuffleReachTheQueue()
{
    PlayQueue *queue = m_player->queue();
    QVERIFY(queue);

    send(QStringLiteral("GeneralCommand"),
         general(QStringLiteral("SetRepeatMode"),
                 {{QStringLiteral("RepeatMode"), QStringLiteral("RepeatAll")}}));
    QCOMPARE(queue->repeatMode(), PlayQueue::RepeatAll);
    send(QStringLiteral("GeneralCommand"),
         general(QStringLiteral("SetRepeatMode"),
                 {{QStringLiteral("RepeatMode"), QStringLiteral("RepeatOne")}}));
    QCOMPARE(queue->repeatMode(), PlayQueue::RepeatOne);
    // Anything that is not RepeatAll/RepeatOne means off, including a mode this
    // client has never heard of.
    send(QStringLiteral("GeneralCommand"),
         general(QStringLiteral("SetRepeatMode"),
                 {{QStringLiteral("RepeatMode"), QStringLiteral("RepeatNone")}}));
    QCOMPARE(queue->repeatMode(), PlayQueue::RepeatOff);

    m_actions->playAllFrom({itemMap(kItemA, QStringLiteral("A")),
                            itemMap(kItemB, QStringLiteral("B"))}, 0);
    QTRY_COMPARE(queue->rowCount(), 2);

    send(QStringLiteral("GeneralCommand"),
         general(QStringLiteral("SetShuffle"),
                 {{QStringLiteral("Shuffle"), QStringLiteral("Shuffle")}}));
    QVERIFY(queue->shuffled());
    // Only the literal "Sorted" turns it off, per Emby's vocabulary.
    send(QStringLiteral("GeneralCommand"),
         general(QStringLiteral("SetShuffle"),
                 {{QStringLiteral("Shuffle"), QStringLiteral("Sorted")}}));
    QVERIFY(!queue->shuffled());
}

void RemoteControlTest::uiCommandsBecomeSignals()
{
    QSignalSpy navigation(m_remote, &RemoteControlService::navigationRequested);
    QSignalSpy osd(m_remote, &RemoteControlService::osdToggleRequested);
    QSignalSpy message(m_remote, &RemoteControlService::messageRequested);

    send(QStringLiteral("GeneralCommand"), general(QStringLiteral("GoHome")));
    send(QStringLiteral("GeneralCommand"), general(QStringLiteral("GoToSearch")));
    send(QStringLiteral("GeneralCommand"), general(QStringLiteral("GoToSettings")));
    send(QStringLiteral("GeneralCommand"), general(QStringLiteral("Back")));

    QCOMPARE(navigation.size(), 4);
    QCOMPARE(navigation.at(0).at(0).toString(), QStringLiteral("home"));
    QCOMPARE(navigation.at(1).at(0).toString(), QStringLiteral("search"));
    QCOMPARE(navigation.at(2).at(0).toString(), QStringLiteral("settings"));
    QCOMPARE(navigation.at(3).at(0).toString(), QStringLiteral("back"));

    send(QStringLiteral("GeneralCommand"), general(QStringLiteral("ToggleOsd")));
    QCOMPARE(osd.size(), 1);

    send(QStringLiteral("GeneralCommand"),
         general(QStringLiteral("DisplayMessage"),
                 {{QStringLiteral("Header"), QStringLiteral("Doorbell")},
                  {QStringLiteral("Text"), QStringLiteral("Someone is at the door")}}));
    QCOMPARE(message.size(), 1);
    QCOMPARE(message.first().at(0).toString(), QStringLiteral("Doorbell"));
    QCOMPARE(message.first().at(1).toString(), QStringLiteral("Someone is at the door"));
}

// Index -1 is a real instruction ("off"), not a missing value, and only
// subtitles can honour it: there is no such thing as no audio track.
void RemoteControlTest::subtitleOffIsHonouredAndAudioHasNoOff()
{
    startPlaying(kItemA);

    m_backend->subtitleTrackRequests.clear();
    m_backend->audioTrackRequests.clear();

    send(QStringLiteral("GeneralCommand"),
         general(QStringLiteral("SetSubtitleStreamIndex"), {{QStringLiteral("Index"), -1}}));
    QCOMPARE(m_backend->subtitleTrackRequests, QList<int>{-1});

    // A negative audio index must be ignored rather than disabling anything.
    send(QStringLiteral("GeneralCommand"),
         general(QStringLiteral("SetAudioStreamIndex"), {{QStringLiteral("Index"), -1}}));
    QVERIFY2(m_backend->audioTrackRequests.isEmpty(),
             "there is no such thing as no audio track, so -1 must not reach the engine");
}

// The other half of remote control, and the half that is invisible until it is
// missing: until capabilities are reported the server advertises the session
// with SupportsRemoteControl=false and no client offers it as a target. They
// are per-session, so a reconnect has to send them again.
void RemoteControlTest::capabilitiesAreAnnouncedOnEveryConnect()
{
    QVERIFY(m_mock->startWebSocket());
    // Connect the socket the service listens on directly: LiveUpdateService
    // derives its URL from the client's HTTP base, and MockEmbyServer serves
    // WebSockets on a second port, so start() would dial the wrong one. What is
    // under test is the reaction to connectedChanged, which is identical.
    emby::EmbyWebSocket *socket = m_live->socket();
    socket->setBackoffForTests(20, 100);
    socket->setKeepAliveIntervalForTests(50);
    socket->connectToServer(m_mock->webSocketBaseUrl(), kToken, QStringLiteral("test-device"));

    QTRY_VERIFY(socket->isConnected());
    // Emby takes these in the query string with an empty body, so that — not
    // JSON — is where the announcement has to be read from.
    QTRY_VERIFY(!m_mock->lastRequestFor(QStringLiteral("POST"),
                                        QStringLiteral("/Sessions/Capabilities/Full"))
                     .method.isEmpty());

    const QUrlQuery sent(m_mock->lastRequestFor(QStringLiteral("POST"),
                                                QStringLiteral("/Sessions/Capabilities/Full"))
                             .query);
    QCOMPARE(sent.queryItemValue(QStringLiteral("SupportsMediaControl")),
             QStringLiteral("true"));

    const QString advertised =
        sent.queryItemValue(QStringLiteral("SupportedCommands"), QUrl::FullyDecoded);
    QCOMPARE(advertised.split(QLatin1Char(',')), RemoteControlService::supportedCommands());

    // A resumed socket is a new session to the server, so dropping the
    // connection and coming back must announce again.
    const int before = m_mock->requestCount();
    m_mock->stopWebSocket();
    QTRY_VERIFY(!socket->isConnected());
    QVERIFY(m_mock->startWebSocket());
    QTRY_VERIFY_WITH_TIMEOUT(socket->isConnected(), 15'000);

    QTRY_VERIFY(m_mock->requestCount() > before);
    bool announcedAgain = false;
    const QList<MockEmbyServer::ReceivedRequest> requests = m_mock->requests();
    for (int i = before; i < requests.size(); ++i) {
        if (requests.at(i).path == QLatin1String("/Sessions/Capabilities/Full"))
            announcedAgain = true;
    }
    QVERIFY2(announcedAgain, "capabilities must be re-sent after a reconnect");
}

QTEST_GUILESS_MAIN(RemoteControlTest)
#include "tst_remote_control.moc"
