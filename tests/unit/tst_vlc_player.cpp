#include <QtTest>

#include "playback/vlc/VlcPlayer.h"

#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace strmqt;

// Lifecycle smoke test for the real libvlc engine (SOL-12). The vmem buffer and
// notification-gate layers have decoupled unit coverage in tst_vlc_frame_buffers,
// but nothing instantiated VlcPlayer itself, so the load → decode → reload →
// stop → destroy path the review flagged had no automated coverage. Media is
// ffmpeg-generated at test time (AGENTS.md); the whole test is only registered
// when libvlc was found at configure time.
class VlcPlayerTest : public QObject
{
    Q_OBJECT

private slots:
    void loadPlayReloadStopDestroy();
    void resumedLoadOpensAtTheResumePoint();
    void selectedExternalSubtitleIsDrawn();
};

namespace {

// Tiny video-only clip: no audio track, so the test does not depend on a sound
// device. mpeg4-in-avi uses ffmpeg's built-in encoder — no libx264 requirement.
// A keyframe every second, so a seek has somewhere exact to land.
QString generateClip(QTemporaryDir &dir,
                     const QString &source = QStringLiteral("testsrc=duration=2:size=64x64:rate=10"))
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return {};
    const QString path = dir.filePath(QStringLiteral("clip.avi"));
    QProcess process;
    process.start(ffmpeg,
                  {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                   QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                   source, QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-g"),
                   QStringLiteral("10"), path});
    if (!process.waitForFinished(15000) || process.exitCode() != 0)
        return {};
    return path;
}

} // namespace

void VlcPlayerTest::loadPlayReloadStopDestroy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString clip = generateClip(dir);
    if (clip.isEmpty())
        QSKIP("ffmpeg is required to generate smoke-test media");

    auto *player = new VlcPlayer;
    QSignalSpy frameSpy(player, &VlcPlayer::frameReady);
    QSignalSpy errorSpy(player, &PlayerBackend::errorOccurred);

    const QUrl url = QUrl::fromLocalFile(clip);

    player->load(url, 0, 1);
    QCOMPARE(player->state(), PlayerBackend::State::Loading);
    // Evidence the full pipeline ran: a Playing state plus a real decoded frame
    // delivered through the vmem callbacks.
    QTRY_VERIFY_WITH_TIMEOUT(player->state() == PlayerBackend::State::Playing &&
                                 !player->currentFrame().isNull(),
                             15000);
    QVERIFY(errorSpy.isEmpty());

    // Reload over live playback: load() stops the outgoing decoder and clears
    // the frame buffers synchronously, so currentFrame() is null again here and
    // only a fresh display from the NEW load can repopulate it — the retired-
    // callback guard must not let stale pictures publish.
    player->load(url, 0, 2);
    QVERIFY(player->currentFrame().isNull());
    QTRY_VERIFY_WITH_TIMEOUT(player->state() == PlayerBackend::State::Playing &&
                                 !player->currentFrame().isNull(),
                             15000);
    QVERIFY(frameSpy.count() > 0);
    QVERIFY(errorSpy.isEmpty());

    player->stop();
    QCOMPARE(player->state(), PlayerBackend::State::Idle);
    QVERIFY(player->currentFrame().isNull());

    // Destroy while libvlc's event thread may still hold queued handleEvent /
    // frameReady invocations; Qt must retire them with the object, not crash.
    delete player;
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
}

// A resume that is not paused used to open the file at 0:00 and seek only once
// libvlc reported Playing, so the opening of the film was decoded and shown
// before the jump. The input has to open at the resume point whether or not
// the load starts paused. The clip is black for its first half and white for
// its second, so a single dark picture is the opening having been shown.
void VlcPlayerTest::resumedLoadOpensAtTheResumePoint()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString clip = generateClip(
        dir, QStringLiteral("color=black:duration=15:size=64x64:rate=10[a];"
                            "color=white:duration=15:size=64x64:rate=10[b];[a][b]concat"));
    if (clip.isEmpty())
        QSKIP("ffmpeg is required to generate smoke-test media");

    VlcPlayer player;
    QSignalSpy errorSpy(&player, &PlayerBackend::errorOccurred);
    int pictures = 0;
    int darkPictures = 0;
    connect(&player, &VlcPlayer::frameReady, &player, [&] {
        const QImage frame = player.currentFrame();
        if (frame.isNull())
            return;
        ++pictures;
        if (frame.pixelColor(frame.width() / 2, frame.height() / 2).lightness() < 128)
            ++darkPictures;
    });

    constexpr qint64 kResumeMs = 20'000;
    player.load(QUrl::fromLocalFile(clip), kResumeMs, 1);
    // The requested position is published at once, not a synthetic zero.
    QCOMPARE(player.positionMs(), kResumeMs);
    QTRY_VERIFY_WITH_TIMEOUT(player.state() == PlayerBackend::State::Playing && pictures >= 5,
                             15000);
    QVERIFY(errorSpy.isEmpty());
    QCOMPARE(darkPictures, 0);
    QVERIFY2(player.positionMs() >= kResumeMs - 2000,
             qPrintable(QString::number(player.positionMs())));
    player.stop();

    // Paused, the picture shown is still the resume point, although the input
    // stops there and reports no time.
    pictures = 0;
    darkPictures = 0;
    player.load(QUrl::fromLocalFile(clip), kResumeMs, 2, true);
    QTRY_VERIFY_WITH_TIMEOUT(player.state() == PlayerBackend::State::Paused && pictures >= 1,
                             15000);
    QTest::qWait(300);
    QVERIFY(errorSpy.isEmpty());
    QCOMPARE(darkPictures, 0);
    QCOMPARE(player.state(), PlayerBackend::State::Paused);
    const QImage shown = player.currentFrame();
    QVERIFY(!shown.isNull());
    QVERIFY(shown.pixelColor(shown.width() / 2, shown.height() / 2).lightness() >= 128);
    player.stop();
}

// PlayerController hands this engine the subtitle the server delivers outside
// the stream. libVLC has no track list here to look it up in, so the evidence
// is the picture: the clip is black from start to end, and the only thing that
// can put a light pixel into a frame is subtitle text drawn over it.
void VlcPlayerTest::selectedExternalSubtitleIsDrawn()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString clip =
        generateClip(dir, QStringLiteral("color=black:duration=60:size=320x180:rate=10"));
    if (clip.isEmpty())
        QSKIP("ffmpeg is required to generate smoke-test media");
    // Named unlike the clip, so libVLC's own sidecar detection cannot find it.
    const QString subtitle = dir.filePath(QStringLiteral("sidecar-en.srt"));
    {
        QFile file(subtitle);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("1\n00:00:00,000 --> 00:00:59,000\nMMMMMMMMMMMM\nMMMMMMMMMMMM\n");
    }

    VlcPlayer player;
    QSignalSpy errorSpy(&player, &PlayerBackend::errorOccurred);
    int pictures = 0;
    int litPictures = 0;
    connect(&player, &VlcPlayer::frameReady, &player, [&] {
        const QImage frame = player.currentFrame();
        if (frame.isNull())
            return;
        ++pictures;
        for (int y = 0; y < frame.height(); ++y) {
            for (int x = 0; x < frame.width(); ++x) {
                if (frame.pixelColor(x, y).lightness() > 160) {
                    ++litPictures;
                    return;
                }
            }
        }
    });

    // Before the input runs there is nothing to attach to: ignored.
    player.addExternalSubtitle(QUrl::fromLocalFile(subtitle), {}, {}, true);
    player.load(QUrl::fromLocalFile(clip), 0, 1);
    QTRY_VERIFY_WITH_TIMEOUT(player.state() == PlayerBackend::State::Playing && pictures >= 5,
                             15000);
    // Offered but not to be shown: this engine could never have it picked, so
    // it is not fetched, and the picture stays black.
    player.addExternalSubtitle(QUrl::fromLocalFile(subtitle), QStringLiteral("English"),
                               QStringLiteral("eng"), false);
    const int before = pictures;
    QTRY_VERIFY_WITH_TIMEOUT(pictures >= before + 10, 15000);
    QCOMPARE(litPictures, 0);

    player.addExternalSubtitle(QUrl::fromLocalFile(subtitle), QStringLiteral("English"),
                               QStringLiteral("eng"), true);
    QTRY_VERIFY2_WITH_TIMEOUT(litPictures > 0, "the attached subtitle never reached the picture",
                              20000);
    QVERIFY(errorSpy.isEmpty());
    QCOMPARE(player.state(), PlayerBackend::State::Playing);
    player.stop();
}

QTEST_GUILESS_MAIN(VlcPlayerTest)
#include "tst_vlc_player.moc"
