#include <QSignalSpy>
#include <QtTest>

#include "MockEmbyServer.h"
#include "app/controllers/HomeController.h"
#include "app/controllers/LibraryController.h"
#include "app/controllers/SessionController.h"
#include "app/models/LibraryListModel.h"
#include "app/models/MediaItemModel.h"
#include "core/Settings.h"
#include "platform/SecretsStore.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;

namespace {

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");

} // namespace

class ControllersTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void sessionLoginPersistsAndRestores();
    void sessionLoginFailureSurfaces();
    void sessionWithoutServerDoesNotRestore();
    void logoutRetiresALateLogin();
    void serverChangeRetiresALateLogin();
    void serverUrlPolicyRejectsUnsafeAddresses();
    void homeRefreshBuildsRails();
    void libraryPaging();

private:
    MockEmbyServer *m_mock = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    SecretsStore *m_secrets = nullptr;
    emby::EmbyClient *m_client = nullptr;
};

void ControllersTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());

    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_settings = new Settings(m_dir->filePath(QStringLiteral("settings.ini")), this);
    m_secrets = new SecretsStore(m_dir->filePath(QStringLiteral("secrets.ini")), this);
    m_settings->setServerUrl(m_mock->baseUrl());

    m_client = new emby::EmbyClient(this);
    m_client->setDeviceId(QStringLiteral("test-device"));
}

void ControllersTest::cleanup()
{
    delete m_client;
    delete m_secrets;
    delete m_settings;
    delete m_dir;
    delete m_mock;
    m_client = nullptr;
    m_secrets = nullptr;
    m_settings = nullptr;
    m_dir = nullptr;
    m_mock = nullptr;
}

void ControllersTest::sessionLoginPersistsAndRestores()
{
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("POST"),
                                     QStringLiteral("/Users/AuthenticateByName"),
                                     fixturePath(QStringLiteral("auth_by_name.json"))));

    SessionController session(m_settings, m_secrets, m_client);
    QVERIFY(!session.restore()); // nothing stored yet
    QVERIFY(!session.authenticated());

    session.login(QStringLiteral("mike"), QStringLiteral("pw"));
    QTRY_VERIFY(session.authenticated());
    QCOMPARE(session.username(), QStringLiteral("mike"));
    QCOMPARE(m_secrets->readSecret(QStringLiteral("emby/accessToken")), kToken);

    // A fresh controller + client restores the same session from storage.
    emby::EmbyClient freshClient;
    SessionController restored(m_settings, m_secrets, &freshClient);
    QVERIFY(restored.restore());
    QVERIFY(restored.authenticated());
    QCOMPARE(freshClient.accessToken(), kToken);
    QCOMPARE(freshClient.userId(), kUserId);

    restored.logout();
    QVERIFY(!restored.authenticated());
    QCOMPARE(m_secrets->readSecret(QStringLiteral("emby/accessToken")), QString());
}

// A stored credential with no server address must NOT come back as a session.
// This shipped: while serverUrl() carried a baked-in default, signing in with
// the pre-filled field never wrote the address to the store, and removing the
// default left those installs restoring into an app with nowhere to send a
// request — every call failing as `Protocol "" is unknown` behind a UI that
// looked signed in.
void ControllersTest::sessionWithoutServerDoesNotRestore()
{
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("POST"),
                                     QStringLiteral("/Users/AuthenticateByName"),
                                     fixturePath(QStringLiteral("auth_by_name.json"))));

    SessionController session(m_settings, m_secrets, m_client);
    session.login(QStringLiteral("mike"), QStringLiteral("pw"));
    QTRY_VERIFY(session.authenticated());

    // The credential survives; only the address is gone.
    m_settings->setServerUrl(QUrl());
    QCOMPARE(m_secrets->readSecret(QStringLiteral("emby/accessToken")), kToken);

    emby::EmbyClient freshClient;
    SessionController restored(m_settings, m_secrets, &freshClient);
    QVERIFY(!restored.restore());
    QVERIFY(!restored.authenticated());
    QVERIFY(!restored.errorMessage().isEmpty()); // the login screen has to say why
}

void ControllersTest::sessionLoginFailureSurfaces()
{
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Users/AuthenticateByName"), 401,
                     QByteArrayLiteral("{}"));

    SessionController session(m_settings, m_secrets, m_client);
    session.login(QStringLiteral("mike"), QStringLiteral("wrong"));
    QTRY_VERIFY(!session.busy());
    QVERIFY(!session.authenticated());
    QVERIFY(!session.errorMessage().isEmpty());
}

void ControllersTest::logoutRetiresALateLogin()
{
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("POST"),
                                     QStringLiteral("/Users/AuthenticateByName"),
                                     fixturePath(QStringLiteral("auth_by_name.json"))));
    m_mock->setRouteDelay(QStringLiteral("POST"),
                          QStringLiteral("/Users/AuthenticateByName"), 200);

    SessionController session(m_settings, m_secrets, m_client);
    session.login(QStringLiteral("mike"), QStringLiteral("pw"));
    QTRY_COMPARE(m_mock->requestCount(), 1);
    QVERIFY(session.busy());

    session.logout();
    QVERIFY(!session.busy());
    QVERIFY(!session.authenticated());
    QTest::qWait(260);
    QVERIFY(!session.authenticated());
    QVERIFY(!m_client->hasSession());
    QVERIFY(m_secrets->readSecret(QStringLiteral("emby/accessToken")).isEmpty());
}

void ControllersTest::serverChangeRetiresALateLogin()
{
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("POST"),
                                     QStringLiteral("/Users/AuthenticateByName"),
                                     fixturePath(QStringLiteral("auth_by_name.json"))));
    m_mock->setRouteDelay(QStringLiteral("POST"),
                          QStringLiteral("/Users/AuthenticateByName"), 200);
    MockEmbyServer nextServer;
    QVERIFY(nextServer.start());

    SessionController session(m_settings, m_secrets, m_client);
    session.login(QStringLiteral("mike"), QStringLiteral("pw"));
    QTRY_COMPARE(m_mock->requestCount(), 1);
    session.setServerUrl(nextServer.baseUrl());

    QTest::qWait(260);
    QCOMPARE(session.serverUrl(), nextServer.baseUrl());
    QCOMPARE(m_client->baseUrl(), nextServer.baseUrl());
    QVERIFY(!session.authenticated());
    QVERIFY(!m_client->hasSession());
    QVERIFY(m_secrets->readSecret(QStringLiteral("emby/accessToken")).isEmpty());
}

void ControllersTest::serverUrlPolicyRejectsUnsafeAddresses()
{
    SessionController session(m_settings, m_secrets, m_client);
    const QUrl original = session.serverUrl();

    const QList<QUrl> rejected = {
        QUrl(QStringLiteral("ftp://example.org")),
        QUrl(QStringLiteral("https://user:password@example.org")),
        QUrl(QStringLiteral("https:///missing-host")),
        QUrl(QStringLiteral("http://example.org:8096")),
        QUrl(QStringLiteral("https://example.org/path?token=secret")),
    };
    for (const QUrl &url : rejected) {
        session.setServerUrl(url);
        QCOMPARE(session.serverUrl(), original);
        QVERIFY(!session.errorMessage().isEmpty());
    }

    session.setServerUrl(QUrl(QStringLiteral("http://127.0.0.1:8096/")));
    QCOMPARE(session.serverUrl(), QUrl(QStringLiteral("http://127.0.0.1:8096")));
    QVERIFY(session.errorMessage().isEmpty());
    session.setServerUrl(QUrl(QStringLiteral("https://emby.example.org/")));
    QCOMPARE(session.serverUrl(), QUrl(QStringLiteral("https://emby.example.org")));
}

void ControllersTest::homeRefreshBuildsRails()
{
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setSession(kToken, kUserId);
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Items/Resume").arg(kUserId),
                                     fixturePath(QStringLiteral("resume.json"))));
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"), QStringLiteral("/Shows/NextUp"),
                                     fixturePath(QStringLiteral("nextup.json"))));
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Views").arg(kUserId),
                                     fixturePath(QStringLiteral("views.json"))));
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Items/Latest").arg(kUserId),
                                     fixturePath(QStringLiteral("latest.json"))));

    HomeController home(m_client);
    home.refresh();
    QTRY_VERIFY(!home.busy());

    QCOMPARE(home.resume()->rowCount(), 1);
    QCOMPARE(home.nextUp()->rowCount(), 1);
    QCOMPARE(home.libraries()->rowCount(), 3);
    // views.json has Movies + TV Shows with collection types → two Latest rails.
    QCOMPARE(home.latestRails().size(), 2);
    const QVariantMap rail = home.latestRails().first().toMap();
    QVERIFY(rail.value(QStringLiteral("title")).toString().startsWith(QStringLiteral("Latest")));
    auto *railModel = rail.value(QStringLiteral("model")).value<MediaItemModel *>();
    QVERIFY(railModel);
    QCOMPARE(railModel->rowCount(), 2);
}

void ControllersTest::libraryPaging()
{
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setSession(kToken, kUserId);
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Items").arg(kUserId),
                                     fixturePath(QStringLiteral("items_movies.json"))));

    LibraryController library(m_client);
    library.open(QStringLiteral("4"), QStringLiteral("Movies"), QStringLiteral("movies"));
    QTRY_VERIFY(!library.loading());

    QCOMPARE(library.title(), QStringLiteral("Movies"));
    QCOMPARE(library.model()->rowCount(), 3);
    QCOMPARE(library.model()->totalRecordCount(), 42);
    QVERIFY(library.canLoadMore());

    const auto firstRequest = m_mock->lastRequestFor(
        QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId));
    QVERIFY(firstRequest.query.contains(QStringLiteral("StartIndex=0")));
    QVERIFY(firstRequest.query.contains(QStringLiteral("IncludeItemTypes=Movie")));

    library.loadMore();
    QTRY_VERIFY(!library.loading());
    QCOMPARE(library.model()->rowCount(), 6); // mock replays the same 3-item page

    const auto secondRequest = m_mock->lastRequestFor(
        QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId));
    QVERIFY(secondRequest.query.contains(QStringLiteral("StartIndex=3")));
}

QTEST_GUILESS_MAIN(ControllersTest)
#include "tst_controllers.moc"
