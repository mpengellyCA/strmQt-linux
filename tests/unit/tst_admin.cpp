// `strmqt admin` (app/admin/Admin.h), against a scratch settings file, a
// scratch host folder and a host's HostLink run on a thread of its own: no
// systemd, no Sunshine and no real StrmQt are touched.

#include "app/admin/Admin.h"
#include "core/Settings.h"
#include "remote/HostLink.h"

#include <QBuffer>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QThread>

using strmqt::HostLink;
using strmqt::Settings;
using strmqt::StreamingHost;

namespace {

// The running host's side of its HostLink.
struct Host
{
    QThread thread;
    HostLink *link = nullptr;
    QMutex mutex;
    QStringList codes;
    QByteArray imported;

    explicit Host(const QString &path)
    {
        link = new HostLink(path);
        link->handle("status", [](const QByteArray &) {
            return HostLink::Answer{true, QStringLiteral(R"({"emby":true,"youtubeMusic":false})")};
        });
        link->handle("youtube-music-import", [this](const QByteArray &body) {
            QMutexLocker lock(&mutex);
            imported = body;
            return HostLink::Answer{true, QStringLiteral("Signed in from Firefox · test.")};
        });
        link->moveToThread(&thread);
        thread.start();
        QMetaObject::invokeMethod(link, [this] {
            link->listen([this](const QString &code) {
                QMutexLocker lock(&mutex);
                codes << code;
                return true;
            });
        }, Qt::BlockingQueuedConnection);
    }
    ~Host()
    {
        QMetaObject::invokeMethod(link, [this] { delete link; }, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
};

} // namespace

class AdminTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        m_settings.reset(new Settings(m_dir->filePath(QStringLiteral("strmqt.conf"))));
        m_out.clear();
        m_err.clear();
        m_context = {};
        m_context.settings = m_settings.get();
        m_context.linkPath = m_dir->filePath(QStringLiteral("link"));
        m_context.host.unitDir = m_dir->filePath(QStringLiteral("units"));
        m_context.host.stateDir = m_dir->filePath(QStringLiteral("state"));
        m_context.host.linger = false;
        QVERIFY(QDir().mkpath(m_context.host.stateDir));
    }

    void wrongUsageSaysHow()
    {
        QCOMPARE(run({}), strmqt::admin::kUsage);
        QVERIFY(m_err.contains(QStringLiteral("Usage: strmqt admin")));
        QCOMPARE(run({QStringLiteral("frobnicate")}), strmqt::admin::kUsage);
        QVERIFY(m_err.contains(QStringLiteral("Unknown command: frobnicate")));
        QCOMPARE(run({QStringLiteral("stream"), QStringLiteral("--fps")}), strmqt::admin::kUsage);
        QCOMPARE(run({QStringLiteral("help")}), strmqt::admin::kOk);
        QVERIFY(m_out.contains(QStringLiteral("host run")));
    }

    void streamSettingsAreShownAndChanged()
    {
        QCOMPARE(run({QStringLiteral("stream"), QStringLiteral("--fps"), QStringLiteral("60"),
                      QStringLiteral("--codec"), QStringLiteral("hevc"), QStringLiteral("--bitrate"),
                      QStringLiteral("25000"), QStringLiteral("--audio"), QStringLiteral("51")}),
                 strmqt::admin::kOk);
        QVERIFY(m_out.contains(QStringLiteral("p60, 25000 kbit/s, codec hevc, 5.1 sound")));
        QCOMPARE(run({QStringLiteral("stream"), QStringLiteral("--bitrate"), QStringLiteral("auto"),
                      QStringLiteral("--json")}),
                 strmqt::admin::kOk);
        const QJsonObject shown = QJsonDocument::fromJson(m_out.toUtf8()).object();
        QCOMPARE(shown.value(QStringLiteral("fps")).toInt(), 60);
        QCOMPARE(shown.value(QStringLiteral("codec")).toString(), QStringLiteral("hevc"));
        QCOMPARE(shown.value(QStringLiteral("bitrateKbps")).toInt(), 0);
        // What the host's StrmQt gives a StrmLight is what was set.
        StreamingHost host(m_context.host);
        QCOMPARE(host.streamAudio(), QStringLiteral("51"));

        for (const QStringList &bad : {QStringList{QStringLiteral("--fps"), QStringLiteral("24")},
                                       QStringList{QStringLiteral("--codec"), QStringLiteral("vp9")},
                                       QStringList{QStringLiteral("--bitrate"), QStringLiteral("fast")},
                                       QStringList{QStringLiteral("--height"), QStringLiteral("900")}}) {
            QCOMPARE(run(QStringList{QStringLiteral("stream")} + bad), strmqt::admin::kFailed);
            QVERIFY(!m_err.isEmpty());
        }
        QCOMPARE(StreamingHost(m_context.host).streamFps(), 60);
    }

    void devicesAreListedWithoutTheirTokensAndUnpaired()
    {
        const auto device = [](const char *id, const char *name, const char *kind) {
            return QVariantMap{{QStringLiteral("id"), QLatin1String(id)},
                               {QStringLiteral("name"), QLatin1String(name)},
                               {QStringLiteral("kind"), QLatin1String(kind)},
                               {QStringLiteral("hash"), QStringLiteral("secret-hash")},
                               {QStringLiteral("added"), QStringLiteral("2026-10-18T10:00:00Z")}};
        };
        m_settings->setWebRemoteDevices({device("a", "Den", "strmlight"), device("b", "Phone", "phone")});

        QCOMPARE(run({QStringLiteral("devices")}), strmqt::admin::kOk);
        QVERIFY(m_out.contains(QStringLiteral("a  StrmLight  Den  (paired 2026-10-18)")));
        QVERIFY(!m_out.contains(QStringLiteral("secret-hash")));
        QCOMPARE(run({QStringLiteral("devices"), QStringLiteral("--json")}), strmqt::admin::kOk);
        const QJsonArray listed = QJsonDocument::fromJson(m_out.toUtf8()).array();
        QCOMPARE(listed.size(), 2);
        QVERIFY(!listed.at(0).toObject().contains(QStringLiteral("hash")));

        QCOMPARE(run({QStringLiteral("unpair"), QStringLiteral("a")}), strmqt::admin::kOk);
        QCOMPARE(m_settings->webRemoteDevices().size(), 1);
        QCOMPARE(run({QStringLiteral("unpair"), QStringLiteral("nobody")}), strmqt::admin::kFailed);
        QCOMPARE(run({QStringLiteral("unpair")}), strmqt::admin::kUsage);
        QCOMPARE(run({QStringLiteral("unpair"), QStringLiteral("--all")}), strmqt::admin::kOk);
        QVERIFY(m_settings->webRemoteDevices().isEmpty());
        QCOMPARE(run({QStringLiteral("devices")}), strmqt::admin::kOk);
        QVERIFY(m_out.contains(QStringLiteral("No device is paired.")));
    }

    void aCodeGoesToTheRunningHost()
    {
        QCOMPARE(run({QStringLiteral("pair"), QStringLiteral("4831")}), strmqt::admin::kFailed);
        QVERIFY(m_err.contains(QStringLiteral("not running")));

        Host host(m_context.linkPath);
        QCOMPARE(run({QStringLiteral("pair"), QStringLiteral("48a1")}), strmqt::admin::kFailed);
        QCOMPARE(run({QStringLiteral("pair"), QStringLiteral("4831")}), strmqt::admin::kOk);
        QMutexLocker lock(&host.mutex);
        QCOMPARE(host.codes, QStringList{QStringLiteral("4831")});
    }

    void aSignInIsSentAsItWasRead()
    {
        const QByteArray signIn = R"({"label":"Firefox · test","cookies":[]})";
        QBuffer in;
        in.setData(signIn);
        QVERIFY(in.open(QIODevice::ReadOnly));
        m_context.in = &in;
        QCOMPARE(run({QStringLiteral("youtube-music"), QStringLiteral("import")}), strmqt::admin::kFailed);

        Host host(m_context.linkPath);
        in.seek(0);
        QCOMPARE(run({QStringLiteral("youtube-music"), QStringLiteral("import"), QStringLiteral("-")}),
                 strmqt::admin::kOk);
        QVERIFY(m_out.contains(QStringLiteral("Signed in from Firefox · test.")));
        {
            QMutexLocker lock(&host.mutex);
            QCOMPARE(host.imported, signIn);
        }
        // From a file as well.
        const QString file = m_dir->filePath(QStringLiteral("sign-in.json"));
        QFile written(file);
        QVERIFY(written.open(QIODevice::WriteOnly));
        written.write("{}");
        written.close();
        QCOMPARE(run({QStringLiteral("youtube-music"), QStringLiteral("import"), file}), strmqt::admin::kOk);
        QMutexLocker lock(&host.mutex);
        QCOMPARE(host.imported, QByteArray("{}"));
    }

    void statusSaysWhetherTheHostRuns()
    {
        m_context.host.foreground = true;
        QCOMPARE(run({QStringLiteral("status"), QStringLiteral("--json")}), strmqt::admin::kOk);
        QJsonObject status = QJsonDocument::fromJson(m_out.toUtf8()).object();
        QCOMPARE(status.value(QStringLiteral("running")).toBool(), false);
        QCOMPARE(status.value(QStringLiteral("remotePort")).toInt(), Settings::kStreamingHostWebRemotePort);

        Host host(m_context.linkPath);
        QCOMPARE(run({QStringLiteral("status"), QStringLiteral("--json")}), strmqt::admin::kOk);
        status = QJsonDocument::fromJson(m_out.toUtf8()).object();
        QCOMPARE(status.value(QStringLiteral("running")).toBool(), true);
        QCOMPARE(status.value(QStringLiteral("host")).toObject().value(QStringLiteral("emby")).toBool(), true);
        QCOMPARE(run({QStringLiteral("status")}), strmqt::admin::kOk);
        QVERIFY(m_out.contains(QStringLiteral("(in a container)")));
        QVERIFY(m_out.contains(QStringLiteral("Emby:           signed in")));
    }

private:
    int run(const QStringList &arguments)
    {
        m_out.clear();
        m_err.clear();
        QTextStream out(&m_out);
        QTextStream err(&m_err);
        m_context.out = &out;
        m_context.err = &err;
        const int code = strmqt::admin::run(arguments, m_context);
        out.flush();
        err.flush();
        return code;
    }

    QScopedPointer<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    strmqt::admin::Context m_context;
    QString m_out;
    QString m_err;
};

QTEST_GUILESS_MAIN(AdminTest)
#include "tst_admin.moc"
