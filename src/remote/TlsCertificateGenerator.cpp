#include "TlsCertificateGenerator.h"

#include "core/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QStandardPaths>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace strmqt {

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

    if (!forceRegenerate && QFile::exists(cPath) && QFile::exists(kPath)) {
        QSslCertificate cert;
        QSslKey key;
        if (load(cert, key, cPath, kPath)) {
            if (!cert.isNull() && !cert.isBlacklisted() &&
                QDateTime::currentDateTime() < cert.expiryDate()) {
                return true;
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
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
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

    // Basic constraints: CA:TRUE, pathlen:0 (self-signed root)
    X509_EXTENSION *ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, "CA:TRUE,pathlen:0");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Key Usage: digitalSignature, keyEncipherment, keyCertSign
    ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage,
                              "digitalSignature, keyEncipherment, keyCertSign");
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
    sanList << QStringLiteral("DNS:localhost");
    sanList << QStringLiteral("IP:127.0.0.1");
    sanList << QStringLiteral("IP:::1");

    for (const QString &addrStr : sanAddresses) {
        if (addrStr.isEmpty())
            continue;
        QHostAddress addr(addrStr);
        if (!addr.isNull()) {
            sanList << QStringLiteral("IP:%1").arg(addrStr);
        } else {
            sanList << QStringLiteral("DNS:%1").arg(addrStr);
        }
    }
    sanList.removeDuplicates();

    const QByteArray sanJoined = sanList.join(QLatin1Char(',')).toUtf8();
    ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_alt_name, sanJoined.constData());
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

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

    // Write private key
    BIO *bioKey = BIO_new_file(kPath.toLocal8Bit().constData(), "w");
    if (!bioKey) {
        qCWarning(logApp) << "tls: failed to open key file for writing:" << kPath;
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }
    PEM_write_bio_PrivateKey(bioKey, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    BIO_free_all(bioKey);

    X509_free(x509);
    EVP_PKEY_free(pkey);

    // Strict 0600 permissions on the private key file
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
