#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

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

    // Starts an asynchronous token restore. The window is already available
    // while KWallet opens; authenticatedChanged announces a successful restore.
    Q_INVOKABLE void restore();
    Q_INVOKABLE void login(const QString &username, const QString &password);
    Q_INVOKABLE void logout();
    // Sign out and return to the login screen WITHOUT forgetting the server —
    // switching user is not the same act as leaving the server behind.
    Q_INVOKABLE void switchUser();

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

private:
    quint64 beginSessionBoundary();
    void clearCredentials(quint64 epoch);
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
