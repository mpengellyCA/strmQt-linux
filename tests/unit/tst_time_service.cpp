// The streaming host's clock (TimeService): its replies over a real socket on
// this machine, and what it will not answer.

#include "remote/TimeService.h"

#include <QNetworkDatagram>
#include <QTest>
#include <QUdpSocket>
#include <QtEndian>

#include <cstring>

using strmqt::TimeService;

namespace {

QByteArray request(quint32 sequence, qint64 t1)
{
    QByteArray r(TimeService::kSize, '\0');
    std::memcpy(r.data(), "STQT", 4);
    r[4] = char(TimeService::kVersion);
    r[5] = char(TimeService::kRequest);
    qToBigEndian<quint32>(sequence, r.data() + 8);
    qToBigEndian<qint64>(t1, r.data() + 16);
    return r;
}

template<typename T>
T field(const QByteArray &datagram, int offset)
{
    return qFromBigEndian<T>(datagram.constData() + offset);
}

} // namespace

class TimeServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void aRequestIsAnsweredWithTheHostsClock()
    {
        TimeService service;
        QVERIFY(service.start(0));
        QVERIFY(service.isRunning());

        QUdpSocket socket;
        QVERIFY(socket.bind(QHostAddress::LocalHost, 0));
        const qint64 before = TimeService::now();
        socket.writeDatagram(request(7, 12345), QHostAddress::LocalHost, service.port());
        QVERIFY(socket.waitForReadyRead(2000));
        const qint64 after = TimeService::now();
        const QByteArray reply = socket.receiveDatagram().data();

        QCOMPARE(reply.size(), TimeService::kSize);
        QCOMPARE(reply.left(4), QByteArray("STQT"));
        QCOMPARE(quint8(reply.at(5)), TimeService::kReply);
        QCOMPARE(field<quint32>(reply, 8), 7u);
        QCOMPARE(field<qint64>(reply, 16), 12345); // t1 is the asker's, echoed
        const qint64 t2 = field<qint64>(reply, 24);
        const qint64 t3 = field<qint64>(reply, 32);
        QVERIFY2(before <= t2 && t2 <= t3 && t3 <= after,
                 qPrintable(QStringLiteral("%1 %2 %3 %4").arg(before).arg(t2).arg(t3).arg(after)));
        QCOMPARE(field<quint64>(reply, 40), TimeService::clockId());
        QVERIFY(TimeService::clockId() != 0);
    }

    void aReplyIsNeverLargerThanItsRequest()
    {
        const QByteArray reply = TimeService::replyTo(request(1, 2), 3, 4, 5);
        QCOMPARE(reply.size(), TimeService::kSize);
        QCOMPARE(field<qint64>(reply, 24), 3);
        QCOMPARE(field<qint64>(reply, 32), 4);
        QCOMPARE(field<quint64>(reply, 40), 5u);
    }

    void anythingElseIsNotAnswered()
    {
        QByteArray shortOne = request(1, 2);
        shortOne.chop(1);
        QByteArray longOne = request(1, 2) + 'x';
        QByteArray otherMagic = request(1, 2);
        otherMagic[0] = 'X';
        QByteArray otherVersion = request(1, 2);
        otherVersion[4] = char(2);
        const QByteArray aReply = TimeService::replyTo(request(1, 2), 3, 4, 5);
        for (const QByteArray &datagram : {shortOne, longOne, otherMagic, otherVersion, aReply})
            QVERIFY(TimeService::replyTo(datagram, 3, 4, 5).isEmpty());

        TimeService service;
        QVERIFY(service.start(0));
        QUdpSocket socket;
        QVERIFY(socket.bind(QHostAddress::LocalHost, 0));
        for (const QByteArray &datagram : {shortOne, longOne, otherMagic, aReply})
            socket.writeDatagram(datagram, QHostAddress::LocalHost, service.port());
        QVERIFY(!socket.waitForReadyRead(500));
    }

    void repliesAreRationed()
    {
        TimeService service;
        QVERIFY(service.start(0));
        QUdpSocket socket;
        QVERIFY(socket.bind(QHostAddress::LocalHost, 0));
        const int sent = TimeService::kMaxPerSenderPerSecond * 3;
        for (int i = 0; i < sent; ++i)
            socket.writeDatagram(request(quint32(i), i), QHostAddress::LocalHost, service.port());
        int replies = 0;
        while (socket.waitForReadyRead(300)) {
            while (socket.hasPendingDatagrams()) {
                socket.receiveDatagram();
                ++replies;
            }
        }
        // Sent within a second or two: two seconds' allowance at most.
        QVERIFY2(replies > 0 && replies <= 2 * TimeService::kMaxPerSenderPerSecond,
                 qPrintable(QString::number(replies)));
    }

    void itStopsAndStartsAgain()
    {
        TimeService service;
        QVERIFY(service.start(0));
        service.stop();
        QVERIFY(!service.isRunning());
        QCOMPARE(service.port(), quint16(0));
        QVERIFY(service.start(0));
        QVERIFY(service.port() != 0);
    }
};

QTEST_GUILESS_MAIN(TimeServiceTest)
#include "tst_time_service.moc"
