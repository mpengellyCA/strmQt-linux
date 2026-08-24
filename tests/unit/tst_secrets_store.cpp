#include <QFileInfo>
#include <QFutureWatcher>
#include <QSettings>
#include <QtTest>

#include "../mocks/FakeSecretsStore.h"
#include "platform/SecretsStore.h"

#include <algorithm>

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
    void walletOperationsAreDelayedAndReportFailures();
    void unavailableWalletIsSessionOnly();
    void legacyMigrationDeletesOnlyAfterEveryWrite();
    void failedLegacyMigrationRetainsTheFile();
    void identityChangeDuringOpenSkipsOldReadAndOrdersNewWork();
    void identityChangeDuringReadRetiresTheReply();
    void identityChangeDuringWriteOrdersRemovalAfterIt();
    void identityChangeDuringRemoveOrdersNewWriteAfterIt();
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

void SecretsStoreTest::walletOperationsAreDelayedAndReportFailures()
{
    QTemporaryDir dir;
    FakeSecretsStore store;
    store.setLegacyFilePathForTests(dir.filePath(QStringLiteral("missing.ini")));

    QFuture<Result<bool>> write = store.writeSecret(QStringLiteral("k"), QStringLiteral("v"));
    QVERIFY(!write.isFinished());
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::NetworkWallet));
    store.replyNetworkWallet(true);
    QCOMPARE(store.calls.last().type, FakeSecretsStore::CallType::Open);
    store.replyOpen(true);
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Write));
    QVERIFY(!write.isFinished());
    store.replyWrite(false);
    QVERIFY(!awaitResult(write).ok());

    write = store.writeSecret(QStringLiteral("k"), QStringLiteral("v2"));
    QTRY_VERIFY(!store.calls.isEmpty() && store.calls.last().value == QStringLiteral("v2"));
    store.replyWrite(true);
    QVERIFY(awaitResult(write).ok());

    QFuture<Result<QString>> read = store.readSecret(QStringLiteral("k"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Read));
    QVERIFY(!read.isFinished());
    store.replyRead(true, QStringLiteral("v2"));
    QCOMPARE(awaitResult(read).value, QStringLiteral("v2"));

    QFuture<Result<bool>> remove = store.removeSecret(QStringLiteral("k"));
    QTRY_VERIFY(lastCallIs(store, FakeSecretsStore::CallType::Remove));
    QVERIFY(!remove.isFinished());
    store.replyRemove(false);
    QVERIFY(!awaitResult(remove).ok());
}

void SecretsStoreTest::unavailableWalletIsSessionOnly()
{
    QTemporaryDir dir;
    FakeSecretsStore store;
    store.available = false;
    store.setLegacyFilePathForTests(dir.filePath(QStringLiteral("legacy.ini")));

    const Result<bool> write =
        awaitResult(store.writeSecret(QStringLiteral("k"), QStringLiteral("memory-only")));
    QVERIFY(write.ok());
    QCOMPARE(store.storageMode(), SecretsStore::StorageMode::SessionOnly);
    QVERIFY(store.calls.isEmpty());
    QCOMPARE(awaitResult(store.readSecret(QStringLiteral("k"))).value,
             QStringLiteral("memory-only"));

    FakeSecretsStore restarted;
    restarted.available = false;
    restarted.setLegacyFilePathForTests(dir.filePath(QStringLiteral("legacy.ini")));
    const Result<QString> absent = awaitResult(restarted.readSecret(QStringLiteral("k")));
    QVERIFY(absent.ok());
    QVERIFY(absent.value.isEmpty());
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

    QCOMPARE(store.calls.last().type, FakeSecretsStore::CallType::Write);
    QVERIFY(QFileInfo::exists(path));
    store.replyWrite(true);
    QCOMPARE(store.calls.last().type, FakeSecretsStore::CallType::Write);
    QVERIFY(QFileInfo::exists(path));
    store.replyWrite(true);
    QVERIFY(!QFileInfo::exists(path));

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
    QCOMPARE(store.calls.last().type, FakeSecretsStore::CallType::Write);
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

QTEST_GUILESS_MAIN(SecretsStoreTest)
#include "tst_secrets_store.moc"
