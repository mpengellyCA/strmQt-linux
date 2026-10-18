#include "HostSupervisor.h"

#include "core/Log.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

#ifdef Q_OS_LINUX
#include <csignal>
#include <sys/prctl.h>
#endif

namespace strmqt::streaming {

namespace {

// A command line given to a service, or to one of its waits.
constexpr int kWaitMs = 30'000;
// How long a service has to end when asked before it is killed.
constexpr int kStopMs = 10'000;

QString expandSpecifiers(const QString &text, const QString &runtimeDir)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('%') && i + 1 < text.size()) {
            const QChar next = text.at(i + 1);
            if (next == QLatin1Char('t')) {
                out += runtimeDir;
                ++i;
                continue;
            }
            if (next == QLatin1Char('%')) {
                out += QLatin1Char('%');
                ++i;
                continue;
            }
        }
        out += text.at(i);
    }
    return out;
}

} // namespace

QStringList splitCommand(const QString &line)
{
    QStringList words;
    QString word;
    bool inWord = false;
    QChar quote;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        const bool dollars = c == QLatin1Char('$') && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('$');
        if (!quote.isNull()) {
            if (c == quote) {
                quote = QChar();
            } else if (dollars) {
                word += QLatin1Char('$');
                ++i;
            } else if (c == QLatin1Char('\\') && quote == QLatin1Char('"') && i + 1 < line.size()) {
                word += line.at(++i);
            } else {
                word += c;
            }
            continue;
        }
        if (c.isSpace()) {
            if (inWord)
                words << word;
            word.clear();
            inWord = false;
            continue;
        }
        inWord = true;
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
        } else if (c == QLatin1Char('\\') && i + 1 < line.size()) {
            word += line.at(++i);
        } else if (dollars) {
            word += QLatin1Char('$');
            ++i;
        } else {
            word += c;
        }
    }
    if (!quote.isNull())
        return {};
    if (inWord)
        words << word;
    return words;
}

Service parseUnit(const QString &name, const QString &text, const QString &runtimeDir)
{
    Service service;
    service.name = name;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('[')))
            continue;
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        const QString key = line.left(equals);
        const QString value = expandSpecifiers(line.mid(equals + 1), runtimeDir);
        if (key == QLatin1String("Environment")) {
            service.environment << splitCommand(value);
        } else if (key == QLatin1String("UnsetEnvironment")) {
            service.unset << splitCommand(value);
        } else if (key == QLatin1String("RuntimeDirectory")) {
            service.runtimeDirectory = runtimeDir + QLatin1Char('/') + value;
        } else if (key == QLatin1String("ExecStartPre")) {
            service.before << splitCommand(value);
        } else if (key == QLatin1String("ExecStart")) {
            service.command = splitCommand(value);
        } else if (key == QLatin1String("ExecStartPost")) {
            service.after << splitCommand(value);
        } else if (key == QLatin1String("RestartSec")) {
            service.restartSeconds = std::max(1, value.toInt());
        }
    }
    return service;
}

HostSupervisor::HostSupervisor(QList<Service> services, QObject *parent)
    : QObject(parent), m_services(std::move(services))
{
}

HostSupervisor::~HostSupervisor()
{
    m_stopping = true;
    stopAll();
}

namespace {

QProcess *newProcess(const Service &service, QObject *parent)
{
    auto *process = new QProcess(parent);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const QString &pair : service.environment) {
        const qsizetype equals = pair.indexOf(QLatin1Char('='));
        if (equals > 0)
            environment.insert(pair.left(equals), pair.mid(equals + 1));
    }
    for (const QString &name : service.unset)
        environment.remove(name);
    process->setProcessEnvironment(environment);
    // What each service says goes where this process's does: the
    // container's log.
    process->setProcessChannelMode(QProcess::ForwardedChannels);
#ifdef Q_OS_LINUX
    // Nothing outlives the supervisor, however it ends.
    process->setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
#endif
    return process;
}

} // namespace

bool HostSupervisor::runAll(const QList<QStringList> &commands, const Service &service)
{
    for (const QStringList &command : commands) {
        if (command.isEmpty())
            return false;
        QProcess *process = newProcess(service, this);
        process->start(command.first(), command.mid(1));
        const bool ok = process->waitForFinished(kWaitMs) && process->exitStatus() == QProcess::NormalExit
                        && process->exitCode() == 0;
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(1000);
        }
        delete process;
        if (!ok) {
            qCWarning(logApp).noquote() << "host:" << service.name << "did not start:"
                                        << command.first() << "failed";
            return false;
        }
    }
    return true;
}

void HostSupervisor::start()
{
    m_stopping = false;
    m_halted = false;
    for (const Service &service : std::as_const(m_services)) {
        if (!service.runtimeDirectory.isEmpty()) {
            QDir().mkpath(service.runtimeDirectory);
            QFile::setPermissions(service.runtimeDirectory,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        }
        if (service.command.isEmpty() || !runAll(service.before, service)) {
            ended(service.name);
            return;
        }
        QProcess *process = newProcess(service, this);
        const QString name = service.name;
        connect(process, &QProcess::finished, this, [this, name] { ended(name); });
        m_processes << process;
        qCInfo(logApp).noquote() << "host: starting" << name;
        process->start(service.command.first(), service.command.mid(1));
        if (!process->waitForStarted(kWaitMs) || !runAll(service.after, service)) {
            ended(name);
            return;
        }
    }
}

void HostSupervisor::ended(const QString &name)
{
    if (m_stopping)
        return;
    m_stopping = true;
    qCWarning(logApp).noquote() << "host:" << name << "ended; restarting the streaming host";
    stopAll();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    m_restarts << now;
    m_restarts.removeIf([now](qint64 at) { return at < now - kRestartWindowSeconds; });
    if (m_restarts.size() > kMaxRestarts) {
        qCWarning(logApp).noquote() << "host: restarted" << m_restarts.size() << "times in"
                                    << kRestartWindowSeconds << "s; giving up";
        m_givenUp = true;
        m_halted = true;
        emit stopped();
        return;
    }
    int seconds = 1;
    for (const Service &service : std::as_const(m_services))
        seconds = std::max(seconds, service.restartSeconds);
    QTimer::singleShot(seconds * 1000, this, [this] {
        if (!m_halted)
            start();
    });
}

void HostSupervisor::stopAll()
{
    // Taken out first: a process ending as it is stopped must not count as
    // one that ended by itself.
    const QList<QProcess *> processes = std::exchange(m_processes, {});
    for (auto it = processes.crbegin(); it != processes.crend(); ++it) {
        QProcess *process = *it;
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(kStopMs)) {
                process->kill();
                process->waitForFinished(1000);
            }
        }
        delete process;
    }
}

void HostSupervisor::stop()
{
    m_stopping = true;
    // A restart ended() has timed is not made.
    m_halted = true;
    stopAll();
    emit stopped();
}

} // namespace strmqt::streaming
