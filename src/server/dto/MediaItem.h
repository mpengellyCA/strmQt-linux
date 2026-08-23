#pragma once

#include <QString>
#include <QStringList>

namespace strmqt {

// Media-server time unit: Emby/Jellyfin use 100 ns "ticks".
inline constexpr qint64 kTicksPerMs = 10'000;
inline constexpr qint64 kTicksPerSecond = 10'000'000;

// Backend-neutral media item (movie, series, season, episode, folder).
// All fields are optional on the wire; defaults here are the contract (tolerant
// parsing per AGENTS.md — server drift must never crash the app).
struct MediaItem
{
    QString id;
    QString name;
    // Server item kind: "Movie", "Series", "Season", "Episode", "CollectionFolder", ...
    QString type;
    QString overview;

    // Episode/season context (empty when not applicable).
    QString seriesId;
    QString seriesName;
    QString seasonId;
    QString seasonName;
    int indexNumber = -1;       // episode number
    int parentIndexNumber = -1; // season number

    int productionYear = 0;
    QString officialRating; // "PG-13", ...
    double communityRating = 0.0;

    qint64 runtimeTicks = 0;

    // Series only: "Continuing" or "Ended" (empty otherwise).
    QString status;
    // ISO-8601 as sent. Present only when the request asked for PremiereDate,
    // which browse queries do not by default.
    QString premiereDate;
    // Playlist ENTRY id, present only on playlist member rows. Not the item id:
    // the same track can appear twice in one playlist, so removing and moving
    // address the entry (ARCHITECTURE.md).
    QString playlistItemId;
    // Emby files a person's birthplace under ProductionLocations and their
    // death date under EndDate. Neither name is a mistake to correct — it is
    // the server's vocabulary for a record type it stores like any other item.
    QStringList productionLocations;

    // ── Music (ARCHITECTURE.md) ───────────────────────────────────────────────
    // A track's performers, and their ids in the same order so a name on
    // screen can become a link. Emby sends Artists (names) and ArtistItems
    // ({Name,Id}); the album artist is the one an album is filed under and is
    // often not the same person as the track artist.
    QStringList artists;
    QStringList artistIds;
    QString albumArtist;
    QString album;
    QString albumId;
    // Tracks on an album, episodes in a season: whatever this item parents.
    int childCount = 0;
    QString endDate;

    // Per-user state (Emby UserData).
    int unplayedItemCount = 0; // series/season rollup
    // Times this user has finished the item. 0 means either "never" or "the
    // request did not ask for UserData" — the two are indistinguishable here,
    // so consumers that must not guess (MPRIS xesam:useCount) publish it only
    // when it is positive.
    int playCount = 0;
    qint64 playbackPositionTicks = 0;
    double playedPercentage = 0.0;
    bool played = false;
    bool favorite = false;

    // Image cache-busting tags; empty tag means "no such image".
    QString primaryImageTag;
    QStringList backdropImageTags;
    // 16:9 sources. Which of these exists depends entirely on the item kind,
    // verified against the live server: a Movie carries ImageTags.Thumb and its
    // own backdrop, while an Episode carries ONLY ImageTags.Primary (which for
    // an episode is the 16:9 still, not a poster) plus its series' backdrop
    // under ParentBackdropImageTags. So a wide card cannot just ask for one
    // image type; see MediaItem::thumbSource().
    QString thumbImageTag;
    QString parentThumbImageTag;
    QString parentThumbItemId;
    QString parentBackdropImageTag;
    QString parentBackdropItemId;

    // 1:1 sources. A track's own Primary is whatever the ripper embedded in the
    // file — often a 300 px scan, sometimes a different pressing, sometimes the
    // back cover — while the album carries the one curated cover the whole
    // record should show. So for audio the album wins and the file's own art is
    // the last resort, which is the reverse of every other item kind. The tag
    // under AlbumPrimaryImageTag belongs to AlbumId, not to this item.
    // See MediaItem::coverSource().
    QString albumPrimaryImageTag;
    QString parentPrimaryImageItemId;
    QString parentPrimaryImageTag;

    qint64 runtimeMs() const { return runtimeTicks / kTicksPerMs; }
    qint64 positionMs() const { return playbackPositionTicks / kTicksPerMs; }
    bool isResumable() const { return playbackPositionTicks > 0 && !played; }

    // Best available 16:9 image, as an {itemId, imageType, tag} triple for the
    // image provider. Empty tag means "nothing suitable" and the caller should
    // fall back to the poster rather than draw a hole.
    //
    // An episode's Primary IS its still, so it comes first for episodes and
    // last for everything else — a movie poster stretched into a 16:9 frame is
    // worse than no wide art at all.
    struct ImageRef
    {
        QString itemId;
        QString imageType;
        QString tag;

        bool isValid() const { return !tag.isEmpty() && !itemId.isEmpty(); }
    };

    ImageRef thumbSource() const
    {
        const bool episode = type.compare(QLatin1String("Episode"), Qt::CaseInsensitive) == 0;
        if (episode && !primaryImageTag.isEmpty())
            return {id, QStringLiteral("Primary"), primaryImageTag};
        if (!thumbImageTag.isEmpty())
            return {id, QStringLiteral("Thumb"), thumbImageTag};
        if (!parentThumbImageTag.isEmpty() && !parentThumbItemId.isEmpty())
            return {parentThumbItemId, QStringLiteral("Thumb"), parentThumbImageTag};
        if (!backdropImageTags.isEmpty())
            return {id, QStringLiteral("Backdrop"), backdropImageTags.first()};
        if (!parentBackdropImageTag.isEmpty() && !parentBackdropItemId.isEmpty())
            return {parentBackdropItemId, QStringLiteral("Backdrop"), parentBackdropImageTag};
        if (!episode && !primaryImageTag.isEmpty())
            return {id, QStringLiteral("Primary"), primaryImageTag};
        return {};
    }

    // Best available 1:1 image — the sleeve. Same triple, same "empty tag means
    // draw the placeholder, not a hole" contract as thumbSource().
    //
    // Precedence is type-dependent because the inversion audio needs is wrong
    // everywhere else: a movie's own poster is the only poster it has, while a
    // track's own Primary is the least trustworthy square it carries.
    ImageRef coverSource() const
    {
        const ImageRef own =
            primaryImageTag.isEmpty()
                ? ImageRef{}
                : ImageRef{id, QStringLiteral("Primary"), primaryImageTag};
        const ImageRef parent =
            (parentPrimaryImageTag.isEmpty() || parentPrimaryImageItemId.isEmpty())
                ? ImageRef{}
                : ImageRef{parentPrimaryImageItemId, QStringLiteral("Primary"),
                           parentPrimaryImageTag};

        if (type.compare(QLatin1String("Audio"), Qt::CaseInsensitive) == 0
            || type.compare(QLatin1String("AudioBook"), Qt::CaseInsensitive) == 0) {
            if (!albumPrimaryImageTag.isEmpty() && !albumId.isEmpty())
                return {albumId, QStringLiteral("Primary"), albumPrimaryImageTag};
            if (parent.isValid())
                return parent;
            return own;
        }
        if (own.isValid())
            return own;
        // An album with no cover of its own falls back to the artist's image:
        // a filled grid cell beats a hole, and the artist is the only other
        // square the server offers for it.
        if (type.compare(QLatin1String("MusicAlbum"), Qt::CaseInsensitive) == 0)
            return parent;
        return {};
    }
};

} // namespace strmqt
