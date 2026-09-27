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

// The docked audio bar (Crate spec §7.1). Pins the audio-only branches so the
// video bar provably keeps its own.
class MiniPlayerSourcesTest : public QObject
{
    Q_OBJECT

private slots:
    void audioBarIsTheCrateBar();
    void videoBarIsUnchanged();
    void audioBarMutes();
};

void MiniPlayerSourcesTest::audioBarIsTheCrateBar()
{
    const QByteArray mini = sourceFor(QStringLiteral("src/ui/shell/MiniPlayer.qml"));
    QVERIFY(!mini.isEmpty());
    QVERIFY(mini.contains("readonly property int audioCoverSize: Theme.scale(72)"));
    QVERIFY(mini.contains("readonly property int playheadHeight: Theme.scale(2)"));
    QVERIFY(mini.contains("mini.isAudio ? mini.playheadHeight + mini.audioCoverSize"));
    QVERIFY(mini.contains("id: playhead"));
    QVERIFY(mini.contains("id: recordSlice"));
    QVERIFY(mini.contains("mini.isAudio && NowPlayingMusicCtl.recordState === \"playing\""));
    QVERIFY(mini.contains("id: audioStopButton"));
    QVERIFY(mini.contains("text: mini.isAudio ? NowPlayingMusicCtl.timeText : mini.timeText"));
    // Shuffle leads the transport row.
    QVERIFY(mini.indexOf("id: shuffleButton") < mini.indexOf("id: prevButton"));
    // The title opens the record in audio mode.
    QVERIFY(mini.contains("if (mini.isAudio && mini.albumId.length > 0)"));
}

void MiniPlayerSourcesTest::videoBarIsUnchanged()
{
    const QByteArray mini = sourceFor(QStringLiteral("src/ui/shell/MiniPlayer.qml"));
    QVERIFY(mini.contains("1 + mini.scrubberHeight + mini.contentHeight"));
    QVERIFY(mini.contains("id: scrubber"));
    QVERIFY(mini.contains("visible: !mini.isAudio"));
    QVERIFY(mini.contains("Actions.openArtist(mini.artistId, mini.artistText)"));
    QVERIFY(mini.contains("Actions.openAlbum(mini.albumId, mini.albumText)"));
}

void MiniPlayerSourcesTest::audioBarMutes()
{
    const QByteArray mini = sourceFor(QStringLiteral("src/ui/shell/MiniPlayer.qml"));
    const qsizetype at = mini.indexOf("id: muteButton");
    QVERIFY(at >= 0);
    const QByteArray button = mini.mid(at, mini.indexOf("StrmIconButton {", at) - at);
    QVERIFY(button.contains("visible: mini.isAudio"));
    QVERIFY(button.contains("onClicked: PlayerCtl.toggleMute()"));
    // The muted state shows: the checked fill and the struck-out speaker.
    QVERIFY(button.contains("checked: PlayerCtl.muted === true"));
    QVERIFY(button.contains("\"volume-mute\""));
    QVERIFY(button.contains("onActiveFocusChanged: mini.rescueFocusFrom(muteButton)"));
    // In the chain between ♡ and the queue, both ways.
    QVERIFY(button.contains("KeyNavigation.left: favoriteButton"));
    QVERIFY(button.contains("KeyNavigation.right: queueButton"));
    QVERIFY(mini.contains("KeyNavigation.right: muteButton"));
    QVERIFY(mini.contains("KeyNavigation.left: muteButton"));
}

QTEST_GUILESS_MAIN(MiniPlayerSourcesTest)
#include "tst_mini_player_sources.moc"
