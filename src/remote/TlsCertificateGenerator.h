#pragma once

#include <QSslCertificate>
#include <QSslKey>
#include <QString>
#include <QStringList>

namespace strmqt {

class TlsCertificateGenerator
{
public:
    static QString defaultCertificatePath();
    static QString defaultPrivateKeyPath();

    // Ensures a valid self-signed certificate and private key exist at the given or default paths.
    // If missing, expired, or forceRegenerate is true, generates a new self-signed cert with SANs.
    static bool ensureCertificate(const QStringList &sanAddresses = {},
                                 bool forceRegenerate = false,
                                 const QString &certPath = {},
                                 const QString &keyPath = {});

    // Loads the certificate and private key.
    static bool load(QSslCertificate &outCert, QSslKey &outKey,
                    const QString &certPath = {},
                    const QString &keyPath = {});

    // Returns the SHA-256 fingerprint in uppercase colon-separated hex (e.g. AA:BB:CC:...).
    static QString sha256Fingerprint(const QSslCertificate &cert);
};

} // namespace strmqt
