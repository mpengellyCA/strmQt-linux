#include <QtTest>

#include "playback/vlc/VlcPlayer.h"

#include <atomic>
#include <thread>
#include <vector>

using namespace strmqt;

class VlcFrameBuffersTest : public QObject
{
    Q_OBJECT

private slots:
    void swapsPreallocatedFramesWithoutPublishingScratch();
    void rejectsUnsafeLayoutsAndAlignsPlanes();
    void notificationsCoalesceAcrossThreads();
};

void VlcFrameBuffersTest::swapsPreallocatedFramesWithoutPublishingScratch()
{
    vlcframes::Buffers buffers;
    unsigned pitch = 0;
    QVERIFY(buffers.configure(64, 36, &pitch));
    QCOMPARE(pitch % vlcframes::Buffers::kPlaneAlignment, 0U);

    void *planes[1] = {};
    void *firstPicture = buffers.lock(planes);
    QVERIFY(firstPicture != nullptr);
    QVERIFY(planes[0] != nullptr);
    QCOMPARE(reinterpret_cast<quintptr>(planes[0]) % vlcframes::Buffers::kPlaneAlignment,
             quintptr(0));
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
    QVERIFY(buffers.configure(64, 36));
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

void VlcFrameBuffersTest::rejectsUnsafeLayoutsAndAlignsPlanes()
{
    vlcframes::Buffers buffers;
    QVERIFY(!buffers.configure(0, 36));
    QVERIFY(!buffers.configure(vlcframes::Buffers::kMaxFrameDimension + 1, 1));
    QVERIFY(!buffers.configure(4097, 4097));

    QVERIFY(buffers.configure(65, 36));
    void *planes[1] = {};
    void *firstPicture = buffers.lock(planes);
    QVERIFY(firstPicture != nullptr);
    QCOMPARE(reinterpret_cast<quintptr>(planes[0]) % vlcframes::Buffers::kPlaneAlignment,
             quintptr(0));
    auto *pixels = static_cast<QRgb *>(planes[0]);
    pixels[0] = qRgb(1, 2, 3);
    buffers.unlock();
    QVERIFY(buffers.display(firstPicture));
    const QImage firstSnapshot = buffers.current();

    // Rotate the first frame into the next back slot while its GUI snapshot is
    // still alive, then ensure the following writer gets fresh aligned storage
    // instead of detaching with a full-frame copy or overwriting the snapshot.
    void *secondPicture = buffers.lock(planes);
    buffers.unlock();
    QVERIFY(buffers.display(secondPicture));
    void *thirdPicture = buffers.lock(planes);
    QVERIFY(thirdPicture != nullptr);
    QVERIFY(planes[0] != firstSnapshot.constBits());
    QCOMPARE(reinterpret_cast<quintptr>(planes[0]) % vlcframes::Buffers::kPlaneAlignment,
             quintptr(0));
    buffers.unlock();
    QVERIFY(buffers.display(thirdPicture));
    QCOMPARE(firstSnapshot.pixel(0, 0), qRgb(1, 2, 3));
}

void VlcFrameBuffersTest::notificationsCoalesceAcrossThreads()
{
    vlcframes::NotificationGate gate;
    std::atomic_int accepted = 0;
    std::vector<std::jthread> contenders;
    for (int i = 0; i < 8; ++i) {
        contenders.emplace_back([&gate, &accepted] {
            if (gate.request())
                ++accepted;
        });
    }
    contenders.clear(); // jthread joins on destruction
    QCOMPARE(accepted.load(), 1);
    QVERIFY(gate.pending());
    QVERIFY(!gate.request());
    gate.release();
    QVERIFY(gate.request());
}

QTEST_GUILESS_MAIN(VlcFrameBuffersTest)
#include "tst_vlc_frame_buffers.moc"
