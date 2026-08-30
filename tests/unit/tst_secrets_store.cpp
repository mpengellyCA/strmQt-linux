#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSettings>
#include <QtTest>

#include "../mocks/FakeSecretsStore.h"
#include "platform/SecretsStore.h"

#include <algorithm>
#include <utility>

using strmqt::Result;
using strmqt::SecretsStore;
using strmqt::test::FakeSecretsStore;

namespace {

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

bool lastCallIs(const FakeSecretsStore &store, FakeSecretsStore::CallType type)
{
    return !store.calls.isEmpty() && store.calls.last().type == type;
}

void initializeWallet(FakeSecretsStore &store)
{
    const QFuture<Result<QString>> probe = store.readSecret(QStringLiteral("probe"));
    QTRY_COMPARE(store.calls.size(), 1);
    QCOMPARE(store.calls.last().type, FakeSecretsStore::CallType::NetworkWallet);
    store.replyNetworkWallet(true);
    QCOMPARE(store.calls.last().type, FakeSecretsStore::CallType::Open);
    store.replyOpen(true);
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Read));
    store.replyRead(true, {});
    QVERIFY(awaitResult(probe).ok());
    store.calls.clear();
}

} // namespace

class SecretsStoreTest : public QObject
{
    Q_OBJECT

private slots:
    void plaintextTestModeRoundTripsAsynchronously();
    void walletWriteFailureFallsBackToTheVaultFile();
    void walletFailureIsReportedWhenTheVaultIsToo();
    void emptyOrFailedWalletReadFallsBackToTheVaultFile();
    void walletFailuresAreReported();
    void unavailableWalletFallsBackToTheVaultFile();
    void rejectedWalletUsesTheVaultFile();
    void vaultWriteFailureIsReported();
    void legacyMigrationDeletesOnlyAfterEveryWrite();
    void failedLegacyMigrationRetainsTheFile();
    void identityChangeDuringOpenSkipsOldReadAndOrdersNewWork();
    void identityChangeDuringReadRetiresTheReply();
    void identityChangeDuringWriteOrdersRemovalAfterIt();
    void identityChangeDuringRemoveOrdersNewWriteAfterIt();
    void selfDeletingContinuationIsSafeAfterLegacyWorker();
    void selfDeletingContinuationIsSafeAfterTransportCompletion();
    void destructionSettlesCurrentAndQueuedOperations();
};

void SecretsStoreTest::plaintextTestModeRoundTripsAsynchronously()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("secrets.ini"));

    {
        SecretsStore store(path);
        QVERIFY(!store.isWalletBacked());
        QCOMPARE(store.storageMode(), SecretsStore::StorageMode::PlaintextFallback);
        const QFuture<Result<bool>> write =
            store.writeSecret(QStringLiteral("emby/accessToken"), QStringLiteral("token-123"));
        QVERIFY(!write.isFinished());
        QVERIFY(awaitResult(write).ok());
        const QFileDevice::Permissions permissions = QFileInfo(path).permissions();
        QVERIFY(permissions.testFlag(QFileDevice::ReadOwner));
        QVERIFY(permissions.testFlag(QFileDevice::WriteOwner));
        QVERIFY(!(permissions &
                  (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                   QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther)));
    }

    SecretsStore reloaded(path);
    const Result<QString> stored =
        awaitResult(reloaded.readSecret(QStringLiteral("emby/accessToken")));
    QVERIFY(stored.ok());
    QCOMPARE(stored.value, QStringLiteral("token-123"));
    QVERIFY(awaitResult(reloaded.removeSecret(QStringLiteral("emby/accessToken"))).ok());
    const Result<QString> removed =
        awaitResult(reloaded.readSecret(QStringLiteral("emby/accessToken")));
    QVERIFY(removed.ok());
    QVERIFY(removed.value.isEmpty());
}

void SecretsStoreTest::walletWriteFailureFallsBackToTheVaultFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("secrets.ini"));
    FakeSecretsStore store;
    store.setLegacyFilePathForTests(path);
    QSignalSpy modeSpy(&store, &SecretsStore::storageModeChanged);

    const QFuture<Result<bool>> write =
        store.writeSecret(QStringLiteral("k"), QStringLiteral("v"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::NetworkWallet));
    store.replyNetworkWallet(true);
    store.replyOpen(true);
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Write));
    QVERIFY(!write.isFinished());

    // The wallet refuses the write (kwalletd6 answering -1 and leaving an
    // empty entry behind is the in-the-wild case); the vault takes over for
    // the rest of the process, and the mode change is announced.
    store.replyWrite(false);
    QVERIFY(awaitResult(write).ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::PlaintextFallback);
    QCOMPARE(modeSpy.count(), 2); // Unknown → Wallet → PlaintextFallback

    // Later operations bypass the wallet entirely.
    store.calls.clear();
    QCOMPARE(awaitResult(store.readSecret(QStringLiteral("k"))).value, QStringLiteral("v"));
    QVERIFY(store.calls.isEmpty());

    // And the value is really on disk, owner-only.
    const QFileDevice::Permissions permissions = QFileInfo(path).permissions();
    QVERIFY(!(permissions &
              (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
               QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther)));
    SecretsStore reloaded(path);
    QCOMPARE(awaitResult(reloaded.readSecret(QStringLiteral("k"))).value, QStringLiteral("v"));
}

void SecretsStoreTest::walletFailureIsReportedWhenTheVaultIsToo()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("secrets.ini"));
    QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));

    FakeSecretsStore store;
    store.setLegacyFilePathForTests(path);
    const QFuture<Result<bool>> write =
        store.writeSecret(QStringLiteral("k"), QStringLiteral("v"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::NetworkWallet));
    store.replyNetworkWallet(true);
    store.replyOpen(true);
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Write));
    store.replyWrite(false);

    const Result<bool> failed = awaitResult(write);
    QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                  QFileDevice::ExeOwner));
    QVERIFY(!failed.ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::Wallet);
    QVERIFY(!QFileInfo::exists(path));
}

void SecretsStoreTest::emptyOrFailedWalletReadFallsBackToTheVaultFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("secrets.ini"));
    FakeSecretsStore store;
    store.setLegacyFilePathForTests(path);
    initializeWallet(store);

    // Seeded after the wallet opened — seeding earlier would migrate it away.
    {
        QSettings vault(path, QSettings::IniFormat);
        vault.setValue(QStringLiteral("k"), QStringLiteral("vault-value"));
        vault.sync();
    }

    // An empty wallet entry (what a refused write leaves behind) shadows nothing.
    QFuture<Result<QString>> read = store.readSecret(QStringLiteral("k"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Read));
    store.replyRead(true, {});
    QCOMPARE(awaitResult(read).value, QStringLiteral("vault-value"));

    // Clear the recorded calls so the QTRY below cannot pass on the stale
    // Read above before the new read is even dispatched.
    store.calls.clear();
    read = store.readSecret(QStringLiteral("k"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Read));
    store.replyRead(false, {});
    QCOMPARE(awaitResult(read).value, QStringLiteral("vault-value"));
}

void SecretsStoreTest::walletFailuresAreReported()
{
    QTemporaryDir dir;
    FakeSecretsStore store;
    store.setLegacyFilePathForTests(dir.filePath(QStringLiteral("missing.ini")));
    initializeWallet(store);

    // The vault has nothing to rescue these with, so the wallet's failure is
    // the answer.
    QFuture<Result<QString>> read = store.readSecret(QStringLiteral("k"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Read));
    store.replyRead(false, {});
    QVERIFY(!awaitResult(read).ok());

    QFuture<Result<bool>> remove = store.removeSecret(QStringLiteral("k"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Remove));
    store.replyRemove(false);
    QVERIFY(!awaitResult(remove).ok());
}

void SecretsStoreTest::unavailableWalletFallsBackToTheVaultFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("secrets.ini"));

    FakeSecretsStore store;
    store.available = false;
    store.setLegacyFilePathForTests(path);
    const Result<bool> write =
        awaitResult(store.writeSecret(QStringLiteral("k"), QStringLiteral("persisted")));
    QVERIFY(write.ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::PlaintextFallback);
    QVERIFY(store.calls.isEmpty());
    QCOMPARE(awaitResult(store.readSecret(QStringLiteral("k"))).value,
             QStringLiteral("persisted"));

    // The point of the fallback: a new process (a fresh store) reads it back.
    FakeSecretsStore restarted;
    restarted.available = false;
    restarted.setLegacyFilePathForTests(path);
    const Result<QString> persisted = awaitResult(restarted.readSecret(QStringLiteral("k")));
    QVERIFY(persisted.ok());
    QCOMPARE(persisted.value, QStringLiteral("persisted"));
}

void SecretsStoreTest::rejectedWalletUsesTheVaultFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("secrets.ini"));
    {
        QSettings vault(path, QSettings::IniFormat);
        vault.setValue(QStringLiteral("emby/accessToken"), QStringLiteral("stale-token"));
        vault.sync();
    }
    QVERIFY(QFileInfo::exists(path));

    FakeSecretsStore store;
    store.setLegacyFilePathForTests(path);
    const QFuture<Result<bool>> removed = store.removeSecret(QStringLiteral("emby/accessToken"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::NetworkWallet));
    store.replyNetworkWallet(false);

    // The wallet never opened, so the remove falls through to the vault file.
    QVERIFY(awaitResult(removed).ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::PlaintextFallback);
    QVERIFY(!QFileInfo::exists(path));
}

void SecretsStoreTest::vaultWriteFailureIsReported()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("secrets.ini"));
    QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));

    FakeSecretsStore store;
    store.available = false;
    store.setLegacyFilePathForTests(path);
    const Result<bool> written = awaitResult(
        store.writeSecret(QStringLiteral("emby/accessToken"), QStringLiteral("token")));

    QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                  QFileDevice::ExeOwner));
    QVERIFY(!written.ok());
    QVERIFY(!QFileInfo::exists(path));
}

void SecretsStoreTest::legacyMigrationDeletesOnlyAfterEveryWrite()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("secrets.ini"));
    {
        QSettings legacy(path, QSettings::IniFormat);
        legacy.setValue(QStringLiteral("emby/accessToken"), QStringLiteral("old-token"));
        legacy.setValue(QStringLiteral("other"), QStringLiteral("other-secret"));
        legacy.sync();
    }

    FakeSecretsStore store;
    store.setLegacyFilePathForTests(path);
    const QFuture<Result<QString>> read = store.readSecret(QStringLiteral("emby/accessToken"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::NetworkWallet));
    store.replyNetworkWallet(true);
    store.replyOpen(true);

    QTRY_COMPARE(store.calls.last().type, FakeSecretsStore::CallType::Write);
    QVERIFY(QFileInfo::exists(path));
    store.replyWrite(true);
    QTRY_COMPARE(store.calls.last().type, FakeSecretsStore::CallType::Write);
    QVERIFY(QFileInfo::exists(path));
    store.replyWrite(true);
    QTRY_VERIFY(!QFileInfo::exists(path));

    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Read));
    store.replyRead(true, QStringLiteral("old-token"));
    QCOMPARE(awaitResult(read).value, QStringLiteral("old-token"));
}

void SecretsStoreTest::failedLegacyMigrationRetainsTheFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("secrets.ini"));
    {
        QSettings legacy(path, QSettings::IniFormat);
        legacy.setValue(QStringLiteral("emby/accessToken"), QStringLiteral("old-token"));
        legacy.sync();
    }

    FakeSecretsStore store;
    store.setLegacyFilePathForTests(path);
    const QFuture<Result<QString>> read = store.readSecret(QStringLiteral("emby/accessToken"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::NetworkWallet));
    store.replyNetworkWallet(true);
    store.replyOpen(true);
    QTRY_COMPARE(store.calls.last().type, FakeSecretsStore::CallType::Write);
    store.replyWrite(false);
    QVERIFY(QFileInfo::exists(path));

    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Read));
    store.replyRead(true, {});
    QVERIFY(awaitResult(read).ok());
}

void SecretsStoreTest::identityChangeDuringOpenSkipsOldReadAndOrdersNewWork()
{
    QTemporaryDir dir;
    FakeSecretsStore store;
    store.setLegacyFilePathForTests(dir.filePath(QStringLiteral("missing.ini")));

    const QFuture<Result<QString>> oldRead = store.readSecret(QStringLiteral("token"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::NetworkWallet));
    store.beginIdentity();
    const QFuture<Result<bool>> removal = store.removeSecret(QStringLiteral("token"));
    const QFuture<Result<bool>> newWrite =
        store.writeSecret(QStringLiteral("token"), QStringLiteral("new"));

    store.replyNetworkWallet(true);
    store.replyOpen(true);
    QTRY_VERIFY(oldRead.isFinished());
    QVERIFY(!oldRead.result().ok());
    QCOMPARE(store.calls.last().type, FakeSecretsStore::CallType::Remove);
    QVERIFY(std::none_of(store.calls.cbegin(), store.calls.cend(), [](const auto &call) {
        return call.type == FakeSecretsStore::CallType::Read;
    }));

    store.replyRemove(true);
    QVERIFY(awaitResult(removal).ok());
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Write));
    QCOMPARE(store.calls.last().value, QStringLiteral("new"));
    store.replyWrite(true);
    QVERIFY(awaitResult(newWrite).ok());
}

void SecretsStoreTest::identityChangeDuringReadRetiresTheReply()
{
    QTemporaryDir dir;
    FakeSecretsStore store;
    store.setLegacyFilePathForTests(dir.filePath(QStringLiteral("missing.ini")));
    initializeWallet(store);

    const QFuture<Result<QString>> read = store.readSecret(QStringLiteral("token"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Read));
    store.beginIdentity();
    const QFuture<Result<bool>> removal = store.removeSecret(QStringLiteral("token"));
    store.replyRead(true, QStringLiteral("old-token"));
    QVERIFY(!awaitResult(read).ok());
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Remove));
    store.replyRemove(true);
    QVERIFY(awaitResult(removal).ok());
}

void SecretsStoreTest::identityChangeDuringWriteOrdersRemovalAfterIt()
{
    QTemporaryDir dir;
    FakeSecretsStore store;
    store.setLegacyFilePathForTests(dir.filePath(QStringLiteral("missing.ini")));
    initializeWallet(store);

    const QFuture<Result<bool>> write =
        store.writeSecret(QStringLiteral("token"), QStringLiteral("old-token"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Write));
    store.beginIdentity();
    const QFuture<Result<bool>> removal = store.removeSecret(QStringLiteral("token"));
    store.replyWrite(true);
    QVERIFY(!awaitResult(write).ok());
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Remove));
    store.replyRemove(true);
    QVERIFY(awaitResult(removal).ok());
}

void SecretsStoreTest::identityChangeDuringRemoveOrdersNewWriteAfterIt()
{
    QTemporaryDir dir;
    FakeSecretsStore store;
    store.setLegacyFilePathForTests(dir.filePath(QStringLiteral("missing.ini")));
    initializeWallet(store);

    const QFuture<Result<bool>> removal = store.removeSecret(QStringLiteral("token"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Remove));
    store.beginIdentity();
    const QFuture<Result<bool>> write =
        store.writeSecret(QStringLiteral("token"), QStringLiteral("new-token"));
    store.replyRemove(true);
    QVERIFY(!awaitResult(removal).ok());
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Write));
    QCOMPARE(store.calls.last().value, QStringLiteral("new-token"));
    store.replyWrite(true);
    QVERIFY(awaitResult(write).ok());
}

void SecretsStoreTest::selfDeletingContinuationIsSafeAfterLegacyWorker()
{
    QTemporaryDir dir;
    SecretsStore *store = new SecretsStore(dir.filePath(QStringLiteral("secrets.ini")));
    QFuture<Result<bool>> write =
        store->writeSecret(QStringLiteral("token"), QStringLiteral("value"));
    bool operationSucceeded = false;
    const QFuture<void> deleted =
        write.then([&store, &operationSucceeded](const Result<bool> &result) {
            operationSucceeded = result.ok();
            delete std::exchange(store, nullptr);
        });

    QTRY_VERIFY(deleted.isFinished());
    QVERIFY(operationSucceeded);
    QVERIFY(store == nullptr);
}

void SecretsStoreTest::selfDeletingContinuationIsSafeAfterTransportCompletion()
{
    QTemporaryDir dir;
    auto *store = new FakeSecretsStore;
    store->setLegacyFilePathForTests(dir.filePath(QStringLiteral("missing.ini")));
    initializeWallet(*store);

    QFuture<Result<QString>> read = store->readSecret(QStringLiteral("token"));
    QTRY_VERIFY(lastCallIs(*store, FakeSecretsStore::CallType::Read));
    bool operationSucceeded = false;
    const QFuture<void> deleted =
        read.then([&store, &operationSucceeded](const Result<QString> &result) {
            operationSucceeded = result.ok();
            delete std::exchange(store, nullptr);
        });

    store->replyRead(true, QStringLiteral("value"));

    // Context-free continuations run inline with promise settlement. This
    // specifically exercises deletion before completeReadPassword() returns.
    QVERIFY(deleted.isFinished());
    QVERIFY(operationSucceeded);
    QVERIFY(store == nullptr);
}

void SecretsStoreTest::destructionSettlesCurrentAndQueuedOperations()
{
    QTemporaryDir dir;
    auto *store = new FakeSecretsStore;
    store->setLegacyFilePathForTests(dir.filePath(QStringLiteral("missing.ini")));
    const QFuture<Result<bool>> current =
        store->writeSecret(QStringLiteral("token"), QStringLiteral("value"));
    const QFuture<Result<QString>> queued = store->readSecret(QStringLiteral("token"));

    QTRY_VERIFY(lastCallIs(*store, FakeSecretsStore::CallType::NetworkWallet));
    store->replyNetworkWallet(true);
    store->replyOpen(true);
    QTRY_VERIFY(lastCallIs(*store, FakeSecretsStore::CallType::Write));
    QVERIFY(!current.isFinished());
    QVERIFY(!queued.isFinished());

    delete store;

    QVERIFY(current.isFinished());
    QVERIFY(queued.isFinished());
    QCOMPARE(current.resultCount(), 1);
    QCOMPARE(queued.resultCount(), 1);
    QVERIFY(!current.result().ok());
    QVERIFY(!queued.result().ok());
}

QTEST_GUILESS_MAIN(SecretsStoreTest)
#include "tst_secrets_store.moc"
