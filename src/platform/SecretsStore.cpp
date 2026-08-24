#include "SecretsStore.h"

#include "core/Log.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
      m_forcedFallbackPath(fallbackFilePath), m_storageMode(StorageMode::PlaintextFallback)
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
        qCWarning(logCore) << "kwalletd6 not reachable; secrets are session-only";
        m_storageMode = StorageMode::SessionOnly;
        emit storageModeChanged();
        return false;
    }

    const QDBusReply<QString> walletName = wallet.call(QStringLiteral("networkWallet"));
    if (!walletName.isValid()) {
        qCWarning(logCore) << "networkWallet failed:" << walletName.error().message();
        m_storageMode = StorageMode::SessionOnly;
        emit storageModeChanged();
        return false;
    }

    // May block on the user's wallet-access dialog — that is intended UX.
    const QDBusReply<int> handle =
        wallet.call(QStringLiteral("open"), walletName.value(), qlonglong(0), appId());
    if (!handle.isValid() || handle.value() < 0) {
        qCWarning(logCore) << "wallet open rejected or failed; secrets are session-only";
        m_storageMode = StorageMode::SessionOnly;
        emit storageModeChanged();
        return false;
    }

    m_walletHandle = handle.value();
    m_storageMode = StorageMode::Wallet;
    emit storageModeChanged();

    // Migrate credentials written by older releases, then remove the plaintext
    // file only if every wallet write succeeded.
    const QString legacyPath = fallbackFilePath();
    if (QFileInfo::exists(legacyPath)) {
        QSettings legacy(legacyPath, QSettings::IniFormat);
        bool migrated = true;
        for (const QString &key : legacy.allKeys()) {
            QDBusInterface target(kWalletService, kWalletPath, kWalletInterface,
                                  QDBusConnection::sessionBus());
            const QDBusReply<int> rc = target.call(QStringLiteral("writePassword"), m_walletHandle,
                                                   kWalletFolder, key,
                                                   legacy.value(key).toString(), appId());
            migrated = migrated && rc.isValid() && rc.value() == 0;
        }
        if (migrated) {
            if (QFile::remove(legacyPath))
                qCInfo(logCore) << "migrated legacy plaintext credentials to KWallet";
            else
                qCWarning(logCore) << "migrated legacy credentials but could not remove"
                                   << legacyPath;
        } else {
            qCWarning(logCore) << "legacy credential migration incomplete; plaintext retained";
        }
    }
    return true;
}

bool SecretsStore::isWalletBacked()
{
    return ensureWallet();
}

SecretsStore::StorageMode SecretsStore::storageMode()
{
    ensureWallet();
    return m_storageMode;
}

QString SecretsStore::fallbackFilePath() const
{
    if (!m_forcedFallbackPath.isEmpty())
        return m_forcedFallbackPath;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/secrets.ini");
}

bool SecretsStore::removeLegacySecret(const QString &key) const
{
    const QString path = fallbackFilePath();
    if (!QFileInfo::exists(path))
        return true;
    QSettings legacy(path, QSettings::IniFormat);
    legacy.remove(key);
    legacy.sync();
    if (legacy.status() != QSettings::NoError || legacy.contains(key))
        return false;
    if (!legacy.allKeys().isEmpty())
        return true;
    return QFile::remove(path) || !QFileInfo::exists(path);
}

bool SecretsStore::writeSecret(const QString &key, const QString &value)
{
    if (ensureWallet()) {
        QDBusInterface *wallet = makeWalletInterface(nullptr);
        const QDBusReply<int> rc = wallet->call(QStringLiteral("writePassword"), m_walletHandle,
                                                kWalletFolder, key, value, appId());
        delete wallet;
        if (rc.isValid() && rc.value() == 0) {
            if (!removeLegacySecret(key)) {
                qCWarning(logCore) << "wallet write succeeded but legacy credential cleanup failed"
                                   << key;
                return false;
            }
            return true;
        }
        qCWarning(logCore) << "wallet writePassword failed for" << key;
        return false;
    }

    if (m_storageMode != StorageMode::PlaintextFallback) {
        m_sessionSecrets.insert(key, value);
        return true;
    }

    QSettings store(fallbackFilePath(), QSettings::IniFormat);
    store.setValue(key, value);
    store.sync();
    const bool written = store.status() == QSettings::NoError && store.value(key).toString() == value;
    const QFile::Permissions ownerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    if (!written || !QFile::setPermissions(fallbackFilePath(), ownerOnly))
        return false;
    const QFile::Permissions actual = QFileInfo(fallbackFilePath()).permissions();
    const QFile::Permissions exposed = QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                                       QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                       QFileDevice::WriteOther | QFileDevice::ExeOther;
    return actual.testFlag(QFileDevice::ReadOwner) &&
           actual.testFlag(QFileDevice::WriteOwner) && !(actual & exposed);
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

    if (m_storageMode != StorageMode::PlaintextFallback)
        return m_sessionSecrets.value(key);

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
        const bool removed = rc.isValid() && rc.value() == 0;
        const bool legacyRemoved = removeLegacySecret(key);
        if (!legacyRemoved)
            qCWarning(logCore) << "could not remove legacy plaintext credential" << key;
        return removed && legacyRemoved;
    }

    if (m_storageMode != StorageMode::PlaintextFallback)
        return m_sessionSecrets.remove(key) > 0 || !m_sessionSecrets.contains(key);

    QSettings store(fallbackFilePath(), QSettings::IniFormat);
    store.remove(key);
    store.sync();
    return store.status() == QSettings::NoError && !store.contains(key);
}

} // namespace strmqt
