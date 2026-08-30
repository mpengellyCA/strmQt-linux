#pragma once

#include "core/Result.h"

#include <QFuture>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

namespace strmqt {

class Settings;
class SecretsStore;
namespace emby {
class EmbyClient;
}

// QML-facing session lifecycle: restore persisted session at startup, login,
// logout. Owns none of its collaborators.
class SessionController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY authenticatedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString username READ username NOTIFY authenticatedChanged)
    Q_PROPERTY(QUrl serverUrl READ serverUrl WRITE setServerUrl NOTIFY serverUrlChanged)
    Q_PROPERTY(QString playbackEngine READ playbackEngine WRITE setPlaybackEngine NOTIFY
                   playbackEngineChanged)
    // "wallet", "vault" (KWallet unavailable — lower security, warn the user),
    // or "unknown" before the first secret operation.
    Q_PROPERTY(QString secretStorage READ secretStorage NOTIFY secretStorageChanged)
    // Saved accounts, most recently used first; QVariantMaps with serverUrl,
    // userId, username, lastUsed. Backs the login screen's profile picker.
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)

public:
    SessionController(Settings *settings, SecretsStore *secrets, emby::EmbyClient *client,
                      QObject *parent = nullptr);

    bool authenticated() const { return m_authenticated; }
    bool busy() const { return m_busy; }
    QString errorMessage() const { return m_errorMessage; }
    QString username() const;
    quint64 epoch() const { return m_epoch; }
    QUrl serverUrl() const;
    void setServerUrl(const QUrl &url);
    QString playbackEngine() const;
    void setPlaybackEngine(const QString &engine); // applies on next launch
    QString secretStorage() const;
    QVariantList profiles() const;

    // Starts an asynchronous token restore. The window is already available
    // while KWallet opens; authenticatedChanged announces a successful restore.
    Q_INVOKABLE void restore();
    Q_INVOKABLE void login(const QString &username, const QString &password);
    // Sign out AND forget the account: token, profile entry, local session.
    Q_INVOKABLE void logout();
    // Back to the profile picker WITHOUT forgetting anything: the account keeps
    // its token and registry entry, and stays the one startup auto-resumes.
    Q_INVOKABLE void switchUser();
    // Enter a profile from the picker: adopts (server, user) as current and
    // restores its token. A missing token lands back on the password form.
    Q_INVOKABLE void selectProfile(const QString &serverUrl, const QString &userId);
    // Forget a picker profile: registry entry and stored token. Forgetting the
    // active account also ends its local session.
    Q_INVOKABLE void removeProfile(const QString &serverUrl, const QString &userId);
    // Avatar URL for a saved profile; usable while logged out (the image is
    // public on a stock server — see EmbyClient::userImageUrl).
    Q_INVOKABLE QString profileAvatarUrl(const QString &serverUrl, const QString &userId) const;

signals:
    // Emitted before credentials or server identity change. Consumers use this
    // as the application-wide teardown edge; it also fires when the old state
    // was already unauthenticated so a late login can still be retired.
    void sessionBoundaryChanged(quint64 epoch);
    void authenticatedChanged();
    void busyChanged();
    void errorMessageChanged();
    void serverUrlChanged();
    void playbackEngineChanged();
    void secretStorageChanged();
    void profilesChanged();

private:
    quint64 beginSessionBoundary();
    // Ends the local session (settings pointer, client identity, authenticated
    // flag) without touching any stored credential or profile.
    void clearLocalSession();
    QFuture<Result<QString>> readAccountToken(const QUrl &serverUrl, const QString &userId);
    void setBusy(bool busy);
    void setError(const QString &message);
    void setAuthenticated(bool authenticated);

    Settings *m_settings;
    SecretsStore *m_secrets;
    emby::EmbyClient *m_client;
    bool m_authenticated = false;
    bool m_busy = false;
    bool m_restorePending = false;
    QString m_errorMessage;
    quint64 m_epoch = 0;
};

} // namespace strmqt
