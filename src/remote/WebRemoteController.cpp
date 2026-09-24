#include "WebRemoteController.h"

#include "NetworkAddressHelper.h"
#include "QrCodeGenerator.h"
#include "TlsCertificateGenerator.h"
#include "WebRemoteServer.h"
#include "app/controllers/SessionController.h"
#include "core/Log.h"
#include "core/Settings.h"

#include <QRandomGenerator>
#include <QUrl>

#include <utility>

namespace strmqt {

namespace {

constexpr int kRebindIntervalMs = 30000;

bool followsNetwork(const QString &mode)
{
    return mode == QLatin1String("lan") || mode == QLatin1String("tailscale");
}

bool isWildcard(const QHostAddress &address)
{
    return address.isNull() || address == QHostAddress(QHostAddress::Any)
           || address == QHostAddress(QHostAddress::AnyIPv4)
           || address == QHostAddress(QHostAddress::AnyIPv6);
}

} // namespace

WebRemoteController::WebRemoteController(Settings *settings, SessionController *session,
                                         WebRemoteServer *server, QObject *parent)
    : QObject(parent), m_settings(settings), m_session(session), m_server(server)
{
    m_rebindTimer.setInterval(kRebindIntervalMs);
    connect(&m_rebindTimer, &QTimer::timeout, this, &WebRemoteController::checkBindAddress);
    if (m_server) {
        connect(m_server, &WebRemoteServer::runningChanged, this, &WebRemoteController::runningChanged);
        // The URL follows the bound address: every start and stop (a port or
        // mode change, a rebind, a regenerated certificate, a failed start)
        // can move it, so the URL and QR code are re-read here, after the
        // restart rather than before it.
        connect(m_server, &WebRemoteServer::runningChanged, this, [this] {
            updateRebindTimer();
            announceUrls();
        });
        connect(m_server, &WebRemoteServer::connectedClientsChanged, this, &WebRemoteController::connectedClientsChanged);
    }
    if (m_settings) {
        connect(m_settings, &Settings::webRemoteEnabledChanged, this, [this] {
            emit enabledChanged();
            evaluate();
        });
        connect(m_settings, &Settings::webRemotePortChanged, this, [this] {
            emit portChanged();
            restart();
            announceUrls(); // also when it stays stopped
        });
        connect(m_settings, &Settings::webRemotePinChanged, this, &WebRemoteController::pinChanged);
        connect(m_settings, &Settings::webRemoteRequirePinChanged, this, &WebRemoteController::requirePinChanged);
        connect(m_settings, &Settings::webRemoteBindModeChanged, this, [this] {
            emit bindModeChanged();
            restart();
            announceUrls();
        });
    }
    if (m_session) {
        connect(m_session, &SessionController::authenticatedChanged, this,
                &WebRemoteController::evaluate);
        // Emitted before the credentials change: nothing opened or signed in
        // under the old session may carry over. The policy is looked at again
        // once the boundary has run its course (a profile switch may leave
        // `authenticated` set throughout and never emit authenticatedChanged).
        connect(m_session, &SessionController::sessionBoundaryChanged, this, [this] {
            if (m_server)
                m_server->stop();
            QTimer::singleShot(0, this, &WebRemoteController::evaluate);
        });
    }

    updateFingerprint();
    evaluate();
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
    if (!m_activeUrl.isEmpty())
        return m_activeUrl;
    // A server on one address (lan, tailscale, localhost) is reachable at
    // exactly that one; only "all" leaves the choice to the network.
    const QHostAddress address = effectiveBindAddress();
    if (!isWildcard(address)) {
        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(address.toString());
        url.setPort(port());
        return url.toString();
    }
    if (hasTailscale())
        return tailscaleUrl();
    return lanUrl();
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
    evaluate();
}

void WebRemoteController::stop()
{
    if (m_server)
        m_server->stop();
}

void WebRemoteController::restart()
{
    if (!m_server)
        return;
    m_server->stop();
    evaluate();
}

void WebRemoteController::regenerateCertificate()
{
    if (!m_server)
        return;
    if (!m_server->regenerateCertificate()) {
        setError(QStringLiteral("Could not create the TLS certificate."));
        updateFingerprint();
        return;
    }
    updateFingerprint();
    // The server restarted its listener under the new certificate. If that
    // failed (the port or address was taken meanwhile), evaluate() reports it
    // in `error` instead of Settings still claiming "Running".
    if (m_server->isRunning())
        setError({});
    else
        evaluate();
}

void WebRemoteController::evaluate()
{
    if (!m_server) // the owner has torn the server down; nothing left to do
        return;
    const bool wanted = isEnabled() && m_session && m_session->authenticated();
    if (!wanted) {
        m_server->stop(); // also drops every connection, stream and token
        setError({});
    } else if (!m_server->isRunning()) {
        if (m_server->start()) {
            setError({});
            updateFingerprint(); // the first start creates the certificate
        } else {
            setError(m_server->errorString());
        }
    }
}

void WebRemoteController::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void WebRemoteController::announceUrls()
{
    emit urlsChanged();
    emit activeUrlChanged();
}

QHostAddress WebRemoteController::resolveBindAddress() const
{
    const QString mode = bindMode();
    return m_resolver ? m_resolver(mode) : NetworkAddressHelper::resolveBindAddress(mode);
}

QHostAddress WebRemoteController::effectiveBindAddress() const
{
    if (m_server && m_server->isRunning())
        return m_server->boundAddress();
    return resolveBindAddress();
}

void WebRemoteController::updateRebindTimer()
{
    if (m_server && m_server->isRunning() && followsNetwork(bindMode())) {
        if (!m_rebindTimer.isActive())
            m_rebindTimer.start();
    } else {
        m_rebindTimer.stop();
    }
}

void WebRemoteController::checkBindAddress()
{
    if (!m_server || !m_server->isRunning() || !followsNetwork(bindMode()))
        return;
    const QHostAddress resolved = resolveBindAddress();
    if (resolved == m_server->boundAddress())
        return;
    qCInfo(logApp) << "webremote: network address changed; rebinding to" << resolved.toString();
    restart(); // runningChanged re-announces the URLs
}

void WebRemoteController::setAddressResolverForTests(
    std::function<QHostAddress(const QString &)> resolver, int rebindIntervalMs)
{
    m_resolver = resolver;
    if (m_server)
        m_server->setBindAddressResolverForTests(std::move(resolver));
    m_rebindTimer.setInterval(rebindIntervalMs);
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
