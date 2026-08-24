#include <QtTest>
#include <QFileInfo>

#include "platform/SecretsStore.h"

using strmqt::SecretsStore;

// Exercises the fallback-file path only. The KWallet D-Bus path needs a live
// kwalletd6 and a user consent dialog — verified manually via strmqt-cli login
// (see ARCHITECTURE.md manual matrix).
class SecretsStoreTest : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip();
    void missingKeyReadsEmpty();
    void removeDeletes();
};

void SecretsStoreTest::roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("secrets.ini"));

    {
        SecretsStore store(path);
        QVERIFY(!store.isWalletBacked());
        QCOMPARE(store.storageMode(), SecretsStore::StorageMode::PlaintextFallback);
        QVERIFY(store.writeSecret(QStringLiteral("emby/accessToken"), QStringLiteral("token-123")));
        const QFileDevice::Permissions permissions = QFileInfo(path).permissions();
        QVERIFY(permissions.testFlag(QFileDevice::ReadOwner));
        QVERIFY(permissions.testFlag(QFileDevice::WriteOwner));
        QVERIFY(!(permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                                 QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                 QFileDevice::WriteOther | QFileDevice::ExeOther)));
    }

    SecretsStore reloaded(path);
    QCOMPARE(reloaded.readSecret(QStringLiteral("emby/accessToken")), QStringLiteral("token-123"));
}

void SecretsStoreTest::missingKeyReadsEmpty()
{
    QTemporaryDir dir;
    SecretsStore store(dir.filePath(QStringLiteral("secrets.ini")));
    QCOMPARE(store.readSecret(QStringLiteral("nope")), QString());
}

void SecretsStoreTest::removeDeletes()
{
    QTemporaryDir dir;
    SecretsStore store(dir.filePath(QStringLiteral("secrets.ini")));
    QVERIFY(store.writeSecret(QStringLiteral("k"), QStringLiteral("v")));
    QVERIFY(store.removeSecret(QStringLiteral("k")));
    QCOMPARE(store.readSecret(QStringLiteral("k")), QString());
}

QTEST_GUILESS_MAIN(SecretsStoreTest)
#include "tst_secrets_store.moc"
