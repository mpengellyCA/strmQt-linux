#include <QtTest>

#include "app/ImageLimits.h"

using namespace strmqt;

class ImageLimitsTest : public QObject
{
    Q_OBJECT

private slots:
    void encodedResponseCeiling();
    void decodedAllocationCeiling();
    void targetDecodeSize();
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

void ImageLimitsTest::targetDecodeSize()
{
    QCOMPARE(imagelimits::boundedTargetSize(QSize(5'000, 4'000), QSize(200, 0)),
             QSize(200, 160));
    QCOMPARE(imagelimits::boundedTargetSize(QSize(120, 80), QSize(200, 0)), QSize(120, 80));

    const QSize absoluteBound =
        imagelimits::boundedTargetSize(QSize(5'000, 4'000), QSize(20'000, 20'000));
    QVERIFY(absoluteBound.width() <= imagelimits::kMaxTargetDimension);
    QVERIFY(absoluteBound.height() <= imagelimits::kMaxTargetDimension);
    QVERIFY(qint64(absoluteBound.width()) * absoluteBound.height() <=
            imagelimits::kMaxTargetPixels);
}

QTEST_GUILESS_MAIN(ImageLimitsTest)
#include "tst_image_limits.moc"
