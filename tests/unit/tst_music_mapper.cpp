#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include "server/dto/music/MusicMediaItem.h"
#include "server/dto/music/MusicQuery.h"
#include "server/dto/music/MusicTypes.h"
#include "server/emby/EmbyMusicMapper.h"

using namespace strmqt;
using namespace strmqt::music;
using namespace strmqt::emby;

namespace {
QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}
} // namespace

class MusicMapperTest : public QObject
{
    Q_OBJECT

private slots:
    void trackConvertsToAudioMediaItem();
    void trackCoverRoundTripsThroughCoverSource();
    void albumAndArtistConvert();
    void queryEqualityAndFilters();
    void formatBadges_data();
    void formatBadges();
    void formatReadsListStreamsAndSourceFallback();
    void featuredSplit_data();
    void featuredSplit();
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

void MusicMapperTest::formatBadges_data()
{
    QTest::addColumn<QString>("codec");
    QTest::addColumn<int>("bitDepth");
    QTest::addColumn<int>("sampleRate");
    QTest::addColumn<int>("bitrate");
    QTest::addColumn<QString>("badge");
    QTest::addColumn<bool>("lossless");
    QTest::addColumn<bool>("hiRes");

    QTest::newRow("flac cd") << "flac" << 16 << 44100 << 900000 << "FLAC 16/44.1" << true << false;
    QTest::newRow("flac hires") << "FLAC" << 24 << 96000 << 0 << "FLAC 24/96" << true << true;
    QTest::newRow("alac 24/48") << "alac" << 24 << 48000 << 0 << "ALAC 24/48" << true << true;
    QTest::newRow("flac 16/88.2") << "flac" << 16 << 88200 << 0 << "FLAC 16/88.2" << true << true;
    QTest::newRow("mp3") << "mp3" << 0 << 44100 << 320000 << "MP3 320" << false << false;
    QTest::newRow("aac vbr") << "aac" << 0 << 44100 << 256400 << "AAC 256" << false << false;
    QTest::newRow("vorbis") << "vorbis" << 0 << 44100 << 192000 << "OGG 192" << false << false;
    QTest::newRow("lossy no bitrate") << "opus" << 0 << 48000 << 0 << "OPUS" << false << false;
    QTest::newRow("dsd64") << "dsd_lsbf" << 1 << 2822400 << 0 << "DSD64" << true << true;
    QTest::newRow("dsd emby") << "dsd_lsbf_planar" << 8 << 352800 << 0 << "DSD64" << true << true;
    QTest::newRow("pcm") << "pcm_s24le" << 24 << 192000 << 0 << "PCM 24/192" << true << true;
    QTest::newRow("wavpack") << "wavpack" << 16 << 44100 << 0 << "WV 16/44.1" << true << false;
    QTest::newRow("lossless no depth") << "flac" << 0 << 0 << 0 << "FLAC" << true << false;
    QTest::newRow("empty") << "" << 24 << 96000 << 0 << "" << false << false;
}

void MusicMapperTest::formatBadges()
{
    QFETCH(QString, codec);
    QFETCH(int, bitDepth);
    QFETCH(int, sampleRate);
    QFETCH(int, bitrate);
    QFETCH(QString, badge);
    QFETCH(bool, lossless);
    QFETCH(bool, hiRes);

    const auto format = deriveAudioFormat(codec, bitDepth, sampleRate, bitrate, 2);
    QCOMPARE(format.badge, badge);
    QCOMPARE(format.isLossless, lossless);
    QCOMPARE(format.isHiRes, hiRes);
    QCOMPARE(format.isValid(), !codec.isEmpty());
    if (!codec.isEmpty())
        QCOMPARE(format.codec, codec.toLower());
}

void MusicMapperTest::formatReadsListStreamsAndSourceFallback()
{
    const auto direct = parseAudioFormat(json(R"({"MediaStreams":[
        {"Type":"Video","Codec":"mjpeg"},
        {"Type":"Audio","Codec":"flac","BitDepth":24,"SampleRate":96000,"Channels":2}]})"));
    QCOMPARE(direct.badge, QStringLiteral("FLAC 24/96"));
    QCOMPARE(direct.channels, 2);

    const auto nested = parseAudioFormat(json(R"({"MediaSources":[{"Container":"mp3",
        "MediaStreams":[{"Type":"Audio","BitRate":320000,"SampleRate":44100}]}]})"));
    QCOMPARE(nested.badge, QStringLiteral("MP3 320")); // codec falls back to Container

    QVERIFY(!parseAudioFormat(json(R"({"MediaStreams":"garbage"})")).isValid());
    QVERIFY(!parseAudioFormat(QJsonObject{}).isValid());
}

void MusicMapperTest::featuredSplit_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("title");
    QTest::addColumn<QStringList>("names");

    QTest::newRow("none") << "Time" << "Time" << QStringList{};
    QTest::newRow("paren feat.") << "Get Lucky (feat. Pharrell Williams)" << "Get Lucky"
                                 << QStringList{"Pharrell Williams"};
    QTest::newRow("bracket ft") << "Song [ft. A & B]" << "Song" << QStringList{"A", "B"};
    QTest::newRow("bare featuring") << "Song featuring A, B & C" << "Song"
                                    << QStringList{"A", "B", "C"};
    QTest::newRow("case") << "Song (FEAT. X)" << "Song" << QStringList{"X"};
    QTest::newRow("not a word boundary") << "Defeat the Feature" << "Defeat the Feature"
                                         << QStringList{};
    QTest::newRow("name with and kept") << "Song (feat. Simon and Garfunkel)" << "Song"
                                        << QStringList{"Simon and Garfunkel"};
}

void MusicMapperTest::featuredSplit()
{
    QFETCH(QString, input);
    QFETCH(QString, title);
    QFETCH(QStringList, names);
    const FeaturedSplit split = splitFeatured(input);
    QCOMPARE(split.title, title);
    QCOMPARE(split.names, names);
}

QTEST_GUILESS_MAIN(MusicMapperTest)
#include "tst_music_mapper.moc"
