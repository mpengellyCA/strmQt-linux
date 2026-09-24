#pragma once

#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <functional>

namespace strmqt {

class Settings;
class SessionController;
class WebRemoteServer;

// The phone remote as QML sees it, and its run policy: the server listens only
// while the remote is enabled AND the session is signed in. A session boundary
// (logout, switching user, profile or server) stops it first, so every
// connection and token from the old session is gone before the new one starts
// it again. A failed start is reported in `error`; nothing retries until a
// setting or the session changes.
//
// In lan and tailscale modes the bind address is re-resolved every 30 s while
// running: a StrmQt started before Wi-Fi came up, or one whose DHCP lease
// changed, moves to the new address by itself.
//
// `offerSetup` drives the one-time "use your phone as a remote?" prompt: true
// once signed in, while the remote is off and the offer was never answered.
// Accepting turns it on with a PIN; either answer, or turning the remote on
// in Settings, retires the offer for good.
//
// m_server is a QPointer: the server's destructor calls stop(), which emits
// runningChanged while this object may still be connected, and everything
// this class calls on its own initiative guards against the server having been
// destroyed first.
class WebRemoteController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(QString lanUrl READ lanUrl NOTIFY urlsChanged)
    Q_PROPERTY(QString tailscaleUrl READ tailscaleUrl NOTIFY urlsChanged)
    Q_PROPERTY(bool hasTailscale READ hasTailscale CONSTANT)
    Q_PROPERTY(QString activeUrl READ activeUrl WRITE setActiveUrl NOTIFY activeUrlChanged)
    Q_PROPERTY(QString qrCodeSvg READ qrCodeSvg NOTIFY activeUrlChanged)
    Q_PROPERTY(QString activeQrDataUri READ activeQrDataUri NOTIFY activeUrlChanged)
    Q_PROPERTY(int connectedClientsCount READ connectedClientsCount NOTIFY connectedClientsChanged)
    Q_PROPERTY(QString certFingerprint READ certFingerprint NOTIFY certFingerprintChanged)
    Q_PROPERTY(QString pin READ pin WRITE setPin NOTIFY pinChanged)
    Q_PROPERTY(bool requirePin READ requirePin WRITE setRequirePin NOTIFY requirePinChanged)
    Q_PROPERTY(QString bindMode READ bindMode WRITE setBindMode NOTIFY bindModeChanged)
    Q_PROPERTY(bool offerSetup READ offerSetup NOTIFY offerSetupChanged)

public:
    WebRemoteController(Settings *settings, SessionController *session, WebRemoteServer *server,
                        QObject *parent = nullptr);

    bool isEnabled() const;
    void setEnabled(bool enabled);
    bool isRunning() const;
    QString error() const { return m_error; }
    int port() const;
    void setPort(int port);
    QString lanUrl() const;
    QString tailscaleUrl() const;
    bool hasTailscale() const;
    QString activeUrl() const;
    void setActiveUrl(const QString &url);
    QString qrCodeSvg() const;
    QString activeQrDataUri() const;
    int connectedClientsCount() const;
    QString certFingerprint() const;

    QString pin() const;
    void setPin(const QString &pin);
    bool requirePin() const;
    void setRequirePin(bool require);
    QString bindMode() const;
    void setBindMode(const QString &mode);
    bool offerSetup() const;

    // start() and restart() still follow the run policy: neither starts a
    // disabled or signed-out remote.
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void regenerateCertificate();
    Q_INVOKABLE void generateNewPin();
    Q_INVOKABLE void copyUrlToClipboard(const QString &url);
    Q_INVOKABLE void acceptSetup();
    Q_INVOKABLE void declineSetup();

    // Replaces NetworkAddressHelper::resolveBindAddress, here and in the
    // server, and sets how often a running lan/tailscale server re-resolves.
    void setAddressResolverForTests(std::function<QHostAddress(const QString &mode)> resolver,
                                    int rebindIntervalMs);

signals:
    void enabledChanged();
    void runningChanged();
    void errorChanged();
    void portChanged();
    void urlsChanged();
    void activeUrlChanged();
    void connectedClientsChanged();
    void certFingerprintChanged();
    void pinChanged();
    void requirePinChanged();
    void bindModeChanged();
    void offerSetupChanged();
    void copyToClipboardRequested(const QString &text);

private:
    // Starts or stops the server to match the run policy above.
    void evaluate();
    void setError(const QString &error);
    void updateFingerprint();
    // The URLs follow the network and the bound address; re-read them.
    void announceUrls();
    QHostAddress resolveBindAddress() const;
    // The address the server is bound to while running, else the one it would bind.
    QHostAddress effectiveBindAddress() const;
    // Runs the rebind check only while running in lan or tailscale mode.
    void updateRebindTimer();
    // Restarts on the new address when the network under lan/tailscale changed.
    void checkBindAddress();

    Settings *m_settings;
    SessionController *m_session;
    QPointer<WebRemoteServer> m_server;
    QString m_activeUrl; // chosen with setActiveUrl(); empty follows the network
    QString m_certFingerprint;
    QString m_error;
    QTimer m_rebindTimer;
    std::function<QHostAddress(const QString &)> m_resolver;
};

} // namespace strmqt
