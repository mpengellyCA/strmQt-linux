// How a StrmLight finds the streaming host (DiscoveryResponder) and how a code
// typed into the desktop StrmQt reaches it (HostLink), over real sockets on
// this machine.

#include "remote/DiscoveryResponder.h"
#include "remote/HostLink.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkDatagram>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUdpSocket>

using strmqt::DiscoveryResponder;
using strmqt::HostLink;

class HostDiscoveryTest : public QObject
{
    Q_OBJECT

private slots:
    void aProbeIsAnsweredWithTheHostsPorts();
    void anythingElseIsNotAnswered();
    void onlyThisNetworkIsAnswered();
    void repliesAreRationed();
    void aCodeReachesTheHost();
    void noHostMeansTheCodeStaysHere();

private:
    static QJsonObject sample()
    {
        return {{QStringLiteral("name"), QStringLiteral("den")},
                {QStringLiteral("remotePort"), 8338},
                {QStringLiteral("sunshinePort"), 48989},
                {QStringLiteral("fingerprint"), QStringLiteral("AA:BB")}};
    }
};

void HostDiscoveryTest::aProbeIsAnsweredWithTheHostsPorts()
{
    DiscoveryResponder responder(&HostDiscoveryTest::sample);
    QVERIFY(responder.start(0));
    QVERIFY(responder.isRunning());

    // The responder answers from the event loop, so wait with it running.
    QUdpSocket socket;
    QVERIFY(socket.bind(QHostAddress::LocalHost, 0));
    socket.writeDatagram(QByteArray(DiscoveryResponder::kProbe), QHostAddress::LocalHost, responder.port());
    QTRY_VERIFY(socket.hasPendingDatagrams());
    const QNetworkDatagram reply = socket.receiveDatagram();
    QCOMPARE(reply.senderPort(), int(responder.port()));
    const QJsonObject json = QJsonDocument::fromJson(reply.data()).object();
    QCOMPARE(json.value(QStringLiteral("service")).toString(), QStringLiteral("strmqt"));
    QCOMPARE(json.value(QStringLiteral("version")).toInt(), 1);
    QCOMPARE(json.value(QStringLiteral("name")).toString(), QStringLiteral("den"));
    QCOMPARE(json.value(QStringLiteral("remotePort")).toInt(), 8338);
    QCOMPARE(json.value(QStringLiteral("sunshinePort")).toInt(), 48989);
    QCOMPARE(json.value(QStringLiteral("fingerprint")).toString(), QStringLiteral("AA:BB"));
}

void HostDiscoveryTest::anythingElseIsNotAnswered()
{
    DiscoveryResponder responder(&HostDiscoveryTest::sample);
    QVERIFY(!responder.replyTo(QByteArray(DiscoveryResponder::kProbe)).isEmpty());
    QVERIFY(!responder.replyTo(QByteArray(DiscoveryResponder::kProbe) + "\n").isEmpty());
    QVERIFY(responder.replyTo("STRMQT-DISCOVER 2").isEmpty());
    QVERIFY(responder.replyTo("M-SEARCH * HTTP/1.1").isEmpty());
    QVERIFY(responder.replyTo({}).isEmpty());
    // The service and version are the responder's, whatever `describe` says.
    DiscoveryResponder liar([] { return QJsonObject{{QStringLiteral("service"), QStringLiteral("x")}}; });
    const QJsonObject json = QJsonDocument::fromJson(liar.replyTo(DiscoveryResponder::kProbe)).object();
    QCOMPARE(json.value(QStringLiteral("service")).toString(), QStringLiteral("strmqt"));
}

void HostDiscoveryTest::onlyThisNetworkIsAnswered()
{
    for (const char *nearby : {"127.0.0.1", "10.1.2.3", "172.16.0.9", "192.168.2.158", "169.254.1.1",
                               "::1", "fe80::1", "fd12:3456::1", "::ffff:192.168.1.5"})
        QVERIFY2(DiscoveryResponder::isNearby(QHostAddress(QString::fromLatin1(nearby))), nearby);
    for (const char *far : {"8.8.8.8", "172.32.0.1", "2001:db8::1", "100.64.0.1"})
        QVERIFY2(!DiscoveryResponder::isNearby(QHostAddress(QString::fromLatin1(far))), far);
}

void HostDiscoveryTest::repliesAreRationed()
{
    qint64 now = 1'000'000;
    DiscoveryResponder responder(&HostDiscoveryTest::sample);
    responder.setClockForTests([&now] { return now; });
    QVERIFY(responder.start(0));

    QUdpSocket socket;
    QVERIFY(socket.bind(QHostAddress::LocalHost, 0));
    const auto probe = [&] {
        socket.writeDatagram(QByteArray(DiscoveryResponder::kProbe), QHostAddress::LocalHost, responder.port());
    };
    const auto replies = [&] {
        QTest::qWait(150);
        int count = 0;
        while (socket.hasPendingDatagrams()) {
            socket.receiveDatagram();
            ++count;
        }
        return count;
    };
    // A StrmLight sends three probes half a second apart: three replies. Three
    // inside the gap: one.
    probe();
    probe();
    probe();
    QCOMPARE(replies(), 1);
    now += DiscoveryResponder::kMinReplyGapMs;
    probe();
    QCOMPARE(replies(), 1);
    now += DiscoveryResponder::kMinReplyGapMs - 1;
    probe();
    QCOMPARE(replies(), 0);
}

namespace {
// The host's side of the link, on a thread of its own: pass() waits without
// running this thread's event loop, as the desktop StrmQt waits on another
// process.
struct HostThread
{
    QThread thread;
    HostLink *link = nullptr;
    QMutex mutex;
    QStringList taken;
    bool accept = true;

    explicit HostThread(const QString &path)
    {
        link = new HostLink(path);
        link->moveToThread(&thread);
        thread.start();
        bool listening = false;
        QMetaObject::invokeMethod(link, [this, &listening] {
            listening = link->listen([this](const QString &code) {
                QMutexLocker lock(&mutex);
                taken << code;
                return accept;
            });
        }, Qt::BlockingQueuedConnection);
        if (!listening)
            qWarning() << "host link did not listen";
    }
    ~HostThread()
    {
        QMetaObject::invokeMethod(link, [this] { delete link; }, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
};
} // namespace

void HostDiscoveryTest::aCodeReachesTheHost()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("link"));
    HostThread host(path);
    HostLink desktop(path);

    QVERIFY(desktop.pass(QStringLiteral("4831")));
    {
        QMutexLocker lock(&host.mutex);
        QCOMPARE(host.taken, QStringList{QStringLiteral("4831")});
        host.accept = false;
    }
    // The host refusing it is no.
    QVERIFY(!desktop.pass(QStringLiteral("1111")));

    // Only this user can open it.
    QFile::Permissions permissions = QFileInfo(path).permissions();
    QVERIFY(!(permissions & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)));
}

void HostDiscoveryTest::noHostMeansTheCodeStaysHere()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostLink desktop(dir.filePath(QStringLiteral("nobody")));
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!desktop.pass(QStringLiteral("4831"), 500));
    QVERIFY(timer.elapsed() < 1000);
}

QTEST_GUILESS_MAIN(HostDiscoveryTest)
#include "tst_host_discovery.moc"
