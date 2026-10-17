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
    void redactsOnlyTheCredential_data();
    void redactsOnlyTheCredential();
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

// Exact output: what a redaction leaves behind matters as much as what it takes.
// A rule that eats the rest of the line hides the diagnostics the log exists
// for; one that stops early leaks the tail of the secret.
void LogRedactionTest::redactsOnlyTheCredential_data()
{
    QTest::addColumn<QString>("message");
    QTest::addColumn<QString>("expected");

    // moc cannot follow a raw string literal through a macro argument, so
    // these rows go through a function instead of QStringLiteral.
    const auto text = [](const char *utf8) { return QString::fromUtf8(utf8); };
    const auto same = [](const char *name, const QString &message) {
        QTest::newRow(name) << message << message;
    };

    // ── Not credentials ──────────────────────────────────────────────────────
    same("cancel-token", QStringLiteral("request cancelToken=1 pageKey=foo done"));
    same("page-token", QStringLiteral("GET /browse?nextPageToken=CAUQAA&continuationToken=abc"));
    same("json-cancel-token", text(R"({"cancelToken":"7","pageKey":"foo"})"));

    // ── Still credentials, whatever they are glued to ────────────────────────
    QTest::newRow("camel-access-token")
        << QStringLiteral("GET /a?AccessToken=abc123&x=1")
        << QStringLiteral("GET /a?AccessToken=<redacted>&x=1");
    QTest::newRow("compound-tokens")
        << QStringLiteral("refresh_token=abc sessionToken=def authToken=ghi NewPw=jkl")
        << QStringLiteral("refresh_token=<redacted> sessionToken=<redacted> "
                          "authToken=<redacted> NewPw=<redacted>");

    // ── A URL inside something keeps what encloses it ────────────────────────
    QTest::newRow("qurl-debug")
        << text(R"x(QUrl("https://host/emby?api_key=SECRET"))x")
        << text(R"x(QUrl("https://host/emby?api_key=<redacted>"))x");
    QTest::newRow("qurl-debug-mid-query")
        << text(R"x(Loading QUrl("https://h/v?api_key=SECRET&Static=true") failed)x")
        << text(R"x(Loading QUrl("https://h/v?api_key=<redacted>&Static=true") failed)x");
    QTest::newRow("parenthesised-url")
        << QStringLiteral("request failed (https://h/v?api_key=SECRET), retrying")
        << QStringLiteral("request failed (https://h/v?api_key=<redacted>), retrying");
    QTest::newRow("single-quoted-url")
        << QStringLiteral("couldn't open 'https://h/v?api_key=SECRET': timed out")
        << QStringLiteral("couldn't open 'https://h/v?api_key=<redacted>': timed out");
    QTest::newRow("url-in-json")
        << text(R"({"Url":"https://h/v?api_key=SECRET","Name":"A B"})")
        << text(R"({"Url":"https://h/v?api_key=<redacted>","Name":"A B"})");
    QTest::newRow("encoded-url-in-qurl-debug")
        << text(R"x(QUrl("https://h/go?to=https%3A%2F%2Fh%3Fapi%5Fkey%3DSECRET%26a%3D1"))x")
        << text(R"x(QUrl("https://h/go?to=https%3A%2F%2Fh%3Fapi%5Fkey%3D<redacted>%26a%3D1"))x");

    // ── …but a closer nothing opened is part of the secret ───────────────────
    // A password may hold any of these; with no opener before the URL there is
    // no reason to read one as punctuation, and the tail must not survive.
    QTest::newRow("password-with-closers")
        << text(R"x(GET https://h/a?pw=ab)cd"ef]gh'ij}kl)x")
        << QStringLiteral("GET https://h/a?pw=<redacted>");
    QTest::newRow("password-ending-in-closer")
        << text(R"x(QUrl("https://h/a?pw=abc)"))x")
        << text(R"x(QUrl("https://h/a?pw=<redacted>"))x");
    QTest::newRow("password-with-closer-in-parentheses")
        << QStringLiteral("request failed (https://h/a?pw=ab)cd), retrying")
        << QStringLiteral("request failed (https://h/a?pw=<redacted>), retrying");
    QTest::newRow("password-ending-in-comma")
        << QStringLiteral("GET https://h/a?pw=abc, retrying")
        << QStringLiteral("GET https://h/a?pw=<redacted> retrying");
    // An encoded ampersand belongs to the value in a plain query string; only
    // in a wholly encoded URL does %26 separate parameters.
    QTest::newRow("password-with-encoded-ampersand")
        << QStringLiteral("GET https://h/a?pw=ab%26cd&x=1")
        << QStringLiteral("GET https://h/a?pw=<redacted>&x=1");

    // ── Quoted values: header parameters ─────────────────────────────────────
    QTest::newRow("emby-authorization-header")
        << text(R"(X-Emby-Authorization: MediaBrowser Client="StrmQt", Token="abc123", Device="tv")")
        << text(R"(X-Emby-Authorization: <redacted> Client="StrmQt", Token="<redacted>", Device="tv")");
    QTest::newRow("emby-authorization-header-qdebug")
        << text(R"x("MediaBrowser Client=\"StrmQt\", Token=\"abc123\", Device=\"tv\"")x")
        << text(R"x("MediaBrowser Client=\"StrmQt\", Token=\"<redacted>\", Device=\"tv\"")x");
    QTest::newRow("quoted-value-with-space")
        << text(R"(password="correct horse" user=alice)")
        << text(R"(password="<redacted>" user=alice)");
    QTest::newRow("quoted-value-with-escaped-quote")
        << text(R"(password="ab\"cd ef", user=alice)")
        << text(R"(password="<redacted>", user=alice)");
    QTest::newRow("quoted-value-never-closed")
        << text(R"(password="correct horse battery)")
        << text(R"(password="<redacted>)");
    // Not a quoted value at all: a password that merely starts with a quote.
    QTest::newRow("value-starting-with-quote")
        << text(R"(GET https://h/a?pw="ab"cd&x=1)")
        << QStringLiteral("GET https://h/a?pw=<redacted>&x=1");
    QTest::newRow("empty-value-before-closing-quote")
        << text(R"x(QUrl("https://h/a?api_key=") for user "alice")x")
        << text(R"x(QUrl("https://h/a?api_key=<redacted>") for user "alice")x");

    // ── JSON bodies ──────────────────────────────────────────────────────────
    QTest::newRow("json-pw")
        << text(R"({"Username":"alice","Pw":"hunter2"})")
        << text(R"({"Username":"alice","Pw":"<redacted>"})");
    QTest::newRow("json-spaced")
        << text(R"({ "Password" : "hunter 2", "AccessToken": "abc123", "X-Emby-Token" :"def456" })")
        << text(R"({ "Password" : "<redacted>", "AccessToken": "<redacted>", "X-Emby-Token" :"<redacted>" })");
    QTest::newRow("json-password-change")
        << text(R"({"CurrentPw":"old one","NewPw":"new one"})")
        << text(R"({"CurrentPw":"<redacted>","NewPw":"<redacted>"})");
    QTest::newRow("json-escaped-quote-in-value")
        << text(R"({"Pw":"hun\"ter2\\","User":"a"})")
        << text(R"({"Pw":"<redacted>","User":"a"})");
    QTest::newRow("json-value-with-closers")
        << text(R"x({"Pw":"a)b]c}d&e f","User":"a"})x")
        << text(R"x({"Pw":"<redacted>","User":"a"})x");
    QTest::newRow("json-truncated")
        << text(R"({"Username":"alice","Pw":"hunter2 and the rest)")
        << text(R"({"Username":"alice","Pw":"<redacted>)");
    // qDebug() << QByteArray/QString prints the body quoted and escaped.
    QTest::newRow("json-qdebug")
        << text(R"x(body "{\"Username\":\"alice\",\"Pw\":\"hunter 2\"}")x")
        << text(R"x(body "{\"Username\":\"alice\",\"Pw\":\"<redacted>\"}")x");
    QTest::newRow("json-qdebug-escaped-quote-in-value")
        << text(R"x(body "{\"Pw\":\"hun\\\"ter2\\\\\",\"User\":\"a\"}")x")
        << text(R"x(body "{\"Pw\":\"<redacted>\",\"User\":\"a\"}")x");

    // ── Headers ──────────────────────────────────────────────────────────────
    QTest::newRow("basic-authorization")
        << QStringLiteral("Authorization: Basic dXNlcjpwYXNz")
        << QStringLiteral("Authorization: Basic <redacted>");
    QTest::newRow("token-header")
        << QStringLiteral("X-Emby-Token: abc123; next")
        << QStringLiteral("X-Emby-Token: <redacted>; next");
}

void LogRedactionTest::redactsOnlyTheCredential()
{
    QFETCH(QString, message);
    QFETCH(QString, expected);

    QCOMPARE(redactSensitiveText(message), expected);
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
