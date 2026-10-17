#include "HostFiles.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace strmqt::streaming {

namespace {

// Offsets from Sunshine's base port (its documentation's "port family").
constexpr int kTcpOffsets[] = {-5, 0, 1, 21};
constexpr int kUdpOffsets[] = {9, 10, 11, 13};

bool plainPath(const QString &path)
{
    static const QRegularExpression unsafe(QStringLiteral("[\\s\"'\\\\%$]"));
    return path.startsWith(QLatin1Char('/')) && !path.contains(unsafe);
}

// A value in sunshine.conf runs to the end of its line.
QString oneLine(QString value)
{
    static const QRegularExpression breaks(QStringLiteral("[\\r\\n]+"));
    return value.replace(breaks, QStringLiteral(" ")).trimmed();
}

} // namespace

QStringList configProblems(const HostConfig &config)
{
    QStringList problems;
    const auto path = [&problems](const QString &what, const QString &value) {
        if (!plainPath(value))
            problems << QStringLiteral("%1 is at a path a service file cannot name: %2")
                            .arg(what, value);
    };
    path(QStringLiteral("StrmQt"), config.strmqt);
    path(QStringLiteral("KWin"), config.kwin);
    path(QStringLiteral("Sunshine"), config.sunshine);
    path(QStringLiteral("The streaming host's folder"), config.stateDir);
    static const QRegularExpression socketName(QStringLiteral("^[A-Za-z0-9_-]+$"));
    static const QRegularExpression unitName(QStringLiteral("^[A-Za-z0-9_.-]+\\.service$"));
    if (!unitName.match(config.hostUnit).hasMatch())
        problems << QStringLiteral("Not a usable service name: %1").arg(config.hostUnit);
    if (!socketName.match(config.socket).hasMatch())
        problems << QStringLiteral("The screen's name is not usable: %1").arg(config.socket);
    if (config.width < 640 || config.height < 360 || config.width > 7680 || config.height > 4320)
        problems << QStringLiteral("The screen size %1x%2 is out of range.")
                        .arg(config.width)
                        .arg(config.height);
    if (config.port < 1029 || config.port > 65000)
        problems << QStringLiteral("Port %1 is out of range.").arg(config.port);
    for (const QString &pair : config.environment) {
        static const QRegularExpression assignment(
            QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*=[^\\s\"'\\\\%$]*$"));
        if (!assignment.match(pair).hasMatch())
            problems << QStringLiteral("Not a usable environment setting: %1").arg(pair);
    }
    return problems;
}

QString sunshineConf(const HostConfig &config)
{
    const QString dir = config.stateDir;
    QString name = oneLine(config.name);
    if (name.isEmpty())
        name = QStringLiteral("StrmQt");
    QStringList lines{
        QStringLiteral("# Written by StrmQt (Settings, Streaming host). Changes are overwritten."),
        QStringLiteral("sunshine_name = %1").arg(name),
        QStringLiteral("port = %1").arg(config.port),
        // KWin's own screencast: the only capture that sees a screen with no
        // monitor behind it without a permission dialog nobody could answer.
        QStringLiteral("capture = kwin"),
        QStringLiteral("upnp = off"),
        QStringLiteral("origin_web_ui_allowed = %1")
            .arg(config.webUiOnNetwork ? QStringLiteral("lan") : QStringLiteral("pc")),
        QStringLiteral("file_apps = %1/apps.json").arg(dir),
        QStringLiteral("file_state = %1/state.json").arg(dir),
        QStringLiteral("credentials_file = %1/state.json").arg(dir),
        QStringLiteral("log_path = %1/sunshine.log").arg(dir),
        QStringLiteral("pkey = %1/key.pem").arg(dir),
        QStringLiteral("cert = %1/cert.pem").arg(dir),
    };
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QByteArray appsJson()
{
    // One entry with no command: the stream is the screen, and StrmQt is
    // already on it.
    QJsonObject app{{QStringLiteral("name"), QStringLiteral("StrmQt")}};
    QJsonObject root{{QStringLiteral("env"), QJsonObject{}},
                     {QStringLiteral("apps"), QJsonArray{app}}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QString hostUnit(const HostConfig &config)
{
    QStringList lines{
        QStringLiteral("# Written by StrmQt (Settings, Streaming host). Changes are overwritten."),
        QStringLiteral("[Unit]"),
        QStringLiteral("Description=StrmQt on a headless screen, for streaming"),
        QStringLiteral("After=pipewire.service"),
        QString(),
        QStringLiteral("[Service]"),
        // Sunshine binds KWin's screencast protocol, which KWin only offers to
        // programs it can identify; on a screen that exists for Sunshine alone
        // there is nobody to protect from it.
        QStringLiteral("Environment=KWIN_WAYLAND_NO_PERMISSION_CHECKS=1"),
        QStringLiteral("Environment=QT_QPA_PLATFORM=wayland"),
        // The desktop session's displays, when there is one, are not this
        // service's.
        QStringLiteral("UnsetEnvironment=DISPLAY WAYLAND_DISPLAY"),
        QStringLiteral("Environment=%1=1").arg(QLatin1String(kHostMarker)),
    };
    for (const QString &pair : config.environment)
        lines << QStringLiteral("Environment=%1").arg(pair);
    lines << QStringLiteral("ExecStart=%1 --virtual --width %2 --height %3 --socket %4 "
                            "--no-lockscreen --no-global-shortcuts --exit-with-session %5")
                 .arg(config.kwin)
                 .arg(config.width)
                 .arg(config.height)
                 .arg(config.socket, config.strmqt)
          << QStringLiteral("Restart=on-failure") << QStringLiteral("RestartSec=3") << QString()
          << QStringLiteral("[Install]") << QStringLiteral("WantedBy=default.target");
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString sunshineUnit(const HostConfig &config)
{
    const QStringList lines{
        QStringLiteral("# Written by StrmQt (Settings, Streaming host). Changes are overwritten."),
        QStringLiteral("[Unit]"),
        QStringLiteral("Description=Sunshine for the headless StrmQt"),
        QStringLiteral("Requires=%1").arg(config.hostUnit),
        QStringLiteral("After=%1").arg(config.hostUnit),
        QStringLiteral("PartOf=%1").arg(config.hostUnit),
        QString(),
        QStringLiteral("[Service]"),
        QStringLiteral("Environment=WAYLAND_DISPLAY=%1").arg(config.socket),
        QStringLiteral("UnsetEnvironment=DISPLAY"),
        // "Started" for KWin is not "listening": a Sunshine that finds no
        // screen stays up with nothing to stream. $$ is a literal $ to systemd.
        QStringLiteral("ExecStartPre=/bin/sh -c 'n=0; until [ -S %t/%1 ] || [ $$n -ge 100 ]; "
                       "do sleep 0.1; n=$$((n+1)); done'")
            .arg(config.socket),
        QStringLiteral("ExecStart=%1 %2/sunshine.conf").arg(config.sunshine, config.stateDir),
        QStringLiteral("Restart=on-failure"),
        QStringLiteral("RestartSec=5"),
        QString(),
        QStringLiteral("[Install]"),
        QStringLiteral("WantedBy=default.target"),
    };
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QList<int> tcpPorts(int base)
{
    QList<int> ports;
    for (const int offset : kTcpOffsets)
        ports << base + offset;
    return ports;
}

QList<int> udpPorts(int base)
{
    QList<int> ports;
    for (const int offset : kUdpOffsets)
        ports << base + offset;
    return ports;
}

int webUiPort(int base)
{
    return base + 1;
}

int freePortBase(const std::function<bool(int, bool)> &isFree, int first, int tries, int step)
{
    for (int attempt = 0; attempt < tries; ++attempt) {
        const int base = first + attempt * step;
        if (base + 21 > 65535)
            break;
        bool free = true;
        for (const int port : tcpPorts(base))
            free = free && isFree(port, false);
        for (const int port : udpPorts(base))
            free = free && isFree(port, true);
        if (free)
            return base;
    }
    return 0;
}

} // namespace strmqt::streaming
