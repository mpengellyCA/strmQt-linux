#pragma once

#include <QObject>
#include <QHash>
#include <QString>

namespace strmqt {

// Secret storage for auth tokens (PLAN §3.2). Talks to KWallet via D-Bus
// (org.kde.kwalletd6) when available — no KF6 link dependency — and degrades to a
// plaintext QSettings file otherwise (portable fallback; a warning is logged once).
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

    // True when secrets are actually going to KWallet.
    bool isWalletBacked();
    StorageMode storageMode();
    bool persistent() { return storageMode() == StorageMode::Wallet ||
                               storageMode() == StorageMode::PlaintextFallback; }

    bool writeSecret(const QString &key, const QString &value);
    QString readSecret(const QString &key);
    bool removeSecret(const QString &key);

signals:
    void storageModeChanged();

private:
    // Opens the wallet on first use. Returns false when kwalletd6 is unavailable
    // or the user rejects access; the caller then keeps secrets in memory only.
    bool ensureWallet();
    QString fallbackFilePath() const;

    bool m_walletProbed = false;
    int m_walletHandle = -1;
    QString m_forcedFallbackPath;
    StorageMode m_storageMode = StorageMode::Unknown;
    QHash<QString, QString> m_sessionSecrets;
};

} // namespace strmqt
