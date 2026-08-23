#include "SecretsStore.h"

#include "core/Log.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace strmqt {

namespace {

const auto kWalletService = QStringLiteral("org.kde.kwalletd6");
const auto kWalletPath = QStringLiteral("/modules/kwalletd6");
const auto kWalletInterface = QStringLiteral("org.kde.KWallet");
const auto kWalletFolder = QStringLiteral("StrmQt");

QString appId()
{
    const QString name = QCoreApplication::applicationName();
    return name.isEmpty() ? QStringLiteral("strmqt") : name;
}

QDBusInterface *makeWalletInterface(QObject *parent)
{
    return new QDBusInterface(kWalletService, kWalletPath, kWalletInterface,
                              QDBusConnection::sessionBus(), parent);
}

} // namespace

SecretsStore::SecretsStore(QObject *parent) : QObject(parent) {}

SecretsStore::SecretsStore(const QString &fallbackFilePath, QObject *parent)
    : QObject(parent), m_walletProbed(true) // never touch the wallet
      ,
      m_forcedFallbackPath(fallbackFilePath)
{
}

SecretsStore::~SecretsStore() = default;

bool SecretsStore::ensureWallet()
{
    if (m_walletProbed)
        return m_walletHandle >= 0;
    m_walletProbed = true;

    QDBusInterface wallet(kWalletService, kWalletPath, kWalletInterface,
                          QDBusConnection::sessionBus());
    if (!wallet.isValid()) {
        qCWarning(logCore) << "kwalletd6 not reachable; secrets fall back to plaintext"
                           << fallbackFilePath();
        return false;
    }

    const QDBusReply<QString> walletName = wallet.call(QStringLiteral("networkWallet"));
    if (!walletName.isValid()) {
        qCWarning(logCore) << "networkWallet failed:" << walletName.error().message();
        return false;
    }

    // May block on the user's wallet-access dialog — that is intended UX.
    const QDBusReply<int> handle =
        wallet.call(QStringLiteral("open"), walletName.value(), qlonglong(0), appId());
    if (!handle.isValid() || handle.value() < 0) {
        qCWarning(logCore) << "wallet open rejected or failed; secrets fall back to plaintext";
        return false;
    }

    m_walletHandle = handle.value();
    return true;
}

bool SecretsStore::isWalletBacked()
{
    return ensureWallet();
}

QString SecretsStore::fallbackFilePath() const
{
    if (!m_forcedFallbackPath.isEmpty())
        return m_forcedFallbackPath;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/secrets.ini");
}

bool SecretsStore::writeSecret(const QString &key, const QString &value)
{
    if (ensureWallet()) {
        QDBusInterface *wallet = makeWalletInterface(nullptr);
        const QDBusReply<int> rc = wallet->call(QStringLiteral("writePassword"), m_walletHandle,
                                                kWalletFolder, key, value, appId());
        delete wallet;
        if (rc.isValid() && rc.value() == 0)
            return true;
        qCWarning(logCore) << "wallet writePassword failed for" << key;
        return false;
    }

    QSettings store(fallbackFilePath(), QSettings::IniFormat);
    store.setValue(key, value);
    store.sync();
    return store.status() == QSettings::NoError;
}

QString SecretsStore::readSecret(const QString &key)
{
    if (ensureWallet()) {
        QDBusInterface *wallet = makeWalletInterface(nullptr);
        const QDBusReply<QString> value = wallet->call(QStringLiteral("readPassword"),
                                                       m_walletHandle, kWalletFolder, key, appId());
        delete wallet;
        return value.isValid() ? value.value() : QString();
    }

    QSettings store(fallbackFilePath(), QSettings::IniFormat);
    return store.value(key).toString();
}

bool SecretsStore::removeSecret(const QString &key)
{
    if (ensureWallet()) {
        QDBusInterface *wallet = makeWalletInterface(nullptr);
        const QDBusReply<int> rc = wallet->call(QStringLiteral("removeEntry"), m_walletHandle,
                                                kWalletFolder, key, appId());
        delete wallet;
        return rc.isValid() && rc.value() == 0;
    }

    QSettings store(fallbackFilePath(), QSettings::IniFormat);
    store.remove(key);
    store.sync();
    return true;
}

} // namespace strmqt
