#include <QFile>
#include <QSslCertificateExtension>
#include <QTemporaryDir>
#include <QtTest>
#include "remote/TlsCertificateGenerator.h"

using namespace strmqt;

class TlsCertificateGeneratorTest : public QObject
{
    Q_OBJECT

private slots:
    void generateAndVerifyProperties();
    void privateKeyPermissionsAreRestricted();
    void fingerprintFormat();
    void ensureCertificateAndLoad();
    void regeneratedCertificatesHaveDistinctSerials();
    void missingAddressReplacesTheCertificate();
    void sameAddressesInAnotherSpellingKeepTheCertificate();
    void namesOfTheReplacedCertificateAreKept();
};

void TlsCertificateGeneratorTest::generateAndVerifyProperties()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString certPath = dir.filePath(QStringLiteral("test_cert.pem"));
    const QString keyPath = dir.filePath(QStringLiteral("test_key.pem"));

    const QStringList sans = { QStringLiteral("127.0.0.1"), QStringLiteral("100.81.48.104") };
    QVERIFY(TlsCertificateGenerator::ensureCertificate(sans, true, certPath, keyPath));
    QVERIFY(QFile::exists(certPath));
    QVERIFY(QFile::exists(keyPath));

    const QList<QSslCertificate> certs = QSslCertificate::fromPath(certPath);
    QCOMPARE(certs.count(), 1);

    const QSslCertificate &cert = certs.first();
    QVERIFY(!cert.isNull());
    // A leaf without keyCertSign is not isSelfSigned() to Qt; it still signs itself.
    QCOMPARE(cert.issuerDisplayName(), cert.subjectDisplayName());
    QCOMPARE(cert.issuerInfo(QSslCertificate::CommonName),
             cert.subjectInfo(QSslCertificate::CommonName));

    // A leaf server certificate: critical CA:FALSE, never a CA root.
    bool sawBasicConstraints = false;
    for (const QSslCertificateExtension &ext : cert.extensions()) {
        if (ext.name() == QLatin1String("basicConstraints")) {
            sawBasicConstraints = true;
            QVERIFY(ext.isCritical());
            QCOMPARE(ext.value().toMap().value(QStringLiteral("ca")).toBool(), false);
        }
    }
    QVERIFY(sawBasicConstraints);

    const QString subject = cert.subjectDisplayName();
    QVERIFY(subject.contains(QStringLiteral("StrmQt")));

    // Subject Alternative Names should include localhost and 127.0.0.1
    const QMultiMap<QSsl::AlternativeNameEntryType, QString> altNames = cert.subjectAlternativeNames();
    const QList<QString> dnsNames = altNames.values(QSsl::DnsEntry);
    const QList<QString> ipEntries = altNames.values(QSsl::IpAddressEntry);

    QVERIFY(dnsNames.contains(QStringLiteral("localhost")));
    QVERIFY(ipEntries.contains(QStringLiteral("127.0.0.1")));
    QVERIFY(ipEntries.contains(QStringLiteral("100.81.48.104")));
}

void TlsCertificateGeneratorTest::privateKeyPermissionsAreRestricted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString certPath = dir.filePath(QStringLiteral("test_cert.pem"));
    const QString keyPath = dir.filePath(QStringLiteral("test_key.pem"));

    QVERIFY(TlsCertificateGenerator::ensureCertificate({}, true, certPath, keyPath));

    const QFileDevice::Permissions perms = QFile::permissions(keyPath);
    // Group and Other must have no read, write, or execute permissions (0600)
    QVERIFY(!(perms & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup)));
    QVERIFY(!(perms & (QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther)));
}

void TlsCertificateGeneratorTest::fingerprintFormat()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString certPath = dir.filePath(QStringLiteral("test_cert.pem"));
    const QString keyPath = dir.filePath(QStringLiteral("test_key.pem"));

    QVERIFY(TlsCertificateGenerator::ensureCertificate({}, true, certPath, keyPath));
    const QList<QSslCertificate> certs = QSslCertificate::fromPath(certPath);
    QCOMPARE(certs.count(), 1);

    const QString fp = TlsCertificateGenerator::sha256Fingerprint(certs.first());
    // SHA256 has 32 bytes = 32 pairs of hex chars + 31 colons = 95 chars
    QCOMPARE(fp.length(), 95);
    QVERIFY(fp.contains(QLatin1Char(':')));
}

void TlsCertificateGeneratorTest::ensureCertificateAndLoad()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString certPath = dir.filePath(QStringLiteral("test_cert.pem"));
    const QString keyPath = dir.filePath(QStringLiteral("test_key.pem"));

    QVERIFY(TlsCertificateGenerator::ensureCertificate({}, false, certPath, keyPath));

    QSslCertificate cert;
    QSslKey key;
    QVERIFY(TlsCertificateGenerator::load(cert, key, certPath, keyPath));
    QVERIFY(!cert.isNull());
    QVERIFY(!key.isNull());
}

void TlsCertificateGeneratorTest::regeneratedCertificatesHaveDistinctSerials()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString certPath = dir.filePath(QStringLiteral("test_cert.pem"));
    const QString keyPath = dir.filePath(QStringLiteral("test_key.pem"));

    QVERIFY(TlsCertificateGenerator::ensureCertificate({}, true, certPath, keyPath));
    const QList<QSslCertificate> first = QSslCertificate::fromPath(certPath);
    QCOMPARE(first.count(), 1);
    QVERIFY(TlsCertificateGenerator::ensureCertificate({}, true, certPath, keyPath));
    const QList<QSslCertificate> second = QSslCertificate::fromPath(certPath);
    QCOMPARE(second.count(), 1);

    // Same issuer, same serial, different certificate: Firefox refuses that pair.
    QVERIFY(!first.first().serialNumber().isEmpty());
    QVERIFY(first.first().serialNumber() != QByteArrayLiteral("01"));
    QVERIFY(first.first().serialNumber() != second.first().serialNumber());
}

namespace {

QString fingerprintAt(const QString &certPath)
{
    const QList<QSslCertificate> certs = QSslCertificate::fromPath(certPath);
    return certs.size() == 1 ? TlsCertificateGenerator::sha256Fingerprint(certs.first()) : QString();
}

QStringList ipSansAt(const QString &certPath)
{
    const QList<QSslCertificate> certs = QSslCertificate::fromPath(certPath);
    return certs.size() == 1
               ? QStringList(certs.first().subjectAlternativeNames().values(QSsl::IpAddressEntry))
               : QStringList();
}

} // namespace

// AUDIT §5.2.4: a certificate made on another network does not name this
// one's address, and only its expiry used to be looked at.
void TlsCertificateGeneratorTest::missingAddressReplacesTheCertificate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString certPath = dir.filePath(QStringLiteral("cert.pem"));
    const QString keyPath = dir.filePath(QStringLiteral("key.pem"));

    const QStringList home = {QStringLiteral("192.168.1.20"), QStringLiteral("mybox")};
    QVERIFY(TlsCertificateGenerator::ensureCertificate(home, false, certPath, keyPath));
    const QString first = fingerprintAt(certPath);
    QVERIFY(!first.isEmpty());

    // Nothing changed, an address went away (an interface is down), or only
    // the always-present loopback names are asked for: the certificate stays.
    QVERIFY(TlsCertificateGenerator::ensureCertificate(home, false, certPath, keyPath));
    QCOMPARE(fingerprintAt(certPath), first);
    QVERIFY(TlsCertificateGenerator::ensureCertificate({QStringLiteral("mybox")}, false, certPath, keyPath));
    QCOMPARE(fingerprintAt(certPath), first);
    QVERIFY(TlsCertificateGenerator::ensureCertificate({}, false, certPath, keyPath));
    QCOMPARE(fingerprintAt(certPath), first);

    // A new IPv4 address (another network, Tailscale coming up) is not named.
    const QStringList moved = {QStringLiteral("192.168.1.20"), QStringLiteral("100.81.48.104"),
                               QStringLiteral("mybox")};
    QVERIFY(TlsCertificateGenerator::ensureCertificate(moved, false, certPath, keyPath));
    const QString second = fingerprintAt(certPath);
    QVERIFY(!second.isEmpty());
    QVERIFY(second != first);
    QVERIFY(ipSansAt(certPath).contains(QStringLiteral("100.81.48.104")));
    QVERIFY(TlsCertificateGenerator::ensureCertificate(moved, false, certPath, keyPath));
    QCOMPARE(fingerprintAt(certPath), second);

    // So is a new host name.
    QVERIFY(TlsCertificateGenerator::ensureCertificate({QStringLiteral("renamed")}, false, certPath, keyPath));
    QVERIFY(fingerprintAt(certPath) != second);
}

// A new certificate is one every phone has to trust again, so the comparison
// is by address: a different spelling of an address already named, or an IPv6
// address that comes and goes, must not cost one.
void TlsCertificateGeneratorTest::sameAddressesInAnotherSpellingKeepTheCertificate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString certPath = dir.filePath(QStringLiteral("cert.pem"));
    const QString keyPath = dir.filePath(QStringLiteral("key.pem"));

    // A scope id used to go into the extension as text, which OpenSSL refuses:
    // the certificate then had no names at all.
    const QStringList sans = {QStringLiteral("192.168.1.20"), QStringLiteral("fd7a:115c:a1e0::1234"),
                              QStringLiteral("FE80::1%lo"), QStringLiteral("MyBox.Local")};
    QVERIFY(TlsCertificateGenerator::ensureCertificate(sans, false, certPath, keyPath));
    const QString first = fingerprintAt(certPath);
    QVERIFY(!first.isEmpty());
    const QStringList ips = ipSansAt(certPath);
    QVERIFY(ips.contains(QStringLiteral("192.168.1.20")));
    QVERIFY(ips.contains(QStringLiteral("fd7a:115c:a1e0::1234")));
    QVERIFY(ips.contains(QStringLiteral("fe80::1")));

    const QStringList respelled = {
        QStringLiteral("::ffff:192.168.1.20"),                      // IPv4-mapped
        QStringLiteral("FD7A:115C:A1E0:0000:0000:0000:0000:1234"), // upper case, uncompressed
        QStringLiteral("fe80::1%wlan0"),                            // another scope id
        QStringLiteral("mybox.local"),
        QStringLiteral("0:0:0:0:0:0:0:1"),
        QString(),
    };
    QVERIFY(TlsCertificateGenerator::ensureCertificate(respelled, false, certPath, keyPath));
    QCOMPARE(fingerprintAt(certPath), first);

    // A new IPv6 address on its own (a rotated privacy address) is not worth
    // a new certificate...
    QStringList rotated = sans;
    rotated << QStringLiteral("2001:db8:1:2:aaaa:bbbb:cccc:dddd");
    QVERIFY(TlsCertificateGenerator::ensureCertificate(rotated, false, certPath, keyPath));
    QCOMPARE(fingerprintAt(certPath), first);
    // ...but it is named once one is written anyway.
    rotated << QStringLiteral("10.0.0.7");
    QVERIFY(TlsCertificateGenerator::ensureCertificate(rotated, false, certPath, keyPath));
    QVERIFY(fingerprintAt(certPath) != first);
    QVERIFY(ipSansAt(certPath).contains(QStringLiteral("2001:db8:1:2:aaaa:bbbb:cccc:dddd")));
}

// Moving between two networks settles on one certificate naming both.
void TlsCertificateGeneratorTest::namesOfTheReplacedCertificateAreKept()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString certPath = dir.filePath(QStringLiteral("cert.pem"));
    const QString keyPath = dir.filePath(QStringLiteral("key.pem"));
    const QStringList home = {QStringLiteral("192.168.1.20")};
    const QStringList work = {QStringLiteral("10.20.30.40")};

    QVERIFY(TlsCertificateGenerator::ensureCertificate(home, false, certPath, keyPath));
    QVERIFY(TlsCertificateGenerator::ensureCertificate(work, false, certPath, keyPath));
    const QString both = fingerprintAt(certPath);
    QVERIFY(ipSansAt(certPath).contains(QStringLiteral("192.168.1.20")));
    QVERIFY(ipSansAt(certPath).contains(QStringLiteral("10.20.30.40")));
    QVERIFY(TlsCertificateGenerator::ensureCertificate(home, false, certPath, keyPath));
    QVERIFY(TlsCertificateGenerator::ensureCertificate(work, false, certPath, keyPath));
    QCOMPARE(fingerprintAt(certPath), both);

    // "Regenerate certificate" in Settings starts from the current addresses.
    QVERIFY(TlsCertificateGenerator::ensureCertificate(work, true, certPath, keyPath));
    QVERIFY(!ipSansAt(certPath).contains(QStringLiteral("192.168.1.20")));
    QVERIFY(ipSansAt(certPath).contains(QStringLiteral("10.20.30.40")));
}

QTEST_MAIN(TlsCertificateGeneratorTest)
#include "tst_tls_certificate_generator.moc"
