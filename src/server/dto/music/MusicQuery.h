#pragma once

#include <QString>
#include <QStringList>

// Browse query state (Crate spec §3.4). The UI's words, not the server's:
// MusicQueryTranslator turns it into an ItemsQuery.
namespace strmqt::music {

enum class Section { Albums, Artists, Songs, Genres, Playlists };
enum class FormatFilter { Any, Lossless, Lossy, HiRes };
enum class ArtistMode { AlbumArtists, Everyone };

inline constexpr int kDecadeAny = 0;
inline constexpr int kDecadeEarlier = -1; // before 1950

struct MusicQuery
{
    QString libraryId;
    Section section = Section::Albums;
    QString sortKey = QStringLiteral("name"); // see MusicQueryTranslator::sortKeysFor
    bool descending = false;
    QString letter;          // "", "#", "A".."Z"
    QStringList genreIds;
    int decade = kDecadeAny; // 1950, 1960, … 2020, kDecadeAny or kDecadeEarlier
    FormatFilter format = FormatFilter::Any;
    bool favouritesOnly = false;
    bool unplayedOnly = false;
    ArtistMode artistMode = ArtistMode::AlbumArtists;

    // Filters narrow the set; sort, letter and section do not count.
    bool hasFilters() const
    {
        return !genreIds.isEmpty() || decade != kDecadeAny || format != FormatFilter::Any
               || favouritesOnly || unplayedOnly;
    }
    bool operator==(const MusicQuery &) const = default;
};

} // namespace strmqt::music
