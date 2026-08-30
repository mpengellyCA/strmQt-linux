#pragma once

#include "app/models/MediaItemModel.h"
#include "server/dto/ItemsQuery.h"
#include "server/emby/RequestHandle.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <memory>

namespace strmqt {

class PlayerController;

namespace emby {
class EmbyClient;
}

// One implementation of every item verb (ARCHITECTURE.md), shared by cards, context
// menus, the details page, the series page and the player. Before this existed
// DetailsPage.qml and SeriesPage.qml each re-derived the resume position and
// mirrored played/favorite state locally; now they call one place.
//
// Every verb accepts either an item id (QString) or an item map as produced by
// MediaItemModel::get(row). Given a bare id, the item is looked up in the models
// registered with registerModel(); verbs that need more than an id fall back to
// safe behaviour and log when the lookup misses.
//
// Written state (played / favorite) is applied optimistically: the signal fires
// before the request, the registered models are patched through
// MediaItemModel::updateUserData(), and a failed reply rolls the value back and
// emits actionFailed(). Requests coalesce per item id, so a held auto-repeating
// key cannot leave the server and the UI disagreeing.
class ItemActions : public QObject
{
    Q_OBJECT

public:
    ItemActions(emby::EmbyClient *client, PlayerController *player, QObject *parent = nullptr);

    // Models kept in sync on a played/favorite change. Registration is weak: a
    // model that is destroyed drops out on its own.
    Q_INVOKABLE void registerModel(strmqt::MediaItemModel *model);
    Q_INVOKABLE void unregisterModel(strmqt::MediaItemModel *model);

    // Resume when the item is resumable, otherwise start from the beginning.
    // True for item kinds that hold media rather than being media (an album, a
    // series, a collection). Playing one means playing its contents.
    static bool isContainer(const QString &type);

    Q_INVOKABLE void play(const QVariant &item);
    Q_INVOKABLE void playFromStart(const QVariant &item);
    // Resume; falls back to the start when there is no stored position.
    Q_INVOKABLE void resume(const QVariant &item);

    // ── Queue verbs (ARCHITECTURE.md) ─────────────────────────────────────
    // Enqueue straight after whatever is playing; with an empty queue this is
    // simply "play". Both emit queueChanged() so a toast can confirm it.
    Q_INVOKABLE void playNext(const QVariant &item);
    Q_INVOKABLE void addToQueue(const QVariant &item);
    // "Play all" / "Shuffle" on a library, a collection, a series or a season.
    // `collectionType` is the server's library kind ("tvshows", "movies",
    // "music", "boxsets", ...) and only narrows which item types are fetched;
    // an empty string means "anything playable".
    Q_INVOKABLE void playAll(const QString &parentId,
                             const QString &collectionType = QString());
    Q_INVOKABLE void shuffle(const QString &parentId, const QString &collectionType = QString());
    // Shuffle a set the CALLER narrowed. The query arrives with its
    // constraints already applied (MusicController's letter, genres and
    // favourites, for the music library's ▸ Shuffle); what stays owned here is
    // the shuffle itself — the sort is forced to Random so the capped fetch is
    // a fair sample of the whole filtered set, not its first page. C++ only:
    // the caller is a controller holding an ItemsQuery, not a page.
    void shuffleFiltered(const ItemsQuery &query);
    // A collection plays in the order the collection was curated in, so this is
    // NOT playAll() with a different parent: playAll sorts (SortName for mixed
    // types), which queues a franchise alphabetically while the grid above it
    // shows release order. Mixed Series/Season slots are expanded sequentially
    // into episode leaves, so their place is retained without queuing folders.
    Q_INVOKABLE void playCollection(const QString &collectionId);
    // Every episode of a series, in a random order, starting anywhere.
    Q_INVOKABLE void shuffleSeries(const QString &seriesId);
    // Queue items the caller already has (an episode list, a search result set).
    Q_INVOKABLE void playAllFrom(const QVariantList &items, int startIndex = 0);

    // A controller that must resolve a play command asynchronously reserves
    // the same intent sequence used by the verbs above. The eventual commit is
    // accepted only while no newer direct/container play has superseded it.
    quint64 reservePlaybackIntent();
    bool isPlaybackIntentCurrent(quint64 generation) const;
    void playAllFromIfCurrent(const QVariantList &items, int startIndex, quint64 generation);

    // ── Batch verbs (MUSIC.md §7) ─────────────────────────────────────────
    // What a multi-selection in a track table can be done to. Both are the
    // single-item verb applied over a set, in C++ rather than as a loop in QML
    // — a page states intent and never iterates a verb (ARCHITECTURE.md rule 1).
    //
    // addAllToQueue() emits queueChanged() ONCE however many rows were picked:
    // it is one gesture and it deserves one toast, not forty.
    Q_INVOKABLE void addAllToQueue(const QVariantList &items);
    void playNextAll(const QVariantList &items);
    // setFavorite() per id, so each keeps its own optimistic patch, its own
    // rollback and its own per-item coalescing. Nothing is gained by batching
    // the requests — Emby has no bulk favourite endpoint — and a good deal is
    // lost, because one failure would have to roll back forty rows.
    Q_INVOKABLE void setFavoriteAll(const QStringList &itemIds, bool favorite);

    // ── Instant mix (MUSIC.md §7) ─────────────────────────────────────────
    // Artist radio, and the same verb for a track and a record. Measured on
    // Emby 4.9.5.0 — see EmbyClient::instantMix() for what the endpoint does
    // and does not do.
    Q_INVOKABLE void instantMix(const QVariant &item);

    Q_INVOKABLE void setPlayed(const QString &itemId, bool played);
    Q_INVOKABLE void togglePlayed(const QVariant &item);
    Q_INVOKABLE void setFavorite(const QString &itemId, bool favorite);
    Q_INVOKABLE void toggleFavorite(const QVariant &item);

    // Navigation is a request, not a command: C++ never drives the QML stack.
    // Scoped browse views reached from a details page: a genre chip, a cast
    // member, a studio. Verbs live here for the same reason the others do —
    // a page states intent and never pushes anything itself.
    Q_INVOKABLE void browseGenre(const QString &genreId, const QString &name);
    Q_INVOKABLE void browsePerson(const QString &personId, const QString &name);
    Q_INVOKABLE void browseStudio(const QString &studioId, const QString &name);
    Q_INVOKABLE void browseCollection(const QString &collectionId, const QString &name);

    // Semantic item policy. QML supplies presentation context and renders the
    // returned presentation keys; item kinds, capability gates, action order,
    // checked state and dispatch targets stay here.
    Q_INVOKABLE QVariantMap itemCapabilities(const QVariant &item) const;
    Q_INVOKABLE QVariantList itemMenuPolicy(const QVariant &item, bool allowDetails,
                                            bool allowAddToPlaylist, bool allowRemoveFromPlaylist,
                                            bool allowMusicNavigation,
                                            const QString &profile = QString()) const;
    Q_INVOKABLE void performItemVerb(const QString &verb, const QVariant &item);

    Q_INVOKABLE void openDetails(const QVariant &item);
    Q_INVOKABLE void openSeries(const QVariant &item);
    Q_INVOKABLE void openAlbum(const QString &albumId, const QString &name);
    Q_INVOKABLE void openArtist(const QString &artistId, const QString &name);

    // Best effort: Emby's /Items/{id}/Refresh is not on EmbyClient yet, so this
    // logs and returns false rather than pretending to have done something.
    Q_INVOKABLE bool refreshMetadata(const QString &itemId);

    // ── Music navigation (MUSIC.md §4) ────────────────────────────────────
    // Which artist a track or an album leads to: {itemId, name, type} in the
    // shape openDetails() takes, or an empty map when the item credits nobody.
    // `itemId` is empty when the credited artist has no artist item on the
    // server — a name to print, nowhere to go.
    //
    // One rule, in one place, because "go to artist" has to land somewhere the
    // user recognises whether it was asked for from the context menu or from the
    // docked bar's subline.
    Q_INVOKABLE QVariantMap artistTarget(const QVariant &item) const;

    // Best-known user state, including changes not yet confirmed by the server.
    Q_INVOKABLE bool isPlayed(const QString &itemId) const;
    Q_INVOKABLE bool isFavorite(const QString &itemId) const;
    // Only locally changed or externally refreshed rows need a mirror here.
    // Pending mutations are pinned in the same bounded set: admission evicts
    // only settled rows and rejects a new id when every slot is in flight.
    static constexpr int kMaxCachedUserStates = 1024;
    // Test seam: production keeps the ceiling above. Tests shrink it so
    // eviction can be exercised without issuing a thousand server mutations.
    void setUserStateCacheLimitForTests(int maximum);
    int cachedUserStateCountForTests() const { return m_state.size(); }
    int pendingUserStateRequestCountForTests() const
    {
        return m_playedRequests.size() + m_favoriteRequests.size();
    }
    // The item map for an id, or an empty map when no registered model has it.
    Q_INVOKABLE QVariantMap itemFor(const QString &itemId) const;

    // Authentication is an ownership boundary for every cached item and every
    // queue-building continuation. Application calls this before credentials
    // are replaced or cleared; replies from the retired session then become
    // harmless no-ops even if Qt had already queued their continuations.
    void resetSessionState();

signals:
    void playedChanged(const QString &itemId, bool played);
    // Emitted only after the final coalesced played-state request succeeds.
    // Server-filtered consumers must wait for this rather than reacting to the
    // optimistic signal above and racing a GET ahead of the mutation.
    void playedCommitted(const QString &itemId, bool played);
    void favoriteChanged(const QString &itemId, bool favorite);
    // Human-readable reason a verb failed; the UI surfaces it as a toast.
    void actionFailed(const QString &message);
    // The play queue was replaced or added to — the UI confirms with a toast.
    void queueChanged();
    // `kind` is "details" | "series" | "album" | "artist" | "playlist".
    // Every target has itemId/name. Direct Details, MusicAlbum, MusicArtist and
    // Playlist routes retain the complete source payload; synthetic series,
    // audio-album and explicit album/artist targets are minimal coherent maps.
    void routeRequested(const QString &kind, const QVariantMap &target);
    // MusicController owns the server-ordered, non-recursive album expansion.
    // ItemActions still owns whether and when this semantic verb is available.
    void orderedAlbumPlayRequested(const QString &albumId);
    // `kind` is "genre" | "person" | "studio" | "collection"; one signal rather
    // keeps Main.qml to a single handler and the routing in one place.
    void browseRequested(const QString &kind, const QString &id, const QString &name);

private:
    struct CollectionWalk
    {
        QList<MediaItem> members;
        QList<MediaItem> playable;
        int memberIndex = 0;
        int expansionCount = 0;
        quint64 generation = 0;
    };

    // A change that has been shown to the user and is on its way to the server.
    struct InFlight
    {
        bool requested = false; // value the outstanding request carries
        bool baseline = false;  // value to roll back to if it fails
        bool hasQueued = false;
        bool queued = false; // newest value asked for while the request ran
    };

    struct UserState
    {
        bool played = false;
        bool favorite = false;
    };

    QVariantMap resolve(const QVariant &item) const;
    void startPlayback(const QVariant &item, bool fromStart);
    quint64 beginPlaybackIntent();
    // One fetch path for playAll()/shuffle(): runs the query, then hands the
    // page to the player as a queue. `randomStart` picks the first item at
    // random, which is what makes a shuffle of an ordered fetch a real shuffle.
    void fetchIntoQueue(const ItemsQuery &query, bool shuffled, bool randomStart);
    void continueCollectionWalk(const std::shared_ptr<CollectionWalk> &walk);
    bool requireQueueTarget();
    UserState knownState(const QString &itemId) const;
    void rememberState(const QString &itemId, const UserState &state);
    void syncCachedUserState(MediaItemModel *model, int firstRow, int lastRow);
    bool admitUserState(const QString &itemId);
    bool evictOldestSettledUserState();
    bool hasPendingUserState(const QString &itemId) const;
    void applyPlayed(const QString &itemId, bool played);
    void applyFavorite(const QString &itemId, bool favorite);
    void patchModels(const QString &itemId, const UserState &state);
    void sendPlayed(const QString &itemId, bool played, bool baseline);
    void sendFavorite(const QString &itemId, bool favorite, bool baseline);

    emby::EmbyClient *m_client;
    PlayerController *m_player;
    QList<QPointer<MediaItemModel>> m_models;
    QHash<QString, UserState> m_state;
    QList<QString> m_stateOrder;
    int m_userStateCacheLimit = kMaxCachedUserStates;
    QHash<QString, InFlight> m_playedRequests;
    QHash<QString, InFlight> m_favoriteRequests;
    QSet<QString> m_admittingUserStates;
    bool m_patchingModels = false;
    emby::RequestHandle m_collectionRequest;
    quint64 m_playbackIntentGeneration = 0;
    quint64 m_sessionGeneration = 0;
};

} // namespace strmqt
