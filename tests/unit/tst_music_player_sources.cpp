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
    void panelListsLeaveLeftForTheStage();
    void nowPlayingKeepsTheKeyboardOnARing();
    void nowPlayingHasVolumeAndMute();
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

void MusicPlayerSourcesTest::panelListsLeaveLeftForTheStage()
{
    const QByteArray panel = sourceFor(QStringLiteral("src/ui/music/MusicPlayerPanel.qml"));
    QVERIFY(!panel.isEmpty());
    // The tab strip and all three lists under it.
    QCOMPARE(panel.count("panel.leftRequested();"), 4);
    for (const char *list : {"id: queueList", "id: albumList", "id: lyricList"}) {
        const qsizetype at = panel.indexOf(list);
        QVERIFY2(at >= 0, list);
        QVERIFY2(panel.indexOf("Keys.onLeftPressed", at) > at, list);
    }
}

void MusicPlayerSourcesTest::nowPlayingKeepsTheKeyboardOnARing()
{
    const QByteArray view = sourceFor(QStringLiteral("src/ui/music/MusicNowPlaying.qml"));
    QVERIFY(view.contains("function rescueFocusFrom(control: Item): void"));
    for (const char *id : {"shuffleButton", "prevButton", "nextButton", "repeatButton",
                           "favouriteButton", "addButton"}) {
        const QByteArray row = QByteArray("onActiveFocusChanged: view.rescueFocusFrom(") + id + ")";
        QVERIFY2(view.contains(row), id);
    }
    QVERIFY(view.contains("view.rescueFocusFrom(scrubber);"));
    // Up off the transport stays on it rather than reaching the ringless page;
    // Down off the bottom row likewise.
    QVERIFY(view.contains("Keys.onUpPressed: event => event.accepted = true"));
    QVERIFY(view.contains("Keys.onDownPressed: event => event.accepted = true"));

    const QByteArray page = sourceFor(QStringLiteral("src/ui/pages/PlayerPage.qml"));
    QVERIFY(page.contains("function settleAudioFocus(): void"));
    QVERIFY(page.contains("page.Window.activeFocusItem === page"));
    QVERIFY(page.contains("Window.onActiveFocusItemChanged: Qt.callLater(page.settleAudioFocus)"));
}

void MusicPlayerSourcesTest::nowPlayingHasVolumeAndMute()
{
    const QByteArray view = sourceFor(QStringLiteral("src/ui/music/MusicNowPlaying.qml"));
    QVERIFY(view.contains("id: muteButton"));
    QVERIFY(view.contains("onClicked: PlayerCtl.toggleMute()"));
    QVERIFY(view.contains("checked: PlayerCtl.muted === true"));
    QVERIFY(view.contains("iconName: view.volumeIcon"));
    QVERIFY(view.contains("? \"volume-mute\""));
    QVERIFY(view.contains("id: volumeSlider"));
    QVERIFY(view.contains("to: PlayerCtl.maxVolume"));
    QVERIFY(view.contains("value: PlayerCtl.muted === true ? 0 : PlayerCtl.volume"));
    QVERIFY(view.contains("PlayerCtl.setVolume(Math.round(value))"));
    // Reachable: scrubber → mute → level, and back up.
    QVERIFY(view.contains("KeyNavigation.down: muteButton"));
    QVERIFY(view.contains("KeyNavigation.right: volumeSlider"));
    QVERIFY(view.contains("KeyNavigation.up: scrubber"));
}

QTEST_GUILESS_MAIN(MusicPlayerSourcesTest)
#include "tst_music_player_sources.moc"
