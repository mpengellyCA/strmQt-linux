#pragma once

#include "core/Result.h"

#include <QFuture>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace strmqt {

// Secret storage for auth tokens. Talks to KWallet asynchronously over D-Bus
// (org.kde.kwalletd6) when available — no KF6 link dependency — and otherwise keeps
// values in memory for this process only. Legacy INI migration and the explicit
// test-file mode dispatch every QSettings/QFile operation to the Qt thread pool;
// the test-file constructor below is never used in production.
class SecretsStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(StorageMode storageMode READ storageMode NOTIFY storageModeChanged)
    Q_PROPERTY(bool persistent READ persistent NOTIFY storageModeChanged)

public:
    enum class StorageMode
    {
        Unknown,
        Wallet,
        SessionOnly,
        PlaintextFallback,
    };
    Q_ENUM(StorageMode)

    explicit SecretsStore(QObject *parent = nullptr);
    // Test-only explicit plaintext mode: bypass the wallet and use this INI file.
    explicit SecretsStore(const QString &fallbackFilePath, QObject *parent = nullptr);
    ~SecretsStore() override;

    // These accessors never probe D-Bus. Unknown means no asynchronous operation
    // has needed the wallet yet.
    bool isWalletBacked() const { return m_storageMode == StorageMode::Wallet; }
    StorageMode storageMode() const { return m_storageMode; }
    bool persistent() const
    {
        return m_storageMode == StorageMode::Wallet ||
               m_storageMode == StorageMode::PlaintextFallback;
    }

    QFuture<Result<bool>> writeSecret(const QString &key, const QString &value);
    QFuture<Result<QString>> readSecret(const QString &key);
    QFuture<Result<bool>> removeSecret(const QString &key);

    // Retires logical reads/writes from the previous authenticated identity.
    // Physical operations remain serialized: an already-sent old write finishes
    // before the boundary's removal, and that removal finishes before a new write.
    quint64 beginIdentity();

    // Test seam used by the fake asynchronous transport and migration tests.
    // It must be set before the first operation is queued.
    void setLegacyFilePathForTests(const QString &path);

signals:
    void storageModeChanged();

protected:
    // Narrow transport seam. Production implementations issue QDBusConnection::asyncCall;
    // tests hold these requests and complete them later without a live wallet daemon.
    virtual bool walletTransportAvailable() const;
    virtual void requestNetworkWallet();
    virtual void requestOpenWallet(const QString &walletName);
    virtual void requestWritePassword(const QString &key, const QString &value);
    virtual void requestReadPassword(const QString &key);
    virtual void requestRemoveEntry(const QString &key);

    void completeNetworkWallet(bool success, const QString &walletName, const QString &error = {});
    void completeOpenWallet(bool success, int handle, const QString &error = {});
    void completeWritePassword(bool success, const QString &error = {});
    void completeReadPassword(bool success, const QString &value, const QString &error = {});
    void completeRemoveEntry(bool success, const QString &error = {});

private:
    enum class InitializationState
    {
        NotStarted,
        NetworkWalletPending,
        OpenPending,
        LegacyScanPending,
        Migrating,
        LegacyCleanupPending,
        Ready,
    };

    enum class OperationType
    {
        Read,
        Write,
        Remove,
    };

    struct Operation
    {
        OperationType type;
        QString key;
        QString value;
        quint64 identity;
        std::function<void(const Result<QString> &)> finish;
    };

    void enqueue(Operation operation);
    void processNext();
    void startWalletInitialization();
    void startLegacyMigration();
    void migrateNext();
    void finishInitialization();
    void finishCurrent(const Result<QString> &result);
    void setStorageMode(StorageMode mode);
    QString fallbackFilePath() const;
    void removeLegacyForCurrent(Result<QString> operationResult);

    int m_walletHandle = -1;
    QString m_forcedFallbackPath;
    StorageMode m_storageMode = StorageMode::Unknown;
    QHash<QString, QString> m_sessionSecrets;
    InitializationState m_initialization = InitializationState::NotStarted;
    QQueue<Operation> m_operations;
    std::optional<Operation> m_current;
    QList<QPair<QString, QString>> m_legacyEntries;
    qsizetype m_legacyIndex = 0;
    bool m_legacyMigrationSucceeded = true;
    quint64 m_identity = 0;
};

} // namespace strmqt
