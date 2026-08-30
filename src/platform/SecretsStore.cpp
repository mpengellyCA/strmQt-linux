#include "SecretsStore.h"

#include "core/Log.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QPromise>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>

namespace strmqt {

namespace {

const auto kWalletService = QStringLiteral("org.kde.kwalletd6");
const auto kWalletPath = QStringLiteral("/modules/kwalletd6");
const auto kWalletInterface = QStringLiteral("org.kde.KWallet");
const auto kWalletFolder = QStringLiteral("StrmQt");

using LegacyEntries = QList<QPair<QString, QString>>;

struct LegacyScan
{
    bool exists = false;
    LegacyEntries entries;
    QString error;

    bool ok() const { return error.isEmpty(); }
};

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
                         const QDBusMessage reply = watcher->reply();
                         // Future settlement below is a reentrancy point: a
                         // context-free continuation may synchronously delete
                         // the store and its children. Detach and retire the
                         // watcher before invoking any completion, then never
                         // touch its raw pointer again.
                         watcher->setParent(QCoreApplication::instance());
                         watcher->deleteLater();
                         if (self)
                             completion(reply);
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

// QSettings is reentrant, not an asynchronous API. Each task constructs and
// destroys its own QSettings instance on a QThreadPool thread, and returns only
// value types to a watcher owned by the GUI-thread SecretsStore. Destroying the
// store destroys the watcher, so a late task completion cannot call back into a
// dead QObject; the worker-owned promise can still finish independently.
template<class T, class Work, class Completion>
void runLegacyTask(SecretsStore *store, Work work, Completion completion)
{
    auto promise = std::make_shared<QPromise<T>>();
    promise->start();
    auto *watcher = new QFutureWatcher<T>(store);
    QObject::connect(watcher, &QFutureWatcher<T>::finished, watcher,
                     [watcher, completion = std::move(completion)]() mutable {
                         T result = watcher->result();
                         // See watchCall(): completion can synchronously delete
                         // SecretsStore through the public operation's future.
                         watcher->setParent(QCoreApplication::instance());
                         watcher->deleteLater();
                         completion(std::move(result));
                     });
    watcher->setFuture(promise->future());
    QThreadPool::globalInstance()->start([promise, work = std::move(work)]() mutable {
        promise->addResult(work());
        promise->finish();
    });
}

LegacyScan scanLegacyFile(const QString &path)
{
    LegacyScan result;
    result.exists = QFileInfo::exists(path);
    if (!result.exists)
        return result;

    QSettings legacy(path, QSettings::IniFormat);
    const QStringList keys = legacy.allKeys();
    result.entries.reserve(keys.size());
    for (const QString &key : keys)
        result.entries.append({key, legacy.value(key).toString()});
    if (legacy.status() != QSettings::NoError)
        result.error = QStringLiteral("could not read legacy secret file");
    return result;
}

Result<bool> removeLegacySecretFile(const QString &path, const QString &key)
{
    if (!QFileInfo::exists(path))
        return Result<bool>::success(true);
    QSettings legacy(path, QSettings::IniFormat);
    legacy.remove(key);
    legacy.sync();
    if (legacy.status() != QSettings::NoError || legacy.contains(key))
        return Result<bool>::failure(QStringLiteral("could not remove legacy plaintext secret"));
    if (!legacy.allKeys().isEmpty())
        return Result<bool>::success(true);
    if (QFile::remove(path) || !QFileInfo::exists(path))
        return Result<bool>::success(true);
    return Result<bool>::failure(QStringLiteral("could not remove empty legacy secret file"));
}

Result<bool> removeLegacyFile(const QString &path)
{
    if (!QFileInfo::exists(path) || QFile::remove(path) || !QFileInfo::exists(path))
        return Result<bool>::success(true);
    return Result<bool>::failure(QStringLiteral("could not remove legacy secret file"));
}

Result<bool> writePlaintextSecretFile(const QString &path, const QString &key, const QString &value)
{
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
    removeLegacySecretFile(path, key);
    return Result<bool>::failure(QStringLiteral("could not safely write the vault file"));
}

Result<QString> readPlaintextSecretFile(const QString &path, const QString &key)
{
    QSettings store(path, QSettings::IniFormat);
    if (store.status() != QSettings::NoError)
        return Result<QString>::failure(QStringLiteral("could not read the vault file"));
    return Result<QString>::success(store.value(key).toString());
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
    // Every public operation yields to the event loop even in vault-file mode.
    // Callers can therefore use one completion contract and no wallet or
    // filesystem path runs inside a QML signal handler.
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

    if (m_storageMode == StorageMode::PlaintextFallback) {
        const QString path = fallbackFilePath();
        const QString key = operation.key;
        if (operation.type == OperationType::Write) {
            const QString value = operation.value;
            runLegacyTask<Result<bool>>(
                this, [path, key, value]() { return writePlaintextSecretFile(path, key, value); },
                [this](const Result<bool> &result) {
                    finishCurrent(result.ok() ? Result<QString>::success({})
                                              : Result<QString>::failure(result.error));
                });
        } else if (operation.type == OperationType::Read) {
            runLegacyTask<Result<QString>>(
                this, [path, key]() { return readPlaintextSecretFile(path, key); },
                [this](const Result<QString> &result) { finishCurrent(result); });
        } else {
            runLegacyTask<Result<bool>>(
                this, [path, key]() { return removeLegacySecretFile(path, key); },
                [this](const Result<bool> &result) {
                    finishCurrent(result.ok() ? Result<QString>::success({})
                                              : Result<QString>::failure(result.error));
                });
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
        qCWarning(logCore) << "kwalletd6 not reachable; secrets fall back to the vault file"
                           << fallbackFilePath();
        setStorageMode(StorageMode::PlaintextFallback);
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
        qCWarning(logCore) << "networkWallet failed; secrets fall back to the vault file"
                           << fallbackFilePath() << error;
        setStorageMode(StorageMode::PlaintextFallback);
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
        qCWarning(logCore) << "wallet open rejected or failed; secrets fall back to the vault file"
                           << fallbackFilePath() << error;
        setStorageMode(StorageMode::PlaintextFallback);
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
    m_legacyEntries.clear();
    m_initialization = InitializationState::LegacyScanPending;
    runLegacyTask<LegacyScan>(
        this, [path]() { return scanLegacyFile(path); },
        [this, path](LegacyScan scan) {
            if (m_initialization != InitializationState::LegacyScanPending)
                return;
            if (!scan.ok()) {
                qCWarning(logCore) << "could not scan legacy credentials; plaintext retained at"
                                   << path << scan.error;
                finishInitialization();
                return;
            }
            if (!scan.exists) {
                finishInitialization();
                return;
            }
            m_legacyEntries = std::move(scan.entries);
            m_legacyIndex = 0;
            m_legacyMigrationSucceeded = true;
            m_initialization = InitializationState::Migrating;
            migrateNext();
        });
}

void SecretsStore::migrateNext()
{
    if (m_legacyIndex < m_legacyEntries.size()) {
        const auto &[key, value] = m_legacyEntries.at(m_legacyIndex);
        requestWritePassword(key, value);
        return;
    }

    if (!m_legacyMigrationSucceeded) {
        const QString path = fallbackFilePath();
        qCWarning(logCore) << "legacy credential migration incomplete; plaintext retained at"
                           << path;
        m_legacyEntries.clear();
        finishInitialization();
        return;
    }

    const QString path = fallbackFilePath();
    m_initialization = InitializationState::LegacyCleanupPending;
    runLegacyTask<Result<bool>>(
        this, [path]() { return removeLegacyFile(path); },
        [this, path](const Result<bool> &removed) {
            if (m_initialization != InitializationState::LegacyCleanupPending)
                return;
            if (removed.ok())
                qCInfo(logCore) << "migrated legacy plaintext credentials to KWallet";
            else
                qCWarning(logCore)
                    << "migrated legacy credentials but could not remove" << path << removed.error;
            m_legacyEntries.clear();
            finishInitialization();
        });
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
    if (success) {
        removeLegacyForCurrent(Result<QString>::success({}));
        return;
    }
    // The wallet refused the write (seen in the wild: kwalletd6 answers -1 and
    // leaves an empty entry behind). Persist to the vault file instead, and
    // demote for the rest of the process so later operations stop trusting the
    // wallet's copy of this key. storageModeChanged drives the user warning.
    qCWarning(logCore) << "wallet write failed; storing the secret in the vault file instead:"
                       << error;
    const QString path = fallbackFilePath();
    const QString key = m_current->key;
    const QString value = m_current->value;
    runLegacyTask<Result<bool>>(
        this, [path, key, value]() { return writePlaintextSecretFile(path, key, value); },
        [this](const Result<bool> &written) {
            if (written.ok())
                setStorageMode(StorageMode::PlaintextFallback);
            finishCurrent(written.ok() ? Result<QString>::success({})
                                       : Result<QString>::failure(written.error));
        });
}

void SecretsStore::completeReadPassword(bool success, const QString &value, const QString &error)
{
    if (!m_current || m_current->type != OperationType::Read)
        return;
    if (success && !value.isEmpty()) {
        finishCurrent(Result<QString>::success(value));
        return;
    }
    // An empty or failed wallet read can shadow a vault copy written while the
    // wallet was refusing writes — consult the vault before answering.
    const QString path = fallbackFilePath();
    const QString key = m_current->key;
    runLegacyTask<Result<QString>>(
        this, [path, key]() { return readPlaintextSecretFile(path, key); },
        [this, success, value, error](const Result<QString> &vault) {
            if (vault.ok() && !vault.value.isEmpty()) {
                finishCurrent(vault);
                return;
            }
            finishCurrent(success ? Result<QString>::success(value)
                                  : Result<QString>::failure(error));
        });
}

void SecretsStore::completeRemoveEntry(bool success, const QString &error)
{
    if (!m_current || m_current->type != OperationType::Remove)
        return;
    removeLegacyForCurrent(success ? Result<QString>::success({})
                                   : Result<QString>::failure(error));
}

void SecretsStore::removeLegacyForCurrent(Result<QString> operationResult)
{
    if (!m_current)
        return;
    const QString path = fallbackFilePath();
    const QString key = m_current->key;
    runLegacyTask<Result<bool>>(
        this, [path, key]() { return removeLegacySecretFile(path, key); },
        [this, path, key,
         operationResult = std::move(operationResult)](const Result<bool> &removed) mutable {
            if (!removed.ok()) {
                qCWarning(logCore) << "could not remove legacy plaintext credential" << key << "at"
                                   << path << removed.error;
                if (operationResult.ok())
                    operationResult = Result<QString>::failure(removed.error);
            }
            finishCurrent(operationResult);
        });
}

void SecretsStore::finishCurrent(const Result<QString> &result)
{
    if (!m_current)
        return;
    Operation operation = std::move(*m_current);
    m_current.reset();
    // Schedule before settling the promise. A context-free QFuture::then can
    // synchronously destroy this store from operation.finish(); the timer is
    // context-bound and will be canceled by QObject destruction.
    QTimer::singleShot(0, this, &SecretsStore::processNext);
    if (operation.identity != m_identity)
        operation.finish(Result<QString>::failure(QStringLiteral("secret operation canceled")));
    else
        operation.finish(result);
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
