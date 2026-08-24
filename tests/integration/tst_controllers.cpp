#include <QFutureWatcher>
#include <QSignalSpy>
#include <QtTest>

#include "FakeSecretsStore.h"
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

template<class T> Result<T> awaitResult(QFuture<Result<T>> future)
{
    if (!future.isFinished()) {
        QEventLoop loop;
        QFutureWatcher<Result<T>> watcher;
        QObject::connect(&watcher, &QFutureWatcher<Result<T>>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(future);
        loop.exec();
    }
    return future.result();
}

bool lastSecretCallIs(const test::FakeSecretsStore &store, test::FakeSecretsStore::CallType type)
{
    return !store.calls.isEmpty() && store.calls.last().type == type;
}

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
    void publicUserImagesUseSessionNamespace();
    void logoutRetiresALateLogin();
    void serverChangeRetiresALateLogin();
    void delayedWalletRestoreIsRetiredByLogout();
    void loginSupersedesADelayedWalletRestore();
    void logoutDuringDelayedTokenWriteCannotReauthenticate();
    void serverUrlPolicyRejectsUnsafeAddresses();
    void homeRefreshBuildsRails();
    void homeSessionResetRetiresOldCountersAndGenreCallbacks();
    void libraryPaging();
    void librarySessionResetRetiresOldPage();

private:
    MockEmbyServer *m_mock = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    SecretsStore *m_secrets = nullptr;
    emby::EmbyClient *m_client = nullptr;
};

void ControllersTest::init()
{
    setEmbyImageSourceNamespace(QStringLiteral("test-session"));
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

void ControllersTest::publicUserImagesUseSessionNamespace()
{
    m_mock->addRoute(
        QStringLiteral("GET"), QStringLiteral("/Users/Public"), 200,
        QByteArrayLiteral("[{\"Id\":\"public-user\",\"Name\":\"Guest\","
                          "\"ImageTags\":{\"Primary\":\"avatar-tag\"}}]"));
    m_client->setBaseUrl(m_mock->baseUrl());
    SessionController session(m_settings, m_secrets, m_client);
    session.loadPublicUsers();
    QTRY_COMPARE_WITH_TIMEOUT(session.publicUsers().size(), 1, 5000);
    QCOMPARE(session.publicUsers().first().toMap().value(QStringLiteral("imageUrl")).toString(),
             QStringLiteral("image://emby/test-session/public-user/Primary/avatar-tag"));
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
    session.restore(); // nothing stored yet
    QVERIFY(!session.authenticated());

    session.login(QStringLiteral("mike"), QStringLiteral("pw"));
    QTRY_VERIFY(session.authenticated());
    QCOMPARE(session.username(), QStringLiteral("mike"));
    QCOMPARE(awaitResult(m_secrets->readSecret(QStringLiteral("emby/accessToken"))).value, kToken);

    // A fresh controller + client restores the same session from storage.
    emby::EmbyClient freshClient;
    SessionController restored(m_settings, m_secrets, &freshClient);
    restored.restore();
    QTRY_VERIFY(restored.authenticated());
    QCOMPARE(freshClient.accessToken(), kToken);
    QCOMPARE(freshClient.userId(), kUserId);

    restored.logout();
    QVERIFY(!restored.authenticated());
    QVERIFY(awaitResult(m_secrets->readSecret(QStringLiteral("emby/accessToken"))).value.isEmpty());
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
    QCOMPARE(awaitResult(m_secrets->readSecret(QStringLiteral("emby/accessToken"))).value, kToken);

    emby::EmbyClient freshClient;
    SessionController restored(m_settings, m_secrets, &freshClient);
    restored.restore();
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
    QVERIFY(awaitResult(m_secrets->readSecret(QStringLiteral("emby/accessToken"))).value.isEmpty());
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
    QVERIFY(awaitResult(m_secrets->readSecret(QStringLiteral("emby/accessToken"))).value.isEmpty());
}

void ControllersTest::delayedWalletRestoreIsRetiredByLogout()
{
    test::FakeSecretsStore secrets;
    secrets.setLegacyFilePathForTests(m_dir->filePath(QStringLiteral("missing-legacy.ini")));
    m_settings->setUsername(QStringLiteral("old-user"));
    m_settings->setUserId(kUserId);

    SessionController session(m_settings, &secrets, m_client);
    session.restore();
    QVERIFY(session.busy());
    QTRY_VERIFY(lastSecretCallIs(secrets, test::FakeSecretsStore::CallType::NetworkWallet));
    secrets.replyNetworkWallet(true);
    QCOMPARE(secrets.calls.last().type, test::FakeSecretsStore::CallType::Open);
    secrets.replyOpen(true);
    QTRY_VERIFY(lastSecretCallIs(secrets, test::FakeSecretsStore::CallType::Read));

    session.logout();
    QVERIFY(!session.busy());
    secrets.replyRead(true, kToken);
    QTRY_VERIFY(lastSecretCallIs(secrets, test::FakeSecretsStore::CallType::Remove));
    secrets.replyRemove(true);
    QCoreApplication::processEvents();

    QVERIFY(!session.authenticated());
    QVERIFY(!m_client->hasSession());
    QVERIFY(m_settings->userId().isEmpty());
}

void ControllersTest::logoutDuringDelayedTokenWriteCannotReauthenticate()
{
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("POST"),
                                     QStringLiteral("/Users/AuthenticateByName"),
                                     fixturePath(QStringLiteral("auth_by_name.json"))));
    test::FakeSecretsStore secrets;
    secrets.setLegacyFilePathForTests(m_dir->filePath(QStringLiteral("missing-legacy.ini")));

    SessionController session(m_settings, &secrets, m_client);
    session.login(QStringLiteral("mike"), QStringLiteral("pw"));
    QTRY_VERIFY(lastSecretCallIs(secrets, test::FakeSecretsStore::CallType::NetworkWallet));
    secrets.replyNetworkWallet(true);
    secrets.replyOpen(true);
    // login() clears the previous persisted token before admitting the new one.
    QTRY_VERIFY(lastSecretCallIs(secrets, test::FakeSecretsStore::CallType::Remove));
    secrets.replyRemove(true);
    QTRY_VERIFY(lastSecretCallIs(secrets, test::FakeSecretsStore::CallType::Write));
    QVERIFY(session.busy());

    session.logout();
    QVERIFY(!m_client->hasSession());
    secrets.replyWrite(true);
    QTRY_VERIFY(lastSecretCallIs(secrets, test::FakeSecretsStore::CallType::Remove));
    secrets.replyRemove(true);
    QCoreApplication::processEvents();

    QVERIFY(!session.authenticated());
    QVERIFY(!session.busy());
    QVERIFY(!m_client->hasSession());
    QVERIFY(m_settings->userId().isEmpty());
}

void ControllersTest::loginSupersedesADelayedWalletRestore()
{
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("POST"),
                                     QStringLiteral("/Users/AuthenticateByName"),
                                     fixturePath(QStringLiteral("auth_by_name.json"))));
    test::FakeSecretsStore secrets;
    secrets.setLegacyFilePathForTests(m_dir->filePath(QStringLiteral("missing-legacy.ini")));
    m_settings->setUsername(QStringLiteral("old-user"));
    m_settings->setUserId(QStringLiteral("old-id"));

    SessionController session(m_settings, &secrets, m_client);
    session.restore();
    QTRY_VERIFY(lastSecretCallIs(secrets, test::FakeSecretsStore::CallType::NetworkWallet));
    secrets.replyNetworkWallet(true);
    secrets.replyOpen(true);
    QTRY_VERIFY(lastSecretCallIs(secrets, test::FakeSecretsStore::CallType::Read));

    // The sign-in gesture must remain usable while a wallet daemon is slow.
    session.login(QStringLiteral("mike"), QStringLiteral("pw"));
    secrets.replyRead(true, QStringLiteral("retired-token"));
    QTRY_VERIFY(lastSecretCallIs(secrets, test::FakeSecretsStore::CallType::Remove));
    secrets.replyRemove(true);
    QTRY_VERIFY(lastSecretCallIs(secrets, test::FakeSecretsStore::CallType::Write));
    secrets.replyWrite(true);

    QTRY_VERIFY(session.authenticated());
    QCOMPARE(m_client->accessToken(), kToken);
    QCOMPARE(m_client->userId(), kUserId);
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

    // A refresh that replaces every child model with same-cardinality content
    // must not republish an identical descriptor snapshot. QML consumes the
    // stable HomeRailModel, and compatibility listeners should be quiet too.
    QSignalSpy latestRailsSpy(&home, &HomeController::latestRailsChanged);
    home.refresh();
    QTRY_VERIFY(!home.busy());
    QCOMPARE(latestRailsSpy.count(), 0);
    QCOMPARE(home.latestRails().first().toMap()
                 .value(QStringLiteral("model")).value<MediaItemModel *>(),
             railModel);
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

    // The server's total is a snapshot. A library can grow between pages, and
    // a stale first-page total must not hide the newly reachable tail.
    m_mock->addRoute(
        QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
        QByteArrayLiteral(
            "{\"Items\":[{\"Id\":\"1\",\"Name\":\"One\",\"Type\":\"Movie\"},"
            "{\"Id\":\"2\",\"Name\":\"Two\",\"Type\":\"Movie\"},"
            "{\"Id\":\"3\",\"Name\":\"Three\",\"Type\":\"Movie\"}],"
            "\"TotalRecordCount\":250}"));
    library.loadMore();
    QTRY_VERIFY(!library.loading());
    QCOMPARE(library.model()->rowCount(), 6); // mock replays the same 3-item page
    QCOMPARE(library.model()->totalRecordCount(), 250);
    QVERIFY(library.canLoadMore());

    const auto secondRequest = m_mock->lastRequestFor(
        QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId));
    QVERIFY(secondRequest.query.contains(QStringLiteral("StartIndex=3")));
}

void ControllersTest::homeSessionResetRetiresOldCountersAndGenreCallbacks()
{
    const auto userB = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    const QString resumeA = QStringLiteral("/Users/%1/Items/Resume").arg(kUserId);
    const QString itemsA = QStringLiteral("/Users/%1/Items").arg(kUserId);
    const QString viewsA = QStringLiteral("/Users/%1/Views").arg(kUserId);

    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setSession(kToken, kUserId);
    m_mock->addRoute(QStringLiteral("GET"), resumeA, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"a-resume\",\"Name\":\"A "
                                       "Resume\",\"Type\":\"Movie\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Shows/NextUp"), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"), itemsA, 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"), viewsA, 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Genres"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"a-genre\",\"Name\":\"A "
                                       "Genre\",\"Type\":\"Genre\"}],"
                                       "\"TotalRecordCount\":1}"));
    for (const QString &path : {resumeA, QStringLiteral("/Shows/NextUp"), itemsA, viewsA,
                                QStringLiteral("/Genres")})
        m_mock->setRouteDelay(QStringLiteral("GET"), path, 350);

    HomeController home(m_client);
    home.refresh();
    home.loadGenreRails();
    QTRY_COMPARE_WITH_TIMEOUT(m_mock->requestCount(), 5, 5000);
    QVERIFY(home.busy());

    home.resetSessionState();
    QVERIFY(!home.busy());
    QCOMPARE(home.resume()->rowCount(), 0);
    QVERIFY(home.genreRails().isEmpty());

    m_client->setSession(kToken, userB);
    const QString resumeB = QStringLiteral("/Users/%1/Items/Resume").arg(userB);
    const QString itemsB = QStringLiteral("/Users/%1/Items").arg(userB);
    const QString viewsB = QStringLiteral("/Users/%1/Views").arg(userB);
    m_mock->addRoute(QStringLiteral("GET"), resumeB, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"b-resume\",\"Name\":\"B "
                                       "Resume\",\"Type\":\"Movie\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Shows/NextUp"), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"), itemsB, 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"), viewsB, 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    for (const QString &path : {resumeB, QStringLiteral("/Shows/NextUp"), itemsB, viewsB})
        m_mock->setRouteDelay(QStringLiteral("GET"), path, 0);

    home.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!home.busy(), 5000);
    QCOMPARE(home.resume()->rowCount(), 1);
    QCOMPARE(home.resume()->get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("B Resume"));

    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Genres"), 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"b-genre\",\"Name\":\"B "
                                       "Genre\",\"Type\":\"Genre\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->addRoute(QStringLiteral("GET"), itemsB, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"b-movie\",\"Name\":\"B "
                                       "Movie\",\"Type\":\"Movie\"}],"
                                       "\"TotalRecordCount\":1}"));
    m_mock->setRouteDelay(QStringLiteral("GET"), QStringLiteral("/Genres"), 0);
    home.loadGenreRails();
    QTRY_COMPARE_WITH_TIMEOUT(home.genreRails().size(), 1, 5000);
    QCOMPARE(home.genreRails().first().toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("B Genre"));

    // User A's five held replies land after B is already visible. They must not
    // decrement B's counters, repopulate A rows, or close B's genre load-once gate.
    QTest::qWait(450);
    QVERIFY(!home.busy());
    QCOMPARE(home.resume()->get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("B Resume"));
    QCOMPARE(home.genreRails().first().toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("B Genre"));
}

void ControllersTest::librarySessionResetRetiresOldPage()
{
    const auto userB = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    const QString itemsA = QStringLiteral("/Users/%1/Items").arg(kUserId);
    const QString itemsB = QStringLiteral("/Users/%1/Items").arg(userB);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setSession(kToken, kUserId);
    m_mock->addRoute(QStringLiteral("GET"), itemsA, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"a\",\"Name\":\"A Movie\","
                                       "\"Type\":\"Movie\"}],\"TotalRecordCount\":1}"));
    m_mock->setRouteDelay(QStringLiteral("GET"), itemsA, 350);

    LibraryController library(m_client);
    library.open(QStringLiteral("shared"), QStringLiteral("A Library"),
                 QStringLiteral("movies"));
    QTRY_COMPARE_WITH_TIMEOUT(m_mock->requestCount(), 1, 5000);
    QVERIFY(library.loading());

    library.resetSessionState();
    QVERIFY(!library.loading());
    QVERIFY(library.title().isEmpty());
    QVERIFY(library.scopeKey().isEmpty());
    QCOMPARE(library.model()->rowCount(), 0);

    m_client->setSession(kToken, userB);
    m_mock->addRoute(QStringLiteral("GET"), itemsB, 200,
                     QByteArrayLiteral("{\"Items\":[{\"Id\":\"b\",\"Name\":\"B Movie\","
                                       "\"Type\":\"Movie\"}],\"TotalRecordCount\":1}"));
    library.open(QStringLiteral("shared"), QStringLiteral("B Library"),
                 QStringLiteral("movies"));
    QTRY_VERIFY_WITH_TIMEOUT(!library.loading(), 5000);
    QCOMPARE(library.title(), QStringLiteral("B Library"));
    QCOMPARE(library.model()->rowCount(), 1);
    QCOMPARE(library.model()->get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("B Movie"));

    QTest::qWait(450);
    QCOMPARE(library.title(), QStringLiteral("B Library"));
    QCOMPARE(library.model()->get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("B Movie"));
}

QTEST_GUILESS_MAIN(ControllersTest)
#include "tst_controllers.moc"
