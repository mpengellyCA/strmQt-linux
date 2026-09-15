#include <QFile>
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
    QVERIFY(cert.isSelfSigned());

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

QTEST_MAIN(TlsCertificateGeneratorTest)
#include "tst_tls_certificate_generator.moc"
