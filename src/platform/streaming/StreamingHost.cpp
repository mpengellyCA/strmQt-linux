#include "StreamingHost.h"

#include "core/Log.h"
#include "platform/SecretsStore.h"

#include <algorithm>
#include <utility>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSettings>
#include <QSslCertificate>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTimer>
#include <QUdpSocket>

namespace strmqt {

namespace {

constexpr int kStepTimeoutMs = 20'000;
constexpr char kSunshineUser[] = "strmqt";
constexpr char kPasswordKey[] = "streaming-host/sunshine-password";

QString newPassword()
{
    QByteArray bytes(24, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), bytes.size() / 4);
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool writeText(const QString &path, const QByteArray &text)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(text);
    return file.commit();
}

bool portFree(int port, bool udp)
{
    if (udp) {
        QUdpSocket socket;
        return socket.bind(QHostAddress::Any, static_cast<quint16>(port));
    }
    QTcpServer server;
    return server.listen(QHostAddress::Any, static_cast<quint16>(port));
}

QString readProc(const char *path)
{
    QFile file(QString::fromLatin1(path));
    return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(file.readAll()) : QString();
}

} // namespace

StreamingHost::Environment StreamingHost::Environment::detect()
{
    Environment env;
    env.unitDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                  + QStringLiteral("/systemd/user");
    env.stateDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                   + QStringLiteral("/streaming-host");
    env.insideFlatpak = QFile::exists(QStringLiteral("/.flatpak-info"));
    // An AppImage's executable path is inside a mount that is gone when it
    // exits; the file the user keeps is $APPIMAGE.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    env.strmqt = appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
    env.kwin = QStandardPaths::findExecutable(QStringLiteral("kwin_wayland"));
    env.sunshine = QStandardPaths::findExecutable(QStringLiteral("sunshine"));
    env.pipewire = QStandardPaths::findExecutable(QStringLiteral("pipewire"));
    env.wireplumber = QStandardPaths::findExecutable(QStringLiteral("wireplumber"));
    env.systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    env.kscreenDoctor = QStandardPaths::findExecutable(QStringLiteral("kscreen-doctor"));
    env.configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    // Set by the strmqt-headless image, whose host runs without systemd.
    env.foreground = qEnvironmentVariableIsSet("STRMQT_HOST_FOREGROUND");
    return env;
}

StreamingHost::StreamingHost(QObject *parent) : StreamingHost(Environment::detect(), parent) {}

StreamingHost::StreamingHost(Environment environment, QObject *parent)
    : QObject(parent), m_env(std::move(environment))
{
    m_config.stateDir = m_env.stateDir;
    m_config.configHome = m_env.configHome;
    m_config.socket = m_env.socket;
    m_config.hostUnit = m_env.hostUnit;
    m_config.audioUnit = m_env.audioUnit;
    m_config.environment = m_env.strmqtEnvironment;
    loadConfig();
    m_vault = new SecretsStore(m_env.stateDir + QStringLiteral("/secrets.ini"), this);
    m_api = new streaming::SunshineApi(this);
    connectApi();
    if (QFile::exists(m_env.stateDir + QStringLiteral("/secrets.ini"))) {
        m_passwordRead = false;
        m_vault->readSecret(QLatin1String(kPasswordKey)).then(this, [this](const Result<QString> &stored) {
            if (stored.ok() && m_password.isEmpty())
                m_password = stored.value;
            m_passwordRead = true;
            if (const auto action = std::exchange(m_afterPassword, nullptr))
                (this->*action)();
        });
    }
}

StreamingHost::~StreamingHost()
{
    for (QProcess *process : {m_process, m_stateProcess}) {
        if (process && process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(500);
        }
    }
}

bool StreamingHost::running() const
{
    return m_screenState == QLatin1String("active") && m_sunshineState == QLatin1String("active");
}

QString StreamingHost::webUiAddress() const
{
    return QStringLiteral("https://localhost:%1").arg(streaming::webUiPort(m_config.port));
}

QString StreamingHost::unitPath(const QString &unit) const
{
    return m_env.unitDir + QLatin1Char('/') + unit;
}

bool StreamingHost::sunshineCapturesKWin(const QString &path)
{
    // The capture is compiled in or it is not, and no option reports it. The
    // protocol's name is in the binary exactly when it is.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const qint64 size = file.size();
    uchar *bytes = size > 0 ? file.map(0, size) : nullptr;
    if (!bytes)
        return false;
    const QByteArrayView view(reinterpret_cast<const char *>(bytes), size);
    return view.contains("zkde_screencast_unstable_v1");
}

QStringList StreamingHost::probeProblems(const Environment &env)
{
    QStringList problems;
    if (env.insideFlatpak) {
        // The sandbox can neither write the user's service files nor name a
        // path to itself that a service could run.
        problems << tr("The Flatpak cannot set up services on this computer. Use the package for "
                       "your distribution to host a stream.");
        return problems;
    }
    if (env.systemctl.isEmpty() && !env.foreground)
        problems << tr("systemd was not found. The streaming host runs as systemd user services.");
    if (env.kwin.isEmpty())
        problems << tr("KWin (kwin_wayland) was not found. It provides the headless screen.");
    // The host's sound is a PipeWire of its own, kept apart from the desktop's
    // (streaming/HostFiles.h).
    if (env.pipewire.isEmpty() || env.wireplumber.isEmpty())
        problems << tr("PipeWire and WirePlumber were not found. The streaming host keeps its sound "
                       "apart from this computer's with a PipeWire of its own.");
    if (env.sunshine.isEmpty()) {
        problems << tr("Sunshine is not installed. Install your distribution's sunshine package, "
                       "not the Flatpak, then look again.");
    } else if (!sunshineCapturesKWin(env.sunshine)) {
        problems << tr("The installed Sunshine cannot capture a KWin screen. Update Sunshine to a "
                       "2026 release or newer, then look again.");
    }
    return problems;
}

void StreamingHost::probe()
{
    m_problems = probeProblems(m_env);
    m_probed = true;
    emit probed();
}

void StreamingHost::refresh()
{
    probe();
    readState();
}

void StreamingHost::readState()
{
    const bool installed = QFile::exists(unitPath(m_env.hostUnit))
                           && QFile::exists(unitPath(m_env.sunshineUnit));
    if (installed != m_installed) {
        m_installed = installed;
        emit stateChanged();
    }
    if (m_env.systemctl.isEmpty() || m_stateProcess)
        return;
    auto *process = new QProcess(this);
    m_stateProcess = process;
    connect(process, &QProcess::finished, this, [this, process] {
        // `is-active` prints one state per unit and exits non-zero unless
        // every one is active: the text is the answer, not the exit code.
        const QStringList states =
            QString::fromUtf8(process->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString screen = states.value(0, QStringLiteral("unknown")).trimmed();
        const QString sunshine = states.value(1, QStringLiteral("unknown")).trimmed();
        process->deleteLater();
        m_stateProcess = nullptr;
        if (screen != m_screenState || sunshine != m_sunshineState) {
            m_screenState = screen;
            m_sunshineState = sunshine;
            emit stateChanged();
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        m_stateProcess = nullptr;
    });
    process->start(m_env.systemctl, {QStringLiteral("--user"), QStringLiteral("is-active"),
                                     m_env.hostUnit, m_env.sunshineUnit});
}

void StreamingHost::loadConfig()
{
    readConfig(m_env.stateDir, &m_config);
}

void StreamingHost::readConfig(const QString &stateDir, streaming::HostConfig *config)
{
    const QSettings stored(stateDir + QStringLiteral("/host.ini"), QSettings::IniFormat);
    const int height = stored.value(QStringLiteral("height"), config->height).toInt();
    config->height = height;
    config->width = height * 16 / 9;
    config->port = stored.value(QStringLiteral("port"), config->port).toInt();
    config->webUiOnNetwork = stored.value(QStringLiteral("webUiOnNetwork"), false).toBool();
    config->scalePercent = std::clamp(stored.value(QStringLiteral("scale"), 100).toInt(), 100, 300);
    const int fps = stored.value(QStringLiteral("fps"), 30).toInt();
    config->fps = fps == 60 ? 60 : 30;
    config->bitrateKbps = std::clamp(stored.value(QStringLiteral("bitrateKbps"), 0).toInt(), 0, 150000);
    static const QStringList codecs{QStringLiteral("auto"), QStringLiteral("hevc"), QStringLiteral("av1"),
                                    QStringLiteral("h264")};
    const QString codec = stored.value(QStringLiteral("codec")).toString();
    config->codec = codecs.contains(codec) ? codec : QStringLiteral("auto");
    static const QStringList layouts{QStringLiteral("2"), QStringLiteral("51"), QStringLiteral("71")};
    const QString audio = stored.value(QStringLiteral("audio")).toString();
    config->audio = layouts.contains(audio) ? audio : QStringLiteral("2");
}

void StreamingHost::saveConfig() const
{
    QSettings stored(m_env.stateDir + QStringLiteral("/host.ini"), QSettings::IniFormat);
    stored.setValue(QStringLiteral("height"), m_config.height);
    stored.setValue(QStringLiteral("port"), m_config.port);
    stored.setValue(QStringLiteral("webUiOnNetwork"), m_config.webUiOnNetwork);
    stored.setValue(QStringLiteral("scale"), m_config.scalePercent);
    stored.setValue(QStringLiteral("fps"), m_config.fps);
    stored.setValue(QStringLiteral("bitrateKbps"), m_config.bitrateKbps);
    stored.setValue(QStringLiteral("codec"), m_config.codec);
    stored.setValue(QStringLiteral("audio"), m_config.audio);
}

void StreamingHost::setScale(int percent)
{
    percent = std::clamp(percent, 100, 300);
    if (percent == m_config.scalePercent)
        return;
    m_config.scalePercent = percent;
    saveConfig();
    emit configChanged();
    applyScale();
}

void StreamingHost::applyScale()
{
    if (m_env.kscreenDoctor.isEmpty())
        return;
    auto *process = new QProcess(this);
    // From the desktop, the screen is reached by its socket; on the screen,
    // WAYLAND_DISPLAY already names it.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("WAYLAND_DISPLAY"), m_env.socket);
    environment.remove(QStringLiteral("DISPLAY"));
    process->setProcessEnvironment(environment);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this, [process](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0)
            qCWarning(logApp) << "streaming host: the screen's scale was not set:"
                              << process->readAll().trimmed().right(300);
        process->deleteLater();
    });
    QTimer::singleShot(kStepTimeoutMs, process, [process] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
    process->start(m_env.kscreenDoctor,
                   {QStringLiteral("output.%1.scale.%2")
                        .arg(QLatin1String(streaming::kVirtualOutput))
                        .arg(m_config.scalePercent / 100.0)});
}

void StreamingHost::setStreamFps(int fps)
{
    if ((fps != 30 && fps != 60) || fps == m_config.fps)
        return;
    m_config.fps = fps;
    saveConfig();
    emit configChanged();
}

void StreamingHost::setStreamBitrate(int kbps)
{
    kbps = std::clamp(kbps, 0, 150000);
    if (kbps == m_config.bitrateKbps)
        return;
    m_config.bitrateKbps = kbps;
    saveConfig();
    emit configChanged();
}

int StreamingHost::automaticBitrate() const
{
    return streaming::automaticBitrate(m_config.height, m_config.fps);
}

void StreamingHost::setStreamCodec(const QString &codec)
{
    static const QStringList codecs{QStringLiteral("auto"), QStringLiteral("hevc"), QStringLiteral("av1"),
                                    QStringLiteral("h264")};
    if (!codecs.contains(codec) || codec == m_config.codec)
        return;
    m_config.codec = codec;
    saveConfig();
    emit configChanged();
}

void StreamingHost::setStreamAudio(const QString &audio)
{
    static const QStringList layouts{QStringLiteral("2"), QStringLiteral("51"), QStringLiteral("71")};
    if (!layouts.contains(audio) || audio == m_config.audio)
        return;
    m_config.audio = audio;
    saveConfig();
    emit configChanged();
}

QJsonObject StreamingHost::strmLightHost() const
{
    streaming::HostConfig current = m_config;
    readConfig(m_env.stateDir, &current);
    return QJsonObject{{QStringLiteral("sunshineFingerprint"), sunshineFingerprint()},
                       {QStringLiteral("stream"), streaming::streamJson(current)}};
}

void StreamingHost::setHeight(int height)
{
    if (height == m_config.height || (height != 720 && height != 1080 && height != 1440 && height != 2160))
        return;
    m_config.height = height;
    m_config.width = height * 16 / 9;
    // Kept before a set-up too: `strmqt admin stream --height` exits at once.
    saveConfig();
    emit configChanged();
    applyIfInstalled();
}

void StreamingHost::setWebUiOnNetwork(bool on)
{
    if (on == m_config.webUiOnNetwork)
        return;
    m_config.webUiOnNetwork = on;
    emit configChanged();
    applyIfInstalled();
}

// A setting changed on a host that is set up: the files are rewritten and the
// services restarted, so what Settings shows is what is running.
void StreamingHost::applyIfInstalled()
{
    if (!m_installed) {
        return;
    }
    if (m_busy) {
        setMessage(tr("Still working. The change is applied the next time the host restarts."), false);
        return;
    }
    if (!writeFiles())
        return;
    if (m_env.foreground) {
        // Nothing here can restart what `strmqt admin host run` started.
        setMessage(tr("The change is applied the next time the host starts."), false);
        return;
    }
    run({{m_env.systemctl, {QStringLiteral("--user"), QStringLiteral("daemon-reload")}, {}},
         {m_env.systemctl, {QStringLiteral("--user"), QStringLiteral("restart"), m_env.hostUnit, m_env.sunshineUnit}, {}}},
        tr("The streaming host restarted with the new setting."));
}

bool StreamingHost::writeFiles()
{
    m_config.strmqt = m_env.strmqt;
    m_config.kwin = m_env.kwin;
    m_config.sunshine = m_env.sunshine;
    m_config.pipewire = m_env.pipewire;
    m_config.wireplumber = m_env.wireplumber;
    const QStringList problems = streaming::configProblems(m_config);
    if (!problems.isEmpty()) {
        setMessage(problems.join(QLatin1Char(' ')), true);
        return false;
    }
    if (!QDir().mkpath(m_env.unitDir) || !QDir().mkpath(m_env.stateDir + QStringLiteral("/audio"))
        || !QDir().mkpath(streaming::kwinConfigHome(m_config))) {
        setMessage(tr("Could not create the streaming host's folders."), true);
        return false;
    }
    // Pairings and the certificate's key live here.
    QFile::setPermissions(m_env.stateDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const bool written =
        writeText(m_env.stateDir + QStringLiteral("/sunshine.conf"), streaming::sunshineConf(m_config).toUtf8())
        && writeText(m_env.stateDir + QStringLiteral("/apps.json"), streaming::appsJson())
        && writeText(streaming::audioConfPath(m_config), streaming::audioConf(m_config).toUtf8())
        && writeText(unitPath(m_env.audioUnit), streaming::audioUnit(m_config).toUtf8())
        && writeText(unitPath(m_env.hostUnit), streaming::hostUnit(m_config).toUtf8())
        && writeText(unitPath(m_env.sunshineUnit), streaming::sunshineUnit(m_config).toUtf8());
    if (!written) {
        setMessage(tr("Could not write the streaming host's files."), true);
        return false;
    }
    saveConfig();
    return true;
}

void StreamingHost::afterPassword(void (StreamingHost::*action)())
{
    m_afterPassword = action;
}

bool StreamingHost::choosePorts()
{
    // Chosen at the first set-up and kept, apart from a move off 0.9.0's
    // base: a StrmLight follows the host to a new port by discovery, but
    // there is no reason to make it.
    if (!m_installed) {
        const int base = streaming::freePortBase(portFree, m_config.port);
        if (base == 0) {
            setMessage(tr("No free range of ports was found for Sunshine."), true);
            return false;
        }
        if (base != m_config.port) {
            m_config.port = base;
            emit configChanged();
        }
    } else {
        leaveLegacyPort();
    }
    return true;
}

void StreamingHost::setUp()
{
    if (m_busy)
        return;
    if (m_env.foreground) {
        setMessage(tr("This streaming host starts and stops with its container."), true);
        return;
    }
    if (!m_passwordRead) {
        afterPassword(&StreamingHost::setUp);
        return;
    }
    probe();
    if (!m_problems.isEmpty()) {
        setMessage(m_problems.join(QLatin1Char(' ')), true);
        return;
    }
    if (!choosePorts() || !writeFiles())
        return;
    notePortsLent();
    QList<Step> steps{
        {m_env.systemctl, {QStringLiteral("--user"), QStringLiteral("daemon-reload")}, {}},
        {m_env.systemctl, {QStringLiteral("--user"), QStringLiteral("enable"), m_env.hostUnit, m_env.sunshineUnit}, {}},
    };
    // Sunshine's web page and API want a sign-in, and an instance without one
    // lets the first visitor choose it. StrmQt chooses, once, and is the only
    // one who knows it. Sunshine is stopped first: it writes its state file on
    // the way out and would write the old sign-in back.
    if (m_password.isEmpty()) {
        m_password = newPassword();
        m_vault->writeSecret(QLatin1String(kPasswordKey), m_password);
        steps << Step{m_env.systemctl, {QStringLiteral("--user"), QStringLiteral("stop"), m_env.sunshineUnit}, QStringLiteral("-")}
              << Step{m_env.sunshine,
                      {m_env.stateDir + QStringLiteral("/sunshine.conf"), QStringLiteral("--creds"),
                       QLatin1String(kSunshineUser), m_password},
                      {}, true};
    }
    steps << Step{m_env.systemctl, {QStringLiteral("--user"), QStringLiteral("restart"), m_env.hostUnit, m_env.sunshineUnit}, {}};
    if (m_env.linger) {
        const QString loginctl = QStandardPaths::findExecutable(QStringLiteral("loginctl"));
        const QString tolerated = tr("It starts when you log in, not at boot: this computer did not "
                                     "allow starting it earlier (loginctl enable-linger).");
        if (loginctl.isEmpty())
            m_notes << tolerated;
        else
            steps << Step{loginctl, {QStringLiteral("enable-linger")}, tolerated};
    }
    run(steps, tr("The streaming host is set up and running."));
}

void StreamingHost::prepare()
{
    if (m_busy)
        return;
    if (!m_passwordRead) {
        afterPassword(&StreamingHost::prepare);
        return;
    }
    probe();
    readState();
    if (!m_problems.isEmpty()) {
        setMessage(m_problems.join(QLatin1Char(' ')), true);
        emit settled();
        return;
    }
    if (!choosePorts() || !writeFiles()) {
        emit settled();
        return;
    }
    notePortsLent();
    // As setUp() does, with nothing running yet to stop first.
    QList<Step> steps;
    if (m_password.isEmpty()) {
        m_password = newPassword();
        m_vault->writeSecret(QLatin1String(kPasswordKey), m_password);
        steps << Step{m_env.sunshine,
                      {m_env.stateDir + QStringLiteral("/sunshine.conf"), QStringLiteral("--creds"),
                       QLatin1String(kSunshineUser), m_password},
                      {}, true};
    }
    run(steps, tr("The streaming host is ready."));
}

void StreamingHost::restart()
{
    if (m_busy || !m_installed)
        return;
    if (m_env.foreground) {
        setMessage(tr("This streaming host starts and stops with its container."), true);
        return;
    }
    // The files are written again first: a newer StrmQt may write them
    // differently (its unit gained STRMQT_STREAMING_HOST; its ports moved),
    // and a restart is when that should take effect.
    leaveLegacyPort();
    if (!writeFiles())
        return;
    notePortsLent();
    run({{m_env.systemctl, {QStringLiteral("--user"), QStringLiteral("daemon-reload")}, {}},
         {m_env.systemctl, {QStringLiteral("--user"), QStringLiteral("restart"), m_env.hostUnit, m_env.sunshineUnit}, {}}},
        tr("The streaming host restarted."));
}

void StreamingHost::leaveLegacyPort()
{
    if (m_config.port != streaming::kLegacyPort)
        return;
    const int base = streaming::freePortBase(portFree);
    if (base == 0)
        return; // stays where it is, which works
    qCInfo(logApp) << "streaming host: Sunshine moves from base port" << m_config.port << "to" << base;
    m_config.port = base;
    emit configChanged();
}

void StreamingHost::notePortsLent()
{
    const int first = std::min(m_config.port - 5, streaming::kBlockFirst);
    const int last = std::max(m_config.port + 21, streaming::kBlockLast);
    if (!streaming::portsLent(readProc("/proc/sys/net/ipv4/ip_local_port_range"),
                              readProc("/proc/sys/net/ipv4/ip_local_reserved_ports"), first, last))
        return;
    m_notes << tr("This computer may lend ports %1 to %2 to other programs' connections, and Sunshine "
                  "can't start its stream while one is lent. To keep them for StrmQt, run: "
                  "echo 'net.ipv4.ip_local_reserved_ports = %1-%2' | sudo tee /etc/sysctl.d/60-strmqt.conf "
                  "&& sudo sysctl --system")
                   .arg(first)
                   .arg(last);
}

void StreamingHost::remove()
{
    if (m_busy)
        return;
    if (m_env.foreground) {
        setMessage(tr("This streaming host starts and stops with its container."), true);
        return;
    }
    // Stopping units that are not loaded and deleting files that are not there
    // are both fine: remove means "make sure it is gone".
    const QString gone = QStringLiteral("-");
    run({{m_env.systemctl, {QStringLiteral("--user"), QStringLiteral("disable"), QStringLiteral("--now"), m_env.sunshineUnit, m_env.hostUnit}, gone},
         // Not enabled itself, only required by the screen's unit.
         {m_env.systemctl, {QStringLiteral("--user"), QStringLiteral("stop"), m_env.audioUnit}, gone},
         {QStringLiteral("rm"), {QStringLiteral("-f"), QStringLiteral("--"), unitPath(m_env.sunshineUnit), unitPath(m_env.hostUnit), unitPath(m_env.audioUnit)}, {}},
         {m_env.systemctl, {QStringLiteral("--user"), QStringLiteral("daemon-reload")}, gone},
         // A KWin that is told to stop exits non-zero; without this the unit
         // lingers as "failed" with no file behind it.
         {m_env.systemctl, {QStringLiteral("--user"), QStringLiteral("reset-failed"), m_env.sunshineUnit, m_env.hostUnit, m_env.audioUnit}, gone}},
        tr("The streaming host is removed. Its pairings are kept in case you set it up again."));
}

void StreamingHost::run(QList<Step> steps, const QString &done)
{
    m_steps = std::move(steps);
    m_done = done;
    m_busy = true;
    emit busyChanged();
    next();
}

void StreamingHost::next()
{
    if (m_steps.isEmpty()) {
        QString text = m_done;
        if (!m_notes.isEmpty())
            text += QLatin1Char(' ') + m_notes.join(QLatin1Char(' '));
        finish(true, text);
        return;
    }
    const Step step = m_steps.takeFirst();
    auto *process = new QProcess(this);
    m_process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    const auto ended = [this, process, step](bool ok) {
        if (m_process != process)
            return;
        const QString output = QString::fromUtf8(process->readAll()).trimmed();
        process->deleteLater();
        m_process = nullptr;
        if (ok) {
            next();
        } else if (!step.toleratedAs.isEmpty()) {
            if (step.toleratedAs != QLatin1String("-"))
                m_notes << step.toleratedAs;
            next();
        } else {
            if (!step.quiet)
                qCWarning(logApp) << "streaming host:" << step.program << step.arguments << "failed:" << output;
            m_steps.clear();
            finish(false, output.isEmpty() || step.quiet
                              ? tr("%1 failed.").arg(QFileInfo(step.program).fileName())
                              : tr("%1 failed: %2").arg(QFileInfo(step.program).fileName(), output));
        }
    };
    connect(process, &QProcess::finished, this, [ended](int code, QProcess::ExitStatus status) {
        ended(status == QProcess::NormalExit && code == 0);
    });
    connect(process, &QProcess::errorOccurred, this, [ended](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            ended(false);
    });
    QTimer::singleShot(kStepTimeoutMs, process, [process] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
    process->start(step.program, step.arguments);
}

void StreamingHost::finish(bool ok, const QString &text)
{
    m_notes.clear();
    m_busy = false;
    emit busyChanged();
    setMessage(text, !ok);
    readState();
    emit settled();
}

void StreamingHost::setMessage(const QString &text, bool failed)
{
    m_message = text;
    m_failed = failed;
    emit messageChanged();
}

// ── Pairing ──────────────────────────────────────────────────────────────────

void StreamingHost::connectApi()
{
    using streaming::SunshineApi;
    connect(m_api, &SunshineApi::clientsFetched, this, [this](const QList<SunshineApi::Client> &clients) {
        QVariantList devices;
        for (const SunshineApi::Client &client : clients)
            devices << QVariantMap{{QStringLiteral("name"), client.name}, {QStringLiteral("uuid"), client.uuid}};
        const QStringList refused = strangers(devices);
        for (const QString &uuid : refused) {
            if (m_refusing.contains(uuid))
                continue;
            qCInfo(logApp) << "streaming host: unpairing a device StrmQt did not pair for a StrmLight";
            m_refusing << uuid;
            m_api->unpair(uuid);
        }
        devices.removeIf([&refused](const QVariant &device) {
            return refused.contains(device.toMap().value(QStringLiteral("uuid")).toString());
        });
        if (!m_pendingPin.isEmpty()) {
            if (!m_pinBaselineKnown) {
                m_pinBaseline.clear();
                for (const SunshineApi::Client &client : clients)
                    m_pinBaseline << client.uuid;
                m_pinBaselineKnown = true;
            } else if (hasNewDevice(m_pinBaseline, devices)) {
                qCInfo(logApp) << "streaming host: a StrmLight paired with Sunshine";
                setPairingMessage(tr("Paired a StrmLight."), false);
                m_pendingPin.clear();
                m_pinTimer->stop();
            }
        }
        if (devices == m_devices && m_pairingReachable)
            return;
        m_devices = devices;
        m_pairingReachable = true;
        emit pairingChanged();
    });
    connect(m_api, &SunshineApi::pairingsFetched, this,
            [this](const QList<SunshineApi::Pairing> &pairings, bool listed) {
                QVariantList requests;
                for (const SunshineApi::Pairing &pairing : pairings)
                    requests << QVariantMap{{QStringLiteral("id"), pairing.id}, {QStringLiteral("name"), pairing.name}};
                if (requests == m_requests && listed == m_requestsListed)
                    return;
                m_requests = requests;
                m_requestsListed = listed;
                emit pairingChanged();
                if (!m_pendingPin.isEmpty())
                    tryPendingPin(); // a Moonlight has just started asking
            });
    connect(m_api, &SunshineApi::unreachable, this, [this](const QString &reason) {
        m_pairingReachable = false;
        m_devices.clear();
        m_requests.clear();
        setPairingMessage(reason, true);
    });
    connect(m_api, &SunshineApi::paired, this, [this](const QString &error) {
        if (m_pinInFlight) {
            // enterPinWhenAsked(): a miss is tried again, and not reported.
            // Taken is not done either: the list says whether the StrmLight
            // is in it (hasNewDevice), and until then the tries go on.
            m_pinInFlight = false;
            if (error.isEmpty())
                qCInfo(logApp) << "streaming host: Sunshine took the StrmLight's code";
            m_api->fetch();
            return;
        }
        setPairingMessage(error.isEmpty() ? tr("Paired.") : tr("Pairing failed: %1").arg(error), !error.isEmpty());
        m_api->fetch();
    });
    connect(m_api, &SunshineApi::unpaired, this, [this](const QString &uuid, const QString &error) {
        if (m_refusing.removeAll(uuid) > 0) {
            if (error.isEmpty())
                setPairingMessage(tr("Removed a device that was not a StrmLight: only a StrmLight paired with "
                                     "its code can watch this computer."),
                                  false);
            else
                qCWarning(logApp) << "streaming host: could not unpair a device that is not a StrmLight:" << error;
            m_api->fetch();
            return;
        }
        setPairingMessage(error.isEmpty() ? tr("The device was removed.") : tr("Could not remove it: %1").arg(error),
                          !error.isEmpty());
        m_api->fetch();
    });
}

// Sunshine writes its certificate the first time it runs, so this is asked
// again on every use rather than once.
void StreamingHost::configureApi()
{
    m_api->configure(streaming::webUiPort(m_config.port), m_env.stateDir + QStringLiteral("/cert.pem"),
                     QLatin1String(kSunshineUser), m_password);
}

void StreamingHost::fetchPairing()
{
    if (!m_installed)
        return;
    configureApi();
    if (!m_api->ready()) {
        if (m_pairingReachable) {
            m_pairingReachable = false;
            emit pairingChanged();
        }
        return;
    }
    m_api->fetch();
}

void StreamingHost::checkPairing()
{
    configureApi();
    m_api->fetch(); // nothing, until Sunshine has written its certificate
}

void StreamingHost::unpair(const QString &uuid)
{
    configureApi();
    m_api->unpair(uuid);
}

void StreamingHost::enterPinWhenAsked(const QString &pin, int seconds)
{
    m_pendingPin = pin;
    m_pendingPinUntil = QDateTime::currentMSecsSinceEpoch() + qint64(seconds) * 1000;
    m_pinInFlight = false;
    m_pinBaselineKnown = false;
    if (!m_pinTimer) {
        m_pinTimer = new QTimer(this);
        m_pinTimer->setInterval(2000);
        connect(m_pinTimer, &QTimer::timeout, this, &StreamingHost::tryPendingPin);
    }
    m_pinTimer->start();
    tryPendingPin();
}

bool StreamingHost::pinTarget(bool requestsListed, const QVariantList &requests, QString *requestId)
{
    QString id;
    if (requestsListed) {
        if (requests.isEmpty())
            return false;
        // More than one is rare (each Moonlight shows its own PIN); the code
        // is wrong for all but one, and Sunshine refuses those.
        id = requests.constFirst().toMap().value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            return false;
    }
    if (requestId)
        *requestId = id;
    return true;
}

QStringList StreamingHost::strangers(const QVariantList &devices)
{
    QStringList uuids;
    for (const QVariant &device : devices) {
        const QVariantMap entry = device.toMap();
        if (entry.value(QStringLiteral("name")).toString() != QLatin1String(kStrmLightDevice))
            uuids << entry.value(QStringLiteral("uuid")).toString();
    }
    return uuids;
}

QString StreamingHost::sunshineFingerprint() const
{
    const QList<QSslCertificate> certificates =
        QSslCertificate::fromPath(m_env.stateDir + QStringLiteral("/cert.pem"), QSsl::Pem);
    if (certificates.isEmpty())
        return {};
    return QString::fromLatin1(certificates.constFirst().digest(QCryptographicHash::Sha256).toHex());
}

bool StreamingHost::hasNewDevice(const QStringList &before, const QVariantList &devices)
{
    for (const QVariant &device : devices) {
        if (!before.contains(device.toMap().value(QStringLiteral("uuid")).toString()))
            return true;
    }
    return false;
}

void StreamingHost::tryPendingPin()
{
    if (m_pendingPin.isEmpty() || QDateTime::currentMSecsSinceEpoch() > m_pendingPinUntil) {
        m_pendingPin.clear();
        if (m_pinTimer)
            m_pinTimer->stop();
        return;
    }
    if (m_pinInFlight)
        return;
    configureApi();
    if (!m_api->ready())
        return; // still starting, or its sign-in not read yet: next time
    QString requestId;
    // Who was paired before the PIN, first: the StrmLight is the one after.
    if (!m_pinBaselineKnown || !pinTarget(m_requestsListed, m_requests, &requestId)) {
        m_api->fetch(); // pairingsFetched tries again
        return;
    }
    m_pinInFlight = true;
    m_api->pair(requestId, m_pendingPin, QLatin1String(kStrmLightDevice));
}

void StreamingHost::setPairingMessage(const QString &text, bool failed)
{
    m_pairingMessage = text;
    m_pairingFailed = failed;
    emit pairingChanged();
}

} // namespace strmqt
