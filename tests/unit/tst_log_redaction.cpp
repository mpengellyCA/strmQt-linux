#include "core/Log.h"

#include <QTest>

using namespace strmqt;

class LogRedactionTest : public QObject
{
    Q_OBJECT

private slots:
    void redactsCredentials_data();
    void redactsCredentials();
};

void LogRedactionTest::redactsCredentials_data()
{
    QTest::addColumn<QString>("message");
    QTest::addColumn<QString>("secret");

    QTest::newRow("query")
        << QStringLiteral("failed https://server/Videos/id?foo=1&api_key=long-secret&bar=2")
        << QStringLiteral("long-secret");
    QTest::newRow("case-insensitive")
        << QStringLiteral("url?X-Emby-Token=MixedCaseSecret&other=value")
        << QStringLiteral("MixedCaseSecret");
    QTest::newRow("encoded-query")
        << QStringLiteral("url%3Ffoo%3D1%26api%5Fkey%3Dencoded-secret%26bar%3D2")
        << QStringLiteral("encoded-secret");
    QTest::newRow("authorization")
        << QStringLiteral("Authorization: Bearer header-secret, request failed")
        << QStringLiteral("header-secret");
}

void LogRedactionTest::redactsCredentials()
{
    QFETCH(QString, message);
    QFETCH(QString, secret);

    const QString result = redactSensitiveText(message);
    QVERIFY2(!result.contains(secret), qPrintable(result));
    QVERIFY(result.contains(QStringLiteral("<redacted>")));
}

QTEST_MAIN(LogRedactionTest)
#include "tst_log_redaction.moc"
