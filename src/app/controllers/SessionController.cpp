#include "SessionController.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "platform/SecretsStore.h"
#include "server/emby/EmbyClient.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QPromise>

#include <memory>

namespace strmqt {

namespace {

QString serverUrlError(const QUrl &url)
{
    if (url.isEmpty())
        return {};
    if (!url.isValid() || url.host().isEmpty())
        return QCoreApplication::translate("SessionController",
                                           "Enter a complete server address with a host.");
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("https") && scheme != QLatin1String("http"))
        return QCoreApplication::translate("SessionController",
                                           "The server address must use HTTPS or HTTP.");
    if (!url.userName().isEmpty() || !url.password().isEmpty())
        return QCoreApplication::translate("SessionController",
                                           "Do not include credentials in the server address.");
    if (url.hasQuery() || url.hasFragment())
        return QCoreApplication::translate("SessionController",
                                           "Remove the query or fragment from the server address.");
    if (scheme == QLatin1String("http")) {
        QHostAddress address;
        const bool loopback = url.host().compare(QLatin1String("localhost"),
                                                  Qt::CaseInsensitive) == 0 ||
                              (address.setAddress(url.host()) && address.isLoopback());
        if (!loopback)
            return QCoreApplication::translate(
                "SessionController",
                "HTTPS is required for non-local servers because HTTP exposes your token.");
    }
    return {};
}

// Normalized comparison form for registry lookups and current-account checks.
QString accountUrl(const QUrl &serverUrl)
{
    return serverUrl.adjusted(QUrl::StripTrailingSlash).toString(QUrl::FullyEncoded);
}

} // namespace

SessionController::SessionController(Settings *settings, SecretsStore *secrets,
                                     emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_settings(settings), m_secrets(secrets), m_client(client)
{
    connect(m_secrets, &SecretsStore::storageModeChanged, this,
            &SessionController::secretStorageChanged);
}

QString SessionController::username() const
{
    return m_settings->username();
}

QUrl SessionController::serverUrl() const
{
    return m_settings->serverUrl();
}

void SessionController::setServerUrl(const QUrl &url)
{
    const QString validationError = serverUrlError(url);
    if (!validationError.isEmpty()) {
        setError(validationError);
        return;
    }
    QUrl normalized = url.adjusted(QUrl::StripTrailingSlash);
    if (normalized.path() == QLatin1String("/"))
        normalized.setPath({});
    if (normalized == m_settings->serverUrl())
        return;
    beginSessionBoundary();
    // Leaving a server ends the local session but keeps its accounts: profiles
    // are tagged by server URL, so coming back is a picker tap, not a re-login.
    clearLocalSession();
    m_settings->setServerUrl(normalized);
    m_client->setBaseUrl(normalized);
    setError({});
    emit serverUrlChanged();
}

QString SessionController::playbackEngine() const
{
    return m_settings->playbackEngine();
}

QString SessionController::secretStorage() const
{
    switch (m_secrets->storageMode()) {
    case SecretsStore::StorageMode::Wallet:
        return QStringLiteral("wallet");
    case SecretsStore::StorageMode::PlaintextFallback:
        return QStringLiteral("vault");
    case SecretsStore::StorageMode::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

QVariantList SessionController::profiles() const
{
    return m_settings->accountProfiles();
}

QString SessionController::profileAvatarUrl(const QString &serverUrl, const QString &userId) const
{
    return emby::EmbyClient::userImageUrl(QUrl(serverUrl), userId).toString();
}

void SessionController::setPlaybackEngine(const QString &engine)
{
    if (engine == m_settings->playbackEngine())
        return;
    m_settings->setPlaybackEngine(engine);
    emit playbackEngineChanged();
}

void SessionController::restore()
{
    if (m_busy)
        return;
    m_client->setBaseUrl(m_settings->serverUrl());
    const QString userId = m_settings->userId();
    if (userId.isEmpty())
        return;
    // A credential is not a session: without an address there is nowhere to
    // send it. Restoring on the token alone brought the app up looking signed
    // in, with every request failing as `Protocol "" is unknown` and no way
    // back to the login screen short of finding Sign out. Upgrades from a build
    // that carried a baked-in server default land here, so it is a real path,
    // not a defensive one.
    if (m_settings->serverUrl().isEmpty()) {
        qCWarning(logApp) << "session for" << m_settings->username()
                          << "has no server address; signing in again";
        setError(tr("Enter the address of your Emby server to sign in again."));
        return;
    }

    const quint64 epoch = m_epoch;
    m_restorePending = true;
    setBusy(true);
    readAccountToken(m_settings->serverUrl(), userId)
        .then(this, [this, epoch, userId](const Result<QString> &result) {
            if (epoch != m_epoch)
                return;
            m_restorePending = false;
            setBusy(false);
            if (!result.ok()) {
                qCWarning(logApp) << "could not read persisted access token:" << result.error;
                setError(tr("Could not read the saved sign-in. Sign in again to continue."));
                return;
            }
            if (result.value.isEmpty())
                return;
            m_client->setSession(result.value, userId);
            m_settings->migrateLegacySessionData();
            // Pre-profiles installs land in the registry here, on their first
            // restore; for everyone else this just refreshes lastUsed.
            m_settings->upsertAccountProfile(m_settings->serverUrl(), userId,
                                             m_settings->username());
            emit profilesChanged();
            setAuthenticated(true);
            qCInfo(logApp) << "session restored for user" << m_settings->username();
        });
}

void SessionController::login(const QString &username, const QString &password)
{
    if (m_settings->serverUrl().isEmpty()) {
        // Reached by signing in with the server field left blank. Without this
        // the request fails somewhere in QtNetwork and surfaces as a URL error,
        // which does not tell the user what to do about it.
        setError(tr("Enter the address of your Emby server."));
        return;
    }
    if (const QString validationError = serverUrlError(m_settings->serverUrl());
        !validationError.isEmpty()) {
        setError(validationError);
        return;
    }
    // A user who is already at the login page may supersede a slow wallet
    // restore. Double-submit during an actual authentication remains blocked.
    if (m_busy && !m_restorePending)
        return;
    const quint64 epoch = beginSessionBoundary();
    // Signing in as another account must not forget the previous one — that is
    // what the profile picker is for. Only the local session is ended.
    clearLocalSession();
    setError({});
    setBusy(true);
    m_client->setBaseUrl(m_settings->serverUrl());

    m_client->authenticateByName(username, password)
        .then(this, [this, epoch](const Result<SessionInfo> &result) {
            if (epoch != m_epoch)
                return;
            if (!result.ok()) {
                setBusy(false);
                setError(result.error);
                return;
            }
            m_settings->setUsername(result.value.user.name);
            m_settings->setUserId(result.value.user.id);
            m_client->setSession(result.value.accessToken, result.value.user.id);
            // Server and user are both known only now, which is the earliest
            // point the pre-scoping keys have an owner to be adopted by.
            m_settings->migrateLegacySessionData();
            m_settings->upsertAccountProfile(m_settings->serverUrl(), result.value.user.id,
                                             result.value.user.name);
            emit profilesChanged();
            const QString key =
                Settings::tokenSecretKeyFor(m_settings->serverUrl(), result.value.user.id);
            m_secrets->writeSecret(key, result.value.accessToken)
                .then(this, [this, epoch](const Result<bool> &stored) {
                    if (epoch != m_epoch)
                        return;
                    if (!stored.ok()) {
                        qCWarning(logApp) << "could not persist access token:" << stored.error;
                    } else {
                        // A flat pre-scoping token still present here was either
                        // not adopted by restore() or belonged to the identity
                        // this login superseded; the scoped write above is the
                        // record now. Only once it succeeded, though.
                        m_secrets->removeSecret(Settings::legacyTokenSecretKey());
                    }
                    setBusy(false);
                    setAuthenticated(true);
                });
        });
}

void SessionController::logout()
{
    const quint64 epoch = beginSessionBoundary();
    const QUrl url = m_settings->serverUrl();
    const QString userId = m_settings->userId();
    const QString key = Settings::tokenSecretKeyFor(url, userId);
    if (!key.isEmpty())
        m_secrets->removeSecret(key).then(this, [this, epoch](const Result<bool> &removed) {
            if (epoch == m_epoch && !removed.ok())
                qCWarning(logApp) << "could not remove persisted access token:" << removed.error;
        });
    if (!userId.isEmpty()) {
        m_settings->removeAccountProfile(url, userId);
        emit profilesChanged();
    }
    clearLocalSession();
}

void SessionController::switchUser()
{
    // Back to the profile picker without forgetting anything: the account keeps
    // its token and registry entry and stays the one startup auto-resumes.
    beginSessionBoundary();
    m_client->setSession({}, {});
    setAuthenticated(false);
    setError({});
}

void SessionController::selectProfile(const QString &serverUrl, const QString &userId)
{
    if (m_busy && !m_restorePending)
        return;
    const QUrl url(serverUrl);
    if (url.isEmpty() || userId.isEmpty())
        return;
    // The registry carries the display name; the picker only offers what it lists.
    QString name;
    const QVariantList registry = m_settings->accountProfiles();
    for (const QVariant &entry : registry) {
        const QVariantMap profile = entry.toMap();
        if (profile.value(QStringLiteral("userId")).toString() == userId &&
            profile.value(QStringLiteral("serverUrl")).toString() == accountUrl(url)) {
            name = profile.value(QStringLiteral("username")).toString();
            break;
        }
    }

    const quint64 epoch = beginSessionBoundary();
    setError({});
    setBusy(true);
    m_settings->setServerUrl(url);
    emit serverUrlChanged();
    m_settings->setUserId(userId);
    if (!name.isEmpty())
        m_settings->setUsername(name);
    m_client->setBaseUrl(url);

    readAccountToken(url, userId).then(this, [this, epoch](const Result<QString> &result) {
        if (epoch != m_epoch)
            return;
        setBusy(false);
        if (!result.ok()) {
            qCWarning(logApp) << "could not read persisted access token:" << result.error;
            setError(tr("Could not read the saved sign-in. Sign in again to continue."));
            return;
        }
        if (result.value.isEmpty()) {
            // The account is known but its token is gone — expired elsewhere or
            // never persisted. The picker hands back to the password form.
            setError(tr("The saved sign-in is no longer valid. Sign in again to continue."));
            return;
        }
        m_client->setSession(result.value, m_settings->userId());
        m_settings->migrateLegacySessionData();
        m_settings->upsertAccountProfile(m_settings->serverUrl(), m_settings->userId(),
                                         m_settings->username());
        emit profilesChanged();
        setAuthenticated(true);
        qCInfo(logApp) << "session restored for user" << m_settings->username();
    });
}

void SessionController::removeProfile(const QString &serverUrl, const QString &userId)
{
    const QUrl url(serverUrl);
    if (url.isEmpty() || userId.isEmpty())
        return;
    // Forgetting the account you are signed in as is signing out.
    if (m_authenticated && userId == m_settings->userId() &&
        accountUrl(url) == accountUrl(m_settings->serverUrl())) {
        logout();
        return;
    }
    const QString key = Settings::tokenSecretKeyFor(url, userId);
    if (!key.isEmpty())
        m_secrets->removeSecret(key).then(this, [this](const Result<bool> &removed) {
            if (!removed.ok())
                qCWarning(logApp) << "could not remove persisted access token:" << removed.error;
        });
    m_settings->removeAccountProfile(url, userId);
    // A forgotten current-but-inactive account must not leave a dangling pointer
    // for the next startup restore.
    if (userId == m_settings->userId() && accountUrl(url) == accountUrl(m_settings->serverUrl()))
        clearLocalSession();
    emit profilesChanged();
}

QFuture<Result<QString>> SessionController::readAccountToken(const QUrl &serverUrl,
                                                             const QString &userId)
{
    // Two stages (scoped key, then the flat pre-scoping key) settled by hand:
    // this Qt's QFuture::then does not unwrap a continuation that returns a
    // future, so a manual promise keeps the chain legible.
    auto promise = std::make_shared<QPromise<Result<QString>>>();
    QFuture<Result<QString>> future = promise->future();
    promise->start();
    const QString scopedKey = Settings::tokenSecretKeyFor(serverUrl, userId);
    m_secrets->readSecret(scopedKey)
        .then(this, [this, promise, scopedKey](const Result<QString> &scoped) {
            if (!scoped.ok() || !scoped.value.isEmpty()) {
                promise->addResult(scoped);
                promise->finish();
                return;
            }
            // Pre-scoping installs kept one flat token. Adopt it into this
            // account's scope; the flat key is forgotten only once the scoped
            // copy is safely stored — it is the last resort if the write fails.
            m_secrets->readSecret(Settings::legacyTokenSecretKey())
                .then(this, [this, promise, scopedKey](const Result<QString> &legacy) {
                    if (legacy.ok() && !legacy.value.isEmpty()) {
                        m_secrets->writeSecret(scopedKey, legacy.value)
                            .then(this, [this](const Result<bool> &written) {
                                // The scoped copy exists now; drop the flat one.
                                if (written.ok())
                                    m_secrets->removeSecret(Settings::legacyTokenSecretKey());
                            });
                    }
                    promise->addResult(legacy);
                    promise->finish();
                });
        });
    return future;
}

quint64 SessionController::beginSessionBoundary()
{
    const quint64 epoch = ++m_epoch;
    m_secrets->beginIdentity();
    m_restorePending = false;
    // The boundary itself retires server work, not the credential clearing that
    // follows it: logging out mid-login leaves the client's identity unchanged
    // (empty to empty), and that reply must still be dropped rather than
    // authenticating the session the user just left.
    m_client->retireOutstandingRequests();
    emit sessionBoundaryChanged(epoch);
    setBusy(false);
    return epoch;
}

void SessionController::clearLocalSession()
{
    m_settings->setUserId({});
    m_client->setSession({}, {});
    setAuthenticated(false);
}

void SessionController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void SessionController::setError(const QString &message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorMessageChanged();
}

void SessionController::setAuthenticated(bool authenticated)
{
    if (m_authenticated == authenticated)
        return;
    m_authenticated = authenticated;
    emit authenticatedChanged();
}

} // namespace strmqt
