#include <QtTest>

#include "playback/upscale/UpscalePolicy.h"

using namespace strmqt;

namespace {

// The real table with made-up absolute paths: the policy never touches disk.
UpscaleCatalog fakeCatalog(QList<UpscaleEntry> extra = {})
{
    QList<UpscaleEntry> entries = UpscaleCatalog::builtinEntries();
    entries.append(extra);
    for (UpscaleEntry &entry : entries) {
        for (const QString &file : entry.shaderFiles)
            entry.shaderPaths << QStringLiteral("/shaders/") + file;
    }
    return UpscaleCatalog::fromEntries(entries);
}

UpscaleInput input(QSize source, QSize output, ContentType content = ContentType::LiveAction)
{
    UpscaleInput in;
    in.source = source;
    in.output = output;
    in.content = content;
    return in;
}

} // namespace

Q_DECLARE_METATYPE(strmqt::ContentType)

class UpscalePolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void decides_data();
    void decides();
    void neuralRungsSetEwaForTheRest();
    void hdrSkipsSdrOnlyEntries();
    void brokenEntryIsSkippedAndSaidSo();
    void brokenOverrideFallsBackToAuto();
    void softwareRenderersAreRecognised();
    void factorFitsTheAspect();
};

void UpscalePolicyTest::decides_data()
{
    QTest::addColumn<QSize>("source");
    QTest::addColumn<QSize>("output");
    QTest::addColumn<ContentType>("content");
    QTest::addColumn<int>("ceiling");
    QTest::addColumn<bool>("software");
    QTest::addColumn<QString>("override");
    QTest::addColumn<QString>("activeId");
    QTest::addColumn<QString>("reason");
    QTest::addColumn<int>("rung");

    const auto live = ContentType::LiveAction;
    const auto anime = ContentType::Animation;
    const QSize p720(1280, 720), p1080(1920, 1080), p1440(2560, 1440), p2160(3840, 2160);

    QTest::newRow("downscale → off") << p2160 << p1440 << live << 4 << false << QString()
        << "off" << "Off · 2160p fills the window" << 0;
    QTest::newRow("equal size → off") << p1440 << p1440 << live << 4 << false << QString()
        << "off" << "Off · 1440p fills the window" << 0;
    QTest::newRow("1.1× → FSR, no doubler") << QSize(1280, 720) << QSize(1408, 792) << live << 4
        << false << QString() << "fsr" << "FSR 1 · 720p → 792p" << 2;
    QTest::newRow("2× live action → FSRCNNX 16") << p720 << p1440 << live << 4 << false << QString()
        << "fsrcnnx16" << "FSRCNNX 16 · 720p → 1440p" << 4;
    QTest::newRow("unknown content → live ladder") << p720 << p1440 << ContentType::Unknown << 4
        << false << QString() << "fsrcnnx16" << "FSRCNNX 16 · 720p → 1440p" << 4;
    QTest::newRow("2× animation → Anime4K VL") << QSize(720, 480) << p2160 << anime << 4 << false
        << QString() << "anime4k-vl" << "Anime4K CNN VL · animation · 480p → 2160p" << 4;
    QTest::newRow("ceiling 3 → FSRCNNX 8") << p720 << p1440 << live << 3 << false << QString()
        << "fsrcnnx8" << "FSRCNNX 8 · 720p → 1440p · stepped down for smooth playback" << 3;
    QTest::newRow("ceiling 1 → EWA") << p720 << p1440 << live << 1 << false << QString()
        << "ewa" << "EWA Lanczos · 720p → 1440p · stepped down for smooth playback" << 1;
    QTest::newRow("ceiling 0 → off") << p720 << p1440 << live << 0 << false << QString()
        << "off" << "Off · stepped down for smooth playback" << 0;
    QTest::newRow("ceiling 3 below 1.5× is not a step down") << QSize(1280, 720) << QSize(1408, 792)
        << live << 3 << false << QString() << "fsr" << "FSR 1 · 720p → 792p" << 2;
    QTest::newRow("software → off") << p720 << p1440 << live << 4 << true << QString()
        << "off" << "Needs a GPU" << 0;
    QTest::newRow("software beats an override") << p720 << p1440 << live << 4 << true
        << QStringLiteral("fsr") << "off" << "Needs a GPU" << 0;
    QTest::newRow("override off") << p720 << p1440 << live << 4 << false << QStringLiteral("off")
        << "off" << "Off" << 0;
    QTest::newRow("override nis") << p720 << p1440 << live << 4 << false << QStringLiteral("nis")
        << "nis" << "NVIDIA Image Scaling · 720p → 1440p · manual" << 2;
    QTest::newRow("override other ladder") << p720 << p1440 << live << 4 << false
        << QStringLiteral("anime4k-m") << "anime4k-m" << "Anime4K CNN M · 720p → 1440p · manual" << 3;
    QTest::newRow("override fsrcnnx8") << p720 << p1440 << live << 4 << false
        << QStringLiteral("fsrcnnx8") << "fsrcnnx8" << "FSRCNNX 8 · 720p → 1440p · manual" << 3;
    QTest::newRow("override anime4k-vl on live action") << p720 << p1440 << live << 4 << false
        << QStringLiteral("anime4k-vl") << "anime4k-vl" << "Anime4K CNN VL · 720p → 1440p · manual" << 4;
    QTest::newRow("override fsr") << p720 << p1440 << live << 4 << false << QStringLiteral("fsr")
        << "fsr" << "FSR 1 · 720p → 1440p · manual" << 2;
    QTest::newRow("override ewa") << p720 << p1440 << live << 4 << false << QStringLiteral("ewa")
        << "ewa" << "EWA Lanczos · 720p → 1440p · manual" << 1;
    QTest::newRow("override ignores the ceiling") << p720 << p1440 << live << 1 << false
        << QStringLiteral("fsrcnnx16") << "fsrcnnx16" << "FSRCNNX 16 · 720p → 1440p · manual" << 4;
    QTest::newRow("unknown override → Auto") << p720 << p1440 << live << 4 << false
        << QStringLiteral("no-such") << "fsrcnnx16" << "FSRCNNX 16 · 720p → 1440p" << 4;
    QTest::newRow("no video yet → off") << QSize() << p1440 << live << 4 << false << QString()
        << "off" << "Off" << 0;
    QTest::newRow("no output yet → off") << p720 << QSize() << live << 4 << false << QString()
        << "off" << "Off · 720p fills the window" << 0;
}

void UpscalePolicyTest::decides()
{
    QFETCH(QSize, source);
    QFETCH(QSize, output);
    QFETCH(ContentType, content);
    QFETCH(int, ceiling);
    QFETCH(bool, software);
    QFETCH(QString, override);
    QFETCH(QString, activeId);
    QFETCH(QString, reason);
    QFETCH(int, rung);

    UpscaleInput in = input(source, output, content);
    in.ceiling = ceiling;
    in.softwareRenderer = software;
    in.override = override;
    const UpscaleCatalog catalog = fakeCatalog();
    const UpscaleDecision decision = decideUpscale(catalog, in);
    QCOMPARE(decision.activeId, activeId);
    QCOMPARE(decision.reason, reason);
    QCOMPARE(decision.rung, rung);
    if (activeId == QLatin1String("off")) {
        QVERIFY(decision.chain.isDefault());
        QCOMPARE(decision.activeName, QStringLiteral("Off"));
    } else {
        QCOMPARE(decision.chain, catalog.find(activeId)->chain());
    }
}

void UpscalePolicyTest::neuralRungsSetEwaForTheRest()
{
    // 360p → 1440p is 4×: 2× neural, then ewa_lanczossharp for the other 2×.
    const UpscaleDecision decision =
        decideUpscale(fakeCatalog(), input(QSize(640, 360), QSize(2560, 1440)));
    QCOMPARE(decision.activeId, QStringLiteral("fsrcnnx16"));
    QCOMPARE(decision.chain.shaderPaths, QStringList{QStringLiteral("/shaders/FSRCNNX_x2_16-0-4-1.glsl")});
    QCOMPARE(decision.chain.scale, QStringLiteral("ewa_lanczossharp"));
    QCOMPARE(decision.chain.cscale, QStringLiteral("ewa_lanczossharp"));
}

void UpscalePolicyTest::hdrSkipsSdrOnlyEntries()
{
    QList<UpscaleEntry> entries = UpscaleCatalog::builtinEntries();
    entries[0].sdrOnly = true; // fsrcnnx16
    for (UpscaleEntry &entry : entries)
        for (const QString &file : entry.shaderFiles)
            entry.shaderPaths << QStringLiteral("/shaders/") + file;
    const UpscaleCatalog catalog = UpscaleCatalog::fromEntries(entries);

    UpscaleInput in = input(QSize(1280, 720), QSize(2560, 1440));
    QCOMPARE(decideUpscale(catalog, in).activeId, QStringLiteral("fsrcnnx16"));
    in.hdr = true;
    QCOMPARE(decideUpscale(catalog, in).activeId, QStringLiteral("fsrcnnx8"));
    in.override = QStringLiteral("fsrcnnx16");
    QCOMPARE(decideUpscale(catalog, in).activeId, QStringLiteral("fsrcnnx8"));
}

void UpscalePolicyTest::brokenEntryIsSkippedAndSaidSo()
{
    UpscaleInput in = input(QSize(1280, 720), QSize(2560, 1440));
    in.broken = {QStringLiteral("fsrcnnx16")};
    const UpscaleDecision decision = decideUpscale(fakeCatalog(), in);
    QCOMPARE(decision.activeId, QStringLiteral("fsrcnnx8"));
    QCOMPARE(decision.reason,
             QStringLiteral("FSRCNNX 16 failed to load — using FSRCNNX 8 · 720p → 1440p"));
}

void UpscalePolicyTest::brokenOverrideFallsBackToAuto()
{
    UpscaleInput in = input(QSize(1280, 720), QSize(2560, 1440));
    in.override = QStringLiteral("nis");
    in.broken = {QStringLiteral("nis")};
    const UpscaleDecision decision = decideUpscale(fakeCatalog(), in);
    QCOMPARE(decision.activeId, QStringLiteral("fsrcnnx16"));
    QCOMPARE(decision.reason, QStringLiteral(
        "NVIDIA Image Scaling failed to load — using FSRCNNX 16 · 720p → 1440p"));
}

void UpscalePolicyTest::softwareRenderersAreRecognised()
{
    QVERIFY(isSoftwareGlRenderer(QStringLiteral("llvmpipe (LLVM 19.1.7, 256 bits)")));
    QVERIFY(isSoftwareGlRenderer(QStringLiteral("softpipe")));
    QVERIFY(isSoftwareGlRenderer(QStringLiteral("SWR (LLVM 12.0.0, 256 bits)")));
    QVERIFY(!isSoftwareGlRenderer(QStringLiteral("AMD Radeon RX 9070 XT (radeonsi, gfx1201, LLVM 21.1.4, DRM 3.64)")));
    QVERIFY(!isSoftwareGlRenderer(QString()));
}

void UpscalePolicyTest::factorFitsTheAspect()
{
    // 4:3 in a 16:9 window: height-bound.
    QCOMPARE(upscaleFactor(QSize(640, 480), QSize(2560, 1440)), 3.0);
    // Scope in 16:9: width-bound.
    QCOMPARE(upscaleFactor(QSize(1920, 800), QSize(3840, 2160)), 2.0);
    QCOMPARE(upscaleFactor(QSize(), QSize(100, 100)), 0.0);
    QCOMPARE(upscaleFactor(QSize(100, 100), QSize(0, 0)), 0.0);
}

QTEST_GUILESS_MAIN(UpscalePolicyTest)
#include "tst_upscale_policy.moc"
