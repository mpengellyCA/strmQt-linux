#pragma once

#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <utility>

namespace strmqt {

class Settings;
class SessionController;
class WebRemoteServer;
class HostLink;

// The phone remote as QML sees it, and its run policy: the server listens
// while the remote is enabled. The Emby sign-in plays no part: a remote is
// paired per device, which has nothing to do with Emby accounts, and the
// sign-in screen and profile picker are driven with it too. A failed start is
// reported in `error`; nothing retries until a setting changes.
//
// In lan and tailscale modes the bind address is re-resolved every 30 s while
// running: a StrmQt started before Wi-Fi came up, or one whose DHCP lease
// changed, moves to the new address by itself.
//
// `offerSetup` drives the one-time "use your phone as a remote?" prompt: true
// once signed in, while the remote is off and the offer was never answered.
// Accepting turns it on with pairing required; either answer, or turning the
// remote on in Settings, retires the offer for good.
//
// Pairing (WebRemoteServer, "Pairing"): `pairingCode` is the code to put on
// the TV while a phone is pairing, `awaitingStrmLight` says a StrmLight's code
// was typed in and it has not asked with it yet, and `devices` lists what is
// paired. Paired devices stay paired across restarts, sign-ins and profiles.
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
    // Whether a phone has to pair. Named for the PIN that came before pairing.
    Q_PROPERTY(bool requirePin READ requirePin WRITE setRequirePin NOTIFY requirePinChanged)
    Q_PROPERTY(QString pairingCode READ pairingCode NOTIFY pairingChanged)
    Q_PROPERTY(bool awaitingStrmLight READ awaitingStrmLight NOTIFY pairingChanged)
    Q_PROPERTY(int pairingSeconds READ pairingSeconds CONSTANT)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
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

    bool requirePin() const;
    void setRequirePin(bool require);
    QString bindMode() const;
    void setBindMode(const QString &mode);
    bool offerSetup() const;
    QString pairingCode() const;
    bool awaitingStrmLight() const;
    int pairingSeconds() const;
    QVariantList devices() const;

    // start() and restart() still follow the run policy: neither starts a
    // disabled or signed-out remote.
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void regenerateCertificate();
    // The code a StrmLight is showing; false unless it is four digits and
    // something took it. A StrmLight pairs with the streaming host's StrmQt,
    // so the desktop StrmQt passes the code there when one is running
    // (setHostLink) and arms its own remote only when none is. The host arms
    // its remote and gives Sunshine the same code (setSunshinePairing): one
    // code pairs the stream and the remote.
    Q_INVOKABLE bool pairStrmLight(const QString &code);
    // Takes the code off the TV (Back, while it is showing).
    Q_INVOKABLE void cancelPairing();
    Q_INVOKABLE void removeDevice(const QString &id);
    Q_INVOKABLE void removeAllDevices();
    Q_INVOKABLE void copyUrlToClipboard(const QString &url);
    Q_INVOKABLE void acceptSetup();
    Q_INVOKABLE void declineSetup();

    // Replaces NetworkAddressHelper::resolveBindAddress, here and in the
    // server, and sets how often a running lan/tailscale server re-resolves.
    void setAddressResolverForTests(std::function<QHostAddress(const QString &mode)> resolver,
                                    int rebindIntervalMs);
    // Not owned. On the desktop StrmQt, how a code reaches the host.
    void setHostLink(HostLink *link) { m_hostLink = link; }
    // On the host, what gives Sunshine the code a StrmLight's remote was armed with.
    void setSunshinePairing(std::function<void(const QString &code)> pair) { m_sunshinePairing = std::move(pair); }
    // On the host, what a StrmLight is told (WebRemoteServer::setStrmLightHost).
    void setStrmLightHost(std::function<QJsonObject()> host);

signals:
    void enabledChanged();
    void runningChanged();
    void errorChanged();
    void portChanged();
    void urlsChanged();
    void activeUrlChanged();
    void connectedClientsChanged();
    void certFingerprintChanged();
    void pairingChanged();
    void devicesChanged();
    // For a toast: a phone ("phone") or a StrmLight ("strmlight") just paired.
    void devicePaired(const QString &name, const QString &kind);
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
    bool armHere(const QString &code);

    Settings *m_settings;
    SessionController *m_session;
    QPointer<WebRemoteServer> m_server;
    QString m_activeUrl; // chosen with setActiveUrl(); empty follows the network
    QString m_certFingerprint;
    QString m_error;
    QTimer m_rebindTimer;
    std::function<QHostAddress(const QString &)> m_resolver;
    HostLink *m_hostLink = nullptr;
    std::function<void(const QString &)> m_sunshinePairing;
    // A code passed to the host, until a StrmLight shows up among the paired
    // remotes or the code would have run out.
    QString m_passedCode;
    QTimer m_passedExpiry;
    QStringList m_knownDeviceIds;
};

} // namespace strmqt
