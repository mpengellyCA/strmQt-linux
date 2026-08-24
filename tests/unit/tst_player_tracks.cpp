// Track-surface tests for the PlayerBackend seam (DESIGN §1.6).
//
// The mpv half is exercised through mpvtracks::buildTracks, which is a pure
// function over a `track-list` already converted to QVariant — so a synthetic
// node stands in for a real one and no mpv core is created. What genuinely
// cannot be covered here is the wiring between them: that mpv observes
// track-list, that `aid`/`sid` round-trip through mpv's own parser, and that
// `demuxer-cache-time` reports what we think it does all need a live decode
// session with a real stream and are verified by hand (ARCHITECTURE.md), not in CI.

#include "FakePlayerBackend.h"
#include "playback/mpv/MpvPlayer.h"

#include <QSignalSpy>
#include <QTest>

using namespace strmqt;

namespace {

// One raw entry as mpv's track-list would hand it over, minus the keys the
// caller wants to vary. Only the fields buildTracks reads are present, which is
// deliberate: an entry missing everything must still produce a usable label.
QVariantMap rawTrack(int id, const QString &type)
{
    QVariantMap track;
    track[QStringLiteral("id")] = id;
    track[QStringLiteral("type")] = type;
    return track;
}

// A backend that implements nothing beyond the pure virtuals, to pin the base
// class defaults that VlcPlayer and any future engine inherit.
class BareBackend : public PlayerBackend
{
public:
    QString engineName() const override { return QStringLiteral("bare"); }
    void load(const QUrl &, qint64, LoadId, bool = false) override {}
    void setPaused(bool) override {}
    void stop() override {}
    void seekTo(qint64) override {}
    void setVolume(int) override {}
    State state() const override { return State::Idle; }
    qint64 positionMs() const override { return 0; }
    qint64 durationMs() const override { return 0; }
};

} // namespace

class TestPlayerTracks : public QObject
{
    Q_OBJECT

private slots:
    void buildsAudioTracksFromTrackList();
    void filtersByTrackType();
    void labelPrefersTitle();
    void labelFallsBackToLanguageCodecLayout();
    void labelFallsBackToOrdinal();
    void selectedIdReportsSelection();
    void selectedIdIsMinusOneWhenNothingSelected();
    void bareBackendDefaults();
    void fakeBackendSelectsTrack();
    void fakeBackendSubtitlesOff();
    void fakeBackendStatsAndScrubberFields();
};

void TestPlayerTracks::buildsAudioTracksFromTrackList()
{
    QVariantMap raw = rawTrack(2, QStringLiteral("audio"));
    raw[QStringLiteral("title")] = QStringLiteral("Commentary");
    raw[QStringLiteral("lang")] = QStringLiteral("eng");
    raw[QStringLiteral("codec")] = QStringLiteral("eac3");
    raw[QStringLiteral("demux-channel-count")] = 6;
    raw[QStringLiteral("demux-channels")] = QStringLiteral("5.1(side)");
    raw[QStringLiteral("default")] = true;
    raw[QStringLiteral("forced")] = false;
    raw[QStringLiteral("external")] = false;
    raw[QStringLiteral("selected")] = true;

    const QVariantList tracks =
        mpvtracks::buildTracks(QVariantList{raw}, QStringLiteral("audio"));
    QCOMPARE(tracks.size(), 1);

    const QVariantMap track = tracks.first().toMap();
    // Every key in the contract is present, so a QML delegate can bind blind.
    QCOMPARE(track.value(QStringLiteral("id")).toInt(), 2);
    QCOMPARE(track.value(QStringLiteral("title")).toString(), QStringLiteral("Commentary"));
    QCOMPARE(track.value(QStringLiteral("language")).toString(), QStringLiteral("eng"));
    QCOMPARE(track.value(QStringLiteral("codec")).toString(), QStringLiteral("eac3"));
    QCOMPARE(track.value(QStringLiteral("channels")).toInt(), 6);
    QCOMPARE(track.value(QStringLiteral("channelLayout")).toString(),
             QStringLiteral("5.1(side)"));
    QCOMPARE(track.value(QStringLiteral("isDefault")).toBool(), true);
    QCOMPARE(track.value(QStringLiteral("isForced")).toBool(), false);
    QCOMPARE(track.value(QStringLiteral("isExternal")).toBool(), false);
    QCOMPARE(track.value(QStringLiteral("selected")).toBool(), true);
}

void TestPlayerTracks::filtersByTrackType()
{
    QVariantMap external = rawTrack(1, QStringLiteral("sub"));
    external[QStringLiteral("external")] = true;
    external[QStringLiteral("forced")] = true;

    const QVariantList list{rawTrack(1, QStringLiteral("video")),
                            rawTrack(1, QStringLiteral("audio")),
                            rawTrack(2, QStringLiteral("audio")), external};

    QCOMPARE(mpvtracks::buildTracks(list, QStringLiteral("video")).size(), 1);
    QCOMPARE(mpvtracks::buildTracks(list, QStringLiteral("audio")).size(), 2);

    const QVariantList subtitles = mpvtracks::buildTracks(list, QStringLiteral("sub"));
    QCOMPARE(subtitles.size(), 1);
    QCOMPARE(subtitles.first().toMap().value(QStringLiteral("isExternal")).toBool(), true);
    QCOMPARE(subtitles.first().toMap().value(QStringLiteral("isForced")).toBool(), true);
}

void TestPlayerTracks::labelPrefersTitle()
{
    QVariantMap raw = rawTrack(1, QStringLiteral("audio"));
    raw[QStringLiteral("title")] = QStringLiteral("Director's Cut");
    raw[QStringLiteral("lang")] = QStringLiteral("eng");
    raw[QStringLiteral("codec")] = QStringLiteral("aac");
    QCOMPARE(mpvtracks::labelFor(raw, 1), QStringLiteral("Director's Cut"));

    // Whitespace-only titles do exist in the wild; they must not win.
    raw[QStringLiteral("title")] = QStringLiteral("   ");
    QCOMPARE(mpvtracks::labelFor(raw, 1), QStringLiteral("eng · aac"));
}

void TestPlayerTracks::labelFallsBackToLanguageCodecLayout()
{
    QVariantMap raw = rawTrack(1, QStringLiteral("audio"));
    raw[QStringLiteral("lang")] = QStringLiteral("jpn");
    raw[QStringLiteral("codec")] = QStringLiteral("flac");
    raw[QStringLiteral("demux-channels")] = QStringLiteral("stereo");
    QCOMPARE(mpvtracks::labelFor(raw, 3), QStringLiteral("jpn · flac · stereo"));

    // Any subset still reads sensibly — no dangling separators.
    raw.remove(QStringLiteral("lang"));
    QCOMPARE(mpvtracks::labelFor(raw, 3), QStringLiteral("flac · stereo"));
    raw.remove(QStringLiteral("demux-channels"));
    QCOMPARE(mpvtracks::labelFor(raw, 3), QStringLiteral("flac"));

    // codec-desc covers the containers that only carry a descriptive name.
    QVariantMap described = rawTrack(1, QStringLiteral("sub"));
    described[QStringLiteral("codec-desc")] = QStringLiteral("SubRip");
    QCOMPARE(mpvtracks::labelFor(described, 1), QStringLiteral("SubRip"));
}

void TestPlayerTracks::labelFallsBackToOrdinal()
{
    // Nothing identifying at all: the ordinal is 1-based within its own type,
    // so the second audio stream is "Track 2" even if its mpv id is 7.
    const QVariantList list{rawTrack(4, QStringLiteral("audio")),
                            rawTrack(7, QStringLiteral("audio"))};
    const QVariantList tracks = mpvtracks::buildTracks(list, QStringLiteral("audio"));
    QCOMPARE(tracks.at(0).toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Track 1"));
    QCOMPARE(tracks.at(1).toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Track 2"));
    QCOMPARE(tracks.at(1).toMap().value(QStringLiteral("id")).toInt(), 7);
}

void TestPlayerTracks::selectedIdReportsSelection()
{
    QVariantMap selected = rawTrack(5, QStringLiteral("sub"));
    selected[QStringLiteral("selected")] = true;
    const QVariantList tracks = mpvtracks::buildTracks(
        QVariantList{rawTrack(3, QStringLiteral("sub")), selected}, QStringLiteral("sub"));
    QCOMPARE(mpvtracks::selectedId(tracks), 5);
}

void TestPlayerTracks::selectedIdIsMinusOneWhenNothingSelected()
{
    // mpv's `sid=no` shows up as a list where no entry is selected; the contract
    // spells that -1, in both directions.
    const QVariantList tracks = mpvtracks::buildTracks(
        QVariantList{rawTrack(1, QStringLiteral("sub"))}, QStringLiteral("sub"));
    QCOMPARE(mpvtracks::selectedId(tracks), -1);
    QCOMPARE(mpvtracks::selectedId({}), -1);
}

void TestPlayerTracks::bareBackendDefaults()
{
    BareBackend backend;
    QVERIFY(backend.audioTracks().isEmpty());
    QVERIFY(backend.subtitleTracks().isEmpty());
    QCOMPARE(backend.currentAudioTrackId(), -1);
    QCOMPARE(backend.currentSubtitleTrackId(), -1);
    QCOMPARE(backend.bufferedMs(), 0);
    QCOMPARE(backend.playbackSpeed(), 1.0);
    QCOMPARE(backend.audioDelayMs(), 0);
    QCOMPARE(backend.subtitleDelayMs(), 0);
    QVERIFY(backend.videoStats().isEmpty());
    // The mutators must be harmless no-ops, not crashes: VlcPlayer inherits them.
    backend.setAudioTrack(3);
    backend.setSubtitleTrack(-1);
    backend.setPlaybackSpeed(2.0);
    backend.setAudioDelayMs(120);
    backend.setSubtitleDelayMs(-80);
    QVERIFY(!backend.screenshotToFile(QStringLiteral("/dev/null")));
    QCOMPARE(backend.currentAudioTrackId(), -1);
    QCOMPARE(backend.playbackSpeed(), 1.0);
}

void TestPlayerTracks::fakeBackendSelectsTrack()
{
    FakePlayerBackend backend;
    QSignalSpy spy(&backend, &PlayerBackend::tracksChanged);

    backend.simulateTracks({FakePlayerBackend::makeTrack(1, QStringLiteral("English"),
                                                         QStringLiteral("eng"),
                                                         QStringLiteral("aac"), true),
                            FakePlayerBackend::makeTrack(2, QStringLiteral("Commentary"),
                                                         QStringLiteral("eng"),
                                                         QStringLiteral("eac3"))},
                           {});
    QCOMPARE(spy.count(), 1);
    QCOMPARE(backend.audioTracks().size(), 2);
    QCOMPARE(backend.currentAudioTrackId(), 1);

    backend.setAudioTrack(2);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(backend.currentAudioTrackId(), 2);
    QCOMPARE(backend.audioTrackRequests, QList<int>{2});
    // The selection moved inside the list, not just in the scalar.
    QCOMPARE(backend.audioTracks().at(0).toMap().value(QStringLiteral("selected")).toBool(),
             false);
    QCOMPARE(backend.audioTracks().at(1).toMap().value(QStringLiteral("selected")).toBool(),
             true);
}

void TestPlayerTracks::fakeBackendSubtitlesOff()
{
    FakePlayerBackend backend;
    backend.simulateTracks(
        {}, {FakePlayerBackend::makeTrack(1, QStringLiteral("Forced"), QStringLiteral("eng"),
                                          QStringLiteral("subrip"), true)});
    QCOMPARE(backend.currentSubtitleTrackId(), 1);

    QSignalSpy spy(&backend, &PlayerBackend::tracksChanged);
    backend.setSubtitleTrack(-1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(backend.currentSubtitleTrackId(), -1);
    QCOMPARE(backend.subtitleTracks().at(0).toMap().value(QStringLiteral("selected")).toBool(),
             false);

    // An id that is not in the list must not fake a selection.
    backend.setSubtitleTrack(99);
    QCOMPARE(backend.currentSubtitleTrackId(), -1);
}

void TestPlayerTracks::fakeBackendStatsAndScrubberFields()
{
    FakePlayerBackend backend;

    QSignalSpy buffered(&backend, &PlayerBackend::bufferedMsChanged);
    backend.simulateBufferedMs(12'000);
    QCOMPARE(buffered.count(), 1);
    QCOMPARE(backend.bufferedMs(), 12'000);

    QSignalSpy speed(&backend, &PlayerBackend::playbackSpeedChanged);
    backend.setPlaybackSpeed(1.5);
    QCOMPARE(speed.count(), 1);
    QCOMPARE(backend.playbackSpeed(), 1.5);

    QSignalSpy audioDelay(&backend, &PlayerBackend::audioDelayChanged);
    backend.setAudioDelayMs(-250);
    QCOMPARE(audioDelay.count(), 1);
    QCOMPARE(backend.audioDelayMs(), -250);

    QSignalSpy subtitleDelay(&backend, &PlayerBackend::subtitleDelayChanged);
    backend.setSubtitleDelayMs(400);
    QCOMPARE(subtitleDelay.count(), 1);
    QCOMPARE(backend.subtitleDelayMs(), 400);

    QSignalSpy stats(&backend, &PlayerBackend::videoStatsChanged);
    QVariantMap videoStats;
    videoStats[QStringLiteral("width")] = 1920;
    videoStats[QStringLiteral("height")] = 1080;
    videoStats[QStringLiteral("hwdec")] = QStringLiteral("vaapi");
    backend.simulateVideoStats(videoStats);
    QCOMPARE(stats.count(), 1);
    QCOMPARE(backend.videoStats().value(QStringLiteral("hwdec")).toString(),
             QStringLiteral("vaapi"));
    // Keys the engine cannot answer stay absent rather than reading as zero.
    QVERIFY(!backend.videoStats().contains(QStringLiteral("droppedFrames")));

    QVERIFY(backend.screenshotToFile(QStringLiteral("/tmp/shot.png")));
    QCOMPARE(backend.screenshots, QStringList{QStringLiteral("/tmp/shot.png")});
}

QTEST_MAIN(TestPlayerTracks)
#include "tst_player_tracks.moc"
