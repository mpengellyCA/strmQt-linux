#include <QtTest>

#include "server/dto/music/MusicMediaItem.h"
#include "server/dto/music/MusicQuery.h"
#include "server/dto/music/MusicTypes.h"

using namespace strmqt;
using namespace strmqt::music;

class MusicMapperTest : public QObject
{
    Q_OBJECT

private slots:
    void trackConvertsToAudioMediaItem();
    void trackCoverRoundTripsThroughCoverSource();
    void albumAndArtistConvert();
    void queryEqualityAndFilters();
};

void MusicMapperTest::trackConvertsToAudioMediaItem()
{
    Track track;
    track.id = QStringLiteral("t1");
    track.title = QStringLiteral("Time");
    track.artists = {{QStringLiteral("ar1"), QStringLiteral("Pink Floyd")},
                     {QStringLiteral("ar2"), QStringLiteral("Clare Torry")}};
    track.albumArtists = {{QStringLiteral("ar1"), QStringLiteral("Pink Floyd")}};
    track.albumId = QStringLiteral("al1");
    track.albumTitle = QStringLiteral("The Dark Side of the Moon");
    track.discNumber = 1;
    track.trackNumber = 4;
    track.runtimeMs = 413'000;
    track.positionMs = 1'000;
    track.playCount = 3;
    track.played = true;
    track.favourite = true;
    track.playlistItemId = QStringLiteral("pl-entry-9");

    const MediaItem item = toMediaItem(track);
    QCOMPARE(item.id, QStringLiteral("t1"));
    QCOMPARE(item.type, QStringLiteral("Audio"));
    QCOMPARE(item.name, QStringLiteral("Time"));
    QCOMPARE(item.artists, QStringList({QStringLiteral("Pink Floyd"), QStringLiteral("Clare Torry")}));
    QCOMPARE(item.artistIds, QStringList({QStringLiteral("ar1"), QStringLiteral("ar2")}));
    QCOMPARE(item.albumArtist, QStringLiteral("Pink Floyd"));
    QCOMPARE(item.album, QStringLiteral("The Dark Side of the Moon"));
    QCOMPARE(item.albumId, QStringLiteral("al1"));
    QCOMPARE(item.parentIndexNumber, 1);
    QCOMPARE(item.indexNumber, 4);
    QCOMPARE(item.runtimeTicks, Q_INT64_C(413000) * kTicksPerMs);
    QCOMPARE(item.playbackPositionTicks, Q_INT64_C(1000) * kTicksPerMs);
    QCOMPARE(item.playCount, 3);
    QVERIFY(item.played);
    QVERIFY(item.favorite);
    QCOMPARE(item.playlistItemId, QStringLiteral("pl-entry-9"));
}

void MusicMapperTest::trackCoverRoundTripsThroughCoverSource()
{
    Track onAlbum;
    onAlbum.id = QStringLiteral("t1");
    onAlbum.albumId = QStringLiteral("al1");
    onAlbum.coverRef = {QStringLiteral("al1"), QStringLiteral("Primary"), QStringLiteral("tagA")};
    const auto a = toMediaItem(onAlbum).coverSource();
    QCOMPARE(a.itemId, QStringLiteral("al1"));
    QCOMPARE(a.tag, QStringLiteral("tagA"));

    Track own;
    own.id = QStringLiteral("t2");
    own.coverRef = {QStringLiteral("t2"), QStringLiteral("Primary"), QStringLiteral("tagO")};
    const auto o = toMediaItem(own).coverSource();
    QCOMPARE(o.itemId, QStringLiteral("t2"));
    QCOMPARE(o.tag, QStringLiteral("tagO"));

    Track parent;
    parent.id = QStringLiteral("t3");
    parent.albumId = QStringLiteral("al3");
    parent.coverRef = {QStringLiteral("folder9"), QStringLiteral("Primary"), QStringLiteral("tagP")};
    const auto p = toMediaItem(parent).coverSource();
    QCOMPARE(p.itemId, QStringLiteral("folder9"));
    QCOMPARE(p.tag, QStringLiteral("tagP"));

    QVERIFY(!toMediaItem(Track{}).coverSource().isValid());
}

void MusicMapperTest::albumAndArtistConvert()
{
    Album album;
    album.id = QStringLiteral("al1");
    album.title = QStringLiteral("Wish You Were Here");
    album.albumArtists = {{QStringLiteral("ar1"), QStringLiteral("Pink Floyd")}};
    album.year = 1975;
    album.trackCount = 5;
    album.runtimeMs = 2'640'000;
    album.favourite = true;
    album.coverRef = {QStringLiteral("al1"), QStringLiteral("Primary"), QStringLiteral("c")};
    const MediaItem a = toMediaItem(album);
    QCOMPARE(a.type, QStringLiteral("MusicAlbum"));
    QCOMPARE(a.name, album.title);
    QCOMPARE(a.albumArtist, QStringLiteral("Pink Floyd"));
    QCOMPARE(a.artistIds, QStringList({QStringLiteral("ar1")}));
    QCOMPARE(a.productionYear, 1975);
    QCOMPARE(a.childCount, 5);
    QVERIFY(a.favorite);
    QCOMPARE(a.coverSource().tag, QStringLiteral("c"));

    Artist artist;
    artist.id = QStringLiteral("ar1");
    artist.name = QStringLiteral("Pink Floyd");
    artist.albumCount = 15;
    artist.coverRef = {QStringLiteral("ar1"), QStringLiteral("Primary"), QStringLiteral("p")};
    const MediaItem r = toMediaItem(artist);
    QCOMPARE(r.type, QStringLiteral("MusicArtist"));
    QCOMPARE(r.childCount, 15);
    QCOMPARE(r.primaryImageTag, QStringLiteral("p"));
}

void MusicMapperTest::queryEqualityAndFilters()
{
    MusicQuery a;
    QVERIFY(!a.hasFilters());
    MusicQuery b = a;
    QVERIFY(a == b);
    b.decade = 1970;
    QVERIFY(b.hasFilters());
    QVERIFY(!(a == b));
    MusicQuery c;
    c.sortKey = QStringLiteral("added"); // sort is not a filter
    QVERIFY(!c.hasFilters());
    c.format = FormatFilter::Lossless;
    QVERIFY(c.hasFilters());
}

QTEST_GUILESS_MAIN(MusicMapperTest)
#include "tst_music_mapper.moc"
