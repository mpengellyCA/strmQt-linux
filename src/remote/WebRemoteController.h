#pragma once

#include <QObject>
#include <QString>

namespace strmqt {

class Settings;
class WebRemoteServer;

class WebRemoteController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
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

public:
    explicit WebRemoteController(Settings *settings, WebRemoteServer *server, QObject *parent = nullptr);

    bool isEnabled() const;
    void setEnabled(bool enabled);
    bool isRunning() const;
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

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void regenerateCertificate();
    Q_INVOKABLE void generateNewPin();
    Q_INVOKABLE void copyUrlToClipboard(const QString &url);

signals:
    void enabledChanged();
    void runningChanged();
    void portChanged();
    void urlsChanged();
    void activeUrlChanged();
    void connectedClientsChanged();
    void certFingerprintChanged();
    void pinChanged();
    void requirePinChanged();
    void bindModeChanged();
    void copyToClipboardRequested(const QString &text);

private:
    void updateFingerprint();

    Settings *m_settings;
    WebRemoteServer *m_server;
    QString m_activeUrl;
    QString m_certFingerprint;
};

} // namespace strmqt
