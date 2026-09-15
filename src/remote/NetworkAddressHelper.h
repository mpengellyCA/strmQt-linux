#pragma once

#include <QHostAddress>
#include <QList>
#include <QString>
#include <QStringList>

namespace strmqt {

class NetworkAddressHelper
{
public:
    static QString tailscaleIp();
    static QString lanIp();
    static bool hasTailscale();

    static QString lanUrl(int port = 8337);
    static QString tailscaleUrl(int port = 8337);

    // All active IPv4 and IPv6 addresses suitable for inclusion in self-signed TLS SANs.
    static QStringList allHostAddressesForSan();

    // Resolves the QHostAddress to bind to according to the bind mode:
    // "all" -> QHostAddress::Any
    // "tailscale" -> tailscaleIp() or QHostAddress::Null
    // "lan" -> lanIp() or QHostAddress::Null
    // "localhost" -> QHostAddress::LocalHost
    static QHostAddress resolveBindAddress(const QString &mode);
};

} // namespace strmqt
