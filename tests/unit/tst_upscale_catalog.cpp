#include <QtTest>

#include "playback/upscale/UpscaleCatalog.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

using namespace strmqt;

namespace {

const QString kShaderDir = QStringLiteral(STRMQT_SOURCE_DIR "/shaders");

QByteArray sha256Of(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}

} // namespace

class UpscaleCatalogTest : public QObject
{
    Q_OBJECT

private slots:
    void everyEntryHasItsFilesAndLicence();
    void sourcesListsEveryShaderWithItsChecksum();
    void bothLaddersHaveEveryRung();
    void missingFilesDropTheEntry();
    void lookupsByIdRungAndPath();
};

void UpscaleCatalogTest::everyEntryHasItsFilesAndLicence()
{
    const UpscaleCatalog catalog = UpscaleCatalog::load(kShaderDir);
    QCOMPARE(catalog.entries().size(), UpscaleCatalog::builtinEntries().size());
    for (const UpscaleEntry &entry : catalog.entries()) {
        QCOMPARE(entry.shaderPaths.size(), entry.shaderFiles.size());
        for (const QString &path : entry.shaderPaths)
            QVERIFY2(QFileInfo(path).isFile(), qPrintable(path));
        if (!entry.shaderFiles.isEmpty()) {
            QVERIFY2(!entry.licenseFile.isEmpty(), qPrintable(entry.id));
            QVERIFY2(QFileInfo(kShaderDir + u'/' + entry.licenseFile).isFile(),
                     qPrintable(entry.licenseFile));
        }
    }
}

void UpscaleCatalogTest::sourcesListsEveryShaderWithItsChecksum()
{
    QFile sources(kShaderDir + QStringLiteral("/SOURCES"));
    QVERIFY(sources.open(QIODevice::ReadOnly | QIODevice::Text));
    QHash<QString, QByteArray> listed;
    while (!sources.atEnd()) {
        const QString line = QString::fromUtf8(sources.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        const QStringList fields = line.split(u' ', Qt::SkipEmptyParts);
        QCOMPARE(fields.size(), 2);
        listed.insert(fields.at(1), fields.at(0).toLatin1());
    }
    for (const UpscaleEntry &entry : UpscaleCatalog::builtinEntries()) {
        for (const QString &file : entry.shaderFiles) {
            QVERIFY2(listed.contains(file), qPrintable(file + QStringLiteral(" not in SOURCES")));
            QCOMPARE(sha256Of(kShaderDir + u'/' + file), listed.value(file));
        }
    }
}

void UpscaleCatalogTest::bothLaddersHaveEveryRung()
{
    const UpscaleCatalog catalog = UpscaleCatalog::load(kShaderDir);
    for (const ContentType type : {ContentType::LiveAction, ContentType::Animation}) {
        for (int rung = 1; rung <= 4; ++rung)
            QVERIFY2(catalog.atRung(type, rung) != nullptr,
                     qPrintable(QStringLiteral("rung %1 missing").arg(rung)));
    }
    QCOMPARE(catalog.atRung(ContentType::LiveAction, 4)->id, QStringLiteral("fsrcnnx16"));
    QCOMPARE(catalog.atRung(ContentType::Animation, 4)->id, QStringLiteral("anime4k-vl"));
    QCOMPARE(catalog.atRung(ContentType::Unknown, 3)->id, QStringLiteral("fsrcnnx8"));
    QCOMPARE(catalog.atRung(ContentType::Animation, 2)->id, QStringLiteral("fsr"));
    QCOMPARE(catalog.atRung(ContentType::Animation, 1)->id, QStringLiteral("ewa"));
    QVERIFY(catalog.find(QStringLiteral("nis"))->manualOnly());
}

void UpscaleCatalogTest::missingFilesDropTheEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Only FSR is present: every other shader entry must go, EWA (no files) stays.
    QVERIFY(QFile::copy(kShaderDir + QStringLiteral("/FSR.glsl"), dir.filePath(QStringLiteral("FSR.glsl"))));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("upscaling shaders missing")));
    const UpscaleCatalog catalog = UpscaleCatalog::load(dir.path());
    QStringList ids;
    for (const UpscaleEntry &entry : catalog.entries())
        ids << entry.id;
    QCOMPARE(ids, (QStringList{QStringLiteral("fsr"), QStringLiteral("ewa")}));
}

void UpscaleCatalogTest::lookupsByIdRungAndPath()
{
    const UpscaleCatalog catalog = UpscaleCatalog::load(kShaderDir);
    QVERIFY(catalog.find(QStringLiteral("no-such-id")) == nullptr);
    const UpscaleEntry *fsr = catalog.find(QStringLiteral("fsr"));
    QVERIFY(fsr);
    QCOMPARE(catalog.idsUsingShader(fsr->shaderPaths.constFirst()), QStringList{QStringLiteral("fsr")});
    const VideoProcessing chain = catalog.find(QStringLiteral("fsrcnnx16"))->chain();
    QCOMPARE(chain.scale, QStringLiteral("ewa_lanczossharp"));
    QCOMPARE(chain.cscale, QStringLiteral("ewa_lanczossharp"));
    QVERIFY(catalog.find(QStringLiteral("ewa"))->chain().shaderPaths.isEmpty());
    QVERIFY(VideoProcessing{}.isDefault());
    QVERIFY(!chain.isDefault());
}

QTEST_GUILESS_MAIN(UpscaleCatalogTest)
#include "tst_upscale_catalog.moc"
