#include "app/admin/Admin.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "platform/streaming/HostFiles.h"
#include "platform/streaming/HostSupervisor.h"
#include "remote/HostLink.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QTextStream>
#include <QTimer>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

namespace strmqt::admin {

namespace {

// How long a set-up, restart or removal may take before this gives up on it.
constexpr int kHostStepsMs = 180'000;

QString json(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)).trimmed();
}

QString json(const QJsonArray &array)
{
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Indented)).trimmed();
}

// Until `host` has settled, if it is busy at all.
void waitForSettled(StreamingHost &host)
{
    if (!host.busy())
        return;
    QEventLoop loop;
    QObject::connect(&host, &StreamingHost::settled, &loop, &QEventLoop::quit);
    QTimer::singleShot(kHostStepsMs, &loop, &QEventLoop::quit);
    loop.exec();
}

// The paired remotes, without the hashes of their tokens.
QJsonArray devices(const Settings &settings)
{
    QJsonArray list;
    const QVariantList stored = settings.webRemoteDevices();
    for (const QVariant &device : stored) {
        QVariantMap map = device.toMap();
        map.remove(QStringLiteral("hash"));
        list << QJsonObject::fromVariantMap(map);
    }
    return list;
}

QString audioName(const QString &audio)
{
    if (audio == QLatin1String("51"))
        return QStringLiteral("5.1");
    if (audio == QLatin1String("71"))
        return QStringLiteral("7.1");
    return QStringLiteral("stereo");
}

QString streamLine(const StreamingHost &host)
{
    const QString bitrate = host.streamBitrate() > 0
                                ? QStringLiteral("%1 kbit/s").arg(host.streamBitrate())
                                : QStringLiteral("automatic (%1 kbit/s)").arg(host.automaticBitrate());
    return QStringLiteral("%1p%2, %3, codec %4, %5 sound")
        .arg(host.height())
        .arg(host.streamFps())
        .arg(bitrate, host.streamCodec(), audioName(host.streamAudio()));
}

QJsonObject streamObject(const StreamingHost &host)
{
    return QJsonObject{
        {QStringLiteral("height"), host.height()},
        {QStringLiteral("fps"), host.streamFps()},
        {QStringLiteral("bitrateKbps"), host.streamBitrate()},
        {QStringLiteral("automaticBitrateKbps"), host.automaticBitrate()},
        {QStringLiteral("codec"), host.streamCodec()},
        {QStringLiteral("audio"), host.streamAudio()},
    };
}

class Command
{
public:
    Command(const QStringList &arguments, Context &context)
        : m_args(arguments), m_c(context), m_out(*context.out), m_err(*context.err)
    {
    }

    int run()
    {
        const QString verb = m_args.value(0);
        if (!m_args.isEmpty())
            m_args.removeFirst();
        if (verb == QLatin1String("status"))
            return status();
        if (verb == QLatin1String("pair"))
            return pair();
        if (verb == QLatin1String("devices"))
            return listDevices();
        if (verb == QLatin1String("unpair"))
            return unpair();
        if (verb == QLatin1String("stream"))
            return stream();
        if (verb == QLatin1String("youtube-music"))
            return youtubeMusic();
        if (verb == QLatin1String("host"))
            return host();
        if (verb == QLatin1String("help") || verb == QLatin1String("--help") || verb == QLatin1String("-h")) {
            m_out << usage();
            return kOk;
        }
        return wrongUsage(verb.isEmpty() ? QString() : QStringLiteral("Unknown command: %1").arg(verb));
    }

private:
    int wrongUsage(const QString &why)
    {
        if (!why.isEmpty())
            m_err << why << '\n';
        m_err << usage();
        return kUsage;
    }

    bool takeFlag(const QString &flag) { return m_args.removeAll(flag) > 0; }

    // `--name value`; empty and false when absent, false when it has no value.
    bool takeOption(const QString &name, QString *value, bool *bad)
    {
        const qsizetype at = m_args.indexOf(name);
        if (at < 0)
            return false;
        if (at + 1 >= m_args.size()) {
            *bad = true;
            return false;
        }
        *value = m_args.at(at + 1);
        m_args.remove(at, 2);
        return true;
    }

    int finish(bool ok, const QString &text)
    {
        (ok ? m_out : m_err) << text << '\n';
        return ok ? kOk : kFailed;
    }

    HostLink link() const { return HostLink(m_c.linkPath); }

    int status()
    {
        const bool asJson = takeFlag(QStringLiteral("--json"));
        if (!m_args.isEmpty())
            return wrongUsage(QStringLiteral("status takes no arguments but --json."));
        StreamingHost host(m_c.host);
        const bool systemd = !m_c.host.foreground && !m_c.host.systemctl.isEmpty();
        if (systemd) {
            QEventLoop loop;
            QObject::connect(&host, &StreamingHost::stateChanged, &loop, [&loop, &host] {
                if (!host.screenState().isEmpty())
                    loop.quit();
            });
            QTimer::singleShot(5000, &loop, &QEventLoop::quit);
            host.refresh();
            if (host.screenState().isEmpty())
                loop.exec();
        } else {
            host.refresh();
        }
        const HostLink::Answer answer = link().ask("status", {}, 3000);
        const QJsonObject running = answer.ok ? QJsonDocument::fromJson(answer.text.toUtf8()).object()
                                              : QJsonObject();
        const QJsonArray paired = devices(*m_c.settings);

        QJsonObject report{
            {QStringLiteral("version"), QStringLiteral(STRMQT_VERSION)},
            {QStringLiteral("running"), answer.ok},
            {QStringLiteral("installed"), host.installed()},
            {QStringLiteral("foreground"), m_c.host.foreground},
            {QStringLiteral("problems"), QJsonArray::fromStringList(host.problems())},
            {QStringLiteral("sunshinePort"), host.port()},
            {QStringLiteral("remotePort"), Settings::kStreamingHostWebRemotePort},
            {QStringLiteral("devices"), paired.size()},
            {QStringLiteral("stream"), streamObject(host)},
            {QStringLiteral("host"), running},
        };
        if (systemd) {
            report.insert(QStringLiteral("screen"), host.screenState());
            report.insert(QStringLiteral("sunshine"), host.sunshineState());
        }
        if (asJson) {
            m_out << json(report) << '\n';
            return kOk;
        }
        const auto yesNo = [](bool on) { return on ? QStringLiteral("yes") : QStringLiteral("no"); };
        m_out << "StrmQt " << STRMQT_VERSION << " streaming host"
              << (m_c.host.foreground ? " (in a container)" : "") << '\n';
        QString runningLine = yesNo(answer.ok);
        if (systemd)
            runningLine += QStringLiteral(" (screen %1, Sunshine %2)").arg(host.screenState(), host.sunshineState());
        m_out << "  Running:        " << runningLine << '\n';
        if (!m_c.host.foreground)
            m_out << "  Set up:         " << yesNo(host.installed()) << '\n';
        m_out << "  Sunshine port:  " << host.port() << '\n';
        m_out << "  Remote port:    " << Settings::kStreamingHostWebRemotePort << '\n';
        m_out << "  Paired devices: " << paired.size() << '\n';
        m_out << "  Stream:         " << streamLine(host) << '\n';
        if (answer.ok) {
            m_out << "  Emby:           "
                  << (running.value(QLatin1String("emby")).toBool() ? "signed in" : "not signed in") << '\n';
            m_out << "  YouTube Music:  "
                  << (running.value(QLatin1String("youtubeMusic")).toBool() ? "on" : "off") << '\n';
        }
        for (const QString &problem : host.problems())
            m_out << "  Problem:        " << problem << '\n';
        return kOk;
    }

    int pair()
    {
        if (m_args.size() != 1)
            return wrongUsage(QStringLiteral("pair takes the code the StrmLight shows."));
        static const QRegularExpression fourDigits(QStringLiteral("^[0-9]{4}$"));
        const QString code = m_args.first();
        if (!fourDigits.match(code).hasMatch())
            return finish(false, QStringLiteral("A StrmLight's code is four digits."));
        const HostLink host = link();
        if (!host.ask("status", {}, 3000).ok)
            return finish(false, QStringLiteral("The streaming host's StrmQt is not running."));
        if (!host.pass(code, 3000))
            return finish(false, QStringLiteral("The streaming host did not take the code. Is the host's "
                                                "remote running, and is %1 still on the TV?")
                                     .arg(code));
        return finish(true, QStringLiteral("Code %1 is armed: the StrmLight showing it pairs on its next "
                                           "try, within two minutes.")
                                .arg(code));
    }

    int listDevices()
    {
        const bool asJson = takeFlag(QStringLiteral("--json"));
        if (!m_args.isEmpty())
            return wrongUsage(QStringLiteral("devices takes no arguments but --json."));
        const QJsonArray list = devices(*m_c.settings);
        if (asJson) {
            m_out << json(list) << '\n';
            return kOk;
        }
        if (list.isEmpty()) {
            m_out << "No device is paired.\n";
            return kOk;
        }
        for (const QJsonValue &value : list) {
            const QJsonObject device = value.toObject();
            const QString kind = device.value(QLatin1String("kind")).toString();
            m_out << device.value(QLatin1String("id")).toString() << "  "
                  << (kind == QLatin1String("strmlight") ? "StrmLight" : "phone    ") << "  "
                  << device.value(QLatin1String("name")).toString() << "  (paired "
                  << device.value(QLatin1String("added")).toString().left(10) << ")\n";
        }
        return kOk;
    }

    int unpair()
    {
        const bool all = takeFlag(QStringLiteral("--all"));
        if (all == !m_args.isEmpty() || m_args.size() > 1)
            return wrongUsage(QStringLiteral("unpair takes a device's id, or --all."));
        if (all) {
            m_c.settings->setWebRemoteDevices({});
            return finish(true, QStringLiteral("Every device is unpaired."));
        }
        const QString id = m_args.first();
        QVariantList stored = m_c.settings->webRemoteDevices();
        const qsizetype removed = stored.removeIf([&id](const QVariant &device) {
            return device.toMap().value(QStringLiteral("id")).toString() == id;
        });
        if (removed == 0)
            return finish(false, QStringLiteral("No paired device has the id %1.").arg(id));
        m_c.settings->setWebRemoteDevices(stored);
        return finish(true, QStringLiteral("Unpaired. Its StrmLight or phone has to pair again to come back."));
    }

    int stream()
    {
        const bool asJson = takeFlag(QStringLiteral("--json"));
        bool bad = false;
        QString fps, bitrate, codec, audio, height;
        const bool setFps = takeOption(QStringLiteral("--fps"), &fps, &bad);
        const bool setBitrate = takeOption(QStringLiteral("--bitrate"), &bitrate, &bad);
        const bool setCodec = takeOption(QStringLiteral("--codec"), &codec, &bad);
        const bool setAudio = takeOption(QStringLiteral("--audio"), &audio, &bad);
        const bool setHeight = takeOption(QStringLiteral("--height"), &height, &bad);
        if (bad || !m_args.isEmpty())
            return wrongUsage(QStringLiteral("stream takes --fps, --bitrate, --codec, --audio, --height and --json."));

        static const QStringList codecs{QStringLiteral("auto"), QStringLiteral("hevc"), QStringLiteral("av1"),
                                        QStringLiteral("h264")};
        static const QStringList layouts{QStringLiteral("2"), QStringLiteral("51"), QStringLiteral("71")};
        static const QStringList heights{QStringLiteral("720"), QStringLiteral("1080"), QStringLiteral("1440"),
                                         QStringLiteral("2160")};
        bool kbpsOk = bitrate == QLatin1String("auto");
        const int kbps = kbpsOk ? 0 : bitrate.toInt(&kbpsOk);
        if (setFps && fps != QLatin1String("30") && fps != QLatin1String("60"))
            return finish(false, QStringLiteral("The frame rate is 30 or 60."));
        if (setBitrate && (!kbpsOk || kbps < 0 || kbps > 150000))
            return finish(false, QStringLiteral("The bitrate is in kbit/s, up to 150000, or auto."));
        if (setCodec && !codecs.contains(codec))
            return finish(false, QStringLiteral("The codec is auto, hevc, av1 or h264."));
        if (setAudio && !layouts.contains(audio))
            return finish(false, QStringLiteral("The sound is 2 (stereo), 51 (5.1) or 71 (7.1)."));
        if (setHeight && !heights.contains(height))
            return finish(false, QStringLiteral("The screen's height is 720, 1080, 1440 or 2160."));

        StreamingHost host(m_c.host);
        host.refresh();
        if (setFps)
            host.setStreamFps(fps.toInt());
        if (setBitrate)
            host.setStreamBitrate(kbps);
        if (setCodec)
            host.setStreamCodec(codec);
        if (setAudio)
            host.setStreamAudio(audio);
        if (setHeight) {
            host.setHeight(height.toInt());
            waitForSettled(host);
        }
        if (asJson) {
            m_out << json(streamObject(host)) << '\n';
            return kOk;
        }
        m_out << streamLine(host) << '\n';
        if (setFps || setBitrate || setCodec || setAudio)
            m_out << "A StrmLight takes the new settings when it next starts a stream.\n";
        if (setHeight && !host.message().isEmpty())
            (host.failed() ? m_err : m_out) << host.message() << '\n';
        return setHeight && host.failed() ? kFailed : kOk;
    }

    int youtubeMusic()
    {
        const QString verb = m_args.value(0);
        if (verb == QLatin1String("sign-out") && m_args.size() == 1) {
            const HostLink::Answer answer = link().ask("youtube-music-sign-out", {}, 10'000);
            return finish(answer.ok, answer.text);
        }
        if (verb != QLatin1String("import") || m_args.size() > 2)
            return wrongUsage(QStringLiteral("youtube-music takes import [<file>|-] or sign-out."));
        const QString from = m_args.value(1, QStringLiteral("-"));
        QByteArray body;
        if (from == QLatin1String("-")) {
            if (!m_c.in)
                return finish(false, QStringLiteral("Nothing to read the sign-in from."));
            body = m_c.in->read(HostLink::kMaxBody + 1);
        } else {
            QFile file(from);
            if (!file.open(QIODevice::ReadOnly))
                return finish(false, QStringLiteral("Could not read %1.").arg(from));
            body = file.read(HostLink::kMaxBody + 1);
        }
        if (body.size() > HostLink::kMaxBody)
            return finish(false, QStringLiteral("That is larger than a sign-in."));
        const HostLink::Answer answer = link().ask("youtube-music-import", body, 15'000);
        return finish(answer.ok, answer.text);
    }

    int host()
    {
        const QString verb = m_args.value(0);
        if (m_args.size() != 1)
            return wrongUsage(QStringLiteral("host takes setup, restart, remove or run."));
        if (verb == QLatin1String("run"))
            return runInForeground();
        StreamingHost host(m_c.host);
        host.refresh();
        if (verb == QLatin1String("setup")) {
            host.setUp();
        } else if (verb == QLatin1String("restart")) {
            if (!host.installed())
                return finish(false, QStringLiteral("The streaming host is not set up here."));
            host.restart();
        } else if (verb == QLatin1String("remove")) {
            host.remove();
        } else {
            return wrongUsage(QStringLiteral("host takes setup, restart, remove or run."));
        }
        // setUp() waits for the host's sign-in to be read first.
        QCoreApplication::processEvents();
        if (!host.busy() && host.message().isEmpty()) {
            QEventLoop loop;
            QObject::connect(&host, &StreamingHost::busyChanged, &loop, &QEventLoop::quit);
            QObject::connect(&host, &StreamingHost::messageChanged, &loop, &QEventLoop::quit);
            QTimer::singleShot(5000, &loop, &QEventLoop::quit);
            loop.exec();
        }
        waitForSettled(host);
        if (host.busy())
            return finish(false, QStringLiteral("The streaming host is still being changed; look again with status."));
        return finish(!host.failed(), host.message());
    }

    int runInForeground()
    {
        const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (runtimeDir.isEmpty())
            return finish(false, QStringLiteral("XDG_RUNTIME_DIR is not set: the host's sockets go there."));
        StreamingHost::Environment environment = m_c.host;
        environment.foreground = true;
        environment.linger = false;
        StreamingHost host(environment);
        host.prepare();
        QCoreApplication::processEvents();
        if (!host.failed() && host.message().isEmpty()) {
            QEventLoop loop;
            QObject::connect(&host, &StreamingHost::settled, &loop, &QEventLoop::quit);
            QTimer::singleShot(kHostStepsMs, &loop, &QEventLoop::quit);
            loop.exec();
        }
        waitForSettled(host);
        if (host.failed() || host.message().isEmpty())
            return finish(false, host.message().isEmpty() ? QStringLiteral("The streaming host could not be prepared.")
                                                          : host.message());
        m_out << host.message() << '\n';
        m_out.flush();

        QList<streaming::Service> services;
        for (const QString &unit : {environment.audioUnit, environment.hostUnit, environment.sunshineUnit}) {
            QFile file(host.unitPath(unit));
            if (!file.open(QIODevice::ReadOnly))
                return finish(false, QStringLiteral("Could not read %1.").arg(file.fileName()));
            services << streaming::parseUnit(unit, QString::fromUtf8(file.readAll()), runtimeDir);
        }
        streaming::HostSupervisor supervisor(services);
        int pipe[2] = {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pipe) != 0)
            return finish(false, QStringLiteral("Could not watch for signals."));
        s_signalWrite = pipe[1];
        std::signal(SIGTERM, &Command::onSignal);
        std::signal(SIGINT, &Command::onSignal);
        QSocketNotifier notifier(pipe[0], QSocketNotifier::Read);
        QObject::connect(&notifier, &QSocketNotifier::activated, &supervisor, [&supervisor, &notifier] {
            notifier.setEnabled(false);
            qCInfo(logApp) << "host: stopping";
            supervisor.stop();
        });
        int code = kOk;
        QEventLoop loop;
        QObject::connect(&supervisor, &streaming::HostSupervisor::stopped, &loop, [&] {
            code = supervisor.givenUp() ? kFailed : kOk;
            loop.quit();
        });
        supervisor.start();
        loop.exec();
        std::signal(SIGTERM, SIG_DFL);
        std::signal(SIGINT, SIG_DFL);
        s_signalWrite = -1;
        ::close(pipe[0]);
        ::close(pipe[1]);
        return code;
    }

    static void onSignal(int)
    {
        const char byte = 1;
        if (s_signalWrite >= 0)
            [[maybe_unused]] const auto written = ::write(s_signalWrite, &byte, 1);
    }

    static inline int s_signalWrite = -1;

    QStringList m_args;
    Context &m_c;
    QTextStream &m_out;
    QTextStream &m_err;
};

} // namespace

QString usage()
{
    return QStringLiteral(
        "Usage: strmqt admin <command>\n"
        "\n"
        "The streaming host's administration, for a host nobody sits at.\n"
        "\n"
        "  status [--json]              What the streaming host is, and how it is.\n"
        "  pair <code>                  Pairs the StrmLight showing <code>.\n"
        "  devices [--json]             The paired StrmLights and phones.\n"
        "  unpair <id> | --all          Unpairs a device, or every one.\n"
        "  stream [--fps 30|60] [--bitrate <kbit/s>|auto] [--codec auto|hevc|av1|h264]\n"
        "         [--audio 2|51|71] [--height 720|1080|1440|2160] [--json]\n"
        "                               Shows the stream's settings, or changes them.\n"
        "  youtube-music import [<file>|-]\n"
        "                               Signs YouTube Music in with a sign-in read out of\n"
        "                               Firefox elsewhere (strmqt-headless's strmqtctl).\n"
        "  youtube-music sign-out       Signs YouTube Music out.\n"
        "  host setup|restart|remove    The streaming host's systemd user services.\n"
        "  host run                     Runs the streaming host in the foreground, without\n"
        "                               systemd, as a container does.\n");
}

int run(const QStringList &arguments, Context &context)
{
    return Command(arguments, context).run();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // The settings file and the host's folder are the GUI's (Application.cpp).
    QCoreApplication::setOrganizationName(QStringLiteral("StrmQt"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("mikesdev.ca"));
    QCoreApplication::setApplicationName(QStringLiteral("strmqt"));
    QCoreApplication::setApplicationVersion(QStringLiteral(STRMQT_VERSION));
    initLogging();

    Settings settings;
    QTextStream out(stdout);
    QTextStream err(stderr);
    QFile in;
    const bool reading = in.open(stdin, QIODevice::ReadOnly);
    Context context;
    context.settings = &settings;
    context.linkPath = HostLink::defaultSocketPath();
    context.host = StreamingHost::Environment::detect();
    context.out = &out;
    context.err = &err;
    context.in = reading ? &in : nullptr;
    QStringList arguments = app.arguments().mid(2);
    return run(arguments, context);
}

} // namespace strmqt::admin
