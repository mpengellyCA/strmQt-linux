#include <QFile>
#include <QTest>

namespace {

QByteArray sourceFor(const QString &relative)
{
    QFile file(QStringLiteral(STRMQT_SOURCE_DIR "/") + relative);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

// The wiring between the player page, the Crate view and the controller.
// Behaviour is covered by tst_record_stage and tst_now_playing_music; these
// rows pin the bindings that connect them, which no C++ test can reach.
class MusicPlayerSourcesTest : public QObject
{
    Q_OBJECT

private slots:
    void playerPageUsesTheCrateView();
    void nowPlayingViewDrivesTheRecord();
    void nowPlayingPanelIsGone();
};

void MusicPlayerSourcesTest::playerPageUsesTheCrateView()
{
    const QByteArray page = sourceFor(QStringLiteral("src/ui/pages/PlayerPage.qml"));
    QVERIFY(!page.isEmpty());
    QVERIFY(page.contains("MusicNowPlaying {"));
    QVERIFY(page.contains("live: page.visible && page.audioMode"));
    QVERIFY(page.contains("return nowPlaying.sleeveRect(target);"));
    QVERIFY(page.contains("readonly property real sleeveRadius: nowPlaying.sleeveRadius"));
    QVERIFY(page.contains("nowPlaying.focusTransport();"));
    QVERIFY(!page.contains("NowPlayingPanel"));
}

void MusicPlayerSourcesTest::nowPlayingViewDrivesTheRecord()
{
    const QByteArray view = sourceFor(QStringLiteral("src/ui/music/MusicNowPlaying.qml"));
    QVERIFY(!view.isEmpty());
    QVERIFY(view.contains("recordState: NowPlayingMusicCtl.recordState"));
    QVERIFY(view.contains("coverUrl: NowPlayingMusicCtl.coverUrl"));
    QVERIFY(view.contains("holdIn: view.sleeveInFlight"));
    QVERIFY(view.contains("function onAlbumChanging()"));
    QVERIFY(view.contains("record.changeAlbum(NowPlayingMusicCtl.coverUrl)"));
    QVERIFY(view.contains("Prefs.animateRecord"));
    QVERIFY(view.contains("MusicPlayerPanel {"));
    QVERIFY(view.contains("MusicPlay.radio(NowPlayingMusicCtl.trackId, NowPlayingMusicCtl.title)"));
    // QML states intent; it does not format durations or badges itself.
    QVERIFY(view.contains("text: NowPlayingMusicCtl.timeText"));
    QVERIFY(!view.contains("formatTime("));
}

void MusicPlayerSourcesTest::nowPlayingPanelIsGone()
{
    QVERIFY(!QFile::exists(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/player/NowPlayingPanel.qml")));
    QVERIFY(!sourceFor(QStringLiteral("src/ui/player/Player.cmake")).contains("NowPlayingPanel"));
    QVERIFY(!sourceFor(QStringLiteral("src/ui/shell/SleeveFlight.qml")).contains("NowPlayingPanel"));
}

QTEST_GUILESS_MAIN(MusicPlayerSourcesTest)
#include "tst_music_player_sources.moc"
