#include "WebRemoteController.h"

#include "HostLink.h"
#include "NetworkAddressHelper.h"
#include "QrCodeGenerator.h"
#include "TlsCertificateGenerator.h"
#include "WebRemoteServer.h"
#include "app/controllers/SessionController.h"
#include "core/Log.h"
#include "core/Settings.h"

#include <QRegularExpression>
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
        connect(m_server, &WebRemoteServer::pairingChanged, this, &WebRemoteController::pairingChanged);
        connect(m_server, &WebRemoteServer::devicePaired, this, &WebRemoteController::devicePaired);
        // A code typed on a phone goes where one typed in Settings goes.
        m_server->setStrmLightCodeHandler([this](const QString &code) { return pairStrmLight(code); });
    }
    m_passedExpiry.setSingleShot(true);
    m_passedExpiry.setInterval(WebRemoteServer::kPairingCodeSeconds * 1000);
    connect(&m_passedExpiry, &QTimer::timeout, this, [this] {
        m_passedCode.clear();
        emit pairingChanged();
    });
    if (m_settings) {
        connect(m_settings, &Settings::webRemoteEnabledChanged, this, [this] {
            // Turned on from Settings: the offer has been answered.
            if (isEnabled())
                m_settings->setWebRemoteSetupOffered(true);
            emit enabledChanged();
            emit offerSetupChanged();
            evaluate();
        });
        connect(m_settings, &Settings::webRemoteSetupOfferedChanged, this,
                &WebRemoteController::offerSetupChanged);
        connect(m_settings, &Settings::webRemotePortChanged, this, [this] {
            emit portChanged();
            restart();
            announceUrls(); // also when it stays stopped
        });
        connect(m_settings, &Settings::webRemoteDevicesChanged, this, [this] {
            // The host paired a StrmLight with a code passed from here: it is
            // written to the settings both share, and that is how it is heard.
            QStringList ids;
            QString pairedName;
            for (const QVariant &device : devices()) {
                const QVariantMap map = device.toMap();
                const QString id = map.value(QStringLiteral("id")).toString();
                ids << id;
                if (!m_knownDeviceIds.contains(id) && map.value(QStringLiteral("kind")) == QLatin1String("strmlight"))
                    pairedName = map.value(QStringLiteral("name")).toString();
            }
            m_knownDeviceIds = ids;
            emit devicesChanged();
            if (!m_passedCode.isEmpty() && !pairedName.isEmpty()) {
                m_passedCode.clear();
                m_passedExpiry.stop();
                emit pairingChanged();
                emit devicePaired(pairedName, QStringLiteral("strmlight"));
            }
        });
        connect(m_settings, &Settings::webRemoteRequirePinChanged, this, &WebRemoteController::requirePinChanged);
        connect(m_settings, &Settings::webRemoteBindModeChanged, this, [this] {
            emit bindModeChanged();
            restart();
            announceUrls();
        });
    }
    // The Emby sign-in has no say in whether the remote runs: its pairing is the
    // device's, a separate thing. Only the setup offer waits for a sign-in.
    if (m_session)
        connect(m_session, &SessionController::authenticatedChanged, this,
                &WebRemoteController::offerSetupChanged);

    for (const QVariant &device : devices())
        m_knownDeviceIds << device.toMap().value(QStringLiteral("id")).toString();
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

void WebRemoteController::setStrmLightHost(std::function<QJsonObject()> host)
{
    if (m_server)
        m_server->setStrmLightHost(std::move(host));
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
    // Not tied to the Emby sign-in: the remote is how a TV gets past the sign-in
    // and the profile picker at all, and how a StrmLight pairs.
    const bool wanted = isEnabled();
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

QString WebRemoteController::pairingCode() const
{
    return m_server ? m_server->pairingCode() : QString();
}

bool WebRemoteController::awaitingStrmLight() const
{
    return !m_passedCode.isEmpty() || (m_server && m_server->awaitingStrmLight());
}

int WebRemoteController::pairingSeconds() const
{
    return WebRemoteServer::kPairingCodeSeconds;
}

QVariantList WebRemoteController::devices() const
{
    return m_server ? m_server->pairedDevices() : QVariantList();
}

bool WebRemoteController::pairStrmLight(const QString &code)
{
    static const QRegularExpression fourDigits(QStringLiteral("^[0-9]{4}$"));
    if (!fourDigits.match(code).hasMatch())
        return false;
    const bool host = m_settings && m_settings->isStreamingHostInstance();
    if (!host && m_hostLink && m_hostLink->pass(code)) {
        m_passedCode = code;
        m_passedExpiry.start();
        emit pairingChanged();
        return true;
    }
    // The host itself, or a desktop StrmQt with no host running: a StrmLight
    // pointed at this StrmQt's own remote.
    return armHere(code);
}

bool WebRemoteController::armHere(const QString &code)
{
    if (!(m_server && m_server->isRunning() && m_server->expectStrmLightCode(code)))
        return false;
    if (m_sunshinePairing)
        m_sunshinePairing(code);
    return true;
}

void WebRemoteController::cancelPairing()
{
    if (!m_passedCode.isEmpty()) {
        m_passedCode.clear();
        m_passedExpiry.stop();
        emit pairingChanged();
    }
    if (m_server)
        m_server->cancelPairing();
}

void WebRemoteController::removeDevice(const QString &id)
{
    if (m_server)
        m_server->removePairedDevice(id);
}

void WebRemoteController::removeAllDevices()
{
    if (m_server)
        m_server->removeAllPairedDevices();
}

void WebRemoteController::copyUrlToClipboard(const QString &url)
{
    emit copyToClipboardRequested(url);
}

bool WebRemoteController::offerSetup() const
{
    return m_settings && m_session && m_session->authenticated() && !isEnabled()
           && !m_settings->webRemoteSetupOffered();
}

void WebRemoteController::acceptSetup()
{
    if (!m_settings)
        return;
    m_settings->setWebRemoteSetupOffered(true);
    // Offered to someone who has not seen the Settings page yet: pairing is
    // what keeps other devices on the network from taking over playback.
    m_settings->setWebRemoteRequirePin(true);
    m_settings->setWebRemoteEnabled(true);
}

void WebRemoteController::declineSetup()
{
    if (m_settings)
        m_settings->setWebRemoteSetupOffered(true);
}

} // namespace strmqt
