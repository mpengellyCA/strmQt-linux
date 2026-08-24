#include "SecretsStore.h"

#include "core/Log.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QPromise>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

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

QDBusMessage walletMessage(const QString &method, const QVariantList &arguments = {})
{
    QDBusMessage message =
        QDBusMessage::createMethodCall(kWalletService, kWalletPath, kWalletInterface, method);
    message.setArguments(arguments);
    return message;
}

template<class Completion>
void watchCall(SecretsStore *store, const QDBusMessage &message, Completion completion)
{
    auto *watcher =
        new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), store);
    const QPointer<SecretsStore> self(store);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
                     [self, watcher, completion = std::move(completion)]() mutable {
                         if (self)
                             completion(watcher->reply());
                         watcher->deleteLater();
                     });
}

QString dbusError(const QDBusMessage &reply)
{
    const QString message = reply.errorMessage();
    return message.isEmpty() ? QStringLiteral("KWallet request failed") : message;
}

template<class T> QFuture<Result<T>> startedFuture(std::shared_ptr<QPromise<Result<T>>> &promise)
{
    promise = std::make_shared<QPromise<Result<T>>>();
    QFuture<Result<T>> future = promise->future();
    promise->start();
    return future;
}

template<class T>
void resolve(const std::shared_ptr<QPromise<Result<T>>> &promise, Result<T> result)
{
    promise->addResult(std::move(result));
    promise->finish();
}

} // namespace

SecretsStore::SecretsStore(QObject *parent) : QObject(parent) {}

SecretsStore::SecretsStore(const QString &fallbackFilePath, QObject *parent)
    : QObject(parent), m_forcedFallbackPath(fallbackFilePath),
      m_storageMode(StorageMode::PlaintextFallback), m_initialization(InitializationState::Ready)
{
}

SecretsStore::~SecretsStore()
{
    const auto canceled = Result<QString>::failure(QStringLiteral("secret store destroyed"));
    if (m_current)
        m_current->finish(canceled);
    while (!m_operations.isEmpty())
        m_operations.dequeue().finish(canceled);
}

QFuture<Result<bool>> SecretsStore::writeSecret(const QString &key, const QString &value)
{
    std::shared_ptr<QPromise<Result<bool>>> promise;
    QFuture<Result<bool>> future = startedFuture(promise);
    enqueue(
        {OperationType::Write, key, value, m_identity, [promise](const Result<QString> &result) {
             resolve(promise, result.ok() ? Result<bool>::success(true)
                                          : Result<bool>::failure(result.error));
         }});
    return future;
}

QFuture<Result<QString>> SecretsStore::readSecret(const QString &key)
{
    std::shared_ptr<QPromise<Result<QString>>> promise;
    QFuture<Result<QString>> future = startedFuture(promise);
    enqueue({OperationType::Read, key, {}, m_identity, [promise](const Result<QString> &result) {
                 resolve(promise, result);
             }});
    return future;
}

QFuture<Result<bool>> SecretsStore::removeSecret(const QString &key)
{
    std::shared_ptr<QPromise<Result<bool>>> promise;
    QFuture<Result<bool>> future = startedFuture(promise);
    enqueue({OperationType::Remove, key, {}, m_identity, [promise](const Result<QString> &result) {
                 resolve(promise, result.ok() ? Result<bool>::success(true)
                                              : Result<bool>::failure(result.error));
             }});
    return future;
}

quint64 SecretsStore::beginIdentity()
{
    return ++m_identity;
}

void SecretsStore::setLegacyFilePathForTests(const QString &path)
{
    if (m_initialization != InitializationState::NotStarted || !m_operations.isEmpty() ||
        m_current) {
        qCWarning(logCore) << "cannot change the legacy secret path after wallet work started";
        return;
    }
    m_forcedFallbackPath = path;
}

void SecretsStore::enqueue(Operation operation)
{
    m_operations.enqueue(std::move(operation));
    // Every public operation yields to the event loop even in session-only and
    // test-file modes. Callers can therefore use one completion contract and no
    // wallet or filesystem path runs inside a QML signal handler.
    QTimer::singleShot(0, this, &SecretsStore::processNext);
}

void SecretsStore::processNext()
{
    if (m_current || m_operations.isEmpty())
        return;
    if (m_initialization != InitializationState::Ready) {
        startWalletInitialization();
        return;
    }

    m_current = std::move(m_operations.head());
    m_operations.dequeue();
    const Operation &operation = *m_current;

    // A queued old read must never restore a retired account, and a queued old
    // write must never be sent after a boundary. Removals are intentionally kept:
    // FIFO serialization guarantees the old removal precedes every new write.
    if (operation.identity != m_identity && operation.type != OperationType::Remove) {
        finishCurrent(Result<QString>::failure(QStringLiteral("secret operation canceled")));
        return;
    }

    if (m_storageMode == StorageMode::SessionOnly) {
        if (operation.type == OperationType::Write) {
            m_sessionSecrets.insert(operation.key, operation.value);
            finishCurrent(Result<QString>::success({}));
        } else if (operation.type == OperationType::Read) {
            finishCurrent(Result<QString>::success(m_sessionSecrets.value(operation.key)));
        } else {
            m_sessionSecrets.remove(operation.key);
            finishCurrent(Result<QString>::success({}));
        }
        return;
    }

    if (m_storageMode == StorageMode::PlaintextFallback) {
        if (operation.type == OperationType::Write) {
            const Result<bool> result = writePlaintextSecret(operation.key, operation.value);
            finishCurrent(result.ok() ? Result<QString>::success({})
                                      : Result<QString>::failure(result.error));
        } else if (operation.type == OperationType::Read) {
            finishCurrent(readPlaintextSecret(operation.key));
        } else {
            const Result<bool> result = removePlaintextSecret(operation.key);
            finishCurrent(result.ok() ? Result<QString>::success({})
                                      : Result<QString>::failure(result.error));
        }
        return;
    }

    if (operation.type == OperationType::Write)
        requestWritePassword(operation.key, operation.value);
    else if (operation.type == OperationType::Read)
        requestReadPassword(operation.key);
    else
        requestRemoveEntry(operation.key);
}

void SecretsStore::startWalletInitialization()
{
    if (m_initialization != InitializationState::NotStarted)
        return;
    if (!walletTransportAvailable()) {
        qCWarning(logCore) << "kwalletd6 not reachable; secrets are session-only";
        setStorageMode(StorageMode::SessionOnly);
        finishInitialization();
        return;
    }
    m_initialization = InitializationState::NetworkWalletPending;
    requestNetworkWallet();
}

void SecretsStore::completeNetworkWallet(bool success, const QString &walletName,
                                         const QString &error)
{
    if (m_initialization != InitializationState::NetworkWalletPending)
        return;
    if (!success || walletName.isEmpty()) {
        qCWarning(logCore) << "networkWallet failed; secrets are session-only:" << error;
        setStorageMode(StorageMode::SessionOnly);
        finishInitialization();
        return;
    }
    m_initialization = InitializationState::OpenPending;
    requestOpenWallet(walletName);
}

void SecretsStore::completeOpenWallet(bool success, int handle, const QString &error)
{
    if (m_initialization != InitializationState::OpenPending)
        return;
    if (!success || handle < 0) {
        qCWarning(logCore) << "wallet open rejected or failed; secrets are session-only:" << error;
        setStorageMode(StorageMode::SessionOnly);
        finishInitialization();
        return;
    }
    m_walletHandle = handle;
    setStorageMode(StorageMode::Wallet);
    startLegacyMigration();
}

void SecretsStore::startLegacyMigration()
{
    const QString path = fallbackFilePath();
    if (!QFileInfo::exists(path)) {
        finishInitialization();
        return;
    }

    QSettings legacy(path, QSettings::IniFormat);
    m_legacyEntries.clear();
    const QStringList keys = legacy.allKeys();
    m_legacyEntries.reserve(keys.size());
    for (const QString &key : keys)
        m_legacyEntries.append({key, legacy.value(key).toString()});
    m_legacyIndex = 0;
    m_legacyMigrationSucceeded = legacy.status() == QSettings::NoError;
    m_initialization = InitializationState::Migrating;
    migrateNext();
}

void SecretsStore::migrateNext()
{
    if (m_legacyIndex < m_legacyEntries.size()) {
        const auto &[key, value] = m_legacyEntries.at(m_legacyIndex);
        requestWritePassword(key, value);
        return;
    }

    const QString path = fallbackFilePath();
    if (m_legacyMigrationSucceeded) {
        if (QFile::remove(path) || !QFileInfo::exists(path))
            qCInfo(logCore) << "migrated legacy plaintext credentials to KWallet";
        else
            qCWarning(logCore) << "migrated legacy credentials but could not remove" << path;
    } else {
        qCWarning(logCore) << "legacy credential migration incomplete; plaintext retained at"
                           << path;
    }
    m_legacyEntries.clear();
    finishInitialization();
}

void SecretsStore::finishInitialization()
{
    m_initialization = InitializationState::Ready;
    QTimer::singleShot(0, this, &SecretsStore::processNext);
}

void SecretsStore::completeWritePassword(bool success, const QString &error)
{
    if (m_initialization == InitializationState::Migrating) {
        m_legacyMigrationSucceeded = m_legacyMigrationSucceeded && success;
        ++m_legacyIndex;
        migrateNext();
        return;
    }
    if (!m_current || m_current->type != OperationType::Write)
        return;
    if (success && !removeLegacySecret(m_current->key)) {
        qCWarning(logCore) << "stored" << m_current->key
                           << "in the wallet but could not remove the older plaintext copy;"
                           << "delete it by hand:" << fallbackFilePath();
    }
    finishCurrent(success ? Result<QString>::success({}) : Result<QString>::failure(error));
}

void SecretsStore::completeReadPassword(bool success, const QString &value, const QString &error)
{
    if (!m_current || m_current->type != OperationType::Read)
        return;
    finishCurrent(success ? Result<QString>::success(value) : Result<QString>::failure(error));
}

void SecretsStore::completeRemoveEntry(bool success, const QString &error)
{
    if (!m_current || m_current->type != OperationType::Remove)
        return;
    if (!removeLegacySecret(m_current->key)) {
        qCWarning(logCore) << "could not remove legacy plaintext credential" << m_current->key
                           << "at" << fallbackFilePath();
    }
    finishCurrent(success ? Result<QString>::success({}) : Result<QString>::failure(error));
}

void SecretsStore::finishCurrent(const Result<QString> &result)
{
    if (!m_current)
        return;
    Operation operation = std::move(*m_current);
    m_current.reset();
    if (operation.identity != m_identity)
        operation.finish(Result<QString>::failure(QStringLiteral("secret operation canceled")));
    else
        operation.finish(result);
    QTimer::singleShot(0, this, &SecretsStore::processNext);
}

void SecretsStore::setStorageMode(StorageMode mode)
{
    if (m_storageMode == mode)
        return;
    m_storageMode = mode;
    emit storageModeChanged();
}

QString SecretsStore::fallbackFilePath() const
{
    if (!m_forcedFallbackPath.isEmpty())
        return m_forcedFallbackPath;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
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

Result<bool> SecretsStore::writePlaintextSecret(const QString &key, const QString &value)
{
    const QString path = fallbackFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSettings store(path, QSettings::IniFormat);
    store.setValue(key, value);
    store.sync();
    const bool written =
        store.status() == QSettings::NoError && store.value(key).toString() == value;
    const QFile::Permissions ownerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    const QFile::Permissions exposed = QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                                       QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                       QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (written && QFile::setPermissions(path, ownerOnly)) {
        const QFile::Permissions actual = QFileInfo(path).permissions();
        if (actual.testFlag(QFileDevice::ReadOwner) && actual.testFlag(QFileDevice::WriteOwner) &&
            !(actual & exposed))
            return Result<bool>::success(true);
    }
    removeLegacySecret(key);
    return Result<bool>::failure(QStringLiteral("could not safely write test secret file"));
}

Result<QString> SecretsStore::readPlaintextSecret(const QString &key) const
{
    QSettings store(fallbackFilePath(), QSettings::IniFormat);
    if (store.status() != QSettings::NoError)
        return Result<QString>::failure(QStringLiteral("could not read test secret file"));
    return Result<QString>::success(store.value(key).toString());
}

Result<bool> SecretsStore::removePlaintextSecret(const QString &key) const
{
    return removeLegacySecret(key)
               ? Result<bool>::success(true)
               : Result<bool>::failure(QStringLiteral("could not remove test secret"));
}

bool SecretsStore::walletTransportAvailable() const
{
    return QDBusConnection::sessionBus().isConnected();
}

void SecretsStore::requestNetworkWallet()
{
    watchCall(this, walletMessage(QStringLiteral("networkWallet")),
              [this](const QDBusMessage &reply) {
                  const QVariantList arguments = reply.arguments();
                  const bool valid = reply.type() != QDBusMessage::ErrorMessage &&
                                     arguments.size() == 1 &&
                                     arguments.first().metaType().id() == QMetaType::QString;
                  completeNetworkWallet(valid, valid ? arguments.first().toString() : QString(),
                                        dbusError(reply));
              });
}

void SecretsStore::requestOpenWallet(const QString &walletName)
{
    watchCall(this,
              walletMessage(QStringLiteral("open"),
                            {walletName, QVariant::fromValue(qlonglong(0)), appId()}),
              [this](const QDBusMessage &reply) {
                  const QVariantList arguments = reply.arguments();
                  const bool valid = reply.type() != QDBusMessage::ErrorMessage &&
                                     arguments.size() == 1 &&
                                     arguments.first().metaType().id() == QMetaType::Int;
                  completeOpenWallet(valid, valid ? arguments.first().toInt() : -1,
                                     dbusError(reply));
              });
}

void SecretsStore::requestWritePassword(const QString &key, const QString &value)
{
    watchCall(this,
              walletMessage(QStringLiteral("writePassword"),
                            {m_walletHandle, kWalletFolder, key, value, appId()}),
              [this](const QDBusMessage &reply) {
                  const QVariantList arguments = reply.arguments();
                  const bool valid = reply.type() != QDBusMessage::ErrorMessage &&
                                     arguments.size() == 1 &&
                                     arguments.first().metaType().id() == QMetaType::Int;
                  completeWritePassword(valid && arguments.first().toInt() == 0, dbusError(reply));
              });
}

void SecretsStore::requestReadPassword(const QString &key)
{
    watchCall(this,
              walletMessage(QStringLiteral("readPassword"),
                            {m_walletHandle, kWalletFolder, key, appId()}),
              [this](const QDBusMessage &reply) {
                  const QVariantList arguments = reply.arguments();
                  const bool valid = reply.type() != QDBusMessage::ErrorMessage &&
                                     arguments.size() == 1 &&
                                     arguments.first().metaType().id() == QMetaType::QString;
                  completeReadPassword(valid, valid ? arguments.first().toString() : QString(),
                                       dbusError(reply));
              });
}

void SecretsStore::requestRemoveEntry(const QString &key)
{
    watchCall(
        this,
        walletMessage(QStringLiteral("removeEntry"), {m_walletHandle, kWalletFolder, key, appId()}),
        [this](const QDBusMessage &reply) {
            const QVariantList arguments = reply.arguments();
            const bool valid = reply.type() != QDBusMessage::ErrorMessage &&
                               arguments.size() == 1 &&
                               arguments.first().metaType().id() == QMetaType::Int;
            completeRemoveEntry(valid && arguments.first().toInt() == 0, dbusError(reply));
        });
}

} // namespace strmqt
