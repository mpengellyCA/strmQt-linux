#pragma once

#include <QObject>
#include <QVariantList>
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
    Q_PROPERTY(QVariantList publicUsers READ publicUsers NOTIFY publicUsersChanged)
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
    QUrl serverUrl() const;
    void setServerUrl(const QUrl &url);
    QString playbackEngine() const;
    void setPlaybackEngine(const QString &engine); // applies on next launch

    // Restores token/user from storage; returns true when a session was restored.
    QVariantList publicUsers() const { return m_publicUsers; }

    Q_INVOKABLE bool restore();
    Q_INVOKABLE void login(const QString &username, const QString &password);
    Q_INVOKABLE void logout();
    // Sign out and return to the login screen WITHOUT forgetting the server —
    // switching user is not the same act as leaving the server behind.
    Q_INVOKABLE void switchUser();
    // Users the server advertises, {id, name, imageUrl}. May be shorter than
    // the real user list, or empty: Emby hides users flagged as hidden, and on
    // the target server the signed-in user is not in it. A picker seeded from
    // this must keep the username field, not replace it.
    Q_INVOKABLE void loadPublicUsers();

signals:
    void authenticatedChanged();
    void busyChanged();
    void errorMessageChanged();
    void publicUsersChanged();
    void serverUrlChanged();
    void playbackEngineChanged();

private:
    void setBusy(bool busy);
    void setError(const QString &message);
    void setAuthenticated(bool authenticated);

    Settings *m_settings;
    SecretsStore *m_secrets;
    emby::EmbyClient *m_client;
    bool m_authenticated = false;
    bool m_busy = false;
    QString m_errorMessage;
    QVariantList m_publicUsers;
};

} // namespace strmqt
