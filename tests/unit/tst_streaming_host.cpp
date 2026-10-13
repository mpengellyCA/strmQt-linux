#include "platform/streaming/HostFiles.h"
#include "platform/streaming/StreamingHost.h"

#include <QCoreApplication>
#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace strmqt;
using namespace strmqt::streaming;

namespace {

HostConfig sample()
{
    HostConfig config;
    config.stateDir = QStringLiteral("/home/u/.local/share/StrmQt/streaming-host");
    config.strmqt = QStringLiteral("/usr/bin/strmqt");
    config.kwin = QStringLiteral("/usr/bin/kwin_wayland");
    config.sunshine = QStringLiteral("/usr/bin/sunshine");
    return config;
}

QString read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void writeScript(const QString &path, const QByteArray &body)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("#!/bin/sh\n" + body);
    file.close();
    QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
}

} // namespace

class TestStreamingHost : public QObject
{
    Q_OBJECT

private slots:
    void sunshineKeepsItsWebPageToThisMachineUnlessAsked()
    {
        HostConfig config = sample();
        QVERIFY(sunshineConf(config).contains(QStringLiteral("origin_web_ui_allowed = pc\n")));
        config.webUiOnNetwork = true;
        QVERIFY(sunshineConf(config).contains(QStringLiteral("origin_web_ui_allowed = lan\n")));
    }

    void sunshineKeepsEverythingItWritesInTheHostsFolder()
    {
        const HostConfig config = sample();
        const QString conf = sunshineConf(config);
        for (const char *key : {"file_apps", "file_state", "credentials_file", "log_path", "pkey", "cert"}) {
            const QString line = QStringLiteral("%1 = %2/").arg(QLatin1String(key), config.stateDir);
            QVERIFY2(conf.contains(line), key);
        }
        QVERIFY(conf.contains(QStringLiteral("capture = kwin\n")));
        QVERIFY(conf.contains(QStringLiteral("port = 48989\n")));
    }

    void aNameCannotAddASettingOfItsOwn()
    {
        HostConfig config = sample();
        config.name = QStringLiteral("Den\norigin_web_ui_allowed = wan");
        const QString conf = sunshineConf(config);
        QCOMPARE(conf.count(QStringLiteral("origin_web_ui_allowed")), 2); // the name's text and the real line
        QVERIFY(conf.contains(QStringLiteral("sunshine_name = Den origin_web_ui_allowed = wan\n")));
        QVERIFY(conf.contains(QStringLiteral("\norigin_web_ui_allowed = pc\n")));
    }

    void theScreenUnitRunsStrmQtOnAHeadlessKWin()
    {
        HostConfig config = sample();
        config.environment = {QStringLiteral("XDG_CONFIG_HOME=/tmp/x")};
        const QString unit = hostUnit(config);
        QVERIFY(unit.contains(QStringLiteral(
            "ExecStart=/usr/bin/kwin_wayland --virtual --width 3840 --height 2160 --socket strmqt-host "
            "--no-lockscreen --no-global-shortcuts --exit-with-session /usr/bin/strmqt\n")));
        QVERIFY(unit.contains(QStringLiteral("Environment=XDG_CONFIG_HOME=/tmp/x\n")));
        QVERIFY(unit.contains(QStringLiteral("WantedBy=default.target\n")));
    }

    void sunshinesUnitFollowsTheScreen()
    {
        const HostConfig config = sample();
        const QString unit = sunshineUnit(config);
        QVERIFY(unit.contains(QStringLiteral("Requires=strmqt-host.service\n")));
        QVERIFY(unit.contains(QStringLiteral("PartOf=strmqt-host.service\n")));
        QVERIFY(unit.contains(QStringLiteral("Environment=WAYLAND_DISPLAY=strmqt-host\n")));
        QVERIFY(unit.contains(QStringLiteral("[ -S %t/strmqt-host ]")));
        QVERIFY(unit.contains(QStringLiteral("ExecStart=/usr/bin/sunshine %1/sunshine.conf\n").arg(config.stateDir)));
    }

    void pathsAServiceFileCannotNameAreRefused()
    {
        QVERIFY(configProblems(sample()).isEmpty());
        HostConfig spaced = sample();
        spaced.strmqt = QStringLiteral("/home/u/My Apps/strmqt");
        QCOMPARE(configProblems(spaced).size(), 1);
        HostConfig relative = sample();
        relative.sunshine = QStringLiteral("sunshine");
        QCOMPARE(configProblems(relative).size(), 1);
        HostConfig injected = sample();
        injected.environment = {QStringLiteral("A=1\nExecStartPre=/bin/true")};
        QCOMPARE(configProblems(injected).size(), 1);
    }

    void aBusyPortMovesTheWholeRange()
    {
        // 49010 is base 48989's RTSP port: one taken port rules the base out.
        const auto busy = [](int port, bool udp) { return !(port == 49010 && !udp); };
        QCOMPARE(freePortBase(busy), 49089);
        QCOMPARE(freePortBase([](int, bool) { return true; }), 48989);
        QCOMPARE(freePortBase([](int, bool) { return false; }), 0);
        const auto udpBusy = [](int port, bool udp) { return !(udp && port == 48989 + 9); };
        QCOMPARE(freePortBase(udpBusy), 49089);
    }

    void theProbeSaysWhatIsMissing()
    {
        QTemporaryDir dir;
        const QString old = dir.filePath(QStringLiteral("old-sunshine"));
        const QString recent = dir.filePath(QStringLiteral("new-sunshine"));
        writeScript(old, "echo wlr only\n");
        writeScript(recent, "# zkde_screencast_unstable_v1\n");

        StreamingHost::Environment env;
        env.systemctl = QStringLiteral("/usr/bin/systemctl");
        env.kwin = QStringLiteral("/usr/bin/kwin_wayland");
        env.sunshine = recent;
        QVERIFY(StreamingHost::probeProblems(env).isEmpty());

        env.sunshine = old;
        QCOMPARE(StreamingHost::probeProblems(env).size(), 1);
        env.sunshine.clear();
        env.kwin.clear();
        QCOMPARE(StreamingHost::probeProblems(env).size(), 2);

        // The sandbox is the whole answer: nothing else is worth listing.
        env.insideFlatpak = true;
        QCOMPARE(StreamingHost::probeProblems(env).size(), 1);
    }

    void setUpWritesTheFilesAndStartsTheServices()
    {
        QTemporaryDir dir;
        StreamingHost::Environment env = fakeEnvironment(dir, "echo \"$@\" >> \"$LOG\"\n"
                                                              "[ \"$2\" = is-active ] && printf 'active\\nactive\\n'\n"
                                                              "exit 0\n");
        qputenv("LOG", dir.filePath(QStringLiteral("calls")).toUtf8());
        StreamingHost host(env);
        QSignalSpy settled(&host, &StreamingHost::settled);
        host.setUp();
        QVERIFY(host.busy());
        QVERIFY(settled.wait(5000));
        QVERIFY2(!host.failed(), qPrintable(host.message()));
        QVERIFY(host.installed());
        QTRY_VERIFY(host.running());

        QVERIFY(read(env.unitDir + QStringLiteral("/t-host.service")).contains(QStringLiteral("--exit-with-session")));
        QVERIFY(read(env.unitDir + QStringLiteral("/t-sunshine.service")).contains(QStringLiteral("Requires=t-host.service")));
        QVERIFY(read(env.stateDir + QStringLiteral("/sunshine.conf")).contains(QStringLiteral("origin_web_ui_allowed = pc")));
        QVERIFY(QFile::exists(env.stateDir + QStringLiteral("/apps.json")));

        const QStringList calls = read(dir.filePath(QStringLiteral("calls"))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY(calls.size() >= 3);
        QCOMPARE(calls.at(0), QStringLiteral("--user daemon-reload"));
        QCOMPARE(calls.at(1), QStringLiteral("--user enable t-host.service t-sunshine.service"));
        QCOMPARE(calls.at(2), QStringLiteral("--user restart t-host.service t-sunshine.service"));

        host.remove();
        QVERIFY(settled.wait(5000));
        QVERIFY(!host.failed());
        QVERIFY(!host.installed());
        QVERIFY(!QFile::exists(env.unitDir + QStringLiteral("/t-host.service")));
        // Pairings outlive a removal.
        QVERIFY(QFile::exists(env.stateDir + QStringLiteral("/sunshine.conf")));
    }

    void aFailedCommandIsReportedInItsOwnWords()
    {
        QTemporaryDir dir;
        const StreamingHost::Environment env =
            fakeEnvironment(dir, "[ \"$2\" = enable ] && { echo 'Failed to connect to bus'; exit 1; }\nexit 0\n");
        StreamingHost host(env);
        QSignalSpy settled(&host, &StreamingHost::settled);
        host.setUp();
        QVERIFY(settled.wait(5000));
        QVERIFY(host.failed());
        QVERIFY(host.message().contains(QStringLiteral("Failed to connect to bus")));
        QVERIFY(!host.busy());
    }

    void aMachineThatCannotHostIsToldWhyAndNothingIsWritten()
    {
        QTemporaryDir dir;
        StreamingHost::Environment env = fakeEnvironment(dir, "exit 0\n");
        env.sunshine.clear();
        StreamingHost host(env);
        host.setUp();
        QVERIFY(host.failed());
        QVERIFY(!host.busy());
        QVERIFY(!host.available());
        QVERIFY(!QFile::exists(env.unitDir + QStringLiteral("/t-host.service")));
    }

    // The real thing, on this machine: a headless KWin with StrmQt on it and a
    // Sunshine capturing it, as transient services that are gone at logout.
    // Run by hand (STRMQT_TEST_LIVE_STREAMING_HOST=1): it needs systemd, KWin,
    // PipeWire and a Sunshine with KWin capture, and it starts real processes.
    void liveOnThisMachine()
    {
        if (!qEnvironmentVariableIsSet("STRMQT_TEST_LIVE_STREAMING_HOST"))
            QSKIP("set STRMQT_TEST_LIVE_STREAMING_HOST=1 to start real services");
        QTemporaryDir dir;
        StreamingHost::Environment env = StreamingHost::Environment::detect();
        env.unitDir = qEnvironmentVariable("XDG_RUNTIME_DIR") + QStringLiteral("/systemd/user");
        env.stateDir = dir.filePath(QStringLiteral("state"));
        env.hostUnit = QStringLiteral("strmqt-test-host.service");
        env.sunshineUnit = QStringLiteral("strmqt-test-host-sunshine.service");
        env.socket = QStringLiteral("strmqt-test-host");
        env.strmqt = QCoreApplication::applicationDirPath() + QStringLiteral("/strmqt");
        env.linger = false;
        // A scratch profile: not this machine's sign-in, settings or cache.
        for (const char *name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"})
            env.strmqtEnvironment << QStringLiteral("%1=%2").arg(QLatin1String(name), dir.filePath(QLatin1String(name)));
        QVERIFY2(StreamingHost::probeProblems(env).isEmpty(),
                 qPrintable(StreamingHost::probeProblems(env).join(QLatin1Char(' '))));

        StreamingHost host(env);
        QSignalSpy settled(&host, &StreamingHost::settled);
        host.setUp();
        QVERIFY(settled.wait(30000));
        const auto cleanup = qScopeGuard([&] {
            host.remove();
            settled.wait(30000);
        });
        QVERIFY2(!host.failed(), qPrintable(host.message()));
        qInfo() << "port" << host.port() << host.message();
        QTest::qWait(12000);
        host.checkState();
        QTRY_VERIFY2_WITH_TIMEOUT(host.running(), qPrintable(host.screenState() + QLatin1Char('/') + host.sunshineState()), 10000);
        const QString log = read(env.stateDir + QStringLiteral("/sunshine.log"));
        QVERIFY2(log.contains(QStringLiteral("Screencasting with KWin ScreenCast")), qPrintable(log.right(2000)));
        QVERIFY2(log.contains(QStringLiteral("resolution: 3840x2160")), qPrintable(log.right(2000)));
        QVERIFY2(!log.contains(QStringLiteral("Fatal:")), qPrintable(log.right(2000)));
    }

private:
    StreamingHost::Environment fakeEnvironment(const QTemporaryDir &dir, const QByteArray &systemctl)
    {
        StreamingHost::Environment env;
        env.unitDir = dir.filePath(QStringLiteral("units"));
        env.stateDir = dir.filePath(QStringLiteral("state"));
        env.hostUnit = QStringLiteral("t-host.service");
        env.sunshineUnit = QStringLiteral("t-sunshine.service");
        env.strmqt = QStringLiteral("/usr/bin/true");
        env.kwin = QStringLiteral("/usr/bin/true");
        env.sunshine = dir.filePath(QStringLiteral("sunshine"));
        env.systemctl = dir.filePath(QStringLiteral("systemctl"));
        env.linger = false;
        writeScript(env.sunshine, "# zkde_screencast_unstable_v1\n");
        writeScript(env.systemctl, systemctl);
        return env;
    }
};

QTEST_GUILESS_MAIN(TestStreamingHost)
#include "tst_streaming_host.moc"
