#include "platform/streaming/HostFiles.h"
#include "platform/streaming/StreamingHost.h"
#include "platform/streaming/SunshineApi.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QSettings>
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
    config.pipewire = QStringLiteral("/usr/bin/pipewire");
    config.wireplumber = QStringLiteral("/usr/bin/wireplumber");
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
        QVERIFY(conf.contains(QStringLiteral("port = 8335\n")));
    }

    void sunshinePutsNoIconInTheDesktopsTray()
    {
        QVERIFY(sunshineConf(sample()).contains(QStringLiteral("system_tray = disabled\n")));
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
            "--no-lockscreen --no-global-shortcuts "
            "--exit-with-session \"/usr/bin/env XDG_CONFIG_HOME=/tmp/x PIPEWIRE_REMOTE=strmqt-host-pipewire "
            "/usr/bin/strmqt\"\n")));
        QVERIFY(unit.contains(QStringLiteral("Environment=XDG_CONFIG_HOME=/tmp/x\n")));
        QVERIFY(unit.contains(QStringLiteral("Environment=STRMQT_STREAMING_HOST=1\n")));
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

    // Sunshine makes what it captures the default output while it streams, so
    // the host's sound lives on a PipeWire of its own, never the desktop's.
    void theHostsSoundIsKeptApartFromTheDesktops()
    {
        const HostConfig config = sample();
        const QString conf = audioConf(config);
        QVERIFY(conf.contains(QStringLiteral("core.name = strmqt-host-pipewire\n")));
        QVERIFY(conf.contains(QStringLiteral("node.name = strmqt-host-speakers\n")));
        QVERIFY(conf.contains(QStringLiteral("factory.name = support.null-audio-sink\n")));
        // No sound card, and no claim on the desktop pulse server's D-Bus name.
        QVERIFY(!conf.contains(QStringLiteral("alsa")));
        QVERIFY(conf.contains(QStringLiteral("server.dbus-name = \"\"")));
        QVERIFY(conf.contains(QStringLiteral("libpipewire-module-access")));
        QVERIFY(conf.contains(QStringLiteral("path = \"/usr/bin/wireplumber\" args = \"--profile policy\"")));

        const QString audio = audioUnit(config);
        QVERIFY(audio.contains(QStringLiteral("ExecStart=/usr/bin/pipewire -c %1/audio/pipewire.conf\n").arg(config.stateDir)));
        QVERIFY(audio.contains(QStringLiteral("PartOf=strmqt-host.service\n")));
        QVERIFY(audio.contains(QStringLiteral("RuntimeDirectory=strmqt-host-audio\n")));
        QVERIFY(audio.contains(QStringLiteral("Environment=PULSE_RUNTIME_PATH=%t/strmqt-host-audio\n")));
        QVERIFY(audio.contains(QStringLiteral("Environment=PIPEWIRE_REMOTE=strmqt-host-pipewire\n")));
        QVERIFY(audio.contains(QStringLiteral("Environment=XDG_STATE_HOME=%1/audio\n").arg(config.stateDir)));
        QVERIFY(audio.contains(QStringLiteral("[ -S %t/strmqt-host-audio/native ]")));

        // The screen needs its sound. Pulse clients there all play on the
        // host's PipeWire; only StrmQt is moved for PipeWire's own protocol,
        // since KWin's screencast stays on the desktop's.
        const QString screen = hostUnit(config);
        QVERIFY(screen.contains(QStringLiteral("Requires=strmqt-host-audio.service\n")));
        QVERIFY(screen.contains(QStringLiteral("After=pipewire.service strmqt-host-audio.service\n")));
        QVERIFY(screen.contains(QStringLiteral("Environment=PULSE_SERVER=unix:%t/strmqt-host-audio/native\n")));
        QVERIFY(!screen.contains(QStringLiteral("Environment=PIPEWIRE_REMOTE")));

        const QString sunshine = sunshineUnit(config);
        QVERIFY(sunshine.contains(QStringLiteral("Environment=PULSE_SERVER=unix:%t/strmqt-host-audio/native\n")));
        QVERIFY(!sunshine.contains(QStringLiteral("PIPEWIRE_REMOTE")));
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
        HostConfig noSound = sample();
        noSound.wireplumber.clear();
        QCOMPARE(configProblems(noSound).size(), 1);
        HostConfig injected = sample();
        injected.environment = {QStringLiteral("A=1\nExecStartPre=/bin/true")};
        QCOMPARE(configProblems(injected).size(), 1);
    }

    void aBusyPortMovesTheWholeRange()
    {
        // 8356 is base 8335's RTSP port: one taken port rules the base out.
        const auto busy = [](int port, bool udp) { return !(port == 8356 && !udp); };
        QCOMPARE(freePortBase(busy), 8435);
        QCOMPARE(freePortBase([](int, bool) { return true; }), 8335);
        QCOMPARE(freePortBase([](int, bool) { return false; }), 0);
        const auto udpBusy = [](int port, bool udp) { return !(udp && port == 8335 + 9); };
        QCOMPARE(freePortBase(udpBusy), 8435);
    }

    void sunshineKeepsToStrmQtsBlockAndLeavesTheRemotesTheirs()
    {
        QList<int> ports = tcpPorts(kDefaultPort) + udpPorts(kDefaultPort);
        std::sort(ports.begin(), ports.end());
        QCOMPARE(ports.constFirst(), kBlockFirst);
        QCOMPARE(ports.constLast(), kBlockLast);
        QVERIFY(!ports.contains(8337)); // the desktop's remote
        QVERIFY(!ports.contains(8338)); // the streaming host's
        QVERIFY(!ports.contains(kTimePort)); // the host's clock
        QCOMPARE(webUiPort(kDefaultPort), 8336);
    }

    void theHostsKWinKeepsItsSettingsToItself()
    {
        HostConfig config = sample();
        config.configHome = QStringLiteral("/home/u/.config");
        const QString unit = hostUnit(config);
        // KWin's own folder, so its virtual screen never lands in the
        // desktop's kwinoutputconfig.json; StrmQt gets the desktop's back.
        QVERIFY(unit.contains(QStringLiteral("Environment=XDG_CONFIG_HOME=%1/kwin\n").arg(config.stateDir)));
        QVERIFY(unit.contains(QStringLiteral("/usr/bin/env XDG_CONFIG_HOME=/home/u/.config PIPEWIRE_REMOTE=")));
    }

    void aStrmLightIsToldWhatToAskSunshineFor()
    {
        HostConfig config = sample();
        QJsonObject stream = streamJson(config);
        QCOMPARE(stream.value(QStringLiteral("width")).toInt(), config.width);
        QCOMPARE(stream.value(QStringLiteral("height")).toInt(), config.height);
        QCOMPARE(stream.value(QStringLiteral("fps")).toInt(), 30);
        QCOMPARE(stream.value(QStringLiteral("codec")).toString(), QStringLiteral("auto"));
        QCOMPARE(stream.value(QStringLiteral("audio")).toString(), QStringLiteral("2"));
        // Automatic: by size and frame rate.
        QCOMPARE(stream.value(QStringLiteral("bitrateKbps")).toInt(), automaticBitrate(config.height, 30));
        QCOMPARE(automaticBitrate(2160, 30), 40000);
        QCOMPARE(automaticBitrate(2160, 60), 60000);
        QCOMPARE(automaticBitrate(1080, 30), 15000);
        config.bitrateKbps = 80000;
        config.fps = 60;
        config.codec = QStringLiteral("av1");
        config.audio = QStringLiteral("51");
        stream = streamJson(config);
        QCOMPARE(stream.value(QStringLiteral("bitrateKbps")).toInt(), 80000);
        QCOMPARE(stream.value(QStringLiteral("fps")).toInt(), 60);
        QCOMPARE(stream.value(QStringLiteral("codec")).toString(), QStringLiteral("av1"));
        QCOMPARE(stream.value(QStringLiteral("audio")).toString(), QStringLiteral("51"));
    }

    void streamSettingsAreKeptAndReadFreshForStrmLight()
    {
        QTemporaryDir dir;
        StreamingHost::Environment env = fakeEnvironment(dir, "exit 0\n");
        QVERIFY(QDir().mkpath(env.stateDir));
        {
            StreamingHost desktop(env);
            desktop.setStreamFps(60);
            desktop.setStreamCodec(QStringLiteral("hevc"));
            desktop.setStreamAudio(QStringLiteral("71"));
            desktop.setStreamBitrate(60000);
            desktop.setStreamCodec(QStringLiteral("vp9")); // not one of them
            desktop.setStreamFps(24);
        }
        StreamingHost host(env);
        QCOMPARE(host.streamFps(), 60);
        QCOMPARE(host.streamCodec(), QStringLiteral("hevc"));
        QCOMPARE(host.streamAudio(), QStringLiteral("71"));
        QCOMPARE(host.streamBitrate(), 60000);

        // The desktop's StrmQt changes them while the host's runs: what a
        // StrmLight is told follows without a restart.
        StreamingHost desktop(env);
        desktop.setStreamFps(30);
        const QJsonObject told = host.strmLightHost();
        QCOMPARE(told.value(QStringLiteral("stream")).toObject().value(QStringLiteral("fps")).toInt(), 30);
        QVERIFY(told.contains(QStringLiteral("sunshineFingerprint")));
    }

    void portsLinuxMayLendAreNoticed()
    {
        // The kernel's default range, and the one this kind of distribution sets.
        QVERIFY(!portsLent(QStringLiteral("32768\t60999\n"), {}, kBlockFirst, kBlockLast));
        QVERIFY(!portsLent(QStringLiteral("10240 65535"), {}, kBlockFirst, kBlockLast));
        // 0.9.0's ports were inside the second.
        QVERIFY(portsLent(QStringLiteral("10240 65535"), QStringLiteral("\n"), 48984, 49010));
        QVERIFY(!portsLent(QStringLiteral("10240 65535"), QStringLiteral("48984-49010\n"), 48984, 49010));
        // Reserved only in part is still lent.
        QVERIFY(portsLent(QStringLiteral("1024 65535"), QStringLiteral("8330-8340,9000"), kBlockFirst, kBlockLast));
        QVERIFY(!portsLent(QStringLiteral("1024 65535"), QStringLiteral("8000,8330-8356"), kBlockFirst, kBlockLast));
        // Unreadable: nothing to say.
        QVERIFY(!portsLent({}, {}, kBlockFirst, kBlockLast));
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
        env.pipewire = QStringLiteral("/usr/bin/pipewire");
        env.wireplumber = QStringLiteral("/usr/bin/wireplumber");
        QVERIFY(StreamingHost::probeProblems(env).isEmpty());

        env.wireplumber.clear();
        QCOMPARE(StreamingHost::probeProblems(env).size(), 1);
        env.wireplumber = QStringLiteral("/usr/bin/wireplumber");

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
        QVERIFY(read(env.unitDir + QStringLiteral("/t-audio.service")).contains(QStringLiteral("PartOf=t-host.service")));
        QVERIFY(read(env.unitDir + QStringLiteral("/t-host.service")).contains(QStringLiteral("Requires=t-audio.service")));
        QVERIFY(read(env.stateDir + QStringLiteral("/audio/pipewire.conf")).contains(QStringLiteral("support.null-audio-sink")));
        QVERIFY(read(env.stateDir + QStringLiteral("/sunshine.conf")).contains(QStringLiteral("origin_web_ui_allowed = pc")));
        QVERIFY(QFile::exists(env.stateDir + QStringLiteral("/apps.json")));

        const QStringList calls = read(dir.filePath(QStringLiteral("calls"))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY(calls.size() >= 4);
        QCOMPARE(calls.at(0), QStringLiteral("--user daemon-reload"));
        QCOMPARE(calls.at(1), QStringLiteral("--user enable t-host.service t-sunshine.service"));
        // Sunshine is stopped while its sign-in is set (the first time only).
        QCOMPARE(calls.at(2), QStringLiteral("--user stop t-sunshine.service"));
        QCOMPARE(calls.at(3), QStringLiteral("--user restart t-host.service t-sunshine.service"));

        host.remove();
        QVERIFY(settled.wait(5000));
        QVERIFY(!host.failed());
        QVERIFY(!host.installed());
        QVERIFY(!QFile::exists(env.unitDir + QStringLiteral("/t-host.service")));
        QVERIFY(!QFile::exists(env.unitDir + QStringLiteral("/t-audio.service")));
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

    void aRestartMovesA090HostOffItsPorts()
    {
        QTemporaryDir dir;
        StreamingHost::Environment env = fakeEnvironment(dir, "exit 0\n");
        {
            StreamingHost first(env);
            QSignalSpy settled(&first, &StreamingHost::settled);
            first.setUp();
            QVERIFY(settled.wait(5000));
        }
        // As 0.9.0 left it.
        {
            QSettings stored(env.stateDir + QStringLiteral("/host.ini"), QSettings::IniFormat);
            stored.setValue(QStringLiteral("port"), kLegacyPort);
        }
        StreamingHost host(env);
        QCOMPARE(host.port(), kLegacyPort);
        host.refresh();
        QVERIFY(host.installed());
        QSignalSpy settled(&host, &StreamingHost::settled);
        host.restart();
        QVERIFY(settled.wait(5000));
        // StrmQt's block, or a step above it when this machine already uses it.
        QVERIFY(host.port() >= kDefaultPort && host.port() < kLegacyPort);
        QVERIFY(read(env.stateDir + QStringLiteral("/sunshine.conf"))
                    .contains(QStringLiteral("port = %1\n").arg(host.port())));
        QCOMPARE(QSettings(env.stateDir + QStringLiteral("/host.ini"), QSettings::IniFormat)
                     .value(QStringLiteral("port"))
                     .toInt(),
                 host.port());
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

    void aStrmLightsCodeWaitsForAMoonlightToAsk()
    {
        QString id = QStringLiteral("stale");
        // A Sunshine that lists who is asking: nobody yet, so wait.
        QVERIFY(!StreamingHost::pinTarget(true, {}, &id));
        // Somebody: that request.
        const QVariantList asking{QVariantMap{{QStringLiteral("id"), QStringLiteral("abc")},
                                              {QStringLiteral("name"), QStringLiteral("Fire TV")}}};
        QVERIFY(StreamingHost::pinTarget(true, asking, &id));
        QCOMPARE(id, QStringLiteral("abc"));
        QVERIFY(!StreamingHost::pinTarget(true, {QVariantMap{}}, &id));
        // One that does not list them takes the PIN for whoever is waiting.
        QVERIFY(StreamingHost::pinTarget(false, {}, &id));
        QVERIFY(id.isEmpty());

        // Done is a device that was not there before, not Sunshine saying yes:
        // the PIN may have gone to an abandoned request from another Moonlight.
        const auto device = [](const char *uuid) {
            return QVariant(QVariantMap{{QStringLiteral("uuid"), QString::fromLatin1(uuid)}});
        };
        const QStringList before{QStringLiteral("a"), QStringLiteral("b")};
        QVERIFY(!StreamingHost::hasNewDevice(before, {device("a"), device("b")}));
        QVERIFY(!StreamingHost::hasNewDevice(before, {device("b")}));
        QVERIFY(StreamingHost::hasNewDevice(before, {device("a"), device("b"), device("c")}));
        QVERIFY(StreamingHost::hasNewDevice({}, {device("c")}));
    }

    void onlyWhatStrmQtPairedForAStrmLightMayStay()
    {
        const auto device = [](const char *name, const char *uuid) {
            return QVariant(QVariantMap{{QStringLiteral("name"), QString::fromLatin1(name)},
                                        {QStringLiteral("uuid"), QString::fromLatin1(uuid)}});
        };
        // StrmQt names every pairing it makes; anything else is a plain
        // Moonlight paired some other way, and is unpaired.
        QVERIFY(StreamingHost::strangers({device("StrmLight", "a"), device("StrmLight", "b")}).isEmpty());
        QCOMPARE(StreamingHost::strangers({device("StrmLight", "a"), device("TV", "b"), device("strmlight", "c")}),
                 (QStringList{QStringLiteral("b"), QStringLiteral("c")}));
        QCOMPARE(StreamingHost::strangers({device("", "d")}), QStringList{QStringLiteral("d")});
        QVERIFY(StreamingHost::strangers({}).isEmpty());
    }

    void theSunshineVouchedForIsTheCertificateItWrote()
    {
        QTemporaryDir dir;
        StreamingHost::Environment env = fakeEnvironment(dir, "exit 0\n");
        StreamingHost host(env);
        // Not run yet: nothing to vouch for.
        QVERIFY(host.sunshineFingerprint().isEmpty());

        // A certificate as Sunshine writes one (made with openssl for this
        // test; a public certificate, no key).
        const QByteArray certificate =
            "-----BEGIN CERTIFICATE-----\n"
            "MIIBnDCCAUOgAwIBAgIUCW9VxXoZCqX0OW5KYPo3fW0ZxxUwCgYIKoZIzj0EAwIw\n"
            "IzEhMB8GA1UEAwwYU3Vuc2hpbmUgR2FtZXN0cmVhbSBIb3N0MCAXDTI2MTAxODAx\n"
            "NTIzN1oYDzIxMjYwOTI0MDE1MjM3WjAjMSEwHwYDVQQDDBhTdW5zaGluZSBHYW1l\n"
            "c3RyZWFtIEhvc3QwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAAQ99s3HEKhozVnj\n"
            "5Cj64yZAs/aqtjUn8ZTpUhrEUJNSZaES4PaQ6wwoWyd/N3CsA4kzYJWJbXW6VXys\n"
            "RtJowIqzo1MwUTAdBgNVHQ4EFgQUyfjZlQOgD2E3ZcfqYFuVf8xbmgYwHwYDVR0j\n"
            "BBgwFoAUyfjZlQOgD2E3ZcfqYFuVf8xbmgYwDwYDVR0TAQH/BAUwAwEB/zAKBggq\n"
            "hkjOPQQDAgNHADBEAiB0Heh4sfZt219Zo/ONW/4d6JqPx1Ek31Gz9AsLWu273AIg\n"
            "CEnUNfQbC74kbcg9VWAZq62VgJZhJnbSVJ9yiZtuwpo=\n"
            "-----END CERTIFICATE-----\n";
        QVERIFY(QDir().mkpath(env.stateDir));
        QFile file(env.stateDir + QStringLiteral("/cert.pem"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(certificate);
        file.close();
        // `openssl x509 -outform der | sha256sum`
        QCOMPARE(host.sunshineFingerprint(),
                 QStringLiteral("14f04bd951e34c3fa35a09d06245b868c540bf1da58dada259de769e24e46a62"));
    }

    void sunshinesAnswersAreReadTolerantly()
    {
        const auto clients = SunshineApi::parseClients(
            R"({"named_certs":[{"name":"Den TV","uuid":"A1","extra":3},{"uuid":"B2"},{"name":"no id"}],"status":true})");
        QCOMPARE(clients.size(), 2);
        QCOMPARE(clients.at(0).name, QStringLiteral("Den TV"));
        QCOMPARE(clients.at(1).uuid, QStringLiteral("B2"));
        QVERIFY(!clients.at(1).name.isEmpty());
        QVERIFY(SunshineApi::parseClients("not json").isEmpty());

        bool listed = false;
        const auto asking = SunshineApi::parsePairings(R"({"pairings":[{"id":"abc","name":"Fire TV"},{"name":"x"}]})", &listed);
        QVERIFY(listed);
        QCOMPARE(asking.size(), 1);
        QCOMPARE(asking.at(0).id, QStringLiteral("abc"));
        // An older Sunshine answers this address with an error object.
        QVERIFY(SunshineApi::parsePairings(R"({"error":"Not Found","status_code":404})", &listed).isEmpty());
        QVERIFY(!listed);

        QVERIFY(SunshineApi::errorIn(R"({"status":true})").isEmpty());
        QCOMPARE(SunshineApi::errorIn(R"({"status":false,"error":"wrong pin"})"), QStringLiteral("wrong pin"));
        QVERIFY(!SunshineApi::errorIn(R"({"status":false})").isEmpty());
        QVERIFY(!SunshineApi::errorIn("").isEmpty());
    }

    void setUpGivesSunshineASignInOnlyStrmQtKnows()
    {
        QTemporaryDir dir;
        StreamingHost::Environment env = fakeEnvironment(dir, "exit 0\n");
        writeScript(env.sunshine, "# zkde_screencast_unstable_v1\necho \"$@\" > \"$CREDS\"\n");
        qputenv("CREDS", dir.filePath(QStringLiteral("creds")).toUtf8());
        StreamingHost host(env);
        QSignalSpy settled(&host, &StreamingHost::settled);
        host.setUp();
        QVERIFY(settled.wait(5000));
        QVERIFY2(!host.failed(), qPrintable(host.message()));
        const QStringList creds = read(dir.filePath(QStringLiteral("creds"))).trimmed().split(QLatin1Char(' '));
        QCOMPARE(creds.size(), 4);
        QCOMPARE(creds.at(0), env.stateDir + QStringLiteral("/sunshine.conf"));
        QCOMPARE(creds.at(1), QStringLiteral("--creds"));
        QVERIFY(creds.at(3).size() >= 24);
        // Kept, owner-only, where the headless StrmQt can read it.
        QTRY_VERIFY(QFile::exists(env.stateDir + QStringLiteral("/secrets.ini")));
        QCOMPARE(QFile::permissions(env.stateDir + QStringLiteral("/secrets.ini"))
                     & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions());

        // Chosen once: setting up again does not change it under Sunshine.
        QFile::remove(dir.filePath(QStringLiteral("creds")));
        host.setUp();
        QVERIFY(settled.wait(5000));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("creds"))));
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
        env.audioUnit = QStringLiteral("strmqt-test-host-audio.service");
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
        for (const QString &line : log.split(QLatin1Char('\n'))) {
            if (line.contains(QStringLiteral("sink"), Qt::CaseInsensitive)
                || line.contains(QStringLiteral("audio"), Qt::CaseInsensitive))
                qInfo().noquote() << "sunshine:" << line;
        }

        // The host's sound is on its own PipeWire: the virtual speaker is
        // there, and it is that server's default.
        const QString pactl = QStandardPaths::findExecutable(QStringLiteral("pactl"));
        if (!pactl.isEmpty()) {
            QProcess query;
            QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
            environment.insert(QStringLiteral("PULSE_SERVER"),
                               QStringLiteral("unix:%1/strmqt-test-host-audio/native")
                                   .arg(qEnvironmentVariable("XDG_RUNTIME_DIR")));
            query.setProcessEnvironment(environment);
            query.start(pactl, {QStringLiteral("info")});
            QVERIFY(query.waitForFinished(5000));
            const QString info = QString::fromUtf8(query.readAllStandardOutput());
            QVERIFY2(info.contains(QStringLiteral("Default Sink: strmqt-host-speakers")), qPrintable(info));
        }

        // Sunshine's API, with the sign-in set-up chose and the certificate
        // Sunshine wrote: nobody is paired and nobody is asking.
        host.fetchPairing();
        QTRY_VERIFY2_WITH_TIMEOUT(host.pairingReachable(), qPrintable(host.pairingMessage()), 10000);
        QCOMPARE(host.devices().size(), 0);
        QCOMPARE(host.sunshineFingerprint().size(), 64);
    }

    void aForegroundHostNeedsNoSystemd()
    {
        QTemporaryDir dir;
        StreamingHost::Environment env = fakeEnvironment(dir, "exit 0\n");
        env.systemctl.clear();
        QCOMPARE(StreamingHost::probeProblems(env).size(), 1);
        env.foreground = true;
        QVERIFY(StreamingHost::probeProblems(env).isEmpty());
    }

    void prepareWritesTheFilesAndChoosesTheSignInOnce()
    {
        QTemporaryDir dir;
        StreamingHost::Environment env = fakeEnvironment(dir, "exit 0\n");
        env.systemctl.clear();
        env.foreground = true;
        // Sunshine is only asked to take its sign-in.
        writeScript(env.sunshine, "# zkde_screencast_unstable_v1\necho \"$2\" >> \"$LOG\"\n");
        qputenv("LOG", dir.filePath(QStringLiteral("calls")).toUtf8());
        {
            StreamingHost host(env);
            QSignalSpy settled(&host, &StreamingHost::settled);
            host.prepare();
            QVERIFY(settled.count() == 1 || settled.wait(5000));
            QVERIFY2(!host.failed(), qPrintable(host.message()));
            QVERIFY(host.installed() || QFile::exists(host.unitPath(env.hostUnit)));
            QVERIFY(read(host.unitPath(env.hostUnit)).contains(QStringLiteral("--exit-with-session")));
            QVERIFY(read(host.unitPath(env.audioUnit)).contains(QStringLiteral("RuntimeDirectory=")));
            QVERIFY(QFile::exists(env.stateDir + QStringLiteral("/sunshine.conf")));
            QTRY_VERIFY(QFile::exists(env.stateDir + QStringLiteral("/secrets.ini")));

            // Nothing here starts or stops it: its container does.
            host.setUp();
            QVERIFY(host.failed());
            QVERIFY(!host.busy());
        }
        QCOMPARE(read(dir.filePath(QStringLiteral("calls"))), QStringLiteral("--creds\n"));

        // A container started again keeps the sign-in it chose: the second
        // prepare() waits for it to be read rather than choosing another.
        StreamingHost again(env);
        QSignalSpy settled(&again, &StreamingHost::settled);
        again.prepare();
        QVERIFY(settled.count() == 1 || settled.wait(5000));
        QVERIFY2(!again.failed(), qPrintable(again.message()));
        QCOMPARE(read(dir.filePath(QStringLiteral("calls"))), QStringLiteral("--creds\n"));
    }

    void anUnsetUpHostKeepsItsHeight()
    {
        QTemporaryDir dir;
        const StreamingHost::Environment env = fakeEnvironment(dir, "exit 0\n");
        QVERIFY(QDir().mkpath(env.stateDir));
        {
            StreamingHost host(env);
            host.setHeight(1080);
        }
        StreamingHost host(env);
        QCOMPARE(host.height(), 1080);
    }

private:
    StreamingHost::Environment fakeEnvironment(const QTemporaryDir &dir, const QByteArray &systemctl)
    {
        StreamingHost::Environment env;
        env.unitDir = dir.filePath(QStringLiteral("units"));
        env.stateDir = dir.filePath(QStringLiteral("state"));
        env.hostUnit = QStringLiteral("t-host.service");
        env.sunshineUnit = QStringLiteral("t-sunshine.service");
        env.audioUnit = QStringLiteral("t-audio.service");
        env.strmqt = QStringLiteral("/usr/bin/true");
        env.kwin = QStringLiteral("/usr/bin/true");
        env.pipewire = QStringLiteral("/usr/bin/true");
        env.wireplumber = QStringLiteral("/usr/bin/true");
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
