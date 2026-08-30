#pragma once

#include "core/Result.h"
#include "server/dto/ItemDetails.h"
#include "server/dto/ItemsPage.h"
#include "server/dto/ItemsQuery.h"
#include "server/dto/Library.h"
#include "server/dto/MediaItem.h"
#include "server/dto/PlaybackTicket.h"
#include "server/dto/ServerInfo.h"
#include "server/dto/SessionInfo.h"
#include "server/emby/RequestHandle.h"

#include <QFuture>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPromise>
#include <QUrl>
#include <QUrlQuery>

#include <QNetworkRequest>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace strmqt::emby {

// Raw Emby 4.x REST client (QtNetwork only, PLAN §3.4). Owns the access token for
// the lifetime of the object; persistence is the caller's job (SecretsStore).
// All request methods return QFuture<Result<T>> resolved on this object's thread.
class EmbyClient : public QObject
{
    Q_OBJECT

public:
    explicit EmbyClient(QObject *parent = nullptr);

    QUrl baseUrl() const { return m_baseUrl; }
    // Server and credential changes are hard request boundaries: every reply
    // launched under the prior identity is aborted and retired.
    void setBaseUrl(const QUrl &url);

    // Retire every reply launched so far: their promises resolve as canceled
    // and their continuations do not touch client state. The identity setters
    // do this for themselves when the identity actually changes; call it
    // directly for a boundary that does not change it, such as logging out
    // while an authentication for the same (empty) identity is still in
    // flight — without it that reply lands and adopts a session nobody asked
    // for any more.
    void retireOutstandingRequests();

    // Device identity used in X-Emby-Authorization (stable per install).
    void setDeviceId(const QString &id) { m_deviceId = id; }
    void setDeviceName(const QString &name) { m_deviceName = name; }

    // Restore a previous session without re-authenticating.
    void setSession(const QString &accessToken, const QString &userId);
    QString accessToken() const { return m_accessToken; }
    QString userId() const { return m_userId; }
    bool hasSession() const { return !m_accessToken.isEmpty() && !m_userId.isEmpty(); }

    // GET /System/Info/Public — no auth required.
    QFuture<Result<ServerInfo>> publicSystemInfo();

    // POST /Users/AuthenticateByName. On success the client adopts the new session.
    QFuture<Result<SessionInfo>> authenticateByName(const QString &username,
                                                    const QString &password);

    // GET /Users/{uid}/Views
    QFuture<Result<QList<Library>>> userViews();
    // GET /Users/{uid}/Items
    QFuture<Result<ItemsPage>> items(const ItemsQuery &query, RequestHandle *handle = nullptr);
    // GET /Users/{uid}/Items/Resume
    QFuture<Result<ItemsPage>> resumeItems(int limit = 20);
    // GET /Users/{uid}/Items/Latest (bare array on the wire)
    QFuture<Result<QList<MediaItem>>> latestItems(const QString &parentId = QString(),
                                                  int limit = 20);
    // GET /Shows/NextUp
    QFuture<Result<ItemsPage>> nextUp(int limit = 20);

    // The episode that follows `episodeId` in series order, across a season
    // boundary. Uses StartItemId, which returns the list FROM that episode
    // onward, so the answer is the second row.
    //
    // Deliberately not AdjacentTo: this server ignores it silently and returns
    // the entire series (measured: 151 episodes), which would have looked like
    // a working query returning the wrong answer.
    QFuture<Result<QList<MediaItem>>> nextEpisode(const QString &seriesId,
                                                  const QString &episodeId);

    // GET /Shows/{seriesId}/Seasons and /Shows/{seriesId}/Episodes
    QFuture<Result<ItemsPage>> seasons(const QString &seriesId);
    QFuture<Result<ItemsPage>> episodes(const QString &seriesId, const QString &seasonId);

    // GET /Users/{uid}/Items/{id} — full payload for the Details page.
    QFuture<Result<ItemDetails>> itemDetails(const QString &itemId,
                                             RequestHandle *handle = nullptr);
    // GET /Persons and /Genres. Both take a SearchTerm and both answer with an
    // ItemsPage whose **TotalRecordCount is 0 even when Items is populated** —
    // measured on 4.9.5.0. Anything that pages on that count renders nothing,
    // so callers must use the returned list's own size.
    // GET /Artists and /Artists/AlbumArtists. Different endpoints, not a filter
    // on one: measured on the target server they return 3,789 and 2,394.
    //
    // They take an ItemsQuery rather than a bare parent/page because they honour
    // the same narrowing axes /Users/{uid}/Items does — measured live on 4.9.5.0:
    // SortBy, SortOrder, NameStartsWith and GenreIds all changed the answer
    // (NameStartsWith=T cut 2,394 album artists to 75; one genre id cut it to 2).
    // MusicController nevertheless sends its letter as NameStartsWithOrGreater +
    // NameLessThan: NameStartsWith's LIKE scan outlasted the 15 s transfer
    // timeout on this library (see ItemsQuery).
    //
    // `filters` is sent as well, and it is the one axis on the query that is
    // **not** measured. The test account has zero favourites of any music kind,
    // so Filters=IsFavorite answering 0 rows is indistinguishable from the
    // parameter being dropped on the floor, and settling it would mean
    // favouriting somebody's artist — a write to the user's library to answer a
    // documentation question. Sent rather than stripped because the two ways of
    // being wrong are not symmetric: if the endpoint honours it, the Favourites
    // toggle works on the Artists tab; if it ignores it, the toggle is a no-op
    // there either way, and stripping it would guarantee what is currently only
    // suspected. Measure it (Filters=IsFolder against Filters=IsNotFolder is a
    // decisive read-only probe — a MusicArtist is a folder) before relying on it.
    //
    // Everything else on the query — parents other than ParentId, item-type and
    // person axes — is simply not sent.
    QFuture<Result<ItemsPage>> musicArtists(const ItemsQuery &query);
    QFuture<Result<ItemsPage>> albumArtists(const ItemsQuery &query);

    QFuture<Result<QList<MediaItem>>> persons(const QString &searchTerm, int limit = 12,
                                              RequestHandle *handle = nullptr);
    QFuture<Result<QList<MediaItem>>> genres(const QString &searchTerm, int limit = 12,
                                             RequestHandle *handle = nullptr);

    // GET /MusicGenres — the genres that actually occur in a music library.
    //
    // Not genres() with a different path: that one is the search typeahead and
    // takes a SearchTerm with no parent and no paging, so it can suggest a
    // genre but cannot enumerate one library's. This takes ParentId and pages,
    // which is what a filter control needs.
    //
    // Measured on the live 4.9.5.0 server: ParentId IS honoured here (the music
    // library answered 289 genres; the film, TV and collection libraries and a
    // bogus id all answered 0), and — unlike /Genres — TotalRecordCount is
    // truthful. Callers still page on the returned array's own size, because a
    // StartIndex past the end answers TotalRecordCount = 0 with an empty array,
    // and because the /Genres trap (ARCHITECTURE.md §2) is one server upgrade
    // away from applying here too.
    QFuture<Result<ItemsPage>> musicGenres(const QString &parentId, int startIndex = 0,
                                           int limit = 200);

    // GET /Items/{id}/Similar — "More like this".
    QFuture<Result<QList<MediaItem>>> similar(const QString &itemId, int limit = 12,
                                              RequestHandle *handle = nullptr);

    // ── GET /Items/{id}/InstantMix — a radio station built from one item ──
    // Measured on Emby 4.9.5.0 against the target library, and every line of
    // this is a measurement rather than a reading of the API:
    //
    //  · **One endpoint serves all three seeds.** A track, an album and an
    //    *artist* item id all answer 200 with `{Items, TotalRecordCount}` of
    //    `Audio` rows, and the artist answer is genuinely artist-shaped — an
    //    Xbox-era rock artist returned Led Zeppelin, Nirvana and Pink Floyd
    //    while a Japanese vocalist returned Mizuki Nana and yanaginagi.
    //  · **`/Artists/InstantMix` is not a second thing to call.** It takes an
    //    `Id`, not a name: `Name=angela` answers **HTTP 500, "Unrecognized Guid
    //    format."**, and so does omitting the id. Given the id it answers the
    //    same shape as `/Items/{artistId}/InstantMix`, so there is one verb here
    //    and not two (ARCHITECTURE.md rule 3).
    //  · **A track seed comes back as item 0 and does not count against the
    //    limit.** `Limit=50` on a track returned 51 rows, `Limit=500` returned
    //    501; an artist or album seed is not itself audio and returns exactly
    //    `Limit`. That is the right shape for "play this, then things like it"
    //    and needs no special-casing.
    //  · **It does not page, and TotalRecordCount is not a total.** The count
    //    always equals the array's own size, and `StartIndex=5` answered a
    //    fresh randomised set rather than the sixth row onward — the same trap
    //    as SortBy=Random (ARCHITECTURE.md §2). One mix is one request.
    //  · **The rows are not distinct.** 500 asked for came back as 493 unique
    //    ids. Callers de-duplicate; `PlayQueue` would happily hold the repeat,
    //    but hearing the same song twice in a radio station reads as a bug.
    //  · The default limit, when none is sent, is 200.
    QFuture<Result<ItemsPage>> instantMix(const QString &itemId, int limit = 200);

    // POST (or DELETE) /Users/{uid}/PlayedItems/{id} and .../FavoriteItems/{id}
    QFuture<Result<bool>> setPlayed(const QString &itemId, bool played);
    QFuture<Result<bool>> setFavorite(const QString &itemId, bool favorite);

    // POST /Items/{id}/Refresh — ask the server to re-read metadata and images
    // for an item. `recursive` extends it to children (a series' seasons and
    // episodes). The two modes are Emby's MetadataRefreshMode / ImageRefreshMode
    // vocabulary: "None" | "ValidationOnly" | "Default" | "FullRefresh".
    // The server answers 204 and does the work in the background, so a success
    // here means "accepted", not "finished".
    QFuture<Result<bool>> refreshMetadata(
        const QString &itemId, bool recursive = false,
        const QString &metadataRefreshMode = QStringLiteral("FullRefresh"),
        const QString &imageRefreshMode = QStringLiteral("FullRefresh"));

    // POST /Items/{id}/PlaybackInfo with our DeviceProfile → ordered stream ladder.
    // Quality preferences applied to the DeviceProfile sent with PlaybackInfo
    // (ARCHITECTURE.md). Set by the app from Settings; the client itself has no
    // opinion about them.
    //   maxBitrateKbps  0 = uncapped. NOT the same as a huge number: a cap
    //                   makes the server transcode, so asking it to transcode
    //                   to 200 Mbps is worse than not asking.
    //   mode            "auto" | "directPlay" | "transcode"
    void setQualityPreferences(int maxBitrateKbps, const QString &mode);

    QFuture<Result<PlaybackTicket>> playbackInfo(const QString &itemId,
                                                 qint64 startPositionTicks = 0);

    // ── Playlists (ARCHITECTURE.md) ──────────────────────────────────────────
    // Members come back with a PlaylistItemId per row: the ENTRY key, not the
    // item id. The same track can appear twice in a playlist, so removal and
    // reordering address entries, exactly as PlayQueue does.
    QFuture<Result<ItemsPage>> playlistItems(const QString &playlistId, int startIndex = 0,
                                             int limit = 200);
    // POST /Playlists — returns the new playlist's id.
    QFuture<Result<QString>> createPlaylist(const QString &name, const QStringList &itemIds,
                                            const QString &mediaType = QString());
    // POST /Playlists/{id}/Items
    QFuture<Result<bool>> addToPlaylist(const QString &playlistId, const QStringList &itemIds);
    // DELETE /Playlists/{id}/Items — takes ENTRY ids (PlaylistItemId).
    QFuture<Result<bool>> removeFromPlaylist(const QString &playlistId,
                                             const QStringList &entryIds);
    // POST /Items/{id} — Emby's UpdateItem. There is no rename endpoint, so a
    // rename is a full-item update: fetch the item with an explicit full Fields
    // list (anything the GET omits would be cleared by the write-back), change
    // Name, post it back. Anything less than the whole object risks clearing
    // fields the server treats as absent-means-empty.
    QFuture<Result<bool>> renameItem(const QString &itemId, const QString &name);
    // DELETE /Items/{id}. Irreversible on the server; callers must confirm.
    QFuture<Result<bool>> deleteItem(const QString &itemId);

    // POST /Playlists/{id}/Items/{entryId}/Move/{newIndex}
    QFuture<Result<bool>> movePlaylistItem(const QString &playlistId, const QString &entryId,
                                           int newIndex);

    // POST /Sessions/Capabilities/Full — tells the server what this client can
    // be asked to do. Until this is sent, /Sessions reports the session with
    // SupportsRemoteControl=false and zero SupportedCommands, so no other Emby
    // app will even offer it as a target (verified against the live server:
    // StrmQt showed 0 commands where Emby Web showed 39).
    QFuture<Result<bool>> reportCapabilities(const QStringList &commands,
                                             bool supportsMediaControl);

    // POST /Sessions/Playing[/Progress|/Stopped] — playback state reports.
    QFuture<Result<bool>> reportPlaybackStart(const PlaybackProgress &progress);
    QFuture<Result<bool>> reportPlaybackProgress(const PlaybackProgress &progress);
    QFuture<Result<bool>> reportPlaybackStopped(const PlaybackProgress &progress);

    // Synchronous URL builder for the image pipeline (M2 wires it to ImageProvider).
    QUrl imageUrl(const QString &itemId, const QString &imageType, int maxWidth,
                  const QString &tag = QString()) const;

signals:
    // Base URL/user/token jointly define the privacy boundary for caches and
    // other retained presentation state.
    void identityChanged();

private:
    struct RequestContext
    {
        QUrl baseUrl;
        QString deviceId;
        QString deviceName;
        QString accessToken;
        QString userId;
        quint64 epoch = 0;
    };

    RequestContext requestContext() const;
    QNetworkReply *startGet(const QString &path, const QUrlQuery &query);
    QNetworkReply *startGet(const QString &path, const QUrlQuery &query,
                            const RequestContext &context);
    QNetworkReply *startPost(const QString &path, const QJsonObject &body);
    QNetworkReply *startPost(const QString &path, const QJsonObject &body,
                             const RequestContext &context);
    // Emby's action endpoints take their arguments in the query string and an
    // empty body. Deliberately not an overload of startPost(): `startPost(p, {})`
    // would become ambiguous at every existing call site.
    QNetworkReply *startPostQuery(const QString &path, const QUrlQuery &query);
    QNetworkReply *startDelete(const QString &path);
    QNetworkRequest baseRequest(const QUrl &url) const;
    QNetworkRequest baseRequest(const QUrl &url, const RequestContext &context) const;
    // For endpoints whose response body we ignore: ok ⇔ HTTP success.
    QFuture<Result<bool>> finishStatus(QNetworkReply *reply);
    QUrl requestUrl(const QString &path, const QUrlQuery &query) const;
    QUrl requestUrl(const QString &path, const QUrlQuery &query,
                    const RequestContext &context) const;

    // Resolves the reply into Result<T> via parse(QJsonDocument) once finished.
    QFuture<Result<QJsonDocument>>
    finishDocument(QNetworkReply *reply, RequestHandle *handle = nullptr,
                   std::shared_ptr<RequestHandle::State> *cancellationOut = nullptr);
    template<class T>
    QFuture<Result<T>> finishJson(QNetworkReply *reply,
                                  std::function<Result<T>(const QJsonDocument &)> parse,
                                  RequestHandle *handle = nullptr);
    template<class T> static QFuture<Result<T>> failedFuture(const QString &error);

    QUrl m_baseUrl;
    QString m_deviceId;
    QString m_deviceName;
    QString m_accessToken;
    QString m_userId;
    QNetworkAccessManager *m_nam = nullptr;
    int m_maxBitrateKbps = 0;
    QString m_playbackMode = QStringLiteral("auto");
    quint64 m_requestEpoch = 0;
};

} // namespace strmqt::emby
