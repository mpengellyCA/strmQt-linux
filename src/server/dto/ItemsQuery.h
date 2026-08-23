#pragma once

#include <QString>
#include <QStringList>

namespace strmqt {

// Backend-neutral item query (PLAN §3.3): parent, filters, sort, search, paging.
struct ItemsQuery
{
    QString parentId;
    QString searchTerm;
    // Server sort key, e.g. "SortName", "DateCreated", "PremiereDate".
    QString sortBy;
    bool sortDescending = false;
    // Server item kinds to include, e.g. {"Movie", "Series"}. Empty = all.
    QStringList includeItemTypes;
    // Extra detail fields to request, e.g. {"Overview"}.
    QStringList fields;
    // Server-side filters, e.g. {"IsFavorite"}, {"IsUnplayed"}.
    QStringList filters;
    // Navigation axes. Emby matches these by id, which is what makes a genre or
    // a cast member a link: querying by name works until a name has an
    // apostrophe or differs in case between two libraries.
    QStringList genreIds;
    QStringList personIds;
    QStringList studioIds;
    // Music. ArtistIds matches anyone who performed on a track; AlbumArtistIds
    // matches only the artist an album is filed under — verified live: for one
    // artist the first returned 45 tracks and 5 albums, the second 5 albums
    // including a self-titled one the first did not surface first.
    QStringList artistIds;
    QStringList albumArtistIds;
    QStringList yearFilters;
    // Reverse membership lookup: with includeItemTypes={"BoxSet"} this asks
    // "which collections contain these items". Nothing on the item payload
    // exposes that, and the obvious-looking `ContainsItemId` is silently
    // ignored by the server (it returns every BoxSet), so this is the one
    // parameter that actually answers the question.
    QStringList listItemIds;
    // Alphabet-bar jump: "A" returns everything sorted under A. Emby treats an
    // empty string as "no constraint", so this is safe to always set.
    QString nameStartsWith;
    bool recursive = false;
    int startIndex = 0;
    int limit = 100;
};

} // namespace strmqt
