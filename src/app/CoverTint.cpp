#include "CoverTint.h"

#include <QHash>

namespace strmqt::covertint {

namespace {

// The sample grid. A dominant colour does not need resolution — it needs a
// spread of the cover — and 64×64 keeps the whole pass under a tenth of a
// millisecond, which matters because this runs on the GUI thread inside the
// image reply handler.
constexpr int kSampleEdge = 64;

// Straight (non-premultiplied) alpha, so this is the pixel's own opacity.
// Anything more transparent than half contributes nothing a viewer would call
// the sleeve's colour.
constexpr int kAlphaFloor = 128;

// A pixel is "chromatic" — a candidate for the accent — when it has HSV
// saturation to spare and is neither crushed to black nor blown to white, where
// hue is numerically unstable and visually meaningless.
constexpr qreal kChromaFloor = 0.20;
constexpr qreal kChromaMinValue = 0.15;
constexpr qreal kChromaMaxValue = 0.95;

// How much of the cover has to be chromatic before the chromatic winner is
// taken over the plain modal colour. Without a floor, a single coloured pixel
// on a grey sleeve would decide the room's colour.
constexpr qreal kChromaMinShare = 0.05;

// 4 bits per channel: 4096 buckets over at most 4096 samples. Coarse on
// purpose — the point is to find the region of colour the sleeve lives in, not
// to reproduce a pixel.
quint16 bucketKey(QRgb pixel)
{
    return static_cast<quint16>(((qRed(pixel) >> 4) << 8) | ((qGreen(pixel) >> 4) << 4) |
                                (qBlue(pixel) >> 4));
}

struct Bucket
{
    quint32 count = 0;
    quint32 r = 0;
    quint32 g = 0;
    quint32 b = 0;

    void add(QRgb pixel)
    {
        ++count;
        r += static_cast<quint32>(qRed(pixel));
        g += static_cast<quint32>(qGreen(pixel));
        b += static_cast<quint32>(qBlue(pixel));
    }

    QColor mean() const
    {
        if (count == 0)
            return {};
        return QColor(static_cast<int>(r / count), static_cast<int>(g / count),
                      static_cast<int>(b / count));
    }
};

// QColor's HSL accessors are float in Qt 6; the clamp itself is stated in qreal
// because that is what the QML property and the tests speak.
struct Hsl
{
    qreal hue = -1.0;
    qreal saturation = 0.0;
    qreal lightness = 0.0;
};

Hsl hslOf(const QColor &colour)
{
    float h = 0.0F;
    float s = 0.0F;
    float l = 0.0F;
    colour.toRgb().getHslF(&h, &s, &l);
    return {qreal(h), qreal(s), qreal(l)};
}

// The fullest bucket, or a default-constructed one when there are none.
Bucket heaviest(const QHash<quint16, Bucket> &buckets)
{
    Bucket best;
    for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
        if (it.value().count > best.count)
            best = it.value();
    }
    return best;
}

} // namespace

QColor clampWashTint(const QColor &raw)
{
    if (!raw.isValid())
        return {};

    const Hsl hsl = hslOf(raw);
    // QColor reports hue -1 for an achromatic colour; there is no hue to keep.
    if (hsl.hue < 0.0 || hsl.saturation < kMinSaturation)
        return {};

    const QColor clamped =
        QColor::fromHslF(float(hsl.hue), float(qMin(hsl.saturation, kMaxSaturation)),
                         float(qBound(kMinLightness, hsl.lightness, kMaxLightness)))
            .toRgb();
    // Belt and braces: the wash is drawn from the RGB value, so it is the RGB
    // value that has to be inside the box. If 8-bit rounding ever put it
    // outside, fall back rather than draw something untested.
    return withinClamp(clamped) ? clamped : QColor();
}

bool withinClamp(const QColor &colour)
{
    if (!colour.isValid())
        return false;
    const Hsl hsl = hslOf(colour);
    return hsl.saturation <= kMaxSaturation + kClampEpsilon &&
           hsl.lightness >= kMinLightness - kClampEpsilon &&
           hsl.lightness <= kMaxLightness + kClampEpsilon;
}

QColor dominantWashTint(const QImage &image)
{
    if (image.isNull())
        return {};

    // Scale first, convert second: the conversion then touches 4 096 pixels
    // instead of the quarter-million a cover arrives as.
    QImage sample = image;
    if (sample.width() > kSampleEdge || sample.height() > kSampleEdge) {
        sample = sample.scaled(kSampleEdge, kSampleEdge, Qt::KeepAspectRatio,
                               Qt::FastTransformation);
    }
    if (sample.format() != QImage::Format_ARGB32)
        sample = sample.convertToFormat(QImage::Format_ARGB32);
    if (sample.isNull() || sample.width() <= 0 || sample.height() <= 0)
        return {};

    QHash<quint16, Bucket> chromatic;
    QHash<quint16, Bucket> everything;
    int opaque = 0;

    for (int y = 0; y < sample.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(sample.constScanLine(y));
        for (int x = 0; x < sample.width(); ++x) {
            const QRgb pixel = line[x];
            if (qAlpha(pixel) < kAlphaFloor)
                continue;
            ++opaque;
            const quint16 key = bucketKey(pixel);
            everything[key].add(pixel);

            const QColor colour(pixel);
            const qreal value = colour.valueF();
            if (colour.saturationF() >= kChromaFloor && value >= kChromaMinValue &&
                value <= kChromaMaxValue)
                chromatic[key].add(pixel);
        }
    }

    if (opaque == 0)
        return {};

    const Bucket chromaticWinner = heaviest(chromatic);
    const qreal share = static_cast<qreal>(chromaticWinner.count) / static_cast<qreal>(opaque);
    const Bucket winner = share >= kChromaMinShare ? chromaticWinner : heaviest(everything);
    return clampWashTint(winner.mean());
}

} // namespace strmqt::covertint
