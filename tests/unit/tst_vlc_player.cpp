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
};

namespace {

// Tiny video-only clip: no audio track, so the test does not depend on a sound
// device. mpeg4-in-avi uses ffmpeg's built-in encoder — no libx264 requirement.
QString generateClip(QTemporaryDir &dir)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return {};
    const QString path = dir.filePath(QStringLiteral("clip.avi"));
    QProcess process;
    process.start(ffmpeg,
                  {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                   QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                   QStringLiteral("testsrc=duration=2:size=64x64:rate=10"),
                   QStringLiteral("-c:v"), QStringLiteral("mpeg4"), path});
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

QTEST_GUILESS_MAIN(VlcPlayerTest)
#include "tst_vlc_player.moc"
