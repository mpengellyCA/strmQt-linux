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

QTEST_MAIN(TlsCertificateGeneratorTest)
#include "tst_tls_certificate_generator.moc"
