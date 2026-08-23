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
//
// And one about the cache in front of it (CoverTintService): a tint that is
// still on screen is never the one thrown away, because losing it is permanent
// — the Images holding that cover keep their pixmap, so it is never decoded
// again and the wash would fall back to flat surface colour for good.

#include "app/CoverTint.h"
#include "app/CoverTintService.h"
#include "app/EmbyImageProvider.h"

#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QSignalSpy>
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

    void serviceRemembersOneTintPerCover();
    void serviceEvictsTheLeastRecentlyUsed();
    void serviceKeepsTheSleeveThatIsStillOnScreen();
    void serviceKeepsTheSleeveThroughSilentFailures();
    void serviceStaysBounded();
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

// ── The cache in front of the clamp ─────────────────────────────────────────
// CoverTintService listens to EmbyImageFetcher::imageDecoded, so the fetcher is
// how a test puts a cover in. It is built with a null client and asked for
// nothing, so it opens no socket.
namespace {

// A cover that definitely produces a tint, and one that definitely does not.
const QImage &blueSleeve()
{
    static const QImage image = solid(QColor(58, 123, 213));
    return image;
}

const QImage &greySleeve()
{
    static const QImage image = solid(QColor(128, 128, 128));
    return image;
}

QString coverId(int n)
{
    return QStringLiteral("cover%1/Primary/tag").arg(n);
}

bool isRemembered(const strmqt::CoverTintService &service, const QString &id)
{
    return service.tintFor(id).alpha() > 0;
}

} // namespace

void TestCoverTint::serviceRemembersOneTintPerCover()
{
    strmqt::EmbyImageFetcher fetcher(nullptr);
    strmqt::CoverTintService service(&fetcher);

    QCOMPARE(service.tintFor(QString()), QColor(Qt::transparent));
    QCOMPARE(service.tintFor(coverId(1)), QColor(Qt::transparent));

    QSignalSpy revisions(&service, &strmqt::CoverTintService::revisionChanged);
    emit fetcher.imageDecoded(coverId(1), blueSleeve());
    QCOMPARE(revisions.count(), 1);

    const QColor tint = service.tintFor(coverId(1));
    QVERIFY(tint.isValid());
    QVERIFY(withinClamp(tint));
    // Both spellings of the same cover reach the same entry: models publish the
    // provider URL, the fetcher keys by the id inside it.
    QCOMPARE(service.tintFor(QStringLiteral("image://emby/") + coverId(1)), tint);

    // A cover that cannot meet the clamp is remembered as a failure, and a
    // remembered failure changes nothing on screen, so it wakes no bindings.
    emit fetcher.imageDecoded(coverId(2), greySleeve());
    QCOMPARE(revisions.count(), 1);
    QCOMPARE(service.tintFor(coverId(2)), QColor(Qt::transparent));
}

// The cache is bounded, so something has to go. It must be the entry nobody has
// asked about for longest — NOT the oldest insertion, which is how the cover
// being looked at gets thrown away while a library scroll fills the map.
void TestCoverTint::serviceEvictsTheLeastRecentlyUsed()
{
    strmqt::EmbyImageFetcher fetcher(nullptr);
    strmqt::CoverTintService service(&fetcher);

    constexpr int kMax = strmqt::CoverTintService::kMaxEntries;
    for (int i = 0; i < kMax; ++i)
        emit fetcher.imageDecoded(coverId(i), blueSleeve());
    // Cover 0 is the oldest insertion; asking for it makes it the youngest use.
    // Cover 2 is asked for afterwards so that the pin is on 2 rather than on 0:
    // what keeps 0 alive below is access order and nothing else. Cover 1 has
    // not been touched since it was inserted, so it is next out.
    QVERIFY(isRemembered(service, coverId(0)));
    QVERIFY(isRemembered(service, coverId(2)));

    emit fetcher.imageDecoded(coverId(kMax), blueSleeve());

    QVERIFY2(isRemembered(service, coverId(0)), "the entry just used was evicted");
    QVERIFY2(!isRemembered(service, coverId(1)), "the least recently used entry survived");
}

// The scroll that started this: one record is playing, its wash is on screen
// and re-asks for its tint on every revision bump, and a whole library's worth
// of covers decodes past it.
void TestCoverTint::serviceKeepsTheSleeveThatIsStillOnScreen()
{
    strmqt::EmbyImageFetcher fetcher(nullptr);
    strmqt::CoverTintService service(&fetcher);

    const QString sleeve = QStringLiteral("nowplaying/Primary/tag");
    emit fetcher.imageDecoded(sleeve, blueSleeve());
    const QColor tint = service.tintFor(sleeve);
    QVERIFY(tint.isValid());

    // Four times the cache. Every decode bumps the revision, CoverWash's
    // binding re-runs, and re-running it is this tintFor call.
    for (int i = 0; i < strmqt::CoverTintService::kMaxEntries * 4; ++i) {
        emit fetcher.imageDecoded(coverId(i), blueSleeve());
        QCOMPARE(service.tintFor(sleeve), tint);
    }

    QCOMPARE(service.tintFor(sleeve), tint);
}

// The hole access order alone does not close: a run of covers that fail the
// clamp inserts entries without bumping `revision`, so nothing on screen is
// woken and nothing re-asks. The last cover asked for is pinned for exactly
// this case.
void TestCoverTint::serviceKeepsTheSleeveThroughSilentFailures()
{
    strmqt::EmbyImageFetcher fetcher(nullptr);
    strmqt::CoverTintService service(&fetcher);

    const QString sleeve = QStringLiteral("nowplaying/Primary/tag");
    emit fetcher.imageDecoded(sleeve, blueSleeve());
    const QColor tint = service.tintFor(sleeve);
    QVERIFY(tint.isValid());

    QSignalSpy revisions(&service, &strmqt::CoverTintService::revisionChanged);
    for (int i = 0; i < strmqt::CoverTintService::kMaxEntries * 2; ++i)
        emit fetcher.imageDecoded(coverId(i), greySleeve());
    QCOMPARE(revisions.count(), 0);

    QCOMPARE(service.tintFor(sleeve), tint);
}

// Bounded is the other half of the contract: keeping the sleeve on screen must
// not turn the cache into the unbounded map of every cover a session scrolled
// past that it exists to avoid.
void TestCoverTint::serviceStaysBounded()
{
    strmqt::EmbyImageFetcher fetcher(nullptr);
    strmqt::CoverTintService service(&fetcher);

    constexpr int kMax = strmqt::CoverTintService::kMaxEntries;
    for (int i = 0; i < kMax * 3; ++i)
        emit fetcher.imageDecoded(coverId(i), blueSleeve());

    int remembered = 0;
    for (int i = 0; i < kMax * 3; ++i) {
        if (isRemembered(service, coverId(i)))
            ++remembered;
    }
    QCOMPARE(remembered, kMax);
}

QTEST_GUILESS_MAIN(TestCoverTint)

#include "tst_cover_tint.moc"
