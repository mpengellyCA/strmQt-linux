#include "SessionController.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "platform/SecretsStore.h"
#include "server/emby/EmbyClient.h"

#include <QCoreApplication>
#include <QHostAddress>

namespace strmqt {

namespace {
const auto kTokenSecretKey = QStringLiteral("emby/accessToken");

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
} // namespace

SessionController::SessionController(Settings *settings, SecretsStore *secrets,
                                     emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_settings(settings), m_secrets(secrets), m_client(client)
{
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
    clearCredentials();
    m_settings->setServerUrl(normalized);
    m_client->setBaseUrl(normalized);
    setError({});
    emit serverUrlChanged();
}

QString SessionController::playbackEngine() const
{
    return m_settings->playbackEngine();
}

void SessionController::setPlaybackEngine(const QString &engine)
{
    if (engine == m_settings->playbackEngine())
        return;
    m_settings->setPlaybackEngine(engine);
    emit playbackEngineChanged();
}

bool SessionController::restore()
{
    m_client->setBaseUrl(m_settings->serverUrl());
    const QString token = m_secrets->readSecret(kTokenSecretKey);
    const QString userId = m_settings->userId();
    if (token.isEmpty() || userId.isEmpty())
        return false;
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
        return false;
    }
    m_client->setSession(token, userId);
    m_settings->migrateLegacySessionData();
    setAuthenticated(true);
    qCInfo(logApp) << "session restored for user" << m_settings->username();
    return true;
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
    if (m_busy)
        return;
    const quint64 epoch = beginSessionBoundary();
    clearCredentials();
    setError({});
    setBusy(true);
    m_client->setBaseUrl(m_settings->serverUrl());

    m_client->authenticateByName(username, password)
        .then(this, [this, epoch](const Result<SessionInfo> &result) {
            if (epoch != m_epoch)
                return;
            setBusy(false);
            if (!result.ok()) {
                setError(result.error);
                return;
            }
            m_settings->setUsername(result.value.user.name);
            m_settings->setUserId(result.value.user.id);
            m_client->setSession(result.value.accessToken, result.value.user.id);
            // Server and user are both known only now, which is the earliest
            // point the pre-scoping keys have an owner to be adopted by.
            m_settings->migrateLegacySessionData();
            if (!m_secrets->writeSecret(kTokenSecretKey, result.value.accessToken))
                qCWarning(logApp) << "could not persist access token";
            setAuthenticated(true);
        });
}

void SessionController::logout()
{
    beginSessionBoundary();
    clearCredentials();
}

void SessionController::switchUser()
{
    // Identical to logout today, and named separately on purpose: the two are
    // different intents, and "switch user" must never grow into "forget the
    // server" if logout ever does.
    logout();
    loadPublicUsers();
}

void SessionController::loadPublicUsers()
{
    const quint64 epoch = m_epoch;
    m_client->publicUsers().then(this, [this, epoch](const Result<QList<MediaItem>> &result) {
        if (epoch != m_epoch)
            return;
        if (!result.ok()) {
            // Not an error worth showing: a server may simply advertise nobody.
            qCDebug(logApp) << "public users unavailable:" << result.error;
            return;
        }
        QVariantList users;
        for (const MediaItem &user : result.value) {
            if (user.name.isEmpty())
                continue;
            QVariantMap map;
            map.insert(QStringLiteral("id"), user.id);
            map.insert(QStringLiteral("name"), user.name);
            map.insert(QStringLiteral("imageUrl"),
                       user.primaryImageTag.isEmpty()
                           ? QString()
                           : QStringLiteral("image://emby/%1/Primary/%2")
                                 .arg(user.id, user.primaryImageTag));
            users.append(map);
        }
        m_publicUsers = users;
        emit publicUsersChanged();
    });
}

quint64 SessionController::beginSessionBoundary()
{
    const quint64 epoch = ++m_epoch;
    // The boundary itself retires server work, not the credential clearing that
    // follows it: logging out mid-login leaves the client's identity unchanged
    // (empty to empty), and that reply must still be dropped rather than
    // authenticating the session the user just left.
    m_client->retireOutstandingRequests();
    emit sessionBoundaryChanged(epoch);
    setBusy(false);
    if (!m_publicUsers.isEmpty()) {
        m_publicUsers.clear();
        emit publicUsersChanged();
    }
    return epoch;
}

void SessionController::clearCredentials()
{
    if (!m_secrets->removeSecret(kTokenSecretKey))
        qCWarning(logApp) << "could not remove persisted access token";
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
