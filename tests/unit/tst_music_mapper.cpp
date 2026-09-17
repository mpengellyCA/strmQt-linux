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
    void datesTrimToMilliseconds();
    void trackParsesEverything();
    void trackFeaturedAndArtistDifference();
    void trackToleratesJunk();
    void albumParses();
    void artistGenrePlaylistParse();
    void classifyRelease_data();
    void classifyRelease();
    void discsGroupAndSum();
    void refineAlbumFromTracks();
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

void MusicMapperTest::datesTrimToMilliseconds()
{
    const QDateTime date = parseEmbyDate(QStringLiteral("2024-03-01T12:34:56.1234567Z"));
    QVERIFY(date.isValid());
    QCOMPARE(date.toUTC().time().msec(), 123);
    QVERIFY(parseEmbyDate(QStringLiteral("2024-03-01T12:34:56Z")).isValid());
    QVERIFY(!parseEmbyDate(QStringLiteral("yesterday")).isValid());
    QVERIFY(!parseEmbyDate(QString()).isValid());
}

void MusicMapperTest::trackParsesEverything()
{
    const Track t = parseTrack(json(R"({
        "Id": 12345, "Name": "Money", "Type": "Audio",
        "ArtistItems": [{"Name":"Pink Floyd","Id":"ar1"}],
        "AlbumArtists": [{"Name":"Pink Floyd","Id":"ar1"}],
        "AlbumId": "al1", "Album": "The Dark Side of the Moon",
        "ParentIndexNumber": 1, "IndexNumber": 6,
        "RunTimeTicks": 3830000000,
        "DateCreated": "2023-01-02T03:04:05.0000000Z",
        "AlbumPrimaryImageTag": "atag",
        "ImageTags": {"Primary": "own"},
        "UserData": {"IsFavorite": true, "Played": true, "PlayCount": 7,
                     "PlaybackPositionTicks": 10000000,
                     "LastPlayedDate": "2026-09-01T10:00:00.0000000Z"},
        "MediaStreams": [{"Type":"Audio","Codec":"flac","BitDepth":16,"SampleRate":44100}]
    })"));
    QCOMPARE(t.id, QStringLiteral("12345"));
    QCOMPARE(t.title, QStringLiteral("Money"));
    QCOMPARE(t.displayTitle, QStringLiteral("Money"));
    QCOMPARE(t.artists.size(), 1);
    QCOMPARE(t.artists.first(), (ArtistRef{QStringLiteral("ar1"), QStringLiteral("Pink Floyd")}));
    QCOMPARE(t.albumArtists.first().id, QStringLiteral("ar1"));
    QCOMPARE(t.albumId, QStringLiteral("al1"));
    QCOMPARE(t.albumTitle, QStringLiteral("The Dark Side of the Moon"));
    QCOMPARE(t.discNumber, 1);
    QCOMPARE(t.trackNumber, 6);
    QCOMPARE(t.runtimeMs, Q_INT64_C(383000));
    QCOMPARE(t.positionMs, Q_INT64_C(1000));
    QVERIFY(t.favourite);
    QVERIFY(t.played);
    QCOMPARE(t.playCount, 7);
    QVERIFY(t.lastPlayed.isValid());
    QVERIFY(t.dateAdded.isValid());
    QCOMPARE(t.format.badge, QStringLiteral("FLAC 16/44.1"));
    QCOMPARE(t.coverRef.itemId, QStringLiteral("al1")); // album tag beats own
    QCOMPARE(t.coverRef.tag, QStringLiteral("atag"));
    QVERIFY(!t.differsFromAlbumArtist);
    QVERIFY(t.featured.isEmpty());
}

void MusicMapperTest::trackFeaturedAndArtistDifference()
{
    // Tagged delimiter: the JSON title itself contains "(feat. ... )" immediately
    // followed by a closing quote, which would prematurely end a plain R"(...)"
    // raw string right there.
    const Track guest = parseTrack(json(R"JSON({
        "Id":"t1","Name":"Get Lucky (feat. Pharrell Williams)",
        "ArtistItems":[{"Name":"Daft Punk","Id":"dp"},{"Name":"Nile Rodgers","Id":"nr"}],
        "AlbumArtists":[{"Name":"Daft Punk","Id":"dp"}]})JSON"));
    QCOMPARE(guest.displayTitle, QStringLiteral("Get Lucky"));
    QCOMPARE(guest.featured.size(), 2);
    QCOMPARE(guest.featured.at(0).name, QStringLiteral("Pharrell Williams"));
    QCOMPARE(guest.featured.at(1), (ArtistRef{QStringLiteral("nr"), QStringLiteral("Nile Rodgers")}));
    QVERIFY(!guest.differsFromAlbumArtist);

    const Track compilation = parseTrack(json(R"({
        "Id":"t2","Name":"Heroes","Artists":["David Bowie"],
        "AlbumArtist":"Various Artists"})"));
    QCOMPARE(compilation.artists.first().name, QStringLiteral("David Bowie"));
    QCOMPARE(compilation.albumArtists.first().name, QStringLiteral("Various Artists"));
    QVERIFY(compilation.differsFromAlbumArtist);
    QVERIFY(compilation.featured.isEmpty());
}

void MusicMapperTest::trackToleratesJunk()
{
    const Track t = parseTrack(json(R"({"Id":"t","ArtistItems":"nope","UserData":[],
        "RunTimeTicks":"abc","MediaStreams":{},"ImageTags":7})"));
    QCOMPARE(t.id, QStringLiteral("t"));
    QVERIFY(t.artists.isEmpty());
    QCOMPARE(t.runtimeMs, Q_INT64_C(0));
    QVERIFY(!t.format.isValid());
    QVERIFY(!t.coverRef.isValid());
    QVERIFY(parseTracks(QJsonArray{}).isEmpty());
}

void MusicMapperTest::albumParses()
{
    const Album a = parseAlbum(json(R"({
        "Id":"al1","Name":"Wish You Were Here","Type":"MusicAlbum",
        "AlbumArtists":[{"Name":"Pink Floyd","Id":"ar1"}],
        "PremiereDate":"1975-09-12T00:00:00.0000000Z",
        "GenreItems":[{"Name":"Progressive Rock","Id":"g1"}],
        "Studios":[{"Name":"Harvest","Id":"s1"}],
        "ChildCount":5, "CumulativeRunTimeTicks":26400000000,
        "DateCreated":"2020-01-01T00:00:00Z",
        "ImageTags":{"Primary":"cover"},
        "UserData":{"IsFavorite":true,"PlayCount":4}})"));
    QCOMPARE(a.id, QStringLiteral("al1"));
    QCOMPARE(a.year, 1975); // from PremiereDate when ProductionYear is absent
    QCOMPARE(a.genres.first(), (GenreRef{QStringLiteral("g1"), QStringLiteral("Progressive Rock")}));
    QCOMPARE(a.studios, QStringList{QStringLiteral("Harvest")});
    QCOMPARE(a.trackCount, 5);
    QCOMPARE(a.runtimeMs, Q_INT64_C(2640000));
    QVERIFY(a.favourite);
    QCOMPARE(a.playCount, 4);
    QCOMPARE(a.coverRef.itemId, QStringLiteral("al1"));
    QCOMPARE(a.releaseType, ReleaseType::Album);

    const Album fallback = parseAlbum(json(R"({"Id":"al2","ProductionYear":1999,
        "Genres":["Trip Hop"],"AlbumArtist":"Massive Attack","RunTimeTicks":600000000})"));
    QCOMPARE(fallback.year, 1999);
    QCOMPARE(fallback.genres.first().name, QStringLiteral("Trip Hop"));
    QCOMPARE(fallback.albumArtists.first().name, QStringLiteral("Massive Attack"));
    QCOMPARE(fallback.runtimeMs, Q_INT64_C(60000));
    QVERIFY(!fallback.releaseTypeFromServer);

    // Measured wire fact: GenreItems[].Id is a JSON number on album items
    // (unlike AlbumArtists[]/ArtistItems[].Id, which are strings).
    const Album numericGenre = parseAlbum(json(R"({"Id":"al3",
        "GenreItems":[{"Id":7,"Name":"Ambient"}]})"));
    QCOMPARE(numericGenre.genres.first().id, QStringLiteral("7"));
    QCOMPARE(numericGenre.genres.first().name, QStringLiteral("Ambient"));

    QCOMPARE(releaseTypeFromTag(QStringLiteral("EP")), std::optional(ReleaseType::EP));
    QCOMPARE(releaseTypeFromTag(QStringLiteral("single")), std::optional(ReleaseType::Single));
    QCOMPARE(releaseTypeFromTag(QStringLiteral("Compilation")), std::optional(ReleaseType::Compilation));
    QCOMPARE(releaseTypeFromTag(QStringLiteral("album")), std::optional(ReleaseType::Album));
    QVERIFY(!releaseTypeFromTag(QStringLiteral("live")).has_value());
}

void MusicMapperTest::artistGenrePlaylistParse()
{
    const Artist artist = parseArtist(json(R"({"Id":"ar1","Name":"Björk",
        "AlbumCount":9,"SongCount":120,"ImageTags":{"Primary":"p"},
        "BackdropImageTags":["b0","b1"],"UserData":{"IsFavorite":true}})"));
    QCOMPARE(artist.name, QStringLiteral("Björk"));
    QCOMPARE(artist.albumCount, 9);
    QCOMPARE(artist.trackCount, 120);
    QCOMPARE(artist.coverRef.tag, QStringLiteral("p"));
    QCOMPARE(artist.backdropRef.imageType, QStringLiteral("Backdrop"));
    QCOMPARE(artist.backdropRef.tag, QStringLiteral("b0"));
    QVERIFY(artist.favourite);
    QCOMPARE(parseArtist(json(R"({"Id":"x","ChildCount":3})")).albumCount, 3);

    const GenreBin genre = parseGenreBin(json(R"({"Id":"g1","Name":"Jazz","AlbumCount":42})"));
    QCOMPARE(genre.recordCount, 42);
    QCOMPARE(parseGenreBin(json(R"({"Id":"g2","ChildCount":5})")).recordCount, 5);
    // GenreBin ids can also arrive as JSON numbers (measured on /MusicGenres).
    QCOMPARE(parseGenreBin(json(R"({"Id":9,"Name":"Ambient"})")).id, QStringLiteral("9"));

    const Playlist playlist = parsePlaylist(json(R"({"Id":"pl1","Name":"Road trip",
        "ChildCount":31,"CumulativeRunTimeTicks":72000000000,"ImageTags":{"Primary":"col"},
        "DateCreated":"2025-05-05T00:00:00Z"})"));
    QCOMPARE(playlist.trackCount, 31);
    QCOMPARE(playlist.runtimeMs, Q_INT64_C(7200000));
    QCOMPARE(playlist.coverRef.tag, QStringLiteral("col"));
    QVERIFY(playlist.dateAdded.isValid());
    QCOMPARE(parseArtists(QJsonArray{QJsonObject{{"Id", "a"}}, QJsonObject{{"Id", "b"}}}).size(), 2);
}

void MusicMapperTest::classifyRelease_data()
{
    QTest::addColumn<QString>("tag");
    QTest::addColumn<QStringList>("albumArtists");
    QTest::addColumn<int>("tracks");
    QTest::addColumn<qint64>("runtimeMs");
    QTest::addColumn<QStringList>("performers");
    QTest::addColumn<int>("expected");

    const qint64 min = 60'000;
    QTest::newRow("server tag wins") << "EP" << QStringList{"A"} << 12 << 60 * min
                                     << QStringList{"A"} << int(ReleaseType::EP);
    QTest::newRow("various artists") << "" << QStringList{"Various Artists"} << 20 << 70 * min
                                     << QStringList{"A", "B"} << int(ReleaseType::Compilation);
    QTest::newRow("four guests") << "" << QStringList{"DJ"} << 10 << 50 * min
                                 << QStringList{"A", "B", "C", "D"} << int(ReleaseType::Compilation);
    QTest::newRow("three guests is not") << "" << QStringList{"DJ"} << 10 << 50 * min
                                         << QStringList{"A", "B", "C"} << int(ReleaseType::Album);
    QTest::newRow("album artist among four") << "" << QStringList{"A"} << 10 << 50 * min
                                             << QStringList{"A", "B", "C", "D"} << int(ReleaseType::Album);
    QTest::newRow("single") << "" << QStringList{"A"} << 2 << 8 * min << QStringList{"A"}
                            << int(ReleaseType::Single);
    QTest::newRow("long 3-track is not single") << "" << QStringList{"A"} << 3 << 25 * min
                                                << QStringList{"A"} << int(ReleaseType::EP);
    QTest::newRow("ep") << "" << QStringList{"A"} << 6 << 24 * min << QStringList{"A"}
                        << int(ReleaseType::EP);
    QTest::newRow("unknown runtime stays album") << "" << QStringList{"A"} << 2 << qint64(0)
                                                 << QStringList{"A"} << int(ReleaseType::Album);
    QTest::newRow("album") << "" << QStringList{"A"} << 9 << 42 * min << QStringList{"A"}
                           << int(ReleaseType::Album);
}

void MusicMapperTest::classifyRelease()
{
    QFETCH(QString, tag);
    QFETCH(QStringList, albumArtists);
    QFETCH(int, tracks);
    QFETCH(qint64, runtimeMs);
    QFETCH(QStringList, performers);
    QFETCH(int, expected);
    const ReleaseEvidence evidence{tag, albumArtists, tracks, runtimeMs, performers};
    QCOMPARE(int(strmqt::emby::classifyRelease(evidence)), expected);
}

namespace {
Track makeTrack(QString id, int disc, int number, qint64 ms, QString codec, int depth, int rate,
                QString performer = QStringLiteral("A"))
{
    Track track;
    track.id = std::move(id);
    track.discNumber = disc;
    track.trackNumber = number;
    track.runtimeMs = ms;
    track.format = deriveAudioFormat(codec, depth, rate, 0, 2);
    track.artists = {{QString(), std::move(performer)}};
    return track;
}
} // namespace

void MusicMapperTest::discsGroupAndSum()
{
    const QList<Track> tracks = {
        makeTrack("a", 2, 1, 1000, "flac", 16, 44100),
        makeTrack("b", 1, 1, 2000, "flac", 16, 44100),
        makeTrack("c", 0, 2, 3000, "flac", 16, 44100), // unknown disc → disc 1
        makeTrack("d", 2, 2, 4000, "flac", 16, 44100),
    };
    const QList<Disc> discs = groupDiscs(tracks);
    QCOMPARE(discs.size(), 2);
    QCOMPARE(discs.at(0).number, 1);
    QCOMPARE(discs.at(0).tracks.size(), 2);
    QCOMPARE(discs.at(0).tracks.at(0).id, QStringLiteral("b")); // input order kept
    QCOMPARE(discs.at(0).runtimeMs, Q_INT64_C(5000));
    QCOMPARE(discs.at(1).number, 2);
    QCOMPARE(discs.at(1).runtimeMs, Q_INT64_C(5000));
    QVERIFY(groupDiscs({}).isEmpty());
}

void MusicMapperTest::refineAlbumFromTracks()
{
    Album album;
    album.albumArtists = {{QString(), QStringLiteral("A")}};
    const QList<Track> tracks = {
        makeTrack("1", 1, 1, 200'000, "flac", 24, 96000),
        makeTrack("2", 1, 2, 200'000, "flac", 24, 96000),
        makeTrack("3", 2, 1, 200'000, "mp3", 0, 44100),
    };
    strmqt::emby::refineAlbumFromTracks(album, tracks);
    QCOMPARE(album.trackCount, 3);
    QCOMPARE(album.runtimeMs, Q_INT64_C(600000));
    QCOMPARE(album.discCount, 2);
    QCOMPARE(album.formatSummary, QStringLiteral("FLAC 24/96"));
    QCOMPARE(album.releaseType, ReleaseType::Single); // 3 tracks, 10 min

    Album tagged;
    tagged.releaseType = ReleaseType::Compilation;
    tagged.releaseTypeFromServer = true;
    strmqt::emby::refineAlbumFromTracks(tagged, tracks);
    QCOMPARE(tagged.releaseType, ReleaseType::Compilation);
    QCOMPARE(dominantFormat({}), QString());
}

QTEST_GUILESS_MAIN(MusicMapperTest)
#include "tst_music_mapper.moc"
