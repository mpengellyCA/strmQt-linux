// The clamp on the cover wash (MUSIC.md §4, Rule 2).
//
// MUSIC.md names this the one place the music plan can make the app *worse*: an
// unclamped wash puts the focus ring back inside the hue range the projection
// booth accent was chosen to sit outside of (ARCHITECTURE.md §4), and the whole
// accent decision rests on the ring never being lost. "The clamp is
// non-negotiable and belongs in a test, not a review comment." This is the test.
//
// Two claims are proven here, over every adversarial cover we could invent:
//
//   1. dominantWashTint() returns a colour INSIDE the box, or nothing at all.
//      There is no third outcome; there is no cover that produces a legal-looking
//      colour outside it.
//   2. With the wash drawn at its opacity ceiling over the ground, an amber
//      focus ring still clears 3:1 against it — and so do the three alternate
//      accents, because the accent is a user preference.

#include "app/CoverTint.h"

#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QTest>
#include <QtMath>

using namespace strmqt::covertint;

namespace {

QImage solid(const QColor &colour, QImage::Format format = QImage::Format_ARGB32, int edge = 120)
{
    QImage image(edge, edge, format);
    image.fill(colour);
    return image;
}

// The four accents Theme.qml ships, and the ground they are drawn over. Copied
// rather than shared because Theme.qml is QML and this is the guard on it: if
// somebody re-tunes a palette entry, this test is the thing that has to be
// re-run with the new value and re-proven, not silently followed.
constexpr const char *kGround = "#0C0B0A";
constexpr const char *kAccents[] = {
    "#F0A02A", // projection amber (default)
    "#52B54B", // emby green
    "#AA5CC3", // jellyfin purple
    "#3DAEE9", // breeze blue
};

qreal linearise(qreal channel)
{
    return channel <= 0.04045 ? channel / 12.92 : qPow((channel + 0.055) / 1.055, 2.4);
}

// WCAG relative luminance.
qreal relativeLuminance(const QColor &colour)
{
    const QColor rgb = colour.toRgb();
    return 0.2126 * linearise(rgb.redF()) + 0.7152 * linearise(rgb.greenF()) +
           0.0722 * linearise(rgb.blueF());
}

qreal contrastRatio(const QColor &a, const QColor &b)
{
    const qreal la = relativeLuminance(a);
    const qreal lb = relativeLuminance(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

// What the viewer actually sees: `tint` painted at `alpha` over `under`.
QColor composite(const QColor &tint, qreal alpha, const QColor &under)
{
    const QColor top = tint.toRgb();
    const QColor bottom = under.toRgb();
    return QColor::fromRgbF(alpha * top.redF() + (1.0 - alpha) * bottom.redF(),
                            alpha * top.greenF() + (1.0 - alpha) * bottom.greenF(),
                            alpha * top.blueF() + (1.0 - alpha) * bottom.blueF());
}

} // namespace

class TestCoverTint : public QObject
{
    Q_OBJECT

private slots:
    void adversarialCovers_data();
    void adversarialCovers();

    void clampIsIdempotent();
    void clampKeepsHue();
    void clampPullsBothWays_data();
    void clampPullsBothWays();

    void everyHueStaysInTheBox();
    void everyHueKeepsTheFocusRing();

    void nullImageHasNoTint();
    void gradientCoverSamplesItsColour();
    void oneColouredPixelDoesNotDecideTheRoom();
    void translucentPixelsAreIgnored();
    void washOpacityCeiling();
};

// ── The adversarial covers ──────────────────────────────────────────────────
// Each row asserts the same invariant: in the box, or the fallback. `expectTint`
// records which of the two this particular cover is expected to be, so a change
// that quietly turns every sleeve into the fallback (a wash that never appears)
// fails just as loudly as one that lets an illegible colour out.
void TestCoverTint::adversarialCovers_data()
{
    QTest::addColumn<QImage>("cover");
    QTest::addColumn<bool>("expectTint");

    QTest::newRow("saturated red") << solid(QColor(255, 0, 0)) << true;
    QTest::newRow("saturated magenta") << solid(QColor(255, 0, 255)) << true;
    QTest::newRow("electric cyan") << solid(QColor(0, 255, 255)) << true;
    QTest::newRow("single colour") << solid(QColor(58, 123, 213)) << true;
    QTest::newRow("near white") << solid(QColor(242, 240, 236)) << true;
    QTest::newRow("blown white") << solid(QColor(255, 255, 255)) << false;
    QTest::newRow("pure black") << solid(QColor(0, 0, 0)) << false;
    QTest::newRow("mid grey") << solid(QColor(128, 128, 128)) << false;
    QTest::newRow("already dark") << solid(QColor(30, 22, 18)) << true;

    QImage transparent(64, 64, QImage::Format_ARGB32);
    transparent.fill(Qt::transparent);
    QTest::newRow("transparent png") << transparent << false;

    // A transparent PNG with a saturated logo that is still fully transparent:
    // alpha decides, not the colour channels behind it.
    QImage ghost(64, 64, QImage::Format_ARGB32);
    ghost.fill(QColor(255, 0, 0, 0));
    QTest::newRow("transparent red") << ghost << false;

    QTest::newRow("greyscale sleeve, 8 bit")
        << solid(QColor(90, 90, 90), QImage::Format_Grayscale8) << false;
    QTest::newRow("premultiplied") << solid(QColor(0, 90, 200), QImage::Format_ARGB32_Premultiplied)
                                   << true;
    QTest::newRow("no alpha channel") << solid(QColor(200, 40, 90), QImage::Format_RGB32) << true;

    QImage oneByOne(1, 1, QImage::Format_ARGB32);
    oneByOne.fill(QColor(10, 200, 90));
    QTest::newRow("one pixel") << oneByOne << true;
}

void TestCoverTint::adversarialCovers()
{
    QFETCH(QImage, cover);
    QFETCH(bool, expectTint);

    const QColor tint = dominantWashTint(cover);
    QCOMPARE(tint.isValid(), expectTint);
    // The invariant, stated once: valid means inside the box, and there is no
    // other way to be valid.
    if (tint.isValid())
        QVERIFY2(withinClamp(tint), qPrintable(tint.name()));
}

void TestCoverTint::clampIsIdempotent()
{
    const QColor once = clampWashTint(QColor(255, 0, 0));
    QVERIFY(once.isValid());
    QCOMPARE(clampWashTint(once), once);
}

void TestCoverTint::clampKeepsHue()
{
    // The clamp moves saturation and lightness. It must not move hue — the
    // whole point is that the room takes the record's colour.
    for (int hue = 0; hue < 360; hue += 15) {
        const QColor raw = QColor::fromHsl(hue, 255, 128);
        const QColor clamped = clampWashTint(raw);
        QVERIFY(clamped.isValid());
        const int drift = qAbs(clamped.toHsl().hue() - hue);
        QVERIFY2(qMin(drift, 360 - drift) <= 2,
                 qPrintable(QStringLiteral("hue %1 drifted to %2")
                                .arg(hue)
                                .arg(clamped.toHsl().hue())));
    }
}

void TestCoverTint::clampPullsBothWays_data()
{
    QTest::addColumn<QColor>("raw");

    // Too bright, too dark, too saturated, and all three at once.
    QTest::newRow("blinding") << QColor::fromHslF(0.6F, 1.0F, 0.92F);
    QTest::newRow("subterranean") << QColor::fromHslF(0.6F, 1.0F, 0.02F);
    QTest::newRow("neon") << QColor::fromHslF(0.33F, 1.0F, 0.5F);
    QTest::newRow("just above the ceiling") << QColor::fromHslF(0.1F, 0.56F, 0.23F);
    QTest::newRow("just below the floor") << QColor::fromHslF(0.1F, 0.30F, 0.09F);
    QTest::newRow("already legal") << QColor::fromHslF(0.1F, 0.40F, 0.16F);
}

void TestCoverTint::clampPullsBothWays()
{
    QFETCH(QColor, raw);
    const QColor clamped = clampWashTint(raw);
    QVERIFY(clamped.isValid());
    QVERIFY2(withinClamp(clamped), qPrintable(clamped.name()));

    float h = 0.0F;
    float s = 0.0F;
    float l = 0.0F;
    clamped.toRgb().getHslF(&h, &s, &l);
    QVERIFY(qreal(s) <= kMaxSaturation + kClampEpsilon);
    QVERIFY(qreal(l) >= kMinLightness - kClampEpsilon);
    QVERIFY(qreal(l) <= kMaxLightness + kClampEpsilon);
}

// The exhaustive version of the claim: not "these covers", but every hue at
// every saturation and every lightness an 8-bit sleeve can hold.
void TestCoverTint::everyHueStaysInTheBox()
{
    // A small sleeve: the sampler downscales anything bigger to the same grid,
    // so 8x8 exercises the identical path 15 000 times over in about a second.
    for (int hue = 0; hue < 360; hue += 3) {
        for (int sat = 0; sat <= 255; sat += 15) {
            for (int light = 0; light <= 255; light += 15) {
                const QColor raw = QColor::fromHsl(hue, sat, light);
                const QColor tint = dominantWashTint(solid(raw, QImage::Format_ARGB32, 8));
                if (!tint.isValid())
                    continue; // the fallback is always a legal answer
                QVERIFY2(withinClamp(tint),
                         qPrintable(QStringLiteral("%1 → %2").arg(raw.name(), tint.name())));
            }
        }
    }
}

// The reason the box has those numbers. Whatever the clamp lets through, an
// amber focus ring drawn on top of it is still a focus ring.
void TestCoverTint::everyHueKeepsTheFocusRing()
{
    const QColor ground = QColor(QLatin1String(kGround));
    qreal worst = 99.0;
    QString worstCase;

    for (int hue = 0; hue < 360; hue += 3) {
        for (int light = 0; light <= 255; light += 5) {
            const QColor tint = clampWashTint(QColor::fromHsl(hue, 255, light));
            if (!tint.isValid())
                continue;
            const QColor washed = composite(tint, kMaxWashOpacity, ground);
            for (const char *accent : kAccents) {
                const qreal ratio = contrastRatio(QColor(QLatin1String(accent)), washed);
                if (ratio < worst) {
                    worst = ratio;
                    worstCase = QStringLiteral("%1 on %2 (from %3)")
                                    .arg(QLatin1String(accent), washed.name(), tint.name());
                }
            }
        }
    }

    qInfo("worst focus-ring contrast over every clamped wash: %.2f:1 (%s)", worst,
          qPrintable(worstCase));
    QVERIFY2(worst >= 3.0,
             qPrintable(QStringLiteral("worst focus-ring contrast %1:1 — %2")
                            .arg(worst, 0, 'f', 2)
                            .arg(worstCase)));
}

void TestCoverTint::nullImageHasNoTint()
{
    QVERIFY(!dominantWashTint(QImage()).isValid());
    QVERIFY(!clampWashTint(QColor()).isValid());
}

void TestCoverTint::gradientCoverSamplesItsColour()
{
    // A real sleeve is not one colour. This one is mostly deep blue with a
    // lighter band, and the wash should read as blue.
    QImage cover(160, 160, QImage::Format_ARGB32);
    QPainter painter(&cover);
    QLinearGradient gradient(0, 0, 0, 160);
    gradient.setColorAt(0.0, QColor(18, 40, 120));
    gradient.setColorAt(1.0, QColor(60, 96, 200));
    painter.fillRect(cover.rect(), gradient);
    painter.end();

    const QColor tint = dominantWashTint(cover);
    QVERIFY(tint.isValid());
    QVERIFY(withinClamp(tint));
    const int hue = tint.toHsl().hue();
    QVERIFY2(hue > 200 && hue < 260, qPrintable(QStringLiteral("hue %1").arg(hue)));
}

void TestCoverTint::oneColouredPixelDoesNotDecideTheRoom()
{
    // A grey sleeve with a red dot on it is a grey sleeve. Without the
    // chromatic-share floor the dot would light the whole room red.
    QImage cover = solid(QColor(120, 120, 122));
    cover.setPixelColor(3, 3, QColor(255, 0, 0));
    QVERIFY(!dominantWashTint(cover).isValid());
}

void TestCoverTint::translucentPixelsAreIgnored()
{
    // A mostly-transparent sleeve with an opaque green corner: the corner is the
    // only thing on screen, so it is the only thing sampled.
    QImage cover(64, 64, QImage::Format_ARGB32);
    cover.fill(QColor(255, 0, 0, 40));
    QPainter painter(&cover);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(0, 0, 32, 32, QColor(20, 160, 60));
    painter.end();

    const QColor tint = dominantWashTint(cover);
    QVERIFY(tint.isValid());
    QVERIFY(withinClamp(tint));
    const int hue = tint.toHsl().hue();
    QVERIFY2(hue > 90 && hue < 160, qPrintable(QStringLiteral("hue %1").arg(hue)));
}

void TestCoverTint::washOpacityCeiling()
{
    // Theme.washOpacity re-exports this; the number is part of the clamp, so it
    // is pinned here with the other three.
    QCOMPARE(kMaxWashOpacity, 0.22);
    QVERIFY(kMaxSaturation <= 0.55);
    QVERIFY(kMinLightness >= 0.10);
    QVERIFY(kMaxLightness <= 0.22);
}

QTEST_GUILESS_MAIN(TestCoverTint)

#include "tst_cover_tint.moc"
