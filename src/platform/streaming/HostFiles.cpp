#include "HostFiles.h"

#include <algorithm>

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

// The host's sound: everything that plays or records it is pointed here.
QString pulseServer(const HostConfig &config)
{
    return QStringLiteral("unix:%t/%1/native").arg(audioRuntimeDir(config));
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
    path(QStringLiteral("PipeWire"), config.pipewire);
    path(QStringLiteral("WirePlumber"), config.wireplumber);
    path(QStringLiteral("The streaming host's folder"), config.stateDir);
    static const QRegularExpression socketName(QStringLiteral("^[A-Za-z0-9_-]+$"));
    static const QRegularExpression unitName(QStringLiteral("^[A-Za-z0-9_.-]+\\.service$"));
    for (const QString &unit : {config.hostUnit, config.audioUnit}) {
        if (!unitName.match(unit).hasMatch())
            problems << QStringLiteral("Not a usable service name: %1").arg(unit);
    }
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
        // The desktop's PipeWire carries KWin's screencast; the host's own
        // carries the sound.
        QStringLiteral("After=pipewire.service %1").arg(config.audioUnit),
        QStringLiteral("Requires=%1").arg(config.audioUnit),
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
        // Programs that play through PulseAudio's protocol (Qt WebEngine, VLC,
        // KWin's own sounds) play on the host's PipeWire.
        QStringLiteral("Environment=PULSE_SERVER=%1").arg(pulseServer(config)),
    };
    for (const QString &pair : config.environment)
        lines << QStringLiteral("Environment=%1").arg(pair);
    // StrmQt alone is moved to the host's PipeWire for programs that talk to
    // PipeWire directly (mpv): KWin itself must stay on the desktop's, where
    // Sunshine finds its screencast. KWin splits the session command itself.
    lines << QStringLiteral("ExecStart=%1 --virtual --width %2 --height %3 --socket %4 "
                            "--no-lockscreen --no-global-shortcuts "
                            "--exit-with-session \"/usr/bin/env PIPEWIRE_REMOTE=%5 %6\"")
                 .arg(config.kwin)
                 .arg(config.width)
                 .arg(config.height)
                 .arg(config.socket, audioRemote(config), config.strmqt)
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
        // It records the host's sound, never the desktop's (HostFiles.h).
        QStringLiteral("Environment=PULSE_SERVER=%1").arg(pulseServer(config)),
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

QString audioRemote(const HostConfig &config)
{
    return config.socket + QStringLiteral("-pipewire");
}

QString audioRuntimeDir(const HostConfig &config)
{
    return config.socket + QStringLiteral("-audio");
}

QString audioConfPath(const HostConfig &config)
{
    return config.stateDir + QStringLiteral("/audio/pipewire.conf");
}

QString audioConf(const HostConfig &config)
{
    // The least PipeWire that links streams to one virtual speaker: no sound
    // card, Bluetooth or camera, and no claim on the desktop PulseAudio
    // server's D-Bus name. WirePlumber's "policy" profile links the streams
    // and manages no hardware. module-access grants clients their
    // permissions; without it they wait for an answer that never comes.
    const QStringList lines{
        QStringLiteral("# Written by StrmQt (Settings, Streaming host). Changes are overwritten."),
        QStringLiteral("context.properties = {"),
        QStringLiteral("    core.daemon = true"),
        QStringLiteral("    core.name = %1").arg(audioRemote(config)),
        QStringLiteral("    link.max-buffers = 16"),
        QStringLiteral("    default.clock.rate = 48000"),
        QStringLiteral("}"),
        QStringLiteral("context.spa-libs = {"),
        QStringLiteral("    audio.convert.* = audioconvert/libspa-audioconvert"),
        QStringLiteral("    support.* = support/libspa-support"),
        QStringLiteral("}"),
        QStringLiteral("context.modules = ["),
        QStringLiteral("    { name = libpipewire-module-rt args = { nice.level = -11 rt.prio = 88 "
                       "rtportal.enabled = false } flags = [ ifexists nofail ] }"),
        QStringLiteral("    { name = libpipewire-module-protocol-native }"),
        QStringLiteral("    { name = libpipewire-module-access }"),
        QStringLiteral("    { name = libpipewire-module-metadata }"),
        QStringLiteral("    { name = libpipewire-module-spa-node-factory }"),
        QStringLiteral("    { name = libpipewire-module-client-node }"),
        QStringLiteral("    { name = libpipewire-module-client-device }"),
        QStringLiteral("    { name = libpipewire-module-adapter }"),
        QStringLiteral("    { name = libpipewire-module-link-factory }"),
        QStringLiteral("    { name = libpipewire-module-session-manager }"),
        // "unix:native" is under PULSE_RUNTIME_PATH, which the unit sets.
        QStringLiteral("    { name = libpipewire-module-protocol-pulse args = { "
                       "server.address = [ \"unix:native\" ] server.dbus-name = \"\" } }"),
        QStringLiteral("]"),
        QStringLiteral("context.objects = ["),
        QStringLiteral("    { factory = spa-node-factory args = { factory.name = support.node.driver "
                       "node.name = Dummy-Driver node.group = pipewire.dummy priority.driver = 200000 } }"),
        QStringLiteral("    { factory = adapter args = {"),
        QStringLiteral("        factory.name = support.null-audio-sink"),
        QStringLiteral("        node.name = %1").arg(QLatin1String(kAudioSink)),
        QStringLiteral("        node.description = \"StrmQt streaming host\""),
        QStringLiteral("        media.class = Audio/Sink"),
        QStringLiteral("        audio.position = [ FL FR ]"),
        QStringLiteral("        monitor.channel-volumes = true"),
        QStringLiteral("        object.linger = true"),
        QStringLiteral("        priority.session = 1000"),
        QStringLiteral("        priority.driver = 1000"),
        QStringLiteral("    } }"),
        QStringLiteral("]"),
        QStringLiteral("context.exec = ["),
        QStringLiteral("    { path = \"%1\" args = \"--profile policy\" }").arg(config.wireplumber),
        QStringLiteral("]"),
    };
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString audioUnit(const HostConfig &config)
{
    const QString runtime = audioRuntimeDir(config);
    const QString home = config.stateDir + QStringLiteral("/audio");
    const QStringList lines{
        QStringLiteral("# Written by StrmQt (Settings, Streaming host). Changes are overwritten."),
        QStringLiteral("[Unit]"),
        QStringLiteral("Description=Sound for the headless StrmQt, apart from the desktop's"),
        QStringLiteral("PartOf=%1").arg(config.hostUnit),
        QString(),
        QStringLiteral("[Service]"),
        // WirePlumber, started by this PipeWire, connects to it by this name.
        QStringLiteral("Environment=PIPEWIRE_REMOTE=%1").arg(audioRemote(config)),
        QStringLiteral("Environment=PULSE_RUNTIME_PATH=%t/%1").arg(runtime),
        // WirePlumber's remembered volumes and defaults, and any drop-ins the
        // user keeps for the desktop's, are not this one's.
        QStringLiteral("Environment=XDG_STATE_HOME=%1").arg(home),
        QStringLiteral("Environment=XDG_CONFIG_HOME=%1").arg(home),
        QStringLiteral("UnsetEnvironment=PIPEWIRE_CORE"),
        QStringLiteral("RuntimeDirectory=%1").arg(runtime),
        QStringLiteral("ExecStart=%1 -c %2").arg(config.pipewire, audioConfPath(config)),
        // Started is not listening: the screen's StrmQt would find no sound.
        QStringLiteral("ExecStartPost=/bin/sh -c 'n=0; until [ -S %t/%1/native ] || [ $$n -ge 100 ]; "
                       "do sleep 0.1; n=$$((n+1)); done'")
            .arg(runtime),
        QStringLiteral("Restart=on-failure"),
        QStringLiteral("RestartSec=3"),
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

bool portsLent(const QString &localRange, const QString &reserved, int first, int last)
{
    const QStringList bounds = localRange.simplified().split(QLatin1Char(' '));
    if (bounds.size() != 2)
        return false;
    bool lowOk = false;
    bool highOk = false;
    const int low = bounds.at(0).toInt(&lowOk);
    const int high = bounds.at(1).toInt(&highOk);
    if (!lowOk || !highOk)
        return false;
    QList<std::pair<int, int>> kept;
    for (const QString &entry : reserved.simplified().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QStringList ends = entry.trimmed().split(QLatin1Char('-'));
        const int from = ends.at(0).toInt();
        kept.append({from, ends.size() > 1 ? ends.at(1).toInt() : from});
    }
    for (int port = std::max(first, low); port <= std::min(last, high); ++port) {
        const bool isKept = std::any_of(kept.cbegin(), kept.cend(), [port](const std::pair<int, int> &range) {
            return port >= range.first && port <= range.second;
        });
        if (!isKept)
            return true;
    }
    return false;
}

} // namespace strmqt::streaming
