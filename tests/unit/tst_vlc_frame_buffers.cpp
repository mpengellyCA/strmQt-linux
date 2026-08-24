#include <QtTest>

#include "playback/vlc/VlcPlayer.h"

using namespace strmqt;

class VlcFrameBuffersTest : public QObject
{
    Q_OBJECT

private slots:
    void swapsPreallocatedFramesWithoutPublishingScratch();
};

void VlcFrameBuffersTest::swapsPreallocatedFramesWithoutPublishingScratch()
{
    vlcframes::Buffers buffers;
    buffers.configure(QSize(64, 36));

    void *planes[1] = {};
    void *firstPicture = buffers.lock(planes);
    QVERIFY(firstPicture != nullptr);
    QVERIFY(planes[0] != nullptr);
    auto *pixels = static_cast<QRgb *>(planes[0]);
    pixels[0] = qRgb(12, 34, 56);
    buffers.unlock();
    QVERIFY(buffers.display(firstPicture));

    const QImage first = buffers.current();
    QVERIFY(!first.isNull());
    QCOMPARE(first.pixel(0, 0), qRgb(12, 34, 56));
    const QImage shallow = buffers.current();
    QCOMPARE(shallow.constBits(), first.constBits());

    // A late display from an earlier format/load must not publish a newly
    // configured buffer. The QImage member address itself is not an identity.
    planes[0] = nullptr;
    void *retiredPicture = buffers.lock(planes);
    buffers.unlock();
    buffers.configure(QSize(64, 36));
    QVERIFY(!buffers.display(retiredPicture));
    QVERIFY(buffers.current().isNull());

    // clear() retires both publishable buffers but deliberately retains a
    // correctly-sized scratch plane for a late decoder lock.
    buffers.clear();
    QVERIFY(buffers.current().isNull());
    planes[0] = nullptr;
    void *scratchPicture = buffers.lock(planes);
    QVERIFY(scratchPicture != nullptr);
    QVERIFY(planes[0] != nullptr);
    buffers.unlock();
    QVERIFY(!buffers.display(scratchPicture));
    QVERIFY(buffers.current().isNull());
}

QTEST_GUILESS_MAIN(VlcFrameBuffersTest)
#include "tst_vlc_frame_buffers.moc"
