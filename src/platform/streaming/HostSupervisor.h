#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

namespace strmqt::streaming {

// How `strmqt admin host run` runs the streaming host where there is no
// systemd, as in a container: it reads the units StreamingHost wrote and does
// with them what systemd would, as far as they ask. There is one description
// of the host either way, so a unit changed in HostFiles.cpp changes the
// container's host too.
//
// What is understood is what HostFiles.cpp writes: Environment,
// UnsetEnvironment, RuntimeDirectory, ExecStartPre, ExecStart, ExecStartPost
// and RestartSec; the %t and %% specifiers; $$ for a literal $; and quoting.
// $VARIABLE is left as it is: no unit uses it.
struct Service
{
    QString name;
    // KEY=VALUE, in the unit's order; UnsetEnvironment is applied after them.
    QStringList environment;
    QStringList unset;
    // Absolute, made before the service starts; empty when there is none.
    QString runtimeDirectory;
    QList<QStringList> before;
    QStringList command;
    QList<QStringList> after;
    int restartSeconds = 3;
};

// The arguments systemd would run for `line`, its specifiers already
// expanded; empty when it is not a command line (an unclosed quote).
QStringList splitCommand(const QString &line);
// `text` is a unit; `runtimeDir` is what %t names (XDG_RUNTIME_DIR).
Service parseUnit(const QString &name, const QString &text, const QString &runtimeDir);

class HostSupervisor : public QObject
{
    Q_OBJECT

public:
    // `services` in the order they start, each after what it requires.
    explicit HostSupervisor(QList<Service> services, QObject *parent = nullptr);
    ~HostSupervisor() override;

    // Starts each in turn. When any of them ends, all are stopped and started
    // again after the longest RestartSec; restarting too often gives up.
    void start();
    // The last one first. Emits stopped() when every one has ended.
    void stop();
    bool givenUp() const { return m_givenUp; }

    // Restarts within kRestartWindowSeconds that are too many.
    static constexpr int kMaxRestarts = 5;
    static constexpr int kRestartWindowSeconds = 120;

signals:
    void stopped();

private:
    bool runAll(const QList<QStringList> &commands, const Service &service);
    void ended(const QString &name);
    void stopAll();

    QList<Service> m_services;
    QList<QProcess *> m_processes;
    QList<qint64> m_restarts;
    bool m_stopping = false;
    bool m_halted = false;
    bool m_givenUp = false;
};

} // namespace strmqt::streaming
