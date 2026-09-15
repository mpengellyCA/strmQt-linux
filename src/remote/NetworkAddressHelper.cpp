#include "NetworkAddressHelper.h"

#include <QHostInfo>
#include <QNetworkInterface>
#include <QSet>

namespace strmqt {

namespace {

bool isTailscaleAddress(const QHostAddress &addr)
{
    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 ipv4 = addr.toIPv4Address();
        // 100.64.0.0/10 is 100.64.0.0 to 100.127.255.255
        // 100.64.0.0 in hex: 0x64400000, mask /10: 0xFFC00000
        return (ipv4 & 0xFFC00000) == 0x64400000;
    }
    if (addr.protocol() == QAbstractSocket::IPv6Protocol) {
        // Tailscale IPv6 ULA is fd7a:115c:a1e0::/48
        const QString str = addr.toString().toLower();
        return str.startsWith(QStringLiteral("fd7a:115c:a1e0:"));
    }
    return false;
}

bool isVirtualOrDockerInterface(const QString &name)
{
    const QString lower = name.toLower();
    return lower.startsWith(QLatin1String("docker")) ||
           lower.startsWith(QLatin1String("br-")) ||
           lower.startsWith(QLatin1String("veth")) ||
           lower.startsWith(QLatin1String("virbr")) ||
           lower.startsWith(QLatin1String("vmnet"));
}

} // namespace

QString NetworkAddressHelper::tailscaleIp()
{
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    // First, check for interface explicitly named "tailscale"
    for (const auto &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp))
            continue;
        if (iface.name().toLower().startsWith(QLatin1String("tailscale"))) {
            for (const auto &entry : iface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                    return entry.ip().toString();
            }
        }
    }

    // Second check: any active interface holding a 100.64.0.0/10 address
    for (const auto &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp))
            continue;
        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && isTailscaleAddress(entry.ip()))
                return entry.ip().toString();
        }
    }

    return {};
}

QString NetworkAddressHelper::lanIp()
{
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();

    // Look for physical / active LAN interfaces first (e.g. eth*, en*, wl*)
    for (const auto &iface : interfaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning))
            continue;
        if (flags & QNetworkInterface::IsLoopBack)
            continue;
        if (iface.name().toLower().startsWith(QLatin1String("tailscale")))
            continue;
        if (isVirtualOrDockerInterface(iface.name()))
            continue;

        for (const auto &entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol &&
                !ip.isLoopback() &&
                !ip.isLinkLocal() &&
                !isTailscaleAddress(ip)) {
                return ip.toString();
            }
        }
    }

    return {};
}

bool NetworkAddressHelper::hasTailscale()
{
    return !tailscaleIp().isEmpty();
}

QString NetworkAddressHelper::lanUrl(int port)
{
    const QString ip = lanIp();
    if (ip.isEmpty())
        return {};
    return QStringLiteral("https://%1:%2").arg(ip).arg(port);
}

QString NetworkAddressHelper::tailscaleUrl(int port)
{
    const QString ip = tailscaleIp();
    if (ip.isEmpty())
        return {};
    return QStringLiteral("https://%1:%2").arg(ip).arg(port);
}

QStringList NetworkAddressHelper::allHostAddressesForSan()
{
    QSet<QString> addresses;
    addresses.insert(QStringLiteral("127.0.0.1"));
    addresses.insert(QStringLiteral("::1"));

    const QString hostName = QHostInfo::localHostName();
    if (!hostName.isEmpty())
        addresses.insert(hostName);

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const auto &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp))
            continue;
        if (isVirtualOrDockerInterface(iface.name()))
            continue;

        for (const auto &entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (!ip.isLoopback() && !ip.isLinkLocal()) {
                addresses.insert(ip.toString());
            }
        }
    }

    return addresses.values();
}

QHostAddress NetworkAddressHelper::resolveBindAddress(const QString &mode)
{
    if (mode == QLatin1String("tailscale")) {
        const QString ts = tailscaleIp();
        if (!ts.isEmpty())
            return QHostAddress(ts);
        return QHostAddress::Any; // fallback if tailscale not available
    }
    if (mode == QLatin1String("lan")) {
        const QString lan = lanIp();
        if (!lan.isEmpty())
            return QHostAddress(lan);
        return QHostAddress::Any;
    }
    if (mode == QLatin1String("localhost") || mode == QLatin1String("loopback")) {
        return QHostAddress::LocalHost;
    }
    return QHostAddress::Any;
}

} // namespace strmqt
