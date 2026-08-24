#pragma once

#include <QSize>
#include <QtTypes>

namespace strmqt::imagelimits {

inline constexpr qint64 kMaxEncodedBytes = 16 * 1024 * 1024;
inline constexpr qint64 kMaxDecodedPixels = 20'000'000;
inline constexpr int kMaxDimension = 8192;

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

} // namespace strmqt::imagelimits
