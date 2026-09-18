#include "app/music/MusicQueryTranslator.h"

#include "server/emby/MusicServerCapabilities.h"

namespace strmqt::music::MusicQueryTranslator {

namespace {

struct SortRow
{
    SortKey key;
    QString fields; // empty: sorted client-side
};

QList<SortRow> rows(Section section)
{
    auto row = [](const char *key, const char *label, bool desc, const char *fields) {
        return SortRow{{QString::fromLatin1(key), QString::fromLatin1(label), desc},
                       QString::fromLatin1(fields)};
    };
    switch (section) {
    case Section::Albums: {
        QList<SortRow> list{row("name", "Name", false, "SortName"),
                            row("artist", "Artist", false, "AlbumArtist,SortName"),
                            row("year", "Year", true, "ProductionYear,PremiereDate,SortName"),
                            row("added", "Date added", true, "DateCreated")};
        if (emby::caps::kAlbumPlayCountSort)
            list.append(row("plays", "Most played", true, "PlayCount"));
        list.append(row("random", "Random", false, "Random"));
        return list;
    }
    case Section::Artists:
        return {row("name", "Name", false, "SortName"), row("plays", "Most played", true, "PlayCount"),
                row("added", "Date added", true, "DateCreated"), row("random", "Random", false, "Random")};
    case Section::Songs:
        return {row("name", "Name", false, "SortName"),
                row("artist", "Artist", false, "AlbumArtist,Album,ParentIndexNumber,IndexNumber,SortName"),
                row("album", "Album", false, "Album,ParentIndexNumber,IndexNumber,SortName"),
                row("added", "Date added", true, "DateCreated"),
                row("plays", "Most played", true, "PlayCount"),
                row("duration", "Duration", true, "Runtime"),
                row("random", "Random", false, "Random")};
    case Section::Genres:
        return {row("size", "Size", true, ""), row("name", "Name", false, "")};
    case Section::Playlists:
        return {row("name", "Name", false, "SortName"), row("added", "Date added", true, "DateCreated")};
    }
    return {};
}

bool supportsLetter(Section section)
{
    return section == Section::Albums || section == Section::Artists || section == Section::Songs;
}

} // namespace

QList<SortKey> sortKeysFor(Section section)
{
    QList<SortKey> keys;
    for (const SortRow &row : rows(section))
        keys.append(row.key);
    return keys;
}

QString sortFields(Section section, const QString &key)
{
    const QList<SortRow> list = rows(section);
    for (const SortRow &row : list) {
        if (row.key.key == key)
            return row.fields;
    }
    return list.isEmpty() ? QString() : list.first().fields;
}

LetterRange letterRange(const QString &letter)
{
    if (letter.isEmpty())
        return {};
    if (letter == QLatin1String("#"))
        return {QString(), QStringLiteral("A")};
    const QChar c = letter.at(0).toUpper();
    if (c == QLatin1Char('Z'))
        return {QStringLiteral("Z"), QString()};
    return {QString(c), QString(QChar(c.unicode() + 1))};
}

QStringList codecsFor(FormatFilter format)
{
    switch (format) {
    case FormatFilter::Lossless:
    case FormatFilter::HiRes:
        return {QStringLiteral("flac"), QStringLiteral("alac"), QStringLiteral("ape"),
                QStringLiteral("wavpack"), QStringLiteral("wav"), QStringLiteral("pcm_s16le"),
                QStringLiteral("pcm_s24le"), QStringLiteral("pcm_s32le"), QStringLiteral("dsd_lsbf"),
                QStringLiteral("dsd_msbf")};
    case FormatFilter::Lossy:
        return {QStringLiteral("mp3"), QStringLiteral("aac"), QStringLiteral("opus"),
                QStringLiteral("vorbis"), QStringLiteral("wma")};
    case FormatFilter::Any:
        break;
    }
    return {};
}

bool formatFilterable(Section section)
{
    if (section == Section::Songs)
        return emby::caps::kAudioCodecsFiltersAudio;
    if (section == Section::Albums)
        return emby::caps::kAudioCodecsFiltersAlbums;
    return false;
}

QList<FormatFilter> formatOptions(Section section)
{
    if (!formatFilterable(section))
        return {};
    QList<FormatFilter> options{FormatFilter::Lossless, FormatFilter::Lossy};
    if (emby::caps::kHiResFilter)
        options.append(FormatFilter::HiRes);
    return options;
}

ItemsQuery toItemsQuery(const MusicQuery &query, int startIndex, int limit)
{
    ItemsQuery items;
    items.parentId = query.libraryId;
    items.recursive = true;
    items.startIndex = startIndex;
    items.limit = limit;
    switch (query.section) {
    case Section::Albums:
        items.includeItemTypes = {QStringLiteral("MusicAlbum")};
        items.fields = {QStringLiteral("ChildCount"), QStringLiteral("DateCreated"),
                        QStringLiteral("PremiereDate"), QStringLiteral("ProductionYear"),
                        QStringLiteral("Genres"), QStringLiteral("CumulativeRunTimeTicks")};
        break;
    case Section::Songs:
        items.includeItemTypes = {QStringLiteral("Audio")};
        // The live server returns UserData.PlayCount 0 and no LastPlayedDate on
        // list queries unless Fields includes these explicitly (controller ruling).
        items.fields = {QStringLiteral("MediaStreams"), QStringLiteral("DateCreated"),
                        QStringLiteral("UserDataPlayCount"), QStringLiteral("UserDataLastPlayedDate")};
        break;
    case Section::Playlists:
        items.includeItemTypes = {QStringLiteral("Playlist")};
        items.fields = {QStringLiteral("ChildCount"), QStringLiteral("CumulativeRunTimeTicks"),
                        QStringLiteral("DateCreated")};
        // Playlists are not children of a music library, so the ParentId set above
        // (from query.libraryId) cannot scope this query: sent as ParentId +
        // non-recursive, it always came back empty on the live server. Match the
        // shape PlaylistController::fetchPlaylistPage() already uses successfully:
        // no ParentId at all, Recursive=true.
        items.parentId.clear();
        items.recursive = true;
        // Dropping the ParentId scope means a bare IncludeItemTypes=Playlist query
        // also returns the human's video playlists (design spec: audio playlists get
        // the 2x2 collage here; video playlists keep the existing PlaylistPage). Ask
        // the server to pre-filter with MediaTypes=Audio, but this server cannot be
        // measured from here for whether it honours MediaTypes on a Playlist query,
        // so this is the belt: MusicRepository::browsePlaylists() also drops
        // non-audio rows client-side after parsing (the brace), and treats a missing
        // MediaType as audio rather than as a reason to show an empty section again.
        items.mediaTypes = {QStringLiteral("Audio")};
        break;
    case Section::Artists:
        items.fields = {QStringLiteral("ItemCounts"), QStringLiteral("DateCreated")};
        break;
    case Section::Genres:
        break;
    }

    items.sortBy = sortFields(query.section, query.sortKey);
    items.sortDescending = query.descending;

    if (query.section == Section::Playlists || query.section == Section::Genres)
        return items; // playlists and genres take no filters

    items.genreIds = query.genreIds;
    if (query.favouritesOnly)
        items.filters.append(QStringLiteral("IsFavorite"));
    if (query.unplayedOnly)
        items.filters.append(QStringLiteral("IsUnplayed"));

    if (query.decade == kDecadeEarlier) {
        items.maxPremiereDate = QStringLiteral("1949-12-31T23:59:59Z");
    } else if (query.decade > 0 && query.section != Section::Artists) {
        if (emby::caps::kYearsAcceptsDecade) {
            for (int year = query.decade; year < query.decade + 10; ++year)
                items.years.append(year);
        } else {
            items.minPremiereDate = QStringLiteral("%1-01-01T00:00:00Z").arg(query.decade);
            items.maxPremiereDate = QStringLiteral("%1-12-31T23:59:59Z").arg(query.decade + 9);
        }
    }

    if (query.format != FormatFilter::Any && formatFilterable(query.section))
        items.audioCodecs = codecsFor(query.format);

    if (query.sortKey == QLatin1String("name") && supportsLetter(query.section)) {
        const LetterRange range = letterRange(query.letter);
        items.nameStartsWithOrGreater = range.greaterOrEqual;
        items.nameLessThan = range.lessThan;
    }
    return items;
}

} // namespace strmqt::music::MusicQueryTranslator
