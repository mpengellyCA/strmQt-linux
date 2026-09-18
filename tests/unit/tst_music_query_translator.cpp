#include <QtTest>

#include "app/models/MediaItemModel.h"
#include "app/music/MusicFormat.h"
#include "app/music/MusicQueryTranslator.h"
#include "server/emby/MusicServerCapabilities.h"

using namespace strmqt;
using namespace strmqt::music;
namespace T = strmqt::music::MusicQueryTranslator;

class MusicQueryTranslatorTest : public QObject
{
    Q_OBJECT

private slots:
    void sortTables();
    void letterRanges();
    void albumsQuery();
    void songsQueryWithFilters();
    void playlistsQueryIsUnscopedAndAudioOnly();
    void decadeTranslation();
    void formatOnlyWhereFilterable();
    void letterOnlyForNameSort();
    void formatting();
};

void MusicQueryTranslatorTest::sortTables()
{
    QStringList albumKeys;
    for (const SortKey &key : T::sortKeysFor(Section::Albums))
        albumKeys.append(key.key);
    QStringList expected{"name", "artist", "year", "added"};
    if (emby::caps::kAlbumPlayCountSort)
        expected.append(QStringLiteral("plays"));
    expected.append(QStringLiteral("random"));
    QCOMPARE(albumKeys, expected);

    QCOMPARE(T::sortFields(Section::Songs, QStringLiteral("album")),
             QStringLiteral("Album,ParentIndexNumber,IndexNumber,SortName"));
    QCOMPARE(T::sortFields(Section::Genres, QStringLiteral("size")), QString());
    QCOMPARE(T::sortFields(Section::Albums, QStringLiteral("bogus")), QStringLiteral("SortName"));
    QVERIFY(T::sortKeysFor(Section::Albums).at(2).defaultDescending); // year
    QCOMPARE(T::sortKeysFor(Section::Playlists).size(), 2);
}

void MusicQueryTranslatorTest::letterRanges()
{
    QCOMPARE(T::letterRange(QStringLiteral("C")).greaterOrEqual, QStringLiteral("C"));
    QCOMPARE(T::letterRange(QStringLiteral("C")).lessThan, QStringLiteral("D"));
    QCOMPARE(T::letterRange(QStringLiteral("#")).greaterOrEqual, QString());
    QCOMPARE(T::letterRange(QStringLiteral("#")).lessThan, QStringLiteral("A"));
    QCOMPARE(T::letterRange(QStringLiteral("Z")).greaterOrEqual, QStringLiteral("Z"));
    QCOMPARE(T::letterRange(QStringLiteral("Z")).lessThan, QString());
    QCOMPARE(T::letterRange(QString()).lessThan, QString());
}

void MusicQueryTranslatorTest::albumsQuery()
{
    MusicQuery query;
    query.libraryId = QStringLiteral("1868998");
    const ItemsQuery items = T::toItemsQuery(query, 100, 50);
    QCOMPARE(items.parentId, QStringLiteral("1868998"));
    QCOMPARE(items.includeItemTypes, QStringList{QStringLiteral("MusicAlbum")});
    QVERIFY(items.recursive);
    QCOMPARE(items.sortBy, QStringLiteral("SortName"));
    QVERIFY(!items.sortDescending);
    QCOMPARE(items.startIndex, 100);
    QCOMPARE(items.limit, 50);
    QVERIFY(items.fields.contains(QStringLiteral("ChildCount")));
    QVERIFY(items.filters.isEmpty());
}

void MusicQueryTranslatorTest::songsQueryWithFilters()
{
    MusicQuery query;
    query.libraryId = QStringLiteral("L");
    query.section = Section::Songs;
    query.sortKey = QStringLiteral("plays");
    query.descending = true;
    query.genreIds = {QStringLiteral("g1"), QStringLiteral("g2")};
    query.favouritesOnly = true;
    query.unplayedOnly = true;
    const ItemsQuery items = T::toItemsQuery(query, 0, 100);
    QCOMPARE(items.includeItemTypes, QStringList{QStringLiteral("Audio")});
    QCOMPARE(items.sortBy, QStringLiteral("PlayCount"));
    QVERIFY(items.sortDescending);
    QCOMPARE(items.genreIds, query.genreIds);
    QCOMPARE(items.filters, QStringList({QStringLiteral("IsFavorite"), QStringLiteral("IsUnplayed")}));
    QVERIFY(items.fields.contains(QStringLiteral("MediaStreams")));
    // The live server returns UserData.PlayCount 0 and no LastPlayedDate on list
    // queries unless Fields includes these explicitly (controller ruling).
    QVERIFY(items.fields.contains(QStringLiteral("UserDataPlayCount")));
    QVERIFY(items.fields.contains(QStringLiteral("UserDataLastPlayedDate")));
}

void MusicQueryTranslatorTest::playlistsQueryIsUnscopedAndAudioOnly()
{
    // Defect 2 (visual-fix-1): playlists are not children of a music library, so
    // scoping this query by ParentId and forcing it non-recursive always came
    // back empty on the live server. Match PlaylistController::fetchPlaylistPage's
    // shape: no ParentId, Recursive=true — and stay audio-only via MediaTypes.
    MusicQuery query;
    query.libraryId = QStringLiteral("1868998");
    query.section = Section::Playlists;
    const ItemsQuery items = T::toItemsQuery(query, 0, 100);
    QCOMPARE(items.includeItemTypes, QStringList{QStringLiteral("Playlist")});
    QVERIFY(items.parentId.isEmpty());
    QVERIFY(items.recursive);
    QCOMPARE(items.mediaTypes, QStringList{QStringLiteral("Audio")});
}

void MusicQueryTranslatorTest::decadeTranslation()
{
    MusicQuery query;
    query.decade = 1970;
    const ItemsQuery items = T::toItemsQuery(query, 0, 10);
    if (emby::caps::kYearsAcceptsDecade) {
        QCOMPARE(items.years.size(), 10);
        QCOMPARE(items.years.first(), 1970);
        QCOMPARE(items.years.last(), 1979);
        QVERIFY(items.minPremiereDate.isEmpty());
    } else {
        QVERIFY(items.years.isEmpty());
        QCOMPARE(items.minPremiereDate, QStringLiteral("1970-01-01T00:00:00Z"));
        QCOMPARE(items.maxPremiereDate, QStringLiteral("1979-12-31T23:59:59Z"));
    }
    query.decade = kDecadeEarlier;
    const ItemsQuery earlier = T::toItemsQuery(query, 0, 10);
    QVERIFY(earlier.years.isEmpty());
    QCOMPARE(earlier.maxPremiereDate, QStringLiteral("1949-12-31T23:59:59Z"));
}

void MusicQueryTranslatorTest::formatOnlyWhereFilterable()
{
    MusicQuery query;
    query.format = FormatFilter::Lossless;
    query.section = Section::Songs;
    const ItemsQuery songs = T::toItemsQuery(query, 0, 10);
    QCOMPARE(!songs.audioCodecs.isEmpty(), emby::caps::kAudioCodecsFiltersAudio);
    if (!songs.audioCodecs.isEmpty())
        QVERIFY(songs.audioCodecs.contains(QStringLiteral("flac")));
    query.section = Section::Albums;
    QCOMPARE(!T::toItemsQuery(query, 0, 10).audioCodecs.isEmpty(), emby::caps::kAudioCodecsFiltersAlbums);
    QVERIFY(T::formatOptions(Section::Artists).isEmpty());
    QCOMPARE(T::formatOptions(Section::Songs).contains(FormatFilter::HiRes),
             emby::caps::kAudioCodecsFiltersAudio && emby::caps::kHiResFilter);
    QVERIFY(T::codecsFor(FormatFilter::Lossy).contains(QStringLiteral("mp3")));
    QVERIFY(T::codecsFor(FormatFilter::Any).isEmpty());
}

void MusicQueryTranslatorTest::letterOnlyForNameSort()
{
    MusicQuery query;
    query.letter = QStringLiteral("M");
    ItemsQuery items = T::toItemsQuery(query, 0, 10);
    QCOMPARE(items.nameStartsWithOrGreater, QStringLiteral("M"));
    QCOMPARE(items.nameLessThan, QStringLiteral("N"));
    query.sortKey = QStringLiteral("added");
    items = T::toItemsQuery(query, 0, 10);
    QVERIFY(items.nameStartsWithOrGreater.isEmpty());
    QVERIFY(items.nameLessThan.isEmpty());
    QVERIFY(items.nameStartsWith.isEmpty()); // never the slow LIKE form
}

void MusicQueryTranslatorTest::formatting()
{
    QCOMPARE(formatDuration(187'000), QStringLiteral("3:07"));
    QCOMPARE(formatDuration(3'723'000), QStringLiteral("1:02:03"));
    QCOMPARE(formatDuration(0), QString());
    QCOMPARE(formatRuntime(48 * 60'000 + 20'000), QStringLiteral("48 min"));
    QCOMPARE(formatRuntime(72 * 60'000), QStringLiteral("1 h 12 min"));
    QCOMPARE(formatRuntime(120 * 60'000), QStringLiteral("2 h"));
    QCOMPARE(formatRuntime(20'000), QStringLiteral("1 min"));
    QCOMPARE(formatRecordCount(1), QStringLiteral("1 record"));
    QCOMPARE(formatRecordCount(12), QStringLiteral("12 records"));
    QCOMPARE(formatTrackCount(9), QStringLiteral("9 tracks"));
    QCOMPARE(joinNames({{"1", "A"}}), QStringLiteral("A"));
    QCOMPARE(joinNames({{"1", "A"}, {"2", "B"}}), QStringLiteral("A & B"));
    QCOMPARE(joinNames({{"1", "A"}, {"2", "B"}, {"3", "C"}}), QStringLiteral("A, B & C"));
    setEmbyImageSourceNamespace(QStringLiteral("test"));
    QCOMPARE(coverUrl({QStringLiteral("al1"), QStringLiteral("Primary"), QStringLiteral("t")}),
             QStringLiteral("image://emby/test/al1/Primary/t"));
    QCOMPARE(coverUrl({}), QString());
}

QTEST_GUILESS_MAIN(MusicQueryTranslatorTest)
#include "tst_music_query_translator.moc"
