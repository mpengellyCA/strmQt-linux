#include "SessionController.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "platform/SecretsStore.h"
#include "server/emby/EmbyClient.h"

namespace strmqt {

namespace {
const auto kTokenSecretKey = QStringLiteral("emby/accessToken");
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
    m_settings->setServerUrl(url);
    m_client->setBaseUrl(url);
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
    if (m_busy)
        return;
    setError({});
    setBusy(true);
    m_client->setBaseUrl(m_settings->serverUrl());

    m_client->authenticateByName(username, password)
        .then(this, [this](const Result<SessionInfo> &result) {
            setBusy(false);
            if (!result.ok()) {
                setError(result.error);
                return;
            }
            m_settings->setUsername(result.value.user.name);
            m_settings->setUserId(result.value.user.id);
            if (!m_secrets->writeSecret(kTokenSecretKey, result.value.accessToken))
                qCWarning(logApp) << "could not persist access token";
            setAuthenticated(true);
        });
}

void SessionController::logout()
{
    m_secrets->removeSecret(kTokenSecretKey);
    m_settings->setUserId({});
    m_client->setSession({}, {});
    setAuthenticated(false);
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
    m_client->publicUsers().then(this, [this](const Result<QList<MediaItem>> &result) {
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
