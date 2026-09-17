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
    if (!track.coverRef.tag.isEmpty()) {
        if (track.coverRef.itemId == track.id) {
            item.primaryImageTag = track.coverRef.tag;
        } else if (!track.albumId.isEmpty() && track.coverRef.itemId == track.albumId) {
            item.albumPrimaryImageTag = track.coverRef.tag;
        } else {
            item.parentPrimaryImageItemId = track.coverRef.itemId;
            item.parentPrimaryImageTag = track.coverRef.tag;
        }
    }
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
    if (album.coverRef.itemId == album.id) {
        item.primaryImageTag = album.coverRef.tag;
    } else if (!album.coverRef.tag.isEmpty()) {
        item.parentPrimaryImageItemId = album.coverRef.itemId;
        item.parentPrimaryImageTag = album.coverRef.tag;
    }
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
    if (artist.coverRef.itemId == artist.id)
        item.primaryImageTag = artist.coverRef.tag;
    if (artist.backdropRef.isValid())
        item.backdropImageTags = {artist.backdropRef.tag};
    return item;
}

} // namespace strmqt::music
