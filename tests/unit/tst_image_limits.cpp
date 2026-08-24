#include <QtTest>

#include "app/ImageLimits.h"

using namespace strmqt;

class ImageLimitsTest : public QObject
{
    Q_OBJECT

private slots:
    void encodedResponseCeiling();
    void decodedAllocationCeiling();
};

void ImageLimitsTest::encodedResponseCeiling()
{
    QVERIFY(!imagelimits::encodedBytesAllowed(-1));
    QVERIFY(imagelimits::encodedBytesAllowed(imagelimits::kMaxEncodedBytes));
    QVERIFY(!imagelimits::encodedBytesAllowed(imagelimits::kMaxEncodedBytes + 1));
}

void ImageLimitsTest::decodedAllocationCeiling()
{
    QVERIFY(!imagelimits::decodedSizeAllowed({}));
    QVERIFY(imagelimits::decodedSizeAllowed(QSize(5'000, 4'000)));
    QVERIFY(!imagelimits::decodedSizeAllowed(QSize(5'001, 4'000)));
    QVERIFY(!imagelimits::decodedSizeAllowed(QSize(imagelimits::kMaxDimension + 1, 1)));
    QVERIFY(!imagelimits::decodedSizeAllowed(QSize(1, imagelimits::kMaxDimension + 1)));
}

QTEST_GUILESS_MAIN(ImageLimitsTest)
#include "tst_image_limits.moc"
