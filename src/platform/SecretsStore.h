#pragma once

#include "core/Result.h"

#include <QFuture>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace strmqt {

namespace secrets {
class DBusTransport;
class SecretBackend;
enum class BackendKind;
} // namespace secrets

// Secret storage for auth tokens. Talks to a system keyring asynchronously over
// D-Bus when one is reachable — KWallet 6, then KWallet 5 (platform/secrets/), no
// KF link dependency. When the keyring is
// unreachable or the open is rejected, secrets persist to a vault file instead
// (<AppDataLocation>/secrets.ini, owner-only 0600) — lower security, surfaced to the
// user through the storageMode property. A vault written while the wallet was down is
// migrated into the wallet and scrubbed the next time the wallet opens. Vault file
// operations are dispatched to the Qt thread pool; the explicit-file constructor below
// is a test seam that forces vault mode against a chosen path.
class SecretsStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(StorageMode storageMode READ storageMode NOTIFY storageModeChanged)
    Q_PROPERTY(bool persistent READ persistent NOTIFY storageModeChanged)
    // The active keyring's name in Wallet mode, "vault file" in PlaintextFallback, "" when Unknown.
    Q_PROPERTY(QString backendName READ backendName NOTIFY storageModeChanged)

public:
    enum class StorageMode
    {
        Unknown,
        Wallet,
        PlaintextFallback,
    };
    Q_ENUM(StorageMode)

    explicit SecretsStore(QObject *parent = nullptr);
    // Reaches the keyrings through `transport` (tests inject a fake; see
    // tests/mocks/FakeDBusTransport.h). The default constructor uses the session bus.
    explicit SecretsStore(std::shared_ptr<secrets::DBusTransport> transport,
                          QObject *parent = nullptr);
    // Test seam: force vault-file mode against this INI path, bypassing the wallet.
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
    QString backendName() const;

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
    // Narrow transport seam. Production implementations delegate to the chosen keyring
    // backend over the D-Bus transport; tests hold these requests and complete them
    // later without a live wallet daemon.
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
    bool takeNextBackend();
    void tryNextBackend();
    void openCurrentBackend();
    void openNextBackend();

    int m_walletHandle = -1;
    QString m_forcedFallbackPath;
    StorageMode m_storageMode = StorageMode::Unknown;
    InitializationState m_initialization = InitializationState::NotStarted;
    QQueue<Operation> m_operations;
    std::optional<Operation> m_current;
    QList<QPair<QString, QString>> m_legacyEntries;
    qsizetype m_legacyIndex = 0;
    bool m_legacyMigrationSucceeded = true;
    quint64 m_identity = 0;
    std::shared_ptr<secrets::DBusTransport> m_transport;
    QList<secrets::BackendKind> m_candidates;
    bool m_probed = false;
    std::unique_ptr<secrets::SecretBackend> m_backend;
};

} // namespace strmqt
