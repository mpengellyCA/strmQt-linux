#pragma once

#include <QSize>
#include <QtMath>
#include <QtTypes>

namespace strmqt::imagelimits {

inline constexpr qint64 kMaxEncodedBytes = 16 * 1024 * 1024;
inline constexpr qint64 kMaxDecodedPixels = 20'000'000;
inline constexpr int kMaxDimension = 8192;
// A source may be large enough to inspect or export, but a QML thumbnail must
// never turn that whole source into an RGBA allocation. Four megapixels is
// deliberately generous for a HiDPI cover while keeping each decode around
// 16 MiB rather than the 80 MiB allowed by the source-metadata ceiling.
inline constexpr qint64 kMaxTargetPixels = 4'194'304;
inline constexpr int kMaxTargetDimension = 4096;
inline constexpr int kDefaultTargetWidth = 480;

inline bool encodedBytesAllowed(qint64 bytes)
{
    return bytes >= 0 && bytes <= kMaxEncodedBytes;
}

inline bool decodedSizeAllowed(const QSize &size)
{
    return size.isValid() && size.width() <= kMaxDimension &&
           size.height() <= kMaxDimension &&
           qint64(size.width()) * size.height() <= kMaxDecodedPixels;
}

inline int boundedRequestWidth(int requestedWidth)
{
    const int width = requestedWidth > 0 ? requestedWidth : kDefaultTargetWidth;
    return qMin(width, kMaxTargetDimension);
}

// Returns a no-upscale decode target that preserves the source aspect ratio.
// A missing requested dimension means "derive it from the source", matching
// how QML commonly asks for a width-only sourceSize.
inline QSize boundedTargetSize(const QSize &source, const QSize &requested)
{
    if (!decodedSizeAllowed(source))
        return {};

    qint64 targetWidth = requested.width();
    qint64 targetHeight = requested.height();
    if (targetWidth <= 0 && targetHeight <= 0)
        targetWidth = kDefaultTargetWidth;
    if (targetWidth <= 0)
        targetWidth = qMax<qint64>(1, qRound64(qreal(source.width()) * targetHeight /
                                              source.height()));
    if (targetHeight <= 0)
        targetHeight = qMax<qint64>(1, qRound64(qreal(source.height()) * targetWidth /
                                               source.width()));

    targetWidth = qMin<qint64>(targetWidth, kMaxTargetDimension);
    targetHeight = qMin<qint64>(targetHeight, kMaxTargetDimension);
    QSize target = source;
    if (source.width() > targetWidth || source.height() > targetHeight) {
        target = source.scaled(QSize(static_cast<int>(targetWidth),
                                     static_cast<int>(targetHeight)),
                               Qt::KeepAspectRatio);
    }

    const qint64 pixels = qint64(target.width()) * target.height();
    if (pixels > kMaxTargetPixels) {
        const qreal scale = qSqrt(qreal(kMaxTargetPixels) / pixels);
        target = QSize(qMax(1, qFloor(target.width() * scale)),
                       qMax(1, qFloor(target.height() * scale)));
    }
    return target;
}

} // namespace strmqt::imagelimits
