// strmqt-cli — Emby probe tool (M1 acceptance). Verifies the client stack against a
// real server without the UI: status / login / libraries / resume / latest / nextup /
// logout. Credentials come from the terminal or STRMQT_PASSWORD; never from the repo.

#include "core/Log.h"
#include "core/Settings.h"
#include "platform/SecretsStore.h"
#include "server/emby/EmbyClient.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QSysInfo>
#include <QTextStream>

#include <cstdio>

#ifdef Q_OS_UNIX
#include <termios.h>
#include <unistd.h>
#endif

using namespace strmqt;

namespace {

const auto kTokenSecretKey = QStringLiteral("emby/accessToken");

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

// Spin the event loop until the future resolves (network I/O needs the loop).
template<class T> Result<T> await(QFuture<Result<T>> future)
{
    if (future.isFinished())
        return future.result();
    QEventLoop loop;
    QFutureWatcher<Result<T>> watcher;
    QObject::connect(&watcher, &QFutureWatcher<Result<T>>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    loop.exec();
    return future.result();
}

QString promptPassword()
{
    err() << "Password: " << Qt::flush;
#ifdef Q_OS_UNIX
    termios oldAttrs{};
    const bool haveTty = tcgetattr(STDIN_FILENO, &oldAttrs) == 0;
    if (haveTty) {
        termios noEcho = oldAttrs;
        noEcho.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &noEcho);
    }
    QTextStream in(stdin);
    const QString password = in.readLine();
    if (haveTty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldAttrs);
        err() << "\n" << Qt::flush;
    }
    return password;
#else
    QTextStream in(stdin);
    return in.readLine();
#endif
}

QString formatTime(qint64 ms)
{
    const qint64 totalSeconds = ms / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(totalSeconds / 3600)
        .arg((totalSeconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

QString itemLabel(const MediaItem &item)
{
    if (item.type == QLatin1String("Episode") && !item.seriesName.isEmpty()) {
        return QStringLiteral("%1 — S%2E%3 — %4")
            .arg(item.seriesName)
            .arg(item.parentIndexNumber)
            .arg(item.indexNumber)
            .arg(item.name);
    }
    if (item.productionYear > 0)
        return QStringLiteral("%1 (%2)").arg(item.name).arg(item.productionYear);
    return item.name;
}

void printItems(const QList<MediaItem> &items)
{
    for (const MediaItem &item : items) {
        QString line = QStringLiteral("  [%1] %2").arg(item.id, itemLabel(item));
        if (item.isResumable()) {
            line += QStringLiteral("  — resume at %1 / %2")
                        .arg(formatTime(item.positionMs()), formatTime(item.runtimeMs()));
        }
        out() << line << "\n";
    }
}

// Restores server URL + stored session onto the client. Returns false when any
// piece is missing (caller should suggest `login`).
bool restoreSession(emby::EmbyClient &client, Settings &settings, SecretsStore &secrets)
{
    const Result<QString> stored = await(secrets.readSecret(kTokenSecretKey));
    if (!stored.ok()) {
        err() << "warning: could not read the saved access token: " << stored.error << "\n";
        return false;
    }
    const QString token = stored.value;
    const QString userId = settings.userId();
    if (token.isEmpty() || userId.isEmpty())
        return false;
    client.setSession(token, userId);
    return true;
}

int commandStatus(emby::EmbyClient &client)
{
    const auto result = await(client.publicSystemInfo());
    if (!result.ok()) {
        err() << "error: " << result.error << "\n";
        return 1;
    }
    out() << "Server:  " << result.value.name << "\n"
          << "Version: " << result.value.version << "\n"
          << "Id:      " << result.value.id << "\n";
    return 0;
}

int commandLogin(emby::EmbyClient &client, Settings &settings, SecretsStore &secrets,
                 const QString &usernameArg)
{
    QString username = usernameArg.isEmpty() ? settings.username() : usernameArg;
    if (username.isEmpty()) {
        err() << "error: no username. Pass --user NAME (it is remembered).\n";
        return 2;
    }

    QString password = qEnvironmentVariable("STRMQT_PASSWORD");
    if (password.isEmpty())
        password = promptPassword();
    if (password.isEmpty()) {
        err() << "error: empty password.\n";
        return 2;
    }

    const auto result = await(client.authenticateByName(username, password));
    if (!result.ok()) {
        err() << "login failed: " << result.error << "\n";
        return 1;
    }

    settings.setServerUrl(client.baseUrl());
    settings.setUsername(result.value.user.name);
    settings.setUserId(result.value.user.id);
    const Result<bool> stored =
        await(secrets.writeSecret(kTokenSecretKey, result.value.accessToken));
    if (!stored.ok()) {
        err() << "warning: could not persist the access token; you will need to log in "
                 "again next time.\n";
    }

    out() << "Logged in as " << result.value.user.name;
    if (!stored.ok())
        out() << " (access token was not saved)\n";
    else if (secrets.isWalletBacked())
        out() << " (token stored in KWallet)\n";
    else
        out() << " (token kept for this process only)\n";
    return 0;
}

int commandLogout(Settings &settings, SecretsStore &secrets)
{
    const Result<bool> removed = await(secrets.removeSecret(kTokenSecretKey));
    settings.setUserId(QString());
    if (!removed.ok()) {
        err() << "warning: logged out, but the saved token could not be removed: " << removed.error
              << "\n";
        return 1;
    }
    out() << "Logged out; saved token removed.\n";
    return 0;
}

int commandLibraries(emby::EmbyClient &client)
{
    const auto result = await(client.userViews());
    if (!result.ok()) {
        err() << "error: " << result.error << "\n";
        return 1;
    }
    out() << result.value.size() << " libraries:\n";
    for (const Library &library : result.value) {
        out() << QStringLiteral("  [%1] %2%3\n")
                     .arg(library.id, library.name,
                          library.collectionType.isEmpty()
                              ? QString()
                              : QStringLiteral(" (%1)").arg(library.collectionType));
    }
    return 0;
}

int commandResume(emby::EmbyClient &client)
{
    const auto result = await(client.resumeItems(20));
    if (!result.ok()) {
        err() << "error: " << result.error << "\n";
        return 1;
    }
    out() << "Continue watching (" << result.value.items.size() << "):\n";
    printItems(result.value.items);
    return 0;
}

int commandLatest(emby::EmbyClient &client, const QString &parentId)
{
    const auto result = await(client.latestItems(parentId, 20));
    if (!result.ok()) {
        err() << "error: " << result.error << "\n";
        return 1;
    }
    out() << "Latest (" << result.value.size() << "):\n";
    printItems(result.value);
    return 0;
}

int commandNextUp(emby::EmbyClient &client)
{
    const auto result = await(client.nextUp(20));
    if (!result.ok()) {
        err() << "error: " << result.error << "\n";
        return 1;
    }
    out() << "Next up (" << result.value.items.size() << "):\n";
    printItems(result.value.items);
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("StrmQt"));
    QCoreApplication::setApplicationName(QStringLiteral("strmqt"));
    QCoreApplication::setApplicationVersion(QStringLiteral(STRMQT_VERSION));
    initLogging();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("StrmQt Emby probe tool.\n"
                       "Commands: status | login | logout | libraries | resume | "
                       "latest [libraryId] | nextup"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("Command to run"));
    parser.addPositionalArgument(QStringLiteral("args"), QStringLiteral("Command arguments"),
                                 QStringLiteral("[args...]"));
    const QCommandLineOption serverOption(
        {QStringLiteral("s"), QStringLiteral("server")},
        QStringLiteral("Server base URL (default: stored/PLAN default)."), QStringLiteral("url"));
    const QCommandLineOption userOption({QStringLiteral("u"), QStringLiteral("user")},
                                        QStringLiteral("Username for login."),
                                        QStringLiteral("name"));
    parser.addOption(serverOption);
    parser.addOption(userOption);
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty())
        parser.showHelp(2);
    const QString command = args.first().toLower();

    Settings settings;
    SecretsStore secrets;

    emby::EmbyClient client;
    const QUrl serverUrl = parser.isSet(serverOption)
                               ? QUrl::fromUserInput(parser.value(serverOption))
                               : settings.serverUrl();
    if (serverUrl.isEmpty()) {
        // There is no default server, by design. Say so plainly rather than
        // failing somewhere in QtNetwork with a URL error.
        qCritical("No server configured. Pass --server <url>, or sign in with the app first.");
        return 2;
    }
    client.setBaseUrl(serverUrl);
    client.setDeviceId(settings.deviceId());
    client.setDeviceName(QSysInfo::machineHostName());

    if (command == QLatin1String("status"))
        return commandStatus(client);
    if (command == QLatin1String("login"))
        return commandLogin(client, settings, secrets, parser.value(userOption));
    if (command == QLatin1String("logout"))
        return commandLogout(settings, secrets);

    // Everything below needs a session.
    if (!restoreSession(client, settings, secrets)) {
        err() << "error: not logged in. Run: strmqt-cli login --user NAME\n";
        return 2;
    }

    if (command == QLatin1String("libraries"))
        return commandLibraries(client);
    if (command == QLatin1String("resume"))
        return commandResume(client);
    if (command == QLatin1String("latest"))
        return commandLatest(client, args.value(1));
    if (command == QLatin1String("nextup"))
        return commandNextUp(client);

    err() << "unknown command: " << command << "\n";
    parser.showHelp(2);
}
