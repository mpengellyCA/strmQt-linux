// How `strmqt admin host run` runs the streaming host without systemd
// (platform/streaming/HostSupervisor.h): the units StrmQt writes, read the
// way systemd reads them, and harmless commands run in their place.

#include "platform/streaming/HostFiles.h"
#include "platform/streaming/HostSupervisor.h"

#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace strmqt::streaming;

namespace {

HostConfig sampleConfig()
{
    HostConfig config;
    config.stateDir = QStringLiteral("/home/u/.local/share/StrmQt/strmqt/streaming-host");
    config.strmqt = QStringLiteral("/usr/bin/strmqt");
    config.kwin = QStringLiteral("/usr/bin/kwin_wayland");
    config.sunshine = QStringLiteral("/usr/bin/sunshine");
    config.pipewire = QStringLiteral("/usr/bin/pipewire");
    config.wireplumber = QStringLiteral("/usr/bin/wireplumber");
    config.configHome = QStringLiteral("/home/u/.config");
    return config;
}

QString read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

class HostSupervisorTest : public QObject
{
    Q_OBJECT

private slots:
    void commandLinesSplitAsSystemdSplitsThem()
    {
        QCOMPARE(splitCommand(QStringLiteral("/bin/a b  c")),
                 (QStringList{QStringLiteral("/bin/a"), QStringLiteral("b"), QStringLiteral("c")}));
        QCOMPARE(splitCommand(QStringLiteral("a \"b c\" 'd e'")),
                 (QStringList{QStringLiteral("a"), QStringLiteral("b c"), QStringLiteral("d e")}));
        // $$ is a literal $, quoted or not; one $ is left alone.
        QCOMPARE(splitCommand(QStringLiteral("sh -c 'n=$$((n+1))' $$x $y")),
                 (QStringList{QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("n=$((n+1))"),
                              QStringLiteral("$x"), QStringLiteral("$y")}));
        QCOMPARE(splitCommand(QStringLiteral("a \"b\\\"c\"")),
                 (QStringList{QStringLiteral("a"), QStringLiteral("b\"c")}));
        QVERIFY(splitCommand(QStringLiteral("a 'unclosed")).isEmpty());
    }

    void theScreensUnitIsReadAsSystemdReadsIt()
    {
        const HostConfig config = sampleConfig();
        const Service screen = parseUnit(QStringLiteral("strmqt-host.service"), hostUnit(config),
                                         QStringLiteral("/run/user/1000"));
        QCOMPARE(screen.command.value(0), config.kwin);
        // KWin is given StrmQt as one argument, which it splits itself.
        const qsizetype session = screen.command.indexOf(QStringLiteral("--exit-with-session"));
        QVERIFY(session > 0);
        QCOMPARE(screen.command.value(session + 1),
                 QStringLiteral("/usr/bin/env XDG_CONFIG_HOME=/home/u/.config "
                                "PIPEWIRE_REMOTE=strmqt-host-pipewire /usr/bin/strmqt"));
        QVERIFY(screen.environment.contains(QStringLiteral("STRMQT_STREAMING_HOST=1")));
        // %t is the runtime directory.
        QVERIFY(screen.environment.contains(
            QStringLiteral("PULSE_SERVER=unix:/run/user/1000/strmqt-host-audio/native")));
        QVERIFY(screen.unset.contains(QStringLiteral("WAYLAND_DISPLAY")));
        QCOMPARE(screen.restartSeconds, 3);

        const Service sunshine = parseUnit(QStringLiteral("strmqt-host-sunshine.service"), sunshineUnit(config),
                                           QStringLiteral("/run/user/1000"));
        QCOMPARE(sunshine.command,
                 (QStringList{config.sunshine, config.stateDir + QStringLiteral("/sunshine.conf")}));
        QCOMPARE(sunshine.before.size(), 1);
        QCOMPARE(sunshine.before.first().value(0), QStringLiteral("/bin/sh"));
        QVERIFY(sunshine.before.first().value(2).contains(QStringLiteral("[ -S /run/user/1000/strmqt-host ]")));
        QVERIFY(sunshine.before.first().value(2).contains(QStringLiteral("n=$((n+1))")));
        QCOMPARE(sunshine.restartSeconds, 5);

        const Service audio = parseUnit(QStringLiteral("strmqt-host-audio.service"), audioUnit(config),
                                        QStringLiteral("/run/user/1000"));
        QCOMPARE(audio.runtimeDirectory, QStringLiteral("/run/user/1000/strmqt-host-audio"));
        QCOMPARE(audio.command, (QStringList{config.pipewire, QStringLiteral("-c"), audioConfPath(config)}));
        QCOMPARE(audio.after.size(), 1);
        QVERIFY(audio.unset.contains(QStringLiteral("PIPEWIRE_CORE")));
    }

    void servicesStartInOrderAndStopLastFirst()
    {
        QTemporaryDir dir;
        const QString log = dir.filePath(QStringLiteral("log"));
        const auto service = [&](const QString &name) {
            Service s;
            s.name = name;
            s.environment << QStringLiteral("NAME=%1").arg(name) << QStringLiteral("LOG=%1").arg(log);
            s.runtimeDirectory = dir.filePath(name);
            s.before << QStringList{QStringLiteral("/bin/sh"), QStringLiteral("-c"),
                                    QStringLiteral("echo before $NAME >> $LOG")};
            s.command = {QStringLiteral("/bin/sh"), QStringLiteral("-c"),
                         QStringLiteral("echo start $NAME >> $LOG; trap 'echo stop $NAME >> $LOG; exit 0' "
                                        "TERM; while :; do sleep 0.1; done")};
            s.restartSeconds = 1;
            return s;
        };
        HostSupervisor supervisor({service(QStringLiteral("a")), service(QStringLiteral("b"))});
        supervisor.start();
        QTRY_VERIFY(read(log).contains(QStringLiteral("start b")));
        QVERIFY(QFileInfo(dir.filePath(QStringLiteral("a"))).isDir());
        QSignalSpy stopped(&supervisor, &HostSupervisor::stopped);
        supervisor.stop();
        QCOMPARE(stopped.count(), 1);
        QVERIFY(!supervisor.givenUp());
        QCOMPARE(read(log).split(QLatin1Char('\n'), Qt::SkipEmptyParts),
                 (QStringList{QStringLiteral("before a"), QStringLiteral("start a"), QStringLiteral("before b"),
                              QStringLiteral("start b"), QStringLiteral("stop b"), QStringLiteral("stop a")}));
    }

    void oneEndingRestartsThemAll()
    {
        QTemporaryDir dir;
        const QString log = dir.filePath(QStringLiteral("log"));
        Service steady;
        steady.name = QStringLiteral("steady");
        steady.environment << QStringLiteral("LOG=%1").arg(log);
        steady.command = {QStringLiteral("/bin/sh"), QStringLiteral("-c"),
                          QStringLiteral("echo steady >> $LOG; exec sleep 30")};
        steady.restartSeconds = 1;
        Service once = steady;
        once.name = QStringLiteral("once");
        // Ends the first time only.
        once.command = {QStringLiteral("/bin/sh"), QStringLiteral("-c"),
                        QStringLiteral("[ -e $LOG.done ] && exec sleep 30; touch $LOG.done; exit 1")};
        HostSupervisor supervisor({steady, once});
        supervisor.start();
        QTRY_COMPARE_WITH_TIMEOUT(read(log).count(QStringLiteral("steady")), 2, 10000);
        QVERIFY(!supervisor.givenUp());
        supervisor.stop();
    }

    void endingOverAndOverGivesUp()
    {
        Service failing;
        failing.name = QStringLiteral("failing");
        failing.command = {QStringLiteral("/bin/false")};
        failing.restartSeconds = 1;
        HostSupervisor supervisor({failing});
        QSignalSpy stopped(&supervisor, &HostSupervisor::stopped);
        supervisor.start();
        QVERIFY(stopped.wait((HostSupervisor::kMaxRestarts + 2) * 1500));
        QVERIFY(supervisor.givenUp());
    }

    void aWaitThatFailsIsAServiceThatEnded()
    {
        Service waiting;
        waiting.name = QStringLiteral("waiting");
        waiting.before << QStringList{QStringLiteral("/bin/false")};
        waiting.command = {QStringLiteral("/bin/sleep"), QStringLiteral("30")};
        waiting.restartSeconds = 1;
        HostSupervisor supervisor({waiting});
        QSignalSpy stopped(&supervisor, &HostSupervisor::stopped);
        supervisor.start();
        QVERIFY(stopped.wait((HostSupervisor::kMaxRestarts + 2) * 1500));
        QVERIFY(supervisor.givenUp());
    }
};

QTEST_GUILESS_MAIN(HostSupervisorTest)
#include "tst_host_supervisor.moc"
