#include "WebRemoteController.h"

#include "NetworkAddressHelper.h"
#include "QrCodeGenerator.h"
#include "TlsCertificateGenerator.h"
#include "WebRemoteServer.h"
#include "core/Settings.h"

#include <QRandomGenerator>

namespace strmqt {

WebRemoteController::WebRemoteController(Settings *settings, WebRemoteServer *server, QObject *parent)
    : QObject(parent), m_settings(settings), m_server(server)
{
    if (m_server) {
        connect(m_server, &WebRemoteServer::runningChanged, this, &WebRemoteController::runningChanged);
        connect(m_server, &WebRemoteServer::connectedClientsChanged, this, &WebRemoteController::connectedClientsChanged);
    }
    if (m_settings) {
        connect(m_settings, &Settings::webRemoteEnabledChanged, this, &WebRemoteController::enabledChanged);
        connect(m_settings, &Settings::webRemotePortChanged, this, &WebRemoteController::portChanged);
        connect(m_settings, &Settings::webRemotePortChanged, this, &WebRemoteController::urlsChanged);
        connect(m_settings, &Settings::webRemotePinChanged, this, &WebRemoteController::pinChanged);
        connect(m_settings, &Settings::webRemoteRequirePinChanged, this, &WebRemoteController::requirePinChanged);
        connect(m_settings, &Settings::webRemoteBindModeChanged, this, &WebRemoteController::bindModeChanged);
    }

    // Default active URL: Tailscale if present, else LAN
    if (hasTailscale())
        m_activeUrl = tailscaleUrl();
    else
        m_activeUrl = lanUrl();

    updateFingerprint();
}

bool WebRemoteController::isEnabled() const
{
    return m_settings ? m_settings->webRemoteEnabled() : false;
}

void WebRemoteController::setEnabled(bool enabled)
{
    if (m_settings)
        m_settings->setWebRemoteEnabled(enabled);
}

bool WebRemoteController::isRunning() const
{
    return m_server ? m_server->isRunning() : false;
}

int WebRemoteController::port() const
{
    return m_settings ? m_settings->webRemotePort() : 8337;
}

void WebRemoteController::setPort(int port)
{
    if (m_settings)
        m_settings->setWebRemotePort(port);
}

QString WebRemoteController::lanUrl() const
{
    return NetworkAddressHelper::lanUrl(port());
}

QString WebRemoteController::tailscaleUrl() const
{
    return NetworkAddressHelper::tailscaleUrl(port());
}

bool WebRemoteController::hasTailscale() const
{
    return NetworkAddressHelper::hasTailscale();
}

QString WebRemoteController::activeUrl() const
{
    if (m_activeUrl.isEmpty()) {
        if (hasTailscale())
            return tailscaleUrl();
        return lanUrl();
    }
    return m_activeUrl;
}

void WebRemoteController::setActiveUrl(const QString &url)
{
    if (url == m_activeUrl)
        return;
    m_activeUrl = url;
    emit activeUrlChanged();
}

QString WebRemoteController::qrCodeSvg() const
{
    const QString target = activeUrl();
    if (target.isEmpty())
        return {};
    return QrCodeGenerator::toSvg(target, 4, QStringLiteral("#0C0B0A"), QStringLiteral("#FFFFFF"));
}

QString WebRemoteController::activeQrDataUri() const
{
    const QString svg = qrCodeSvg();
    if (svg.isEmpty())
        return {};
    return QStringLiteral("data:image/svg+xml;base64,") + svg.toUtf8().toBase64();
}

int WebRemoteController::connectedClientsCount() const
{
    return m_server ? m_server->connectedClientsCount() : 0;
}

QString WebRemoteController::certFingerprint() const
{
    return m_certFingerprint;
}

void WebRemoteController::updateFingerprint()
{
    QSslCertificate cert;
    QSslKey key;
    if (TlsCertificateGenerator::load(cert, key)) {
        m_certFingerprint = TlsCertificateGenerator::sha256Fingerprint(cert);
    } else {
        m_certFingerprint.clear();
    }
    emit certFingerprintChanged();
}

QString WebRemoteController::pin() const
{
    return m_settings ? m_settings->webRemotePin() : QString();
}

void WebRemoteController::setPin(const QString &pin)
{
    if (m_settings)
        m_settings->setWebRemotePin(pin);
}

bool WebRemoteController::requirePin() const
{
    return m_settings ? m_settings->webRemoteRequirePin() : false;
}

void WebRemoteController::setRequirePin(bool require)
{
    if (m_settings)
        m_settings->setWebRemoteRequirePin(require);
}

QString WebRemoteController::bindMode() const
{
    return m_settings ? m_settings->webRemoteBindMode() : QStringLiteral("all");
}

void WebRemoteController::setBindMode(const QString &mode)
{
    if (m_settings)
        m_settings->setWebRemoteBindMode(mode);
}

void WebRemoteController::start()
{
    if (m_server)
        m_server->start();
}

void WebRemoteController::stop()
{
    if (m_server)
        m_server->stop();
}

void WebRemoteController::restart()
{
    if (m_server) {
        m_server->stop();
        m_server->start();
    }
}

void WebRemoteController::regenerateCertificate()
{
    if (m_server) {
        m_server->regenerateCertificate();
        updateFingerprint();
    }
}

void WebRemoteController::generateNewPin()
{
    if (m_settings) {
        const QString newPin = QString::asprintf("%04u", QRandomGenerator::global()->bounded(10000u));
        m_settings->setWebRemotePin(newPin);
    }
}

void WebRemoteController::copyUrlToClipboard(const QString &url)
{
    emit copyToClipboardRequested(url);
}

} // namespace strmqt
