#include "TlsCertificateGenerator.h"

#include "core/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QStandardPaths>

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <fcntl.h>
#include <unistd.h>

#include <utility>

namespace strmqt {

namespace {

// One subjectAltName entry in the form it is compared and written in.
struct SanEntry {
    bool isIp = false;
    QString value;
    bool operator==(const SanEntry &other) const = default;
    QString toConfig() const { return (isIp ? QLatin1String("IP:") : QLatin1String("DNS:")) + value; }
};

// Carried over from the certificate being replaced, at most this many names.
constexpr qsizetype kMaxCarriedSans = 32;

// An address is compared as an address, never as text: a scope id is dropped
// (it is not part of a certificate name), an IPv4-mapped IPv6 address is its
// IPv4 form, and IPv6 is in QHostAddress's one canonical spelling, which is
// also how QSslCertificate reports it. A host name is compared lower-case.
// Empty for a name that cannot go into the extension: one stray character
// there would make OpenSSL drop every name, not just that one.
SanEntry sanEntry(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return {};
    QHostAddress address(trimmed);
    if (!address.isNull()) {
        address.setScopeId(QString());
        bool isV4 = false;
        const quint32 v4 = address.toIPv4Address(&isV4);
        if (isV4)
            address = QHostAddress(v4);
        return {true, address.toString()};
    }
    const QString name = trimmed.toLower();
    for (const QChar c : name) {
        const bool ok = (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
                        || (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                        || c == QLatin1Char('-') || c == QLatin1Char('.') || c == QLatin1Char('_');
        if (!ok)
            return {};
    }
    return {false, name};
}

void appendSan(QList<SanEntry> &list, const SanEntry &entry)
{
    if (!entry.value.isEmpty() && !list.contains(entry))
        list.append(entry);
}

QList<SanEntry> wantedSans(const QStringList &sanAddresses)
{
    QList<SanEntry> wanted;
    appendSan(wanted, {false, QStringLiteral("localhost")});
    appendSan(wanted, {true, QStringLiteral("127.0.0.1")});
    appendSan(wanted, {true, QStringLiteral("::1")});
    for (const QString &address : sanAddresses)
        appendSan(wanted, sanEntry(address));
    return wanted;
}

QList<SanEntry> certificateSans(const QSslCertificate &cert)
{
    QList<SanEntry> names;
    const QMultiMap<QSsl::AlternativeNameEntryType, QString> alt = cert.subjectAlternativeNames();
    for (auto it = alt.cbegin(); it != alt.cend(); ++it) {
        if (it.key() != QSsl::DnsEntry && it.key() != QSsl::IpAddressEntry)
            continue;
        const SanEntry entry = sanEntry(it.value());
        // Kept by the type the certificate gave it, whatever the text looks like.
        if (entry.isIp == (it.key() == QSsl::IpAddressEntry))
            appendSan(names, entry);
    }
    return names;
}

bool isIpv6(const SanEntry &entry)
{
    return entry.isIp && entry.value.contains(QLatin1Char(':'));
}

// The first wanted name the certificate lacks that is worth a new certificate
// for; empty when there is none. A new certificate is one every phone has to
// trust again, so a missing IPv6 address does not count on its own: privacy
// addresses (RFC 4941) change daily, and every URL the app shows or encodes in
// the QR code is an IPv4 address. IPv6 addresses still go into whatever
// certificate is written next.
QString missingSan(const QList<SanEntry> &present, const QList<SanEntry> &wanted)
{
    for (const SanEntry &entry : wanted) {
        if (!isIpv6(entry) && !present.contains(entry))
            return entry.value;
    }
    return {};
}

} // namespace

QString TlsCertificateGenerator::defaultCertificatePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                        QLatin1String("/webremote");
    return dir + QLatin1String("/cert.pem");
}

QString TlsCertificateGenerator::defaultPrivateKeyPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                        QLatin1String("/webremote");
    return dir + QLatin1String("/key.pem");
}

bool TlsCertificateGenerator::ensureCertificate(const QStringList &sanAddresses,
                                               bool forceRegenerate,
                                               const QString &certPath,
                                               const QString &keyPath)
{
    const QString cPath = certPath.isEmpty() ? defaultCertificatePath() : certPath;
    const QString kPath = keyPath.isEmpty() ? defaultPrivateKeyPath() : keyPath;

    QList<SanEntry> sans = wantedSans(sanAddresses);

    if (!forceRegenerate && QFile::exists(cPath) && QFile::exists(kPath)) {
        QSslCertificate cert;
        QSslKey key;
        if (load(cert, key, cPath, kPath)) {
            const QList<SanEntry> present = certificateSans(cert);
            const QString missing = missingSan(present, sans);
            if (!cert.isNull() && !cert.isBlacklisted() &&
                QDateTime::currentDateTime() < cert.expiryDate() && missing.isEmpty()) {
                return true;
            }
            if (!missing.isEmpty()) {
                qCInfo(logApp) << "tls: the certificate does not name" << missing
                               << "- replacing it; devices have to trust the new one";
            }
            // The names the old certificate had stay: a computer that moves
            // between two networks then settles on one certificate naming both
            // instead of replacing it on every move. IPv6 is left behind (those
            // are the addresses that come and go), and an explicit regenerate
            // starts from the current addresses alone.
            qsizetype carried = 0;
            for (const SanEntry &entry : present) {
                if (carried >= kMaxCarriedSans)
                    break;
                if (isIpv6(entry) || sans.contains(entry))
                    continue;
                sans.append(entry);
                ++carried;
            }
        }
    }

    // Ensure parent directory exists
    const QFileInfo fi(cPath);
    QDir().mkpath(fi.absolutePath());

    EVP_PKEY *pkey = nullptr;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!pctx) {
        qCWarning(logApp) << "tls: failed to create EVP_PKEY_CTX";
        return false;
    }
    if (EVP_PKEY_keygen_init(pctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048) <= 0 ||
        EVP_PKEY_keygen(pctx, &pkey) <= 0) {
        qCWarning(logApp) << "tls: failed to generate RSA 2048 key";
        EVP_PKEY_CTX_free(pctx);
        return false;
    }
    EVP_PKEY_CTX_free(pctx);

    X509 *x509 = X509_new();
    if (!x509) {
        qCWarning(logApp) << "tls: failed to allocate X509 structure";
        EVP_PKEY_free(pkey);
        return false;
    }

    // X509v3
    X509_set_version(x509, 2);
    // A random non-zero 64-bit serial: a browser that remembered an earlier
    // certificate (same issuer, serial 1) can refuse a regenerated one.
    BIGNUM *serial = BN_new();
    if (!serial || BN_rand(serial, 64, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY) != 1
        || BN_is_zero(serial) || !BN_to_ASN1_INTEGER(serial, X509_get_serialNumber(x509))) {
        qCWarning(logApp) << "tls: failed to generate a certificate serial";
        BN_free(serial);
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }
    BN_free(serial);
    X509_gmtime_adj(X509_get_notBefore(x509), -3600); // 1 hour ago
    X509_gmtime_adj(X509_get_notAfter(x509), 10LL * 365 * 24 * 3600); // 10 years validity

    X509_set_pubkey(x509, pkey);

    X509_NAME *name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (const unsigned char *)"CA", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (const unsigned char *)"StrmQt", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               (const unsigned char *)"StrmQt Web Remote", -1, -1, 0);
    X509_set_issuer_name(x509, name);

    // X509v3 Extensions
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, x509, x509, nullptr, nullptr, 0);

    // Basic constraints: a leaf server certificate, never a CA. A trusted
    // CA:TRUE certificate could sign certificates for any host.
    X509_EXTENSION *ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, "critical,CA:FALSE");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Key Usage: digitalSignature, keyEncipherment (no keyCertSign: not a CA)
    ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage,
                              "critical,digitalSignature,keyEncipherment");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Extended Key Usage
    ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_ext_key_usage, "serverAuth");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Subject Alternative Names (SAN)
    QStringList sanList;
    for (const SanEntry &entry : std::as_const(sans))
        sanList << entry.toConfig();

    const QByteArray sanJoined = sanList.join(QLatin1Char(',')).toUtf8();
    ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_alt_name, sanJoined.constData());
    if (!ext) {
        // A certificate without names would be found wanting, and replaced, on
        // every start.
        qCWarning(logApp) << "tls: failed to build the subjectAltName extension";
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }
    X509_add_ext(x509, ext, -1);
    X509_EXTENSION_free(ext);

    // Sign with SHA-256
    if (X509_sign(x509, pkey, EVP_sha256()) <= 0) {
        qCWarning(logApp) << "tls: failed to sign X509 certificate";
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }

    // Write certificate
    BIO *bioCert = BIO_new_file(cPath.toLocal8Bit().constData(), "w");
    if (!bioCert) {
        qCWarning(logApp) << "tls: failed to open certificate file for writing:" << cPath;
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }
    PEM_write_bio_X509(bioCert, x509);
    BIO_free_all(bioCert);

    // Write private key with immediate 0600 permissions to avoid a world-readable window
    const int fd = ::open(kPath.toLocal8Bit().constData(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        qCWarning(logApp) << "tls: failed to open key file for writing:" << kPath;
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }
    BIO *bioKey = BIO_new_fd(fd, BIO_CLOSE);
    if (!bioKey) {
        ::close(fd);
        qCWarning(logApp) << "tls: failed to allocate bio for key file:" << kPath;
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }
    PEM_write_bio_PrivateKey(bioKey, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    BIO_free_all(bioKey);

    X509_free(x509);
    EVP_PKEY_free(pkey);

    // Ensure permissions explicitly in Qt as well
    QFile::setPermissions(kPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    qCInfo(logApp) << "tls: generated new self-signed certificate at" << cPath;
    return true;
}

bool TlsCertificateGenerator::load(QSslCertificate &outCert, QSslKey &outKey,
                                  const QString &certPath,
                                  const QString &keyPath)
{
    const QString cPath = certPath.isEmpty() ? defaultCertificatePath() : certPath;
    const QString kPath = keyPath.isEmpty() ? defaultPrivateKeyPath() : keyPath;

    QFile certFile(cPath);
    if (!certFile.open(QIODevice::ReadOnly)) {
        qCWarning(logApp) << "tls: cannot open certificate:" << cPath;
        return false;
    }
    outCert = QSslCertificate(&certFile, QSsl::Pem);
    certFile.close();

    if (outCert.isNull()) {
        qCWarning(logApp) << "tls: failed to parse certificate from" << cPath;
        return false;
    }

    QFile keyFile(kPath);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        qCWarning(logApp) << "tls: cannot open private key:" << kPath;
        return false;
    }
    outKey = QSslKey(&keyFile, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
    keyFile.close();

    if (outKey.isNull()) {
        qCWarning(logApp) << "tls: failed to parse private key from" << kPath;
        return false;
    }

    return true;
}

QString TlsCertificateGenerator::sha256Fingerprint(const QSslCertificate &cert)
{
    if (cert.isNull())
        return {};
    const QByteArray digest = cert.digest(QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex(':')).toUpper();
}

} // namespace strmqt
