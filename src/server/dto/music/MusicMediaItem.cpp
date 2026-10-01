#include "server/dto/music/MusicMediaItem.h"

namespace strmqt::music {

namespace {

void applyArtists(MediaItem &item, const QList<ArtistRef> &artists)
{
    for (const ArtistRef &artist : artists) {
        item.artists.append(artist.name);
        item.artistIds.append(artist.id);
    }
}

} // namespace

void applyCover(MediaItem &item, const ImageRef &cover)
{
    if (cover.tag.isEmpty() || cover.itemId.isEmpty())
        return;
    const bool own = cover.itemId == item.id;
    if (cover.imageType == QLatin1String("Thumb")) {
        if (own) {
            item.thumbImageTag = cover.tag;
        } else {
            item.parentThumbItemId = cover.itemId;
            item.parentThumbImageTag = cover.tag;
        }
    } else if (own) {
        item.primaryImageTag = cover.tag;
    } else {
        item.parentPrimaryImageItemId = cover.itemId;
        item.parentPrimaryImageTag = cover.tag;
    }
}

MediaItem toMediaItem(const Track &track)
{
    MediaItem item;
    item.id = track.id;
    item.name = track.title;
    item.type = QStringLiteral("Audio");
    applyArtists(item, track.artists);
    if (!track.albumArtists.isEmpty())
        item.albumArtist = track.albumArtists.first().name;
    item.album = track.albumTitle;
    item.albumId = track.albumId;
    item.parentIndexNumber = track.discNumber > 0 ? track.discNumber : -1;
    item.indexNumber = track.trackNumber > 0 ? track.trackNumber : -1;
    item.runtimeTicks = track.runtimeMs * kTicksPerMs;
    item.playbackPositionTicks = track.positionMs * kTicksPerMs;
    item.playCount = track.playCount;
    item.played = track.played;
    item.favorite = track.favourite;
    item.playlistItemId = track.playlistItemId;
    // MediaItem::coverSource() prefers album tag, then parent, then own for
    // Audio; place the ref in the slot that reproduces it.
    if (!track.coverRef.tag.isEmpty() && !track.albumId.isEmpty()
        && track.coverRef.itemId == track.albumId)
        item.albumPrimaryImageTag = track.coverRef.tag;
    else
        applyCover(item, track.coverRef);
    return item;
}

MediaItem toMediaItem(const Album &album)
{
    MediaItem item;
    item.id = album.id;
    item.name = album.title;
    item.type = QStringLiteral("MusicAlbum");
    applyArtists(item, album.albumArtists);
    if (!album.albumArtists.isEmpty())
        item.albumArtist = album.albumArtists.first().name;
    item.productionYear = album.year;
    item.childCount = album.trackCount;
    item.runtimeTicks = album.runtimeMs * kTicksPerMs;
    item.playCount = album.playCount;
    item.favorite = album.favourite;
    applyCover(item, album.coverRef);
    return item;
}

MediaItem toMediaItem(const Artist &artist)
{
    MediaItem item;
    item.id = artist.id;
    item.name = artist.name;
    item.type = QStringLiteral("MusicArtist");
    item.childCount = artist.albumCount;
    item.favorite = artist.favourite;
    applyCover(item, artist.coverRef);
    if (artist.backdropRef.isValid())
        item.backdropImageTags = {artist.backdropRef.tag};
    return item;
}

} // namespace strmqt::music
