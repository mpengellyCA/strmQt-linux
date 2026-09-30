#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QEventLoop>
#include <QFile>
#include <QFutureWatcher>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "FakeDBusTransport.h"
#include "platform/SecretsStore.h"
#include "platform/secrets/DBusTransport.h"
#include "platform/secrets/KWalletBackend.h"
#include "platform/secrets/SecretBackend.h"
#include "platform/secrets/SecretServiceBackend.h"

#include <memory>
#include <utility>

Q_DECLARE_METATYPE(strmqt::secrets::Outcome)

using strmqt::Result;
using strmqt::SecretsStore;
using strmqt::secrets::BackendKind;
using strmqt::secrets::DBusSecret;
using strmqt::secrets::KWalletBackend;
using strmqt::secrets::Outcome;
using strmqt::secrets::Reply;
using strmqt::secrets::SecretServiceBackend;
using strmqt::secrets::StringMap;
using strmqt::test::FakeDBusTransport;

namespace {

const auto kKWallet6 = QStringLiteral("org.kde.kwalletd6");
const auto kKWallet5 = QStringLiteral("org.kde.kwalletd5");
const auto kSecrets = QStringLiteral("org.freedesktop.secrets");
const auto kBus = QStringLiteral("org.freedesktop.DBus");
const auto kKey = QStringLiteral("emby/a1b2c3d4e5f60718293a4b5c6d7e8f90/accessToken");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");

// Copied from tst_secrets_store.cpp.
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

// Records the one reply a backend hands its callback.
struct Captured
{
    int count = 0;
    Reply reply;
    strmqt::secrets::Callback callback()
    {
        return [this](const Reply &r) {
            ++count;
            reply = r;
        };
    }
};

QStringList kindNames(const QList<BackendKind> &kinds)
{
    QStringList names;
    for (const BackendKind kind : kinds)
        names.append(strmqt::secrets::backendKindName(kind));
    return names;
}

QDBusMessage errorReply(const QString &name)
{
    return QDBusMessage::createMethodCall(kBus, QStringLiteral("/"), kBus, QStringLiteral("Ping"))
        .createErrorReply(name, QStringLiteral("fake"));
}

QString dbusError(const char *name)
{
    return QStringLiteral("org.freedesktop.DBus.Error.") + QLatin1String(name);
}

// Asserts the oldest unanswered call is `member` on `service` at KWallet's path.
bool nextIsKWallet(const FakeDBusTransport &fake, int generation, const QString &member)
{
    if (fake.pending() < 0)
        return false;
    const QDBusMessage &m = fake.next();
    return m.service() == QStringLiteral("org.kde.kwalletd%1").arg(generation) &&
           m.path() == QStringLiteral("/modules/kwalletd%1").arg(generation) &&
           m.interface() == QStringLiteral("org.kde.KWallet") && m.member() == member;
}

// Answers the store's probes of org.freedesktop.DBus: ListNames, ListActivatableNames
// and, when both KWallet names are owned, GetNameOwner for each.
void answerProbe(FakeDBusTransport &fake, const QStringList &owned, const QStringList &activatable,
                 const QString &owner6 = QStringLiteral(":1.6"),
                 const QString &owner5 = QStringLiteral(":1.5"))
{
    QTRY_VERIFY(fake.pending() >= 0);
    QCOMPARE(fake.next().service(), kBus);
    QCOMPARE(fake.next().path(), QStringLiteral("/org/freedesktop/DBus"));
    QCOMPARE(fake.next().interface(), kBus);
    QCOMPARE(fake.next().member(), QStringLiteral("ListNames"));
    fake.reply({QVariant(owned)});
    QVERIFY(fake.pending() >= 0);
    QCOMPARE(fake.next().service(), kBus);
    QCOMPARE(fake.next().member(), QStringLiteral("ListActivatableNames"));
    fake.reply({QVariant(activatable)});
    if (!owned.contains(kKWallet6) || !owned.contains(kKWallet5))
        return;
    for (const auto &[name, owner] : {std::pair{kKWallet6, owner6}, std::pair{kKWallet5, owner5}}) {
        QVERIFY(fake.pending() >= 0);
        QCOMPARE(fake.next().service(), kBus);
        QCOMPARE(fake.next().member(), QStringLiteral("GetNameOwner"));
        QCOMPARE(fake.next().arguments(), QVariantList{name});
        fake.reply({owner});
    }
}

const auto kSecretsPath = QStringLiteral("/org/freedesktop/secrets");
const auto kServiceInterface = QStringLiteral("org.freedesktop.Secret.Service");
const auto kCollectionInterface = QStringLiteral("org.freedesktop.Secret.Collection");
const auto kItemInterface = QStringLiteral("org.freedesktop.Secret.Item");
const auto kPromptInterface = QStringLiteral("org.freedesktop.Secret.Prompt");
const auto kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const auto kSession = QStringLiteral("/org/freedesktop/secrets/session/s1");
const auto kCollection = QStringLiteral("/org/freedesktop/secrets/collection/login");
const auto kItem = QStringLiteral("/org/freedesktop/secrets/collection/login/1");
const auto kItem2 = QStringLiteral("/org/freedesktop/secrets/collection/login/2");
const auto kPrompt = QStringLiteral("/org/freedesktop/secrets/prompt/p1");
const auto kNoPrompt = QStringLiteral("/");

QVariant objectPath(const QString &path)
{
    return QVariant::fromValue(QDBusObjectPath(path));
}

QVariant objectPaths(const QStringList &paths)
{
    QList<QDBusObjectPath> list;
    for (const QString &path : paths)
        list.append(QDBusObjectPath(path));
    return QVariant::fromValue(list);
}

QVariant dbusVariant(const QVariant &value)
{
    return QVariant::fromValue(QDBusVariant(value));
}

QStringList pathsOf(const QVariant &value)
{
    QStringList paths;
    for (const QDBusObjectPath &path : value.value<QList<QDBusObjectPath>>())
        paths.append(path.path());
    return paths;
}

StringMap attributesFor(const QString &key)
{
    return {{QStringLiteral("xdg:schema"), QStringLiteral("ca.mikesdev.StrmQt.Secret")},
            {QStringLiteral("strmqt-key"), key}};
}

// Asserts the oldest unanswered call is `interface`.`member` at `path` on org.freedesktop.secrets.
bool nextIsSecrets(const FakeDBusTransport &fake, const QString &path, const QString &interface,
                   const QString &member)
{
    if (fake.pending() < 0)
        return false;
    const QDBusMessage &m = fake.next();
    return m.service() == kSecrets && m.path() == path && m.interface() == interface &&
           m.member() == member;
}

// Answers OpenSession and ReadAlias, then reports the collection unlocked.
void openSecretService(FakeDBusTransport &fake, SecretServiceBackend &backend)
{
    Captured prepared;
    backend.prepare(prepared.callback());
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("OpenSession")));
    fake.reply({dbusVariant(QString()), objectPath(kSession)});
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("ReadAlias")));
    fake.reply({objectPath(kCollection)});
    QCOMPARE(prepared.count, 1);
    QCOMPARE(prepared.reply.outcome, Outcome::Ok);

    Captured opened;
    backend.open(opened.callback());
    QVERIFY(nextIsSecrets(fake, kCollection, kPropertiesInterface, QStringLiteral("Get")));
    fake.reply({dbusVariant(false)});
    QCOMPARE(opened.count, 1);
    QCOMPARE(opened.reply.outcome, Outcome::Ok);
}

// The backend has just been handed prompt kPrompt: it must subscribe to its
// Completed signal, then call Prompt(""); the user answers `dismissed`, and the
// signal carries `result` (Unlock's `ao` unless given).
void answerPrompt(FakeDBusTransport &fake, bool dismissed, const QVariant &result = objectPaths({}))
{
    QVERIFY(!fake.subscriptions.isEmpty());
    const FakeDBusTransport::Subscription s = fake.subscriptions.last();
    QCOMPARE(s.service, kSecrets);
    QCOMPARE(s.path, kPrompt);
    QCOMPARE(s.interface, kPromptInterface);
    QCOMPARE(s.name, QStringLiteral("Completed"));
    QVERIFY(!s.context.isNull());

    QVERIFY(nextIsSecrets(fake, kPrompt, kPromptInterface, QStringLiteral("Prompt")));
    QCOMPARE(fake.next().arguments(), QVariantList{QString()});
    QCOMPARE(fake.calls[fake.pending()].timeoutMs, strmqt::secrets::kInteractiveTimeoutMs);
    fake.reply({});
    fake.emitSignal(kPrompt, kPromptInterface, QStringLiteral("Completed"),
                    {dismissed, dbusVariant(result)});
}

// Answers OpenSession and a ReadAlias of "/" (a fresh gnome-keyring account has no
// "login" keyring yet); opening then needs no call at all.
void openWithoutADefaultCollection(FakeDBusTransport &fake, SecretServiceBackend &backend)
{
    Captured prepared;
    backend.prepare(prepared.callback());
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("OpenSession")));
    fake.reply({dbusVariant(QString()), objectPath(kSession)});
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("ReadAlias")));
    fake.reply({objectPath(kNoPrompt)});
    QCOMPARE(prepared.count, 1);
    QCOMPARE(prepared.reply.outcome, Outcome::Ok);

    Captured opened;
    backend.open(opened.callback());
    QCOMPARE(opened.count, 1);
    QCOMPARE(opened.reply.outcome, Outcome::Ok);
    QCOMPARE(fake.pending(), -1);
}

// Asserts the oldest unanswered call is CreateCollection(Label "Login", alias "default").
void expectCreateCollection(FakeDBusTransport &fake)
{
    QVERIFY(
        nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("CreateCollection")));
    QCOMPARE(fake.calls[fake.pending()].timeoutMs, strmqt::secrets::kInteractiveTimeoutMs);
    const QVariantList args = fake.next().arguments();
    QCOMPARE(args.size(), 2);
    const QVariantMap properties = args.at(0).toMap();
    QCOMPARE(properties.size(), 1);
    QCOMPARE(properties.value(QStringLiteral("org.freedesktop.Secret.Collection.Label")).toString(),
             QStringLiteral("Login"));
    QCOMPARE(args.at(1).toString(), QStringLiteral("default"));
}

bool calledMember(const FakeDBusTransport &fake, const QString &member)
{
    for (const FakeDBusTransport::Call &call : fake.calls)
        if (call.message.member() == member)
            return true;
    return false;
}

} // namespace

class SecretBackendsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void chooseSecretBackendsTable_data();
    void chooseSecretBackendsTable();
    void unavailableErrors();
    void kwallet5SpeaksTheKWalletProtocolAtItsOwnPath();
    void kwallet6KeepsTodaysWireFormat();
    void kwalletOpenMinusOneIsRefused();
    void kwalletServiceUnknownIsUnavailable();
    void kwalletOtherErrorsFail();
    void kwalletOpenWaitsForTheUser_data();
    void kwalletOpenWaitsForTheUser();

    void storeFallsThroughAnUnavailableBackend();
    void storeReopensOnTheNextBackendWhenOneVanishes();
    void storeGoesToTheVaultWhenAKeyringRefuses();
    void storeGoesToTheVaultWhenTheWalletPromptTimesOut();
    void storeDropsTheKWallet5AliasOfKWallet6();
    void storeUsesTheVaultWithNoCandidates();
    void storeUsesTheVaultWithoutASessionBus();

    void secretServiceMarshalsTheSpecTypes();
    void secretServicePreparesAPlainSessionAndTheDefaultCollection();
    void secretServiceWithoutADefaultCollectionIsReady();
    void secretServiceOpenOfAnUnlockedCollectionNeedsNoPrompt();
    void secretServiceUnlockRunsThePrompt();
    void secretServiceDismissedPromptIsRefused();
    void secretServiceUnlockWaitsForTheUser_data();
    void secretServiceUnlockWaitsForTheUser();
    void secretServiceUnansweredPromptFails();
    void secretServiceWriteStoresOneItemPerKey();
    void secretServiceWriteRunsItsPrompt();
    void secretServiceReadFindsTheItemAndGetsItsSecret();
    void secretServiceReadOfAMissingKeyIsAnEmptySuccess();
    void secretServiceReadUnlocksLockedItems();
    void secretServiceRemoveDeletesEveryMatch();
    void secretServiceWriteCreatesTheMissingDefaultCollection();
    void secretServiceCreateCollectionRunsItsPrompt();
    void secretServiceDismissedCreateCollectionIsRefused();
    void secretServiceUnansweredCreateCollectionFails();
    void secretServiceCreateCollectionWithoutAPathFails_data();
    void secretServiceCreateCollectionWithoutAPathFails();
    void secretServiceCreateCollectionErrors_data();
    void secretServiceCreateCollectionErrors();
    void secretServiceReadWithoutADefaultCollectionCreatesNothing();
    void storeReachesTheSecretServiceOnGnome();
    void storeFallsFromKWalletToTheSecretService();
    void storeCreatesTheDefaultCollectionOnAFreshAccount();
    void storeGoesToTheVaultWhenTheNewKeyringIsDismissed();
    void storeDefersMigrationUntilACollectionExists();

private:
    void driveKWallet(int generation);

    QByteArray m_savedDesktop;
    bool m_hadDesktop = false;
    std::unique_ptr<QTemporaryDir> m_dir;
};

void SecretBackendsTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void SecretBackendsTest::init()
{
    m_hadDesktop = qEnvironmentVariableIsSet("XDG_CURRENT_DESKTOP");
    m_savedDesktop = qgetenv("XDG_CURRENT_DESKTOP");
    qputenv("XDG_CURRENT_DESKTOP", "KDE");
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
}

void SecretBackendsTest::cleanup()
{
    if (m_hadDesktop)
        qputenv("XDG_CURRENT_DESKTOP", m_savedDesktop);
    else
        qunsetenv("XDG_CURRENT_DESKTOP");
    m_dir.reset();
}

void SecretBackendsTest::chooseSecretBackendsTable_data()
{
    QTest::addColumn<QStringList>("owned");
    QTest::addColumn<QStringList>("activatable");
    QTest::addColumn<QString>("desktop");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("owned kwalletd6 counts on any desktop")
        << QStringList{kKWallet6} << QStringList{} << QStringLiteral("GNOME")
        << QStringList{QStringLiteral("KWallet6")};
    QTest::newRow("activatable kwalletd6 on KDE")
        << QStringList{} << QStringList{kKWallet6} << QStringLiteral("KDE")
        << QStringList{QStringLiteral("KWallet6")};
    QTest::newRow("activatable kwalletd6 not on GNOME")
        << QStringList{} << QStringList{kKWallet6} << QStringLiteral("GNOME") << QStringList{};
    QTest::newRow("desktop is a colon list")
        << QStringList{} << QStringList{kKWallet5} << QStringLiteral("ubuntu:KDE")
        << QStringList{QStringLiteral("KWallet5")};
    QTest::newRow("owned secret service")
        << QStringList{kSecrets} << QStringList{} << QStringLiteral("GNOME")
        << QStringList{QStringLiteral("SecretService")};
    QTest::newRow("activatable secret service on any desktop")
        << QStringList{} << QStringList{kSecrets} << QStringLiteral("XFCE")
        << QStringList{QStringLiteral("SecretService")};
    QTest::newRow("fixed order") << QStringList{kSecrets, kKWallet5, kKWallet6} << QStringList{}
                                 << QStringLiteral("KDE")
                                 << QStringList{QStringLiteral("KWallet6"),
                                                QStringLiteral("KWallet5"),
                                                QStringLiteral("SecretService")};
    QTest::newRow("nothing") << QStringList{} << QStringList{} << QStringLiteral("KDE")
                             << QStringList{};
}

void SecretBackendsTest::chooseSecretBackendsTable()
{
    QFETCH(QStringList, owned);
    QFETCH(QStringList, activatable);
    QFETCH(QString, desktop);
    QFETCH(QStringList, expected);
    QCOMPARE(kindNames(strmqt::secrets::chooseSecretBackends(owned, activatable, desktop)),
             expected);
}

void SecretBackendsTest::unavailableErrors()
{
    for (const char *name : {"ServiceUnknown", "NoReply", "UnknownObject", "UnknownMethod",
                             "NameHasNoOwner", "Timeout"})
        QVERIFY2(strmqt::secrets::isUnavailableError(errorReply(dbusError(name))), name);
    QVERIFY(!strmqt::secrets::isUnavailableError(errorReply(dbusError("AccessDenied"))));
    const QDBusMessage ok =
        QDBusMessage::createMethodCall(kBus, QStringLiteral("/"), kBus, QStringLiteral("Ping"))
            .createReply(QVariantList{});
    QVERIFY(!strmqt::secrets::isUnavailableError(ok));
}

void SecretBackendsTest::driveKWallet(int generation)
{
    FakeDBusTransport fake;
    QObject ctx;
    KWalletBackend backend(generation, fake, &ctx);
    const QString appId = strmqt::secrets::applicationId();

    Captured prepared;
    backend.prepare(prepared.callback());
    QCOMPARE(fake.calls.size(), 1);
    QVERIFY(nextIsKWallet(fake, generation, QStringLiteral("networkWallet")));
    QVERIFY(fake.next().arguments().isEmpty());
    QCOMPARE(fake.calls.last().timeoutMs, strmqt::secrets::kDefaultTimeoutMs);
    fake.reply({QStringLiteral("kdewallet")});
    QCOMPARE(prepared.count, 1);
    QCOMPARE(prepared.reply.outcome, Outcome::Ok);

    Captured opened;
    backend.open(opened.callback());
    QVERIFY(nextIsKWallet(fake, generation, QStringLiteral("open")));
    // open answers only once the user answers the unlock dialog.
    QCOMPARE(fake.calls.last().timeoutMs, strmqt::secrets::kInteractiveTimeoutMs);
    QCOMPARE(strmqt::secrets::kInteractiveTimeoutMs, 300000);
    QVariantList args = fake.next().arguments();
    QCOMPARE(args.size(), 3);
    QCOMPARE(args.at(0).toString(), QStringLiteral("kdewallet"));
    QCOMPARE(args.at(1).metaType().id(), int(QMetaType::LongLong));
    QCOMPARE(args.at(1).toLongLong(), 0);
    QCOMPARE(args.at(2).toString(), appId);
    fake.reply({7});
    QCOMPARE(opened.count, 1);
    QCOMPARE(opened.reply.outcome, Outcome::Ok);

    Captured written;
    backend.write(kKey, kToken, written.callback());
    QVERIFY(nextIsKWallet(fake, generation, QStringLiteral("writePassword")));
    QCOMPARE(fake.calls.last().timeoutMs, strmqt::secrets::kDefaultTimeoutMs);
    args = fake.next().arguments();
    QCOMPARE(args.size(), 5);
    QCOMPARE(args.at(0).metaType().id(), int(QMetaType::Int));
    QCOMPARE(args.at(0).toInt(), 7);
    QCOMPARE(args.at(1).toString(), QStringLiteral("StrmQt"));
    QCOMPARE(args.at(2).toString(), kKey);
    QCOMPARE(args.at(3).toString(), kToken);
    QCOMPARE(args.at(4).toString(), appId);
    fake.reply({0});
    QCOMPARE(written.count, 1);
    QCOMPARE(written.reply.outcome, Outcome::Ok);

    Captured read;
    backend.read(kKey, read.callback());
    QVERIFY(nextIsKWallet(fake, generation, QStringLiteral("readPassword")));
    args = fake.next().arguments();
    QCOMPARE(args.size(), 4);
    QCOMPARE(args.at(0).toInt(), 7);
    QCOMPARE(args.at(1).toString(), QStringLiteral("StrmQt"));
    QCOMPARE(args.at(2).toString(), kKey);
    QCOMPARE(args.at(3).toString(), appId);
    fake.reply({QStringLiteral("tok")});
    QCOMPARE(read.count, 1);
    QCOMPARE(read.reply.outcome, Outcome::Ok);
    QCOMPARE(read.reply.value, QStringLiteral("tok"));

    Captured removed;
    backend.remove(kKey, removed.callback());
    QVERIFY(nextIsKWallet(fake, generation, QStringLiteral("removeEntry")));
    args = fake.next().arguments();
    QCOMPARE(args.size(), 4);
    QCOMPARE(args.at(0).toInt(), 7);
    QCOMPARE(args.at(1).toString(), QStringLiteral("StrmQt"));
    QCOMPARE(args.at(2).toString(), kKey);
    QCOMPARE(args.at(3).toString(), appId);
    fake.reply({0});
    QCOMPARE(removed.count, 1);
    QCOMPARE(removed.reply.outcome, Outcome::Ok);
    QCOMPARE(fake.pending(), -1);
}

void SecretBackendsTest::kwallet5SpeaksTheKWalletProtocolAtItsOwnPath()
{
    driveKWallet(5);
    FakeDBusTransport fake;
    QCOMPARE(KWalletBackend(5, fake, nullptr).name(), QStringLiteral("KWallet 5"));
}

void SecretBackendsTest::kwallet6KeepsTodaysWireFormat()
{
    driveKWallet(6);
    FakeDBusTransport fake;
    QCOMPARE(KWalletBackend(6, fake, nullptr).name(), QStringLiteral("KWallet"));
}

void SecretBackendsTest::kwalletOpenMinusOneIsRefused()
{
    FakeDBusTransport fake;
    QObject ctx;
    KWalletBackend backend(6, fake, &ctx);
    Captured prepared;
    backend.prepare(prepared.callback());
    fake.reply({QStringLiteral("kdewallet")});
    Captured opened;
    backend.open(opened.callback());
    fake.reply({-1});
    QCOMPARE(opened.count, 1);
    QCOMPARE(opened.reply.outcome, Outcome::Refused);
}

void SecretBackendsTest::kwalletServiceUnknownIsUnavailable()
{
    FakeDBusTransport fake;
    QObject ctx;
    KWalletBackend backend(6, fake, &ctx);
    Captured prepared;
    backend.prepare(prepared.callback());
    fake.replyError(dbusError("ServiceUnknown"));
    QCOMPARE(prepared.count, 1);
    QCOMPARE(prepared.reply.outcome, Outcome::Unavailable);
}

void SecretBackendsTest::kwalletOtherErrorsFail()
{
    FakeDBusTransport fake;
    QObject ctx;
    KWalletBackend backend(5, fake, &ctx);
    Captured prepared;
    backend.prepare(prepared.callback());
    fake.replyError(dbusError("AccessDenied"));
    QCOMPARE(prepared.count, 1);
    QCOMPARE(prepared.reply.outcome, Outcome::Failed);

    // A malformed reply fails too.
    Captured again;
    backend.prepare(again.callback());
    fake.reply({42});
    QCOMPARE(again.count, 1);
    QCOMPARE(again.reply.outcome, Outcome::Failed);
}

void SecretBackendsTest::kwalletOpenWaitsForTheUser_data()
{
    QTest::addColumn<QString>("error");
    QTest::addColumn<Outcome>("expected");
    QTest::newRow("NoReply") << dbusError("NoReply") << Outcome::Failed;
    QTest::newRow("Timeout") << dbusError("Timeout") << Outcome::Failed;
    QTest::newRow("TimedOut") << dbusError("TimedOut") << Outcome::Failed;
    QTest::newRow("UnknownMethod") << dbusError("UnknownMethod") << Outcome::Failed;
    QTest::newRow("ServiceUnknown") << dbusError("ServiceUnknown") << Outcome::Unavailable;
    QTest::newRow("NameHasNoOwner") << dbusError("NameHasNoOwner") << Outcome::Unavailable;
    QTest::newRow("UnknownObject") << dbusError("UnknownObject") << Outcome::Unavailable;
}

void SecretBackendsTest::kwalletOpenWaitsForTheUser()
{
    QFETCH(QString, error);
    QFETCH(Outcome, expected);
    FakeDBusTransport fake;
    QObject ctx;
    KWalletBackend backend(6, fake, &ctx);
    Captured prepared;
    backend.prepare(prepared.callback());
    fake.reply({QStringLiteral("kdewallet")});
    Captured opened;
    backend.open(opened.callback());
    fake.replyError(error);
    QCOMPARE(opened.count, 1);
    QCOMPARE(opened.reply.outcome, expected);
}

void SecretBackendsTest::storeFallsThroughAnUnavailableBackend()
{
    auto fake = std::make_shared<FakeDBusTransport>();
    SecretsStore store(fake);
    const QString vault = m_dir->filePath(QStringLiteral("secrets.ini"));
    store.setLegacyFilePathForTests(vault);
    QCOMPARE(store.backendName(), QString());

    const QFuture<Result<bool>> write = store.writeSecret(kKey, kToken);
    // Two daemons (different owners): KWallet 5 stays a candidate.
    answerProbe(*fake, {kKWallet6, kKWallet5}, {});
    if (QTest::currentTestFailed())
        return;

    QVERIFY(nextIsKWallet(*fake, 6, QStringLiteral("networkWallet")));
    fake->replyError(dbusError("ServiceUnknown"));
    QTRY_VERIFY(nextIsKWallet(*fake, 5, QStringLiteral("networkWallet")));
    fake->reply({QStringLiteral("kdewallet")});
    QVERIFY(nextIsKWallet(*fake, 5, QStringLiteral("open")));
    fake->reply({7});
    QTRY_VERIFY(nextIsKWallet(*fake, 5, QStringLiteral("writePassword")));
    QCOMPARE(fake->next().arguments().at(2).toString(), kKey);
    fake->reply({0});

    QVERIFY(awaitResult(write).ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::Wallet);
    QCOMPARE(store.backendName(), QStringLiteral("KWallet 5"));
    QVERIFY(!QFile::exists(vault));
}

void SecretBackendsTest::storeReopensOnTheNextBackendWhenOneVanishes()
{
    auto fake = std::make_shared<FakeDBusTransport>();
    SecretsStore store(fake);
    store.setLegacyFilePathForTests(m_dir->filePath(QStringLiteral("secrets.ini")));

    const QFuture<Result<QString>> read = store.readSecret(kKey);
    answerProbe(*fake, {kKWallet6, kKWallet5}, {});
    if (QTest::currentTestFailed())
        return;

    QVERIFY(nextIsKWallet(*fake, 6, QStringLiteral("networkWallet")));
    fake->reply({QStringLiteral("kdewallet")});
    QVERIFY(nextIsKWallet(*fake, 6, QStringLiteral("open")));
    fake->replyError(dbusError("ServiceUnknown"));
    QTRY_VERIFY(nextIsKWallet(*fake, 5, QStringLiteral("networkWallet")));
    fake->reply({QStringLiteral("kdewallet")});
    QVERIFY(nextIsKWallet(*fake, 5, QStringLiteral("open")));
    fake->reply({3});
    QTRY_VERIFY(nextIsKWallet(*fake, 5, QStringLiteral("readPassword")));
    QCOMPARE(fake->next().arguments().at(0).toInt(), 3);
    fake->reply({kToken});

    const Result<QString> result = awaitResult(read);
    QVERIFY(result.ok());
    QCOMPARE(result.value, kToken);
    QCOMPARE(store.backendName(), QStringLiteral("KWallet 5"));
}

void SecretBackendsTest::storeGoesToTheVaultWhenAKeyringRefuses()
{
    auto fake = std::make_shared<FakeDBusTransport>();
    SecretsStore store(fake);
    const QString vault = m_dir->filePath(QStringLiteral("secrets.ini"));
    store.setLegacyFilePathForTests(vault);
    QSignalSpy modeChanged(&store, &SecretsStore::storageModeChanged);

    const QFuture<Result<bool>> write = store.writeSecret(kKey, kToken);
    answerProbe(*fake, {kKWallet6, kKWallet5, kSecrets}, {});
    if (QTest::currentTestFailed())
        return;

    QVERIFY(nextIsKWallet(*fake, 6, QStringLiteral("networkWallet")));
    fake->reply({QStringLiteral("kdewallet")});
    QVERIFY(nextIsKWallet(*fake, 6, QStringLiteral("open")));
    fake->reply({-1});

    QVERIFY(awaitResult(write).ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::PlaintextFallback);
    QCOMPARE(store.backendName(), QStringLiteral("vault file"));
    QCOMPARE(modeChanged.count(), 1);
    QCOMPARE(fake->calls.size(), 6);
    for (const FakeDBusTransport::Call &call : std::as_const(fake->calls))
        QVERIFY2(call.message.service() == kBus || call.message.service() == kKWallet6,
                 qPrintable(call.message.service()));
    QCOMPARE(QSettings(vault, QSettings::IniFormat).value(kKey).toString(), kToken);
}

void SecretBackendsTest::storeGoesToTheVaultWhenTheWalletPromptTimesOut()
{
    auto fake = std::make_shared<FakeDBusTransport>();
    SecretsStore store(fake);
    const QString vault = m_dir->filePath(QStringLiteral("secrets.ini"));
    store.setLegacyFilePathForTests(vault);

    const QFuture<Result<bool>> write = store.writeSecret(kKey, kToken);
    // A separate kwalletd5 (different owner) is a real candidate, and still not asked.
    answerProbe(*fake, {kKWallet6, kKWallet5, kSecrets}, {});
    if (QTest::currentTestFailed())
        return;

    QVERIFY(nextIsKWallet(*fake, 6, QStringLiteral("networkWallet")));
    fake->reply({QStringLiteral("kdewallet")});
    QVERIFY(nextIsKWallet(*fake, 6, QStringLiteral("open")));
    fake->replyError(dbusError("NoReply"));
    // A cascade would leave the write waiting on another keyring.
    QTRY_VERIFY(write.isFinished());

    QVERIFY(awaitResult(write).ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::PlaintextFallback);
    QCOMPARE(store.backendName(), QStringLiteral("vault file"));
    for (const FakeDBusTransport::Call &call : std::as_const(fake->calls))
        QVERIFY2(call.message.service() == kBus || call.message.service() == kKWallet6,
                 qPrintable(call.message.service()));
    QCOMPARE(fake->pending(), -1);
    QCOMPARE(QSettings(vault, QSettings::IniFormat).value(kKey).toString(), kToken);
}

void SecretBackendsTest::storeDropsTheKWallet5AliasOfKWallet6()
{
    auto fake = std::make_shared<FakeDBusTransport>();
    SecretsStore store(fake);
    store.setLegacyFilePathForTests(m_dir->filePath(QStringLiteral("secrets.ini")));

    const QFuture<Result<bool>> write = store.writeSecret(kKey, kToken);
    // Plasma 6: kwalletd6 also owns org.kde.kwalletd5.
    answerProbe(*fake, {kKWallet6, kKWallet5}, {}, QStringLiteral(":1.42"),
                QStringLiteral(":1.42"));
    if (QTest::currentTestFailed())
        return;

    QVERIFY(nextIsKWallet(*fake, 6, QStringLiteral("networkWallet")));
    fake->replyError(dbusError("ServiceUnknown"));

    QTRY_VERIFY(write.isFinished());
    QVERIFY(awaitResult(write).ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::PlaintextFallback);
    QCOMPARE(fake->calls.size(), 5);
    for (const FakeDBusTransport::Call &call : std::as_const(fake->calls))
        QVERIFY2(call.message.service() != kKWallet5, qPrintable(call.message.member()));
}

void SecretBackendsTest::storeUsesTheVaultWithNoCandidates()
{
    auto fake = std::make_shared<FakeDBusTransport>();
    SecretsStore store(fake);
    const QString vault = m_dir->filePath(QStringLiteral("secrets.ini"));
    store.setLegacyFilePathForTests(vault);

    const QFuture<Result<bool>> write = store.writeSecret(kKey, kToken);
    answerProbe(*fake, {}, {});
    if (QTest::currentTestFailed())
        return;

    QVERIFY(awaitResult(write).ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::PlaintextFallback);
    QCOMPARE(store.backendName(), QStringLiteral("vault file"));
    QCOMPARE(fake->calls.size(), 2);
    for (const FakeDBusTransport::Call &call : std::as_const(fake->calls))
        QCOMPARE(call.message.service(), kBus);
}

void SecretBackendsTest::storeUsesTheVaultWithoutASessionBus()
{
    auto fake = std::make_shared<FakeDBusTransport>();
    fake->busConnected = false;
    SecretsStore store(fake);
    store.setLegacyFilePathForTests(m_dir->filePath(QStringLiteral("secrets.ini")));

    QVERIFY(awaitResult(store.writeSecret(kKey, kToken)).ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::PlaintextFallback);
    QCOMPARE(store.backendName(), QStringLiteral("vault file"));
    QVERIFY(fake->calls.isEmpty());
}

void SecretBackendsTest::secretServiceMarshalsTheSpecTypes()
{
    FakeDBusTransport fake;
    const SecretServiceBackend backend(fake, nullptr); // registers the D-Bus types
    QCOMPARE(backend.name(), QStringLiteral("Secret Service"));
    QCOMPARE(QDBusMetaType::typeToSignature(QMetaType::fromType<DBusSecret>()),
             QByteArray("(oayays)"));
    QCOMPARE(QDBusMetaType::typeToSignature(QMetaType::fromType<StringMap>()), QByteArray("a{ss}"));

    // A reply from the bus arrives as a QDBusArgument; a test's as the plain value.
    QCOMPARE(strmqt::secrets::fromDBus<QDBusObjectPath>(objectPath(kItem)).path(), kItem);
    QCOMPARE(strmqt::secrets::fromDBus<QDBusVariant>(dbusVariant(true)).variant().toBool(), true);
}

void SecretBackendsTest::secretServicePreparesAPlainSessionAndTheDefaultCollection()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);

    Captured prepared;
    backend.prepare(prepared.callback());
    QCOMPARE(fake.calls.size(), 1);
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("OpenSession")));
    QVariantList args = fake.next().arguments();
    QCOMPARE(args.size(), 2);
    QCOMPARE(args.at(0).toString(), QStringLiteral("plain"));
    QCOMPARE(args.at(1).metaType(), QMetaType::fromType<QDBusVariant>());
    const QVariant input = args.at(1).value<QDBusVariant>().variant();
    QCOMPARE(input.metaType(), QMetaType::fromType<QString>());
    QCOMPARE(input.toString(), QString());
    fake.reply({dbusVariant(QString()), objectPath(kSession)});
    QCOMPARE(prepared.count, 0);

    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("ReadAlias")));
    QCOMPARE(fake.next().arguments(), QVariantList{QStringLiteral("default")});
    fake.reply({objectPath(kCollection)});
    QCOMPARE(prepared.count, 1);
    QCOMPARE(prepared.reply.outcome, Outcome::Ok);
    QCOMPARE(fake.pending(), -1);

    // The session and collection from prepare() are what later steps use.
    Captured opened;
    backend.open(opened.callback());
    QCOMPARE(fake.next().path(), kCollection);
    fake.reply({dbusVariant(false)});
    Captured written;
    backend.write(kKey, kToken, written.callback());
    QCOMPARE(fake.next().path(), kCollection);
    QCOMPARE(fake.next().arguments().at(1).value<DBusSecret>().session.path(), kSession);
}

void SecretBackendsTest::secretServiceWithoutADefaultCollectionIsReady()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    // A reachable keyring with no default collection is still usable: the
    // first write creates it, so neither prepare nor open prompts.
    openWithoutADefaultCollection(fake, backend);
    if (QTest::currentTestFailed())
        return;
    QVERIFY(fake.subscriptions.isEmpty());
    QVERIFY(!backend.hasStorage());

    // A daemon that is not there is unavailable too.
    Captured again;
    backend.prepare(again.callback());
    fake.replyError(dbusError("ServiceUnknown"));
    QCOMPARE(again.count, 1);
    QCOMPARE(again.reply.outcome, Outcome::Unavailable);
}

void SecretBackendsTest::secretServiceOpenOfAnUnlockedCollectionNeedsNoPrompt()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    Captured prepared;
    backend.prepare(prepared.callback());
    fake.reply({dbusVariant(QString()), objectPath(kSession)});
    fake.reply({objectPath(kCollection)});

    Captured opened;
    backend.open(opened.callback());
    QVERIFY(nextIsSecrets(fake, kCollection, kPropertiesInterface, QStringLiteral("Get")));
    QCOMPARE(fake.next().arguments(),
             (QVariantList{kCollectionInterface, QStringLiteral("Locked")}));
    fake.reply({dbusVariant(false)});
    QCOMPARE(opened.count, 1);
    QCOMPARE(opened.reply.outcome, Outcome::Ok);
    QCOMPARE(fake.pending(), -1);
    for (const FakeDBusTransport::Call &call : std::as_const(fake.calls))
        QVERIFY(call.message.member() != QLatin1String("Unlock"));
    QVERIFY(fake.subscriptions.isEmpty());
}

void SecretBackendsTest::secretServiceUnlockRunsThePrompt()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    Captured prepared;
    backend.prepare(prepared.callback());
    fake.reply({dbusVariant(QString()), objectPath(kSession)});
    fake.reply({objectPath(kCollection)});

    Captured opened;
    backend.open(opened.callback());
    fake.reply({dbusVariant(true)});
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("Unlock")));
    const QVariantList args = fake.next().arguments();
    QCOMPARE(args.size(), 1);
    QCOMPARE(pathsOf(args.at(0)), QStringList{kCollection});
    QCOMPARE(fake.calls[fake.pending()].timeoutMs, strmqt::secrets::kInteractiveTimeoutMs);
    QVERIFY(fake.subscriptions.isEmpty());
    fake.reply({objectPaths({}), objectPath(kPrompt)});
    QCOMPARE(opened.count, 0);

    answerPrompt(fake, false);
    if (QTest::currentTestFailed())
        return;
    QCOMPARE(opened.count, 1);
    QCOMPARE(opened.reply.outcome, Outcome::Ok);
    QCOMPARE(fake.pending(), -1);

    // A repeated signal does not complete the step twice, and the subscription goes away.
    fake.emitSignal(kPrompt, kPromptInterface, QStringLiteral("Completed"),
                    {false, dbusVariant(objectPaths({}))});
    QCOMPARE(opened.count, 1);
    QTRY_VERIFY(fake.subscriptions.last().context.isNull());
}

void SecretBackendsTest::secretServiceDismissedPromptIsRefused()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    Captured prepared;
    backend.prepare(prepared.callback());
    fake.reply({dbusVariant(QString()), objectPath(kSession)});
    fake.reply({objectPath(kCollection)});

    Captured opened;
    backend.open(opened.callback());
    fake.reply({dbusVariant(true)});
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("Unlock")));
    fake.reply({objectPaths({}), objectPath(kPrompt)});
    answerPrompt(fake, true);
    if (QTest::currentTestFailed())
        return;
    QCOMPARE(opened.count, 1);
    QCOMPARE(opened.reply.outcome, Outcome::Refused);
    QCOMPARE(fake.pending(), -1);
}

void SecretBackendsTest::secretServiceUnlockWaitsForTheUser_data()
{
    QTest::addColumn<QString>("error");
    QTest::addColumn<Outcome>("expected");
    QTest::newRow("NoReply") << dbusError("NoReply") << Outcome::Failed;
    QTest::newRow("Timeout") << dbusError("Timeout") << Outcome::Failed;
    QTest::newRow("AccessDenied") << dbusError("AccessDenied") << Outcome::Failed;
    QTest::newRow("ServiceUnknown") << dbusError("ServiceUnknown") << Outcome::Unavailable;
    QTest::newRow("NameHasNoOwner") << dbusError("NameHasNoOwner") << Outcome::Unavailable;
    QTest::newRow("UnknownObject") << dbusError("UnknownObject") << Outcome::Unavailable;
}

void SecretBackendsTest::secretServiceUnlockWaitsForTheUser()
{
    QFETCH(QString, error);
    QFETCH(Outcome, expected);
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    Captured prepared;
    backend.prepare(prepared.callback());
    fake.reply({dbusVariant(QString()), objectPath(kSession)});
    fake.reply({objectPath(kCollection)});
    Captured opened;
    backend.open(opened.callback());
    fake.reply({dbusVariant(true)});
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("Unlock")));
    // A slow user is not a vanished daemon: only a daemon that is gone moves on.
    fake.replyError(error);
    QCOMPARE(opened.count, 1);
    QCOMPARE(opened.reply.outcome, expected);
}

void SecretBackendsTest::secretServiceUnansweredPromptFails()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    backend.setPromptTimeoutMs(20);
    Captured prepared;
    backend.prepare(prepared.callback());
    fake.reply({dbusVariant(QString()), objectPath(kSession)});
    fake.reply({objectPath(kCollection)});
    Captured opened;
    backend.open(opened.callback());
    fake.reply({dbusVariant(true)});
    fake.reply({objectPaths({}), objectPath(kPrompt)});
    QVERIFY(nextIsSecrets(fake, kPrompt, kPromptInterface, QStringLiteral("Prompt")));
    fake.reply({});
    QCOMPARE(opened.count, 0);

    // Nobody answers: the step fails (the vault), it does not try another keyring.
    QTRY_COMPARE(opened.count, 1);
    QCOMPARE(opened.reply.outcome, Outcome::Failed);
    // ...and takes the keyring's dialog down, so a late answer stores nothing.
    QVERIFY(nextIsSecrets(fake, kPrompt, kPromptInterface, QStringLiteral("Dismiss")));
    QCOMPARE(fake.next().arguments(), QVariantList{});
    fake.reply({});
    fake.emitSignal(kPrompt, kPromptInterface, QStringLiteral("Completed"),
                    {false, dbusVariant(objectPaths({}))});
    QCOMPARE(opened.count, 1);
    QTRY_VERIFY(fake.subscriptions.last().context.isNull());
}

void SecretBackendsTest::secretServiceWriteStoresOneItemPerKey()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    openSecretService(fake, backend);
    if (QTest::currentTestFailed())
        return;

    Captured written;
    backend.write(kKey, kToken, written.callback());
    QVERIFY(nextIsSecrets(fake, kCollection, kCollectionInterface, QStringLiteral("CreateItem")));
    QCOMPARE(fake.calls[fake.pending()].timeoutMs, strmqt::secrets::kInteractiveTimeoutMs);
    const QVariantList args = fake.next().arguments();
    QCOMPARE(args.size(), 3);

    const QVariantMap properties = args.at(0).toMap();
    QCOMPARE(properties.size(), 2);
    QCOMPARE(properties.value(QStringLiteral("org.freedesktop.Secret.Item.Label")).toString(),
             QStringLiteral("StrmQt access token"));
    const QVariant attributes =
        properties.value(QStringLiteral("org.freedesktop.Secret.Item.Attributes"));
    QCOMPARE(attributes.metaType(), QMetaType::fromType<StringMap>());
    // The hashed key and nothing else: no server URL, no user name.
    QCOMPARE(attributes.value<StringMap>(), attributesFor(kKey));

    QCOMPARE(args.at(1).metaType(), QMetaType::fromType<DBusSecret>());
    const DBusSecret secret = args.at(1).value<DBusSecret>();
    QCOMPARE(secret.session.path(), kSession);
    QCOMPARE(secret.parameters, QByteArray());
    QCOMPARE(secret.value, kToken.toUtf8());
    QCOMPARE(secret.contentType, QStringLiteral("text/plain; charset=utf8"));
    QCOMPARE(args.at(2).metaType(), QMetaType::fromType<bool>());
    QCOMPARE(args.at(2).toBool(), true);

    fake.reply({objectPath(kItem), objectPath(kNoPrompt)});
    QCOMPARE(written.count, 1);
    QCOMPARE(written.reply.outcome, Outcome::Ok);
    QCOMPARE(fake.pending(), -1);
    QVERIFY(fake.subscriptions.isEmpty());
}

void SecretBackendsTest::secretServiceWriteRunsItsPrompt()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    openSecretService(fake, backend);
    if (QTest::currentTestFailed())
        return;

    Captured written;
    backend.write(kKey, kToken, written.callback());
    QVERIFY(nextIsSecrets(fake, kCollection, kCollectionInterface, QStringLiteral("CreateItem")));
    fake.reply({objectPath(kNoPrompt), objectPath(kPrompt)});
    answerPrompt(fake, false);
    if (QTest::currentTestFailed())
        return;
    QCOMPARE(written.count, 1);
    QCOMPARE(written.reply.outcome, Outcome::Ok);

    Captured refused;
    backend.write(kKey, kToken, refused.callback());
    fake.reply({objectPath(kNoPrompt), objectPath(kPrompt)});
    answerPrompt(fake, true);
    if (QTest::currentTestFailed())
        return;
    QCOMPARE(refused.count, 1);
    QCOMPARE(refused.reply.outcome, Outcome::Refused);
}

void SecretBackendsTest::secretServiceReadFindsTheItemAndGetsItsSecret()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    openSecretService(fake, backend);
    if (QTest::currentTestFailed())
        return;

    Captured read;
    backend.read(kKey, read.callback());
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("SearchItems")));
    QVariantList args = fake.next().arguments();
    QCOMPARE(args.size(), 1);
    QCOMPARE(args.at(0).metaType(), QMetaType::fromType<StringMap>());
    QCOMPARE(args.at(0).value<StringMap>(), attributesFor(kKey));
    fake.reply({objectPaths({kItem}), objectPaths({})});

    QVERIFY(nextIsSecrets(fake, kItem, kItemInterface, QStringLiteral("GetSecret")));
    args = fake.next().arguments();
    QCOMPARE(args.size(), 1);
    QCOMPARE(args.at(0).value<QDBusObjectPath>().path(), kSession);
    QCOMPARE(read.count, 0);
    fake.reply({QVariant::fromValue(DBusSecret{
        QDBusObjectPath(kSession), {}, "tok", QStringLiteral("text/plain; charset=utf8")})});
    QCOMPARE(read.count, 1);
    QCOMPARE(read.reply.outcome, Outcome::Ok);
    QCOMPARE(read.reply.value, QStringLiteral("tok"));
    QCOMPARE(fake.pending(), -1);
}

void SecretBackendsTest::secretServiceReadOfAMissingKeyIsAnEmptySuccess()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    openSecretService(fake, backend);
    if (QTest::currentTestFailed())
        return;

    Captured read;
    backend.read(kKey, read.callback());
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("SearchItems")));
    fake.reply({objectPaths({}), objectPaths({})});
    QCOMPARE(read.count, 1);
    QCOMPARE(read.reply.outcome, Outcome::Ok);
    QVERIFY(read.reply.value.isEmpty());
    QCOMPARE(fake.pending(), -1);
}

void SecretBackendsTest::secretServiceReadUnlocksLockedItems()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    openSecretService(fake, backend);
    if (QTest::currentTestFailed())
        return;

    Captured read;
    backend.read(kKey, read.callback());
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("SearchItems")));
    fake.reply({objectPaths({}), objectPaths({kItem})});
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("Unlock")));
    QCOMPARE(pathsOf(fake.next().arguments().at(0)), QStringList{kItem});
    fake.reply({objectPaths({kItem}), objectPath(kNoPrompt)});
    QVERIFY(nextIsSecrets(fake, kItem, kItemInterface, QStringLiteral("GetSecret")));
    fake.reply({QVariant::fromValue(DBusSecret{
        QDBusObjectPath(kSession), {}, "tok", QStringLiteral("text/plain; charset=utf8")})});
    QCOMPARE(read.count, 1);
    QCOMPARE(read.reply.outcome, Outcome::Ok);
    QCOMPARE(read.reply.value, QStringLiteral("tok"));
}

void SecretBackendsTest::secretServiceRemoveDeletesEveryMatch()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    openSecretService(fake, backend);
    if (QTest::currentTestFailed())
        return;

    Captured removed;
    backend.remove(kKey, removed.callback());
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("SearchItems")));
    QCOMPARE(fake.next().arguments().at(0).value<StringMap>(), attributesFor(kKey));
    fake.reply({objectPaths({kItem, kItem2}), objectPaths({})});
    for (const QString &item : {kItem, kItem2}) {
        QVERIFY(nextIsSecrets(fake, item, kItemInterface, QStringLiteral("Delete")));
        QVERIFY(fake.next().arguments().isEmpty());
        QCOMPARE(removed.count, 0);
        fake.reply({objectPath(kNoPrompt)});
    }
    QCOMPARE(removed.count, 1);
    QCOMPARE(removed.reply.outcome, Outcome::Ok);
    QCOMPARE(fake.pending(), -1);

    // No matches: success, and nothing to delete.
    const int before = fake.calls.size();
    Captured none;
    backend.remove(kKey, none.callback());
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("SearchItems")));
    fake.reply({objectPaths({}), objectPaths({})});
    QCOMPARE(none.count, 1);
    QCOMPARE(none.reply.outcome, Outcome::Ok);
    QCOMPARE(fake.calls.size(), before + 1);
}

void SecretBackendsTest::storeReachesTheSecretServiceOnGnome()
{
    qputenv("XDG_CURRENT_DESKTOP", "GNOME");
    auto fake = std::make_shared<FakeDBusTransport>();
    SecretsStore store(fake);
    const QString vault = m_dir->filePath(QStringLiteral("secrets.ini"));
    store.setLegacyFilePathForTests(vault);

    const QFuture<Result<bool>> write = store.writeSecret(kKey, kToken);
    answerProbe(*fake, {kSecrets}, {});
    if (QTest::currentTestFailed())
        return;

    QTRY_VERIFY(
        nextIsSecrets(*fake, kSecretsPath, kServiceInterface, QStringLiteral("OpenSession")));
    fake->reply({dbusVariant(QString()), objectPath(kSession)});
    QVERIFY(nextIsSecrets(*fake, kSecretsPath, kServiceInterface, QStringLiteral("ReadAlias")));
    fake->reply({objectPath(kCollection)});
    QTRY_VERIFY(nextIsSecrets(*fake, kCollection, kPropertiesInterface, QStringLiteral("Get")));
    fake->reply({dbusVariant(false)});
    QTRY_VERIFY(
        nextIsSecrets(*fake, kCollection, kCollectionInterface, QStringLiteral("CreateItem")));
    QCOMPARE(fake->next().arguments().at(1).value<DBusSecret>().value, kToken.toUtf8());
    fake->reply({objectPath(kItem), objectPath(kNoPrompt)});

    QVERIFY(awaitResult(write).ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::Wallet);
    QCOMPARE(store.backendName(), QStringLiteral("Secret Service"));
    QVERIFY(!QFile::exists(vault));
    QCOMPARE(fake->pending(), -1);
}

void SecretBackendsTest::storeFallsFromKWalletToTheSecretService()
{
    auto fake = std::make_shared<FakeDBusTransport>();
    SecretsStore store(fake);
    const QString vault = m_dir->filePath(QStringLiteral("secrets.ini"));
    store.setLegacyFilePathForTests(vault);

    const QFuture<Result<bool>> write = store.writeSecret(kKey, kToken);
    answerProbe(*fake, {kKWallet6, kSecrets}, {});
    if (QTest::currentTestFailed())
        return;

    QVERIFY(nextIsKWallet(*fake, 6, QStringLiteral("networkWallet")));
    fake->replyError(dbusError("ServiceUnknown"));
    QTRY_VERIFY(
        nextIsSecrets(*fake, kSecretsPath, kServiceInterface, QStringLiteral("OpenSession")));
    fake->reply({dbusVariant(QString()), objectPath(kSession)});
    QVERIFY(nextIsSecrets(*fake, kSecretsPath, kServiceInterface, QStringLiteral("ReadAlias")));
    fake->reply({objectPath(kCollection)});
    QTRY_VERIFY(nextIsSecrets(*fake, kCollection, kPropertiesInterface, QStringLiteral("Get")));
    fake->reply({dbusVariant(false)});
    QTRY_VERIFY(
        nextIsSecrets(*fake, kCollection, kCollectionInterface, QStringLiteral("CreateItem")));
    fake->reply({objectPath(kItem), objectPath(kNoPrompt)});

    QVERIFY(awaitResult(write).ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::Wallet);
    QCOMPARE(store.backendName(), QStringLiteral("Secret Service"));
    QVERIFY(!QFile::exists(vault));
}

void SecretBackendsTest::secretServiceWriteCreatesTheMissingDefaultCollection()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    openWithoutADefaultCollection(fake, backend);
    if (QTest::currentTestFailed())
        return;

    Captured written;
    backend.write(kKey, kToken, written.callback());
    expectCreateCollection(fake);
    if (QTest::currentTestFailed())
        return;
    fake.reply({objectPath(kCollection), objectPath(kNoPrompt)});
    // As if ReadAlias had found it: the unlock check, then the item goes there.
    QVERIFY(nextIsSecrets(fake, kCollection, kPropertiesInterface, QStringLiteral("Get")));
    fake.reply({dbusVariant(false)});
    QVERIFY(nextIsSecrets(fake, kCollection, kCollectionInterface, QStringLiteral("CreateItem")));
    QCOMPARE(fake.next().arguments().at(1).value<DBusSecret>().value, kToken.toUtf8());
    QCOMPARE(written.count, 0);
    fake.reply({objectPath(kItem), objectPath(kNoPrompt)});
    QCOMPARE(written.count, 1);
    QCOMPARE(written.reply.outcome, Outcome::Ok);
    QCOMPARE(fake.pending(), -1);
    QVERIFY(fake.subscriptions.isEmpty());
    QVERIFY(backend.hasStorage());

    // Created once: the next write goes straight to the collection.
    const int before = fake.calls.size();
    Captured again;
    backend.write(kKey, kToken, again.callback());
    QVERIFY(nextIsSecrets(fake, kCollection, kCollectionInterface, QStringLiteral("CreateItem")));
    fake.reply({objectPath(kItem), objectPath(kNoPrompt)});
    QCOMPARE(again.reply.outcome, Outcome::Ok);
    QCOMPARE(fake.calls.size(), before + 1);
}

void SecretBackendsTest::secretServiceCreateCollectionRunsItsPrompt()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    openWithoutADefaultCollection(fake, backend);
    if (QTest::currentTestFailed())
        return;

    Captured written;
    backend.write(kKey, kToken, written.callback());
    expectCreateCollection(fake);
    if (QTest::currentTestFailed())
        return;
    // gnome-keyring asks for the new keyring's password; the path comes with Completed.
    fake.reply({objectPath(kNoPrompt), objectPath(kPrompt)});
    answerPrompt(fake, false, objectPath(kCollection));
    if (QTest::currentTestFailed())
        return;
    QCOMPARE(written.count, 0);
    QVERIFY(nextIsSecrets(fake, kCollection, kPropertiesInterface, QStringLiteral("Get")));
    fake.reply({dbusVariant(false)});
    QVERIFY(nextIsSecrets(fake, kCollection, kCollectionInterface, QStringLiteral("CreateItem")));
    fake.reply({objectPath(kItem), objectPath(kNoPrompt)});
    QCOMPARE(written.count, 1);
    QCOMPARE(written.reply.outcome, Outcome::Ok);
    QCOMPARE(fake.pending(), -1);
}

void SecretBackendsTest::secretServiceDismissedCreateCollectionIsRefused()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    openWithoutADefaultCollection(fake, backend);
    if (QTest::currentTestFailed())
        return;

    Captured written;
    backend.write(kKey, kToken, written.callback());
    expectCreateCollection(fake);
    if (QTest::currentTestFailed())
        return;
    fake.reply({objectPath(kNoPrompt), objectPath(kPrompt)});
    answerPrompt(fake, true, objectPath(kNoPrompt));
    if (QTest::currentTestFailed())
        return;
    QCOMPARE(written.count, 1);
    QCOMPARE(written.reply.outcome, Outcome::Refused);
    QCOMPARE(fake.pending(), -1);
    QVERIFY(!calledMember(fake, QStringLiteral("CreateItem")));

    // The refusal sticks: the next write answers it without asking again.
    const int before = fake.calls.size();
    const int subscribed = fake.subscriptions.size();
    Captured again;
    backend.write(kKey, kToken, again.callback());
    QCOMPARE(again.count, 1);
    QCOMPARE(again.reply.outcome, Outcome::Refused);
    QCOMPARE(fake.calls.size(), before);
    QCOMPARE(fake.subscriptions.size(), subscribed);
}

void SecretBackendsTest::secretServiceUnansweredCreateCollectionFails()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    openWithoutADefaultCollection(fake, backend);
    if (QTest::currentTestFailed())
        return;

    // Unanswered: the step fails and the dialog is taken down; no item.
    backend.setPromptTimeoutMs(20);
    Captured unanswered;
    backend.write(kKey, kToken, unanswered.callback());
    expectCreateCollection(fake);
    if (QTest::currentTestFailed())
        return;
    fake.reply({objectPath(kNoPrompt), objectPath(kPrompt)});
    QVERIFY(nextIsSecrets(fake, kPrompt, kPromptInterface, QStringLiteral("Prompt")));
    fake.reply({});
    QTRY_COMPARE(unanswered.count, 1);
    QCOMPARE(unanswered.reply.outcome, Outcome::Failed);
    QVERIFY(nextIsSecrets(fake, kPrompt, kPromptInterface, QStringLiteral("Dismiss")));
    fake.reply({});
    QVERIFY(!calledMember(fake, QStringLiteral("CreateItem")));

    // ...and is not asked again by this backend.
    const int before = fake.calls.size();
    Captured again;
    backend.write(kKey, kToken, again.callback());
    QCOMPARE(again.count, 1);
    QCOMPARE(again.reply.outcome, Outcome::Failed);
    QCOMPARE(fake.calls.size(), before);
}

void SecretBackendsTest::secretServiceCreateCollectionWithoutAPathFails_data()
{
    QTest::addColumn<bool>("prompted");
    QTest::addColumn<QVariant>("result");
    // No prompt and no collection in the reply.
    QTest::newRow("reply /") << false << objectPath(kNoPrompt);
    // A completed (not dismissed) prompt whose result is not a collection.
    QTest::newRow("Completed /") << true << objectPath(kNoPrompt);
    QTest::newRow("Completed string") << true << QVariant(QStringLiteral("not a path"));
    QTest::newRow("Completed ao") << true << objectPaths({kCollection});
}

void SecretBackendsTest::secretServiceCreateCollectionWithoutAPathFails()
{
    QFETCH(bool, prompted);
    QFETCH(QVariant, result);
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    openWithoutADefaultCollection(fake, backend);
    if (QTest::currentTestFailed())
        return;

    Captured written;
    backend.write(kKey, kToken, written.callback());
    expectCreateCollection(fake);
    if (QTest::currentTestFailed())
        return;
    if (prompted) {
        fake.reply({objectPath(kNoPrompt), objectPath(kPrompt)});
        answerPrompt(fake, false, result);
        if (QTest::currentTestFailed())
            return;
    } else {
        fake.reply({result, objectPath(kNoPrompt)});
    }
    QCOMPARE(written.count, 1);
    QCOMPARE(written.reply.outcome, Outcome::Failed);
    QCOMPARE(fake.pending(), -1);
    QVERIFY(!calledMember(fake, QStringLiteral("CreateItem")));
    QVERIFY(!backend.hasStorage());
}

void SecretBackendsTest::secretServiceCreateCollectionErrors_data()
{
    QTest::addColumn<QString>("error");
    QTest::addColumn<Outcome>("expected");
    // A keyring that cannot create one is unavailable, as a missing one was before.
    QTest::newRow("NotSupported") << dbusError("NotSupported") << Outcome::Unavailable;
    QTest::newRow("AccessDenied") << dbusError("AccessDenied") << Outcome::Unavailable;
    QTest::newRow("ServiceUnknown") << dbusError("ServiceUnknown") << Outcome::Unavailable;
    // A slow user is not a missing feature.
    QTest::newRow("NoReply") << dbusError("NoReply") << Outcome::Failed;
    QTest::newRow("Timeout") << dbusError("Timeout") << Outcome::Failed;
}

void SecretBackendsTest::secretServiceCreateCollectionErrors()
{
    QFETCH(QString, error);
    QFETCH(Outcome, expected);
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    openWithoutADefaultCollection(fake, backend);
    if (QTest::currentTestFailed())
        return;

    Captured written;
    backend.write(kKey, kToken, written.callback());
    expectCreateCollection(fake);
    if (QTest::currentTestFailed())
        return;
    fake.replyError(error);
    QCOMPARE(written.count, 1);
    QCOMPARE(written.reply.outcome, expected);
    QCOMPARE(fake.pending(), -1);
    QVERIFY(!calledMember(fake, QStringLiteral("CreateItem")));

    // Sticky: the same answer again, without another CreateCollection.
    const int before = fake.calls.size();
    Captured again;
    backend.write(kKey, kToken, again.callback());
    QCOMPARE(again.reply.outcome, expected);
    QCOMPARE(fake.calls.size(), before);
}

void SecretBackendsTest::secretServiceReadWithoutADefaultCollectionCreatesNothing()
{
    FakeDBusTransport fake;
    QObject ctx;
    SecretServiceBackend backend(fake, &ctx);
    openWithoutADefaultCollection(fake, backend);
    if (QTest::currentTestFailed())
        return;

    // Nothing to find, nothing to create: no keyring password dialog for a read.
    Captured read;
    backend.read(kKey, read.callback());
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("SearchItems")));
    fake.reply({objectPaths({}), objectPaths({})});
    QCOMPARE(read.count, 1);
    QCOMPARE(read.reply.outcome, Outcome::Ok);
    QVERIFY(read.reply.value.isEmpty());

    // ...nor for a remove.
    Captured removed;
    backend.remove(kKey, removed.callback());
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("SearchItems")));
    fake.reply({objectPaths({}), objectPaths({})});
    QCOMPARE(removed.count, 1);
    QCOMPARE(removed.reply.outcome, Outcome::Ok);

    QCOMPARE(fake.pending(), -1);
    QVERIFY(!calledMember(fake, QStringLiteral("CreateCollection")));
    QVERIFY(fake.subscriptions.isEmpty());
}

void SecretBackendsTest::storeCreatesTheDefaultCollectionOnAFreshAccount()
{
    qputenv("XDG_CURRENT_DESKTOP", "XFCE");
    auto fake = std::make_shared<FakeDBusTransport>();
    SecretsStore store(fake);
    const QString vault = m_dir->filePath(QStringLiteral("secrets.ini"));
    store.setLegacyFilePathForTests(vault);

    const QFuture<Result<bool>> write = store.writeSecret(kKey, kToken);
    answerProbe(*fake, {kSecrets}, {});
    if (QTest::currentTestFailed())
        return;

    QTRY_VERIFY(
        nextIsSecrets(*fake, kSecretsPath, kServiceInterface, QStringLiteral("OpenSession")));
    fake->reply({dbusVariant(QString()), objectPath(kSession)});
    QVERIFY(nextIsSecrets(*fake, kSecretsPath, kServiceInterface, QStringLiteral("ReadAlias")));
    fake->reply({objectPath(kNoPrompt)});
    QTRY_VERIFY(fake->pending() >= 0);
    expectCreateCollection(*fake);
    if (QTest::currentTestFailed())
        return;
    fake->reply({objectPath(kNoPrompt), objectPath(kPrompt)});
    answerPrompt(*fake, false, objectPath(kCollection));
    if (QTest::currentTestFailed())
        return;
    QVERIFY(nextIsSecrets(*fake, kCollection, kPropertiesInterface, QStringLiteral("Get")));
    fake->reply({dbusVariant(false)});
    QVERIFY(nextIsSecrets(*fake, kCollection, kCollectionInterface, QStringLiteral("CreateItem")));
    fake->reply({objectPath(kItem), objectPath(kNoPrompt)});

    QVERIFY(awaitResult(write).ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::Wallet);
    QCOMPARE(store.backendName(), QStringLiteral("Secret Service"));
    QVERIFY(!QFile::exists(vault));
    QCOMPARE(fake->pending(), -1);
}

// Probe, OpenSession and a ReadAlias of "/": the store's Secret Service has no
// collection. Opening it makes no call.
void answerFreshSecretService(FakeDBusTransport &fake)
{
    answerProbe(fake, {kSecrets}, {});
    if (QTest::currentTestFailed())
        return;
    QTRY_VERIFY(
        nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("OpenSession")));
    fake.reply({dbusVariant(QString()), objectPath(kSession)});
    QVERIFY(nextIsSecrets(fake, kSecretsPath, kServiceInterface, QStringLiteral("ReadAlias")));
    fake.reply({objectPath(kNoPrompt)});
}

void SecretBackendsTest::storeGoesToTheVaultWhenTheNewKeyringIsDismissed()
{
    qputenv("XDG_CURRENT_DESKTOP", "XFCE");
    auto fake = std::make_shared<FakeDBusTransport>();
    SecretsStore store(fake);
    const QString vault = m_dir->filePath(QStringLiteral("secrets.ini"));
    store.setLegacyFilePathForTests(vault);

    const QFuture<Result<bool>> write = store.writeSecret(kKey, kToken);
    answerFreshSecretService(*fake);
    if (QTest::currentTestFailed())
        return;
    QTRY_VERIFY(fake->pending() >= 0);
    expectCreateCollection(*fake);
    if (QTest::currentTestFailed())
        return;
    fake->reply({objectPath(kNoPrompt), objectPath(kPrompt)});
    answerPrompt(*fake, true, objectPath(kNoPrompt));
    if (QTest::currentTestFailed())
        return;

    // The user said no to a new keyring: the token goes to the vault, which the
    // store now uses (and the UI warns about).
    QVERIFY(awaitResult(write).ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::PlaintextFallback);
    QCOMPARE(QSettings(vault, QSettings::IniFormat).value(kKey).toString(), kToken);
    QVERIFY(!calledMember(*fake, QStringLiteral("CreateItem")));
    QCOMPARE(fake->pending(), -1);
}

void SecretBackendsTest::storeDefersMigrationUntilACollectionExists()
{
    qputenv("XDG_CURRENT_DESKTOP", "XFCE");
    const QString vault = m_dir->filePath(QStringLiteral("secrets.ini"));
    const QString otherKey = QStringLiteral("emby/00112233445566778899aabbccddeeff/accessToken");
    {
        QSettings seed(vault, QSettings::IniFormat);
        seed.setValue(kKey, kToken);
        seed.setValue(otherKey, QStringLiteral("other-token"));
    }
    auto fake = std::make_shared<FakeDBusTransport>();
    SecretsStore store(fake);
    store.setLegacyFilePathForTests(vault);

    // Session restore reads first. With no collection yet, the vault is not
    // migrated (that would ask for a new keyring from a read): the read finds
    // nothing in the keyring and answers from the vault.
    const QFuture<Result<QString>> read = store.readSecret(kKey);
    answerFreshSecretService(*fake);
    if (QTest::currentTestFailed())
        return;
    QTRY_VERIFY(fake->pending() >= 0);
    QVERIFY(nextIsSecrets(*fake, kSecretsPath, kServiceInterface, QStringLiteral("SearchItems")));
    fake->reply({objectPaths({}), objectPaths({})});
    const Result<QString> restored = awaitResult(read);
    QVERIFY(restored.ok());
    QCOMPARE(restored.value, kToken);
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::Wallet);
    QVERIFY(!calledMember(*fake, QStringLiteral("CreateCollection")));
    QVERIFY(fake->subscriptions.isEmpty());
    QCOMPARE(QSettings(vault, QSettings::IniFormat).allKeys().size(), 2);

    // The first explicit write asks once; the user dismisses it.
    const QFuture<Result<bool>> login = store.writeSecret(kKey, kToken);
    QTRY_VERIFY(fake->pending() >= 0);
    expectCreateCollection(*fake);
    if (QTest::currentTestFailed())
        return;
    fake->reply({objectPath(kNoPrompt), objectPath(kPrompt)});
    answerPrompt(*fake, true, objectPath(kNoPrompt));
    if (QTest::currentTestFailed())
        return;
    QVERIFY(awaitResult(login).ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::PlaintextFallback);

    // A second write in this session does not ask again.
    const int before = fake->calls.size();
    const int subscribed = fake->subscriptions.size();
    QVERIFY(awaitResult(store.writeSecret(otherKey, QStringLiteral("rotated"))).ok());
    QCOMPARE(fake->calls.size(), before);
    QCOMPARE(fake->subscriptions.size(), subscribed);
    int creates = 0;
    for (const FakeDBusTransport::Call &call : std::as_const(fake->calls))
        creates += call.message.member() == QLatin1String("CreateCollection");
    QCOMPARE(creates, 1);
    QCOMPARE(QSettings(vault, QSettings::IniFormat).value(otherKey).toString(),
             QStringLiteral("rotated"));
}

QTEST_GUILESS_MAIN(SecretBackendsTest)
#include "tst_secret_backends.moc"
