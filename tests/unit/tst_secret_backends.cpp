#include <QDBusMessage>
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

#include <memory>
#include <utility>

Q_DECLARE_METATYPE(strmqt::secrets::Outcome)

using strmqt::Result;
using strmqt::SecretsStore;
using strmqt::secrets::BackendKind;
using strmqt::secrets::KWalletBackend;
using strmqt::secrets::Outcome;
using strmqt::secrets::Reply;
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

QTEST_GUILESS_MAIN(SecretBackendsTest)
#include "tst_secret_backends.moc"
