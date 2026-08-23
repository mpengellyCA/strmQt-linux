#include <QDBusObjectPath>
#include <QSignalSpy>
#include <QtTest>

#include "platform/MprisPlayer.h"

using namespace strmqt;

// MprisPlayer builds its Metadata map without ever touching the session bus —
// registerOnBus() is a separate, optional step — so the assembly is testable
// headless. What is NOT covered here is the D-Bus surface itself: the adaptors'
// property reads, the PropertiesChanged signal and the Next/Previous slots all
// need a registered connection, which CI does not have.
class MprisMetadataTest : public QObject
{
    Q_OBJECT

private slots:
    void inactivePublishesNothing();
    void fullTrackPublishesEveryKey();
    void sparseTrackOmitsUnknownKeys();
    void trackIdFollowsTheItem();
    void ratingIsClamped();
    void queueStateDrivesCanGoNextAndPrevious();
    void repeatedSetNowPlayingIsIdempotent();

private:
    static MprisPlayer::TrackInfo fullTrack();
};

MprisPlayer::TrackInfo MprisMetadataTest::fullTrack()
{
    MprisPlayer::TrackInfo track;
    track.itemId = QStringLiteral("abc123");
    track.title = QStringLiteral("Sing to the Moon");
    track.artists = {QStringLiteral("Laura Mvula"), QStringLiteral("Metropole Orkest")};
    track.album = QStringLiteral("Sing to the Moon");
    track.albumArtists = {QStringLiteral("Laura Mvula")};
    track.artUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/strmqt/mpris/abc123-tag.jpg"));
    track.durationMs = 245'000;
    track.trackNumber = 4;
    track.useCount = 7;
    track.userRating = 1.0;
    return track;
}

void MprisMetadataTest::inactivePublishesNothing()
{
    MprisPlayer mpris;
    mpris.setNowPlaying(fullTrack());
    // Stopped is stopped: a client that keeps drawing the last track's sleeve
    // after playback ended looks stuck, not helpful.
    QVERIFY(mpris.metadata().isEmpty());
    QCOMPARE(mpris.playbackStatus(), QStringLiteral("Stopped"));
}

void MprisMetadataTest::fullTrackPublishesEveryKey()
{
    MprisPlayer mpris;
    mpris.setPlaybackActive(true, false);
    mpris.setNowPlaying(fullTrack());

    const QVariantMap map = mpris.metadata();
    QCOMPARE(QStringList(map.keys()),
             QStringList({QStringLiteral("mpris:artUrl"), QStringLiteral("mpris:length"),
                          QStringLiteral("mpris:trackid"), QStringLiteral("xesam:album"),
                          QStringLiteral("xesam:albumArtist"), QStringLiteral("xesam:artist"),
                          QStringLiteral("xesam:title"), QStringLiteral("xesam:trackNumber"),
                          QStringLiteral("xesam:useCount"), QStringLiteral("xesam:userRating")}));

    // The D-Bus type of each value is the contract; a client reading by
    // signature drops anything typed differently, which looks exactly like the
    // key never having been published.
    QCOMPARE(map.value(QStringLiteral("mpris:trackid")).metaType().id(),
             qMetaTypeId<QDBusObjectPath>());
    QCOMPARE(map.value(QStringLiteral("mpris:length")).metaType().id(), QMetaType::LongLong);
    QCOMPARE(map.value(QStringLiteral("mpris:length")).toLongLong(), 245'000'000LL);
    QCOMPARE(map.value(QStringLiteral("mpris:artUrl")).metaType().id(), QMetaType::QString);
    QCOMPARE(map.value(QStringLiteral("mpris:artUrl")).toString(),
             QStringLiteral("file:///tmp/strmqt/mpris/abc123-tag.jpg"));
    QCOMPARE(map.value(QStringLiteral("xesam:title")).metaType().id(), QMetaType::QString);
    // Arrays, not strings: xesam declares both of these as `as`.
    QCOMPARE(map.value(QStringLiteral("xesam:artist")).metaType().id(), QMetaType::QStringList);
    QCOMPARE(map.value(QStringLiteral("xesam:artist")).toStringList(),
             QStringList({QStringLiteral("Laura Mvula"), QStringLiteral("Metropole Orkest")}));
    QCOMPARE(map.value(QStringLiteral("xesam:album")).metaType().id(), QMetaType::QString);
    QCOMPARE(map.value(QStringLiteral("xesam:albumArtist")).metaType().id(),
             QMetaType::QStringList);
    QCOMPARE(map.value(QStringLiteral("xesam:albumArtist")).toStringList(),
             QStringList({QStringLiteral("Laura Mvula")}));
    QCOMPARE(map.value(QStringLiteral("xesam:trackNumber")).metaType().id(), QMetaType::Int);
    QCOMPARE(map.value(QStringLiteral("xesam:trackNumber")).toInt(), 4);
    QCOMPARE(map.value(QStringLiteral("xesam:useCount")).metaType().id(), QMetaType::Int);
    QCOMPARE(map.value(QStringLiteral("xesam:useCount")).toInt(), 7);
    QCOMPARE(map.value(QStringLiteral("xesam:userRating")).metaType().id(), QMetaType::Double);
    QCOMPARE(map.value(QStringLiteral("xesam:userRating")).toDouble(), 1.0);
}

void MprisMetadataTest::sparseTrackOmitsUnknownKeys()
{
    MprisPlayer mpris;
    mpris.setPlaybackActive(true, false);

    // What a bare playItem() seeds the queue with: an id and a title, nothing else.
    MprisPlayer::TrackInfo track;
    track.itemId = QStringLiteral("only-an-id");
    track.title = QStringLiteral("Unknown Track");
    mpris.setNowPlaying(track);

    const QVariantMap map = mpris.metadata();
    // An empty string is a *visible* blank in a media applet, and 0.0 reads as
    // "rated zero stars" — so an unknown value is absent, never empty.
    QCOMPARE(QStringList(map.keys()),
             QStringList({QStringLiteral("mpris:trackid"), QStringLiteral("xesam:title")}));

    // Zero and negative sentinels are unknown, not values.
    track.durationMs = 0;
    track.trackNumber = 0;
    track.useCount = -1;
    track.userRating = -1.0;
    track.album = QString();
    track.artists = QStringList();
    track.albumArtists = QStringList();
    track.artUrl = QUrl();
    mpris.setNowPlaying(track);
    QCOMPARE(QStringList(mpris.metadata().keys()),
             QStringList({QStringLiteral("mpris:trackid"), QStringLiteral("xesam:title")}));

    // A genuine zero play count still publishes: 0 is a fact, -1 is the absence of one.
    track.useCount = 0;
    mpris.setNowPlaying(track);
    QVERIFY(mpris.metadata().contains(QStringLiteral("xesam:useCount")));
    QCOMPARE(mpris.metadata().value(QStringLiteral("xesam:useCount")).toInt(), 0);
}

void MprisMetadataTest::trackIdFollowsTheItem()
{
    MprisPlayer mpris;
    mpris.setPlaybackActive(true, false);

    const auto pathOf = [&mpris] {
        return mpris.metadata()
            .value(QStringLiteral("mpris:trackid"))
            .value<QDBusObjectPath>()
            .path();
    };

    MprisPlayer::TrackInfo track;
    track.itemId = QStringLiteral("a1b2c3");
    mpris.setNowPlaying(track);
    const QString first = pathOf();
    QCOMPARE(first, QStringLiteral("/ca/mikesdev/strmqt/track/a1b2c3"));

    // A different item must be a different path or clients treat a whole album
    // as one very long track.
    track.itemId = QStringLiteral("d4e5f6");
    mpris.setNowPlaying(track);
    QVERIFY(pathOf() != first);

    // Anything not legal in a path element is folded, because an invalid
    // QDBusObjectPath would take the entire Metadata property down with it.
    track.itemId = QStringLiteral("id/with.bad-chars");
    mpris.setNowPlaying(track);
    QCOMPARE(pathOf(), QStringLiteral("/ca/mikesdev/strmqt/track/id_with_bad_chars"));
    QVERIFY(!QDBusObjectPath(pathOf()).path().isEmpty());

    // No id at all still yields a valid path rather than a broken property.
    track.itemId.clear();
    mpris.setNowPlaying(track);
    QVERIFY(!QDBusObjectPath(pathOf()).path().isEmpty());
}

void MprisMetadataTest::ratingIsClamped()
{
    MprisPlayer mpris;
    mpris.setPlaybackActive(true, false);

    MprisPlayer::TrackInfo track;
    track.itemId = QStringLiteral("x");
    track.userRating = 4.2; // a caller that forgot to normalise a 0–10 rating
    mpris.setNowPlaying(track);
    QCOMPARE(mpris.metadata().value(QStringLiteral("xesam:userRating")).toDouble(), 1.0);

    track.userRating = 0.0;
    mpris.setNowPlaying(track);
    QCOMPARE(mpris.metadata().value(QStringLiteral("xesam:userRating")).toDouble(), 0.0);
}

void MprisMetadataTest::queueStateDrivesCanGoNextAndPrevious()
{
    MprisPlayer mpris;
    // The old hardcoded false is what made Plasma draw both buttons dead.
    QVERIFY(!mpris.canGoNext());
    QVERIFY(!mpris.canGoPrevious());

    mpris.setQueueState(true, false);
    QVERIFY(mpris.canGoNext());
    QVERIFY(!mpris.canGoPrevious());

    mpris.setQueueState(true, true);
    QVERIFY(mpris.canGoNext());
    QVERIFY(mpris.canGoPrevious());

    mpris.setQueueState(false, false);
    QVERIFY(!mpris.canGoNext());
    QVERIFY(!mpris.canGoPrevious());
}

void MprisMetadataTest::repeatedSetNowPlayingIsIdempotent()
{
    MprisPlayer mpris;
    mpris.setPlaybackActive(true, false);
    mpris.setNowPlaying(fullTrack());
    const QVariantMap before = mpris.metadata();

    // titleChanged and durationChanged both push the same rebuild; the second
    // one must not change what is published.
    mpris.setNowPlaying(fullTrack());
    QCOMPARE(mpris.metadata(), before);

    // Artwork lands separately and long after, so it has its own setter.
    const QUrl art = QUrl::fromLocalFile(QStringLiteral("/tmp/strmqt/mpris/late.png"));
    mpris.setArtUrl(art);
    QCOMPARE(mpris.metadata().value(QStringLiteral("mpris:artUrl")).toString(), art.toString());
    QCOMPARE(mpris.metadata().value(QStringLiteral("xesam:title")).toString(),
             before.value(QStringLiteral("xesam:title")).toString());
}

QTEST_GUILESS_MAIN(MprisMetadataTest)
#include "tst_mpris_metadata.moc"
