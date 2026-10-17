#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "MockEmbyServer.h"
#include "core/Settings.h"
#include "platform/SecretsStore.h"

#include <csignal>
#include <pty.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <utmp.h>

using namespace strmqt;

namespace {

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kTokenA = QStringLiteral("token-issued-by-server-a");

template<class T> T await(QFuture<T> future)
{
    QFutureWatcher<T> watcher;
    QSignalSpy finished(&watcher, &QFutureWatcher<T>::finished);
    watcher.setFuture(future);
    if (!future.isFinished())
        finished.wait(5000);
    return future.result();
}

} // namespace

// strmqt-cli is its own executable with its own main(), so nothing else in the
// suite runs it. These run the built binary against mock servers, in a private
// HOME with no session bus: Settings and the vault file land in the temporary
// directory, and no keyring is ever asked.
class CliTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void storedTokenIsSentOnlyToTheServerThatIssuedIt();
    void interruptedPasswordPromptGivesTheTerminalItsEchoBack();

private:
    QProcessEnvironment environment() const;
    void seedSession(const QUrl &server);
    int runCli(const QStringList &arguments, QByteArray *output = nullptr);

    QTemporaryDir *m_home = nullptr;
};

void CliTest::init()
{
    m_home = new QTemporaryDir;
    QVERIFY(m_home->isValid());
}

void CliTest::cleanup()
{
    delete m_home;
    m_home = nullptr;
}

QProcessEnvironment CliTest::environment() const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("HOME"), m_home->path());
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), m_home->filePath(QStringLiteral("config")));
    env.insert(QStringLiteral("XDG_DATA_HOME"), m_home->filePath(QStringLiteral("data")));
    env.insert(QStringLiteral("XDG_CACHE_HOME"), m_home->filePath(QStringLiteral("cache")));
    // No session bus, so SecretsStore takes the vault file under XDG_DATA_HOME
    // and the user's real keyring is never touched.
    env.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"),
               QStringLiteral("unix:path=") + m_home->filePath(QStringLiteral("no-bus")));
    env.remove(QStringLiteral("STRMQT_PASSWORD"));
    return env;
}

// What a login against `server` leaves behind: the account in the settings
// file and its token in the vault, under the key scoped to that server.
void CliTest::seedSession(const QUrl &server)
{
    {
        Settings settings(m_home->filePath(QStringLiteral("config/StrmQt/strmqt.conf")));
        settings.setServerUrl(server);
        settings.setUserId(kUserId);
        settings.setUsername(QStringLiteral("fixture"));
    }
    SecretsStore vault(m_home->filePath(QStringLiteral("data/StrmQt/strmqt/secrets.ini")));
    QVERIFY(await(vault.writeSecret(Settings::tokenSecretKeyFor(server, kUserId), kTokenA)).ok());
}

int CliTest::runCli(const QStringList &arguments, QByteArray *output)
{
    QProcess cli;
    cli.setProcessEnvironment(environment());
    cli.setProcessChannelMode(QProcess::MergedChannels);
    cli.start(QStringLiteral(STRMQT_CLI_PATH), arguments);
    if (!cli.waitForStarted(5000))
        return -1;
    // Not waitForFinished(): the mock servers answer from this event loop.
    QElapsedTimer timer;
    timer.start();
    while (cli.state() != QProcess::NotRunning && timer.elapsed() < 15000)
        QTest::qWait(20);
    if (cli.state() != QProcess::NotRunning) {
        cli.kill();
        cli.waitForFinished(2000);
        return -1;
    }
    if (output)
        *output = cli.readAll();
    return cli.exitStatus() == QProcess::NormalExit ? cli.exitCode() : -1;
}

// --server points the CLI somewhere other than the stored server. The token
// was issued by the stored one and must never be offered to another: before
// the fix it was looked up under the stored server's key and sent to whatever
// --server named.
void CliTest::storedTokenIsSentOnlyToTheServerThatIssuedIt()
{
    MockEmbyServer serverA;
    MockEmbyServer serverB;
    QVERIFY(serverA.start());
    QVERIFY(serverB.start());
    const QString views = QStringLiteral("/Users/%1/Views").arg(kUserId);
    const QByteArray library =
        QByteArrayLiteral("{\"Items\":[{\"Id\":\"lib1\",\"Name\":\"Films\","
                          "\"CollectionType\":\"movies\"}],\"TotalRecordCount\":1}");
    serverA.addRoute(QStringLiteral("GET"), views, 200, library);
    serverB.addRoute(QStringLiteral("GET"), views, 200, library);
    seedSession(serverA.baseUrl());

    const auto carriesToken = [](const MockEmbyServer &server) {
        for (const MockEmbyServer::ReceivedRequest &request : server.requests()) {
            for (auto it = request.headers.cbegin(); it != request.headers.cend(); ++it) {
                if (it.value().contains(kTokenA.toUtf8()))
                    return true;
            }
            if (request.query.contains(kTokenA))
                return true;
        }
        return false;
    };

    // Elsewhere: no session for that server, and nothing sent to it.
    QByteArray output;
    QCOMPARE(runCli({QStringLiteral("--server"), serverB.baseUrl().toString(),
                     QStringLiteral("libraries")},
                    &output),
             2);
    QVERIFY2(output.contains("not logged in"), output.constData());
    QVERIFY(!carriesToken(serverB));
    QCOMPARE(serverB.requestCount(), 0);

    // The stored server, by default and by --server: the token goes there, so
    // the case above failed for the right reason.
    QCOMPARE(runCli({QStringLiteral("libraries")}, &output), 0);
    QVERIFY2(output.contains("Films"), output.constData());
    QVERIFY(carriesToken(serverA));
    const int requestsToA = serverA.requestCount();
    QCOMPARE(runCli({QStringLiteral("--server"), serverA.baseUrl().toString(),
                     QStringLiteral("libraries")}),
             0);
    QVERIFY(serverA.requestCount() > requestsToA);
    QVERIFY(!carriesToken(serverB));
}

// Ctrl+C at the password prompt. Echo is off while the password is typed; a
// process killed there used to leave the shell a terminal that shows nothing.
// The CLI must put echo back and still die of the signal.
void CliTest::interruptedPasswordPromptGivesTheTerminalItsEchoBack()
{
    int master = -1;
    int slave = -1;
    QVERIFY(openpty(&master, &slave, nullptr, nullptr, nullptr) == 0);
    termios initial{};
    QVERIFY(tcgetattr(slave, &initial) == 0);
    QVERIFY(initial.c_lflag & ECHO);

    const QByteArray program = QByteArrayLiteral(STRMQT_CLI_PATH);
    const QProcessEnvironment env = environment();
    QList<QByteArray> envStorage;
    for (const QString &entry : env.toStringList())
        envStorage.append(entry.toLocal8Bit());
    QList<char *> envp;
    for (QByteArray &entry : envStorage)
        envp.append(entry.data());
    envp.append(nullptr);
    QByteArray argServer = QByteArrayLiteral("--server");
    QByteArray argUrl = QByteArrayLiteral("http://127.0.0.1:1");
    QByteArray argLogin = QByteArrayLiteral("login");
    QByteArray argUser = QByteArrayLiteral("--user");
    QByteArray argName = QByteArrayLiteral("fixture");
    char *argv[] = {const_cast<char *>(program.constData()), argServer.data(), argUrl.data(),
                    argLogin.data(), argUser.data(), argName.data(), nullptr};

    const pid_t child = fork();
    QVERIFY(child >= 0);
    if (child == 0) {
        ::close(master);
        if (login_tty(slave) != 0)
            _exit(126);
        execve(argv[0], argv, envp.data());
        _exit(127);
    }

    // The prompt is written before echo goes off; wait for both.
    QByteArray seen;
    bool echoOff = false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000 && !echoOff) {
        char buffer[256];
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(master, &readable);
        timeval tick{0, 20000};
        if (select(master + 1, &readable, nullptr, nullptr, &tick) > 0) {
            const ssize_t got = ::read(master, buffer, sizeof buffer);
            if (got > 0)
                seen.append(buffer, got);
        }
        termios now{};
        if (seen.contains("Password:") && tcgetattr(slave, &now) == 0)
            echoOff = !(now.c_lflag & ECHO);
    }
    if (!echoOff) {
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        ::close(master);
        ::close(slave);
        QFAIL(qPrintable(QStringLiteral("no echo-off password prompt; saw: ") +
                         QString::fromLocal8Bit(seen)));
    }

    QVERIFY(kill(child, SIGINT) == 0);
    int status = 0;
    QCOMPARE(waitpid(child, &status, 0), child);
    termios after{};
    const bool readBack = tcgetattr(slave, &after) == 0;
    ::close(master);
    ::close(slave);

    QVERIFY(WIFSIGNALED(status));
    QCOMPARE(WTERMSIG(status), SIGINT);
    QVERIFY(readBack);
    QVERIFY2(after.c_lflag & ECHO, "the terminal was left with echo off");
}

QTEST_GUILESS_MAIN(CliTest)
#include "tst_cli.moc"
