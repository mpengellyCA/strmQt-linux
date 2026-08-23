#pragma once

#include <QObject>
#include <QString>

namespace strmqt {

// Secret storage for auth tokens (PLAN §3.2). Talks to KWallet via D-Bus
// (org.kde.kwalletd6) when available — no KF6 link dependency — and degrades to a
// plaintext QSettings file otherwise (portable fallback; a warning is logged once).
class SecretsStore : public QObject
{
    Q_OBJECT

public:
    explicit SecretsStore(QObject *parent = nullptr);
    // Test/fallback-only constructor: bypass the wallet, store in this INI file.
    explicit SecretsStore(const QString &fallbackFilePath, QObject *parent = nullptr);
    ~SecretsStore() override;

    // True when secrets are actually going to KWallet (not the plaintext fallback).
    bool isWalletBacked();

    bool writeSecret(const QString &key, const QString &value);
    QString readSecret(const QString &key);
    bool removeSecret(const QString &key);

private:
    // Opens the wallet on first use. Returns false when kwalletd6 is unavailable
    // or the user rejects access; the caller then uses the fallback.
    bool ensureWallet();
    QString fallbackFilePath() const;

    bool m_walletProbed = false;
    int m_walletHandle = -1;
    QString m_forcedFallbackPath;
};

} // namespace strmqt
