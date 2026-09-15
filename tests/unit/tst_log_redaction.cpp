#include "core/Log.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>

#include <QTest>

using namespace strmqt;

class LogRedactionTest : public QObject
{
    Q_OBJECT

private slots:
    void redactsCredentials_data();
    void redactsCredentials();
    void installedMessageHandlerRedacts();
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
    QTest::newRow("query-pw")
        << QStringLiteral("failed https://server/Videos/id?foo=1&pw=secret-password&bar=2")
        << QStringLiteral("secret-password");
    QTest::newRow("query-password")
        << QStringLiteral("https://server/Users/authenticate?user=alice&password=SuperSecretPassword&format=json")
        << QStringLiteral("SuperSecretPassword");
    QTest::newRow("encoded-pw")
        << QStringLiteral("url%3Ffoo%3D1%26pw%3Dencoded-secret-pass%26bar%3D2")
        << QStringLiteral("encoded-secret-pass");
    QTest::newRow("encoded-password")
        << QStringLiteral("url%3Ffoo%3D1%26password%3Dencoded-long-pass%26bar%3D2")
        << QStringLiteral("encoded-long-pass");
}

void LogRedactionTest::redactsCredentials()
{
    QFETCH(QString, message);
    QFETCH(QString, secret);

    const QString result = redactSensitiveText(message);
    QVERIFY2(!result.contains(secret), qPrintable(result));
    QVERIFY(result.contains(QStringLiteral("<redacted>")));
}

void LogRedactionTest::installedMessageHandlerRedacts()
{
    const QtMessageHandler defaultHandler = qInstallMessageHandler(nullptr);
    initLogging();

    const QtMessageHandler installedHandler = qInstallMessageHandler(nullptr);
    QVERIFY(installedHandler != nullptr);
    QVERIFY(installedHandler != defaultHandler);

    // Reinstall the strmqt message handler
    qInstallMessageHandler(installedHandler);

    int pipeFds[2];
    QCOMPARE(pipe(pipeFds), 0);

    const int savedStderr = dup(STDERR_FILENO);
    QVERIFY(savedStderr >= 0);
    QVERIFY(dup2(pipeFds[1], STDERR_FILENO) >= 0);
    close(pipeFds[1]);

    const QString secretWarning = QStringLiteral("WarningSecretToken123");
    const QString pwWarning = QStringLiteral("WarningPassword456");
    const QString secretDirect = QStringLiteral("DirectSecretPassword789");

    // 1. Verify message passed to qWarning() is redacted by the installed handler
    qWarning("Failed request: https://server/api?api_key=%s&pw=%s",
             qPrintable(secretWarning), qPrintable(pwWarning));

    // 2. Verify message formatted directly by the installed handler
    installedHandler(QtWarningMsg, QMessageLogContext(),
                     QStringLiteral("Direct error https://server/api?password=%1").arg(secretDirect));

    fflush(stderr);

    QVERIFY(dup2(savedStderr, STDERR_FILENO) >= 0);
    close(savedStderr);

    char buf[4096];
    memset(buf, 0, sizeof(buf));
    const ssize_t bytesRead = read(pipeFds[0], buf, sizeof(buf) - 1);
    close(pipeFds[0]);

    // Restore original QtTest message handler
    qInstallMessageHandler(defaultHandler);

    QVERIFY(bytesRead > 0);
    const QString output = QString::fromUtf8(buf);

    // Ensure raw secrets were never printed to stderr
    QVERIFY2(!output.contains(secretWarning), qPrintable(output));
    QVERIFY2(!output.contains(pwWarning), qPrintable(output));
    QVERIFY2(!output.contains(secretDirect), qPrintable(output));

    // Ensure redaction marker is present
    QVERIFY2(output.contains(QStringLiteral("<redacted>")), qPrintable(output));
}

QTEST_MAIN(LogRedactionTest)
#include "tst_log_redaction.moc"
