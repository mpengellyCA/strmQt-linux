#include <QtTest>
#include "remote/NetworkAddressHelper.h"

using namespace strmqt;

class NetworkAddressHelperTest : public QObject
{
    Q_OBJECT

private slots:
    void sanIpAddressesIncludesLoopback();
    void tailscaleDetection();
    void lanIpDetection();
    void urlFormatting();
};

void NetworkAddressHelperTest::sanIpAddressesIncludesLoopback()
{
    const QStringList sans = NetworkAddressHelper::allHostAddressesForSan();
    QVERIFY(sans.contains(QStringLiteral("127.0.0.1")));
    QVERIFY(sans.contains(QStringLiteral("::1")));
}

void NetworkAddressHelperTest::tailscaleDetection()
{
    const bool hasTs = NetworkAddressHelper::hasTailscale();
    const QString tsIp = NetworkAddressHelper::tailscaleIp();
    if (hasTs) {
        QVERIFY(!tsIp.isEmpty());
        QVERIFY(tsIp.startsWith(QStringLiteral("100.")) || tsIp.contains(QStringLiteral(":")));
        const QString tsUrl = NetworkAddressHelper::tailscaleUrl(8337);
        QVERIFY(tsUrl.startsWith(QStringLiteral("https://")));
        QVERIFY(tsUrl.endsWith(QStringLiteral(":8337")));
    } else {
        QVERIFY(tsIp.isEmpty());
    }
}

void NetworkAddressHelperTest::lanIpDetection()
{
    const QString lan = NetworkAddressHelper::lanIp();
    if (!lan.isEmpty()) {
        QVERIFY(lan != QStringLiteral("127.0.0.1"));
        QVERIFY(lan != QStringLiteral("::1"));
    }
}

void NetworkAddressHelperTest::urlFormatting()
{
    const QString lanUrl = NetworkAddressHelper::lanUrl(9000);
    if (!NetworkAddressHelper::lanIp().isEmpty()) {
        QCOMPARE(lanUrl, QStringLiteral("https://%1:9000").arg(NetworkAddressHelper::lanIp()));
    } else {
        QCOMPARE(lanUrl, QStringLiteral("https://localhost:9000"));
    }
}

QTEST_MAIN(NetworkAddressHelperTest)
#include "tst_network_address_helper.moc"
