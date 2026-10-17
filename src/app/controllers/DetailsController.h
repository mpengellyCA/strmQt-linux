#pragma once

#include "app/models/MediaItemModel.h"
#include "server/emby/RequestHandle.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace strmqt {

class LiveUpdateService;

namespace emby {
class EmbyClient;
}

// Enriches the Details page beyond the browse-model roles: tagline, structured
// genre/cast/crew records, and the "More like this" rail — fetched on page entry.
class DetailsController : public QObject
{
    Q_OBJECT
    // Identity of the item whose detail lanes currently own this controller.
    // Main uses it when restoring an already-live history entry so Back does
    // not clear a populated page and race focus restoration against a needless
    // replacement request.
    Q_PROPERTY(QString itemId READ itemId NOTIFY detailsChanged)
    // True only while the primary detail request for itemId is live. `itemId`
    // remains non-empty after a successful settlement, but is retired on
    // failure or when the shared request lane is handed to a Person page.
    Q_PROPERTY(bool itemLoading READ itemLoading NOTIFY detailsChanged)
    Q_PROPERTY(QString tagline READ tagline NOTIFY detailsChanged)
    // The item's ClearLogo as the details reply describes it (its own, else its
    // series'), or empty. The route's item map carries the same role when it
    // came from a model; a page opened from a bare id has only this.
    Q_PROPERTY(QString logoUrl READ logoUrl NOTIFY detailsChanged)
    // Playable versions of this item, one QVariantMap per MediaSource (the
    // MediaSource::toVariantMap() shape, plus an "index"). A later wave builds
    // the version picker and the media-info panel on top of this; the details
    // page can finally say something about codec/resolution/bitrate *before*
    // playback starts (ARCHITECTURE.md).
    Q_PROPERTY(QVariantList mediaSources READ mediaSources NOTIFY detailsChanged)
    Q_PROPERTY(int mediaSourceCount READ mediaSourceCount NOTIFY detailsChanged)
    // Chapter list, one QVariantMap per chapter {name, startPositionTicks,
    // startMs, imageTag}. Feeds scrubber markers and the chapter panel (D7).
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY detailsChanged)
    // Cast and crew as full records {id, name, role, type, primaryImageTag},
    // server order (billing) preserved. `cast` and `crew` are the same list
    // split, so a page can lay out headshots and credits separately without
    // filtering in QML.
    Q_PROPERTY(QVariantList people READ people NOTIFY detailsChanged)
    Q_PROPERTY(QVariantList cast READ cast NOTIFY detailsChanged)
    Q_PROPERTY(QVariantList crew READ crew NOTIFY detailsChanged)
    // {id, name} pairs, navigable by id (E3 → E1 filtered views).
    Q_PROPERTY(QVariantList genres READ genres NOTIFY detailsChanged)
    Q_PROPERTY(QVariantList studios READ studios NOTIFY detailsChanged)
    // {name, url} as the *server* built them — IMDb, TheMovieDb, TheTVDB, Trakt.
    Q_PROPERTY(QVariantList externalLinks READ externalLinks NOTIFY detailsChanged)
    // Collections this item belongs to, {id, name} (ARCHITECTURE.md). Fetched
    // separately because nothing on the item payload carries it — see
    // ItemsQuery::listItemIds for why this is the only query that answers it.
    Q_PROPERTY(QVariantList collections READ collections NOTIFY collectionsChanged)
    // {name, url} for each trailer the server knows. These are off-site URLs
    // (YouTube on this server), so they open externally rather than in the
    // player — mpv can only take them with yt-dlp installed, which is not a
    // dependency this app declares.
    Q_PROPERTY(QVariantList trailers READ trailers NOTIFY detailsChanged)
    Q_PROPERTY(QString premiereDate READ premiereDate NOTIFY detailsChanged)
    // Person-page fields. Empty for anything that is not a person.
    Q_PROPERTY(QVariantMap person READ person NOTIFY personChanged)
    // 0-100 Rotten-Tomatoes-style score; 0 when the server has none.
    Q_PROPERTY(double criticRating READ criticRating NOTIFY detailsChanged)
    Q_PROPERTY(strmqt::MediaItemModel *similar READ similar CONSTANT)
    // The similar-items request is independent of the primary details reply.
    // Focus restoration must wait for this model's own terminal state rather
    // than guessing from itemLoading.
    Q_PROPERTY(bool similarLoading READ similarLoading NOTIFY similarStatusChanged)
    Q_PROPERTY(strmqt::MediaItemModel *upcomingEpisodes READ upcomingEpisodes CONSTANT)
    Q_PROPERTY(bool upcomingEpisodesLoading READ upcomingEpisodesLoading NOTIFY upcomingEpisodesChanged)
    Q_PROPERTY(QVariantMap nextEpisode READ nextEpisode NOTIFY upcomingEpisodesChanged)
    Q_PROPERTY(bool hasNextEpisode READ hasNextEpisode NOTIFY upcomingEpisodesChanged)
    // User data of the item named by itemId: seeded by the primary details
    // reply, then kept current by live UserDataChanged records and by
    // ItemActions' own changes. The route's item map is a snapshot taken when
    // the page was opened, so without these a resume point moved on a phone
    // never reaches a page that is already showing. All four are the defaults
    // while no item owns the controller.
    Q_PROPERTY(bool played READ played NOTIFY userStateChanged)
    Q_PROPERTY(bool favorite READ favorite NOTIFY userStateChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY userStateChanged)
    Q_PROPERTY(bool resumable READ resumable NOTIFY userStateChanged)

public:
    explicit DetailsController(emby::EmbyClient *client, QObject *parent = nullptr);
    ~DetailsController() override;

    QString itemId() const { return m_itemId; }
    bool itemLoading() const { return m_itemLoading; }
    QString tagline() const { return m_tagline; }
    QString logoUrl() const { return m_logoUrl; }
    QVariantList mediaSources() const { return m_mediaSources; }
    int mediaSourceCount() const { return static_cast<int>(m_mediaSources.size()); }
    QVariantList chapters() const { return m_chapters; }
    QVariantList people() const { return m_people; }
    QVariantList cast() const { return m_cast; }
    QVariantList crew() const { return m_crew; }
    QVariantList genres() const { return m_genres; }
    QVariantList studios() const { return m_studios; }
    QVariantList externalLinks() const { return m_externalLinks; }
    QVariantList collections() const { return m_collections; }
    QVariantList trailers() const { return m_trailers; }
    QString premiereDate() const { return m_premiereDate; }
    QVariantMap person() const { return m_person; }
    double criticRating() const { return m_criticRating; }
    MediaItemModel *similar() const { return m_similar; }
    bool similarLoading() const { return m_similarLoading; }
    MediaItemModel *upcomingEpisodes() const { return m_upcomingEpisodes; }
    bool upcomingEpisodesLoading() const { return m_upcomingEpisodesLoading; }
    QVariantMap nextEpisode() const { return m_nextEpisode; }
    bool hasNextEpisode() const { return !m_nextEpisode.isEmpty(); }
    bool played() const { return m_played; }
    bool favorite() const { return m_favorite; }
    qint64 positionMs() const { return m_positionMs; }
    // MediaItem::isResumable()'s rule, so this and a model row cannot disagree.
    bool resumable() const { return m_positionMs > 0 && !m_played; }

    void bindLiveUpdates(LiveUpdateService *service);
    Q_INVOKABLE void refreshUpcomingEpisodes();

    // What Play and Start over hand ItemActions: the route's item map, with
    // the resume point this controller holds copied over the map's when the
    // controller has answered for that item. ItemActions takes the start
    // position from the map it is given, so a button labelled from the live
    // position over a map that still carries the snapshot taken when the page
    // opened would promise one place and start at another. Any other item, or
    // this one while its reply is still in flight, comes back unchanged.
    Q_INVOKABLE QVariantMap withUserState(const QVariant &item) const;

    void resetSessionState();

    Q_INVOKABLE void load(const QString &itemId);
    // Joins a live or successfully-settled request for the same item. Failed
    // and cancelled ownership is cleared, so the same call becomes a retry.
    Q_INVOKABLE void ensureLoaded(const QString &itemId);
    // A person is an ordinary item to Emby: /Users/{uid}/Items/{personId}
    // returns Overview, PremiereDate (their birth date) and
    // ProductionLocations (their birthplace). Verified on the live server.
    // Loading one through load() would also fire the collections and
    // "more like this" requests, which mean nothing for a person.
    Q_INVOKABLE void loadPerson(const QString &personId);

public slots:
    // ItemActions::playedChanged / favoriteChanged. Only the loaded item's own
    // id matters here; the rails are models ItemActions patches itself.
    void notePlayed(const QString &itemId, bool played);
    void noteFavorite(const QString &itemId, bool favorite);

signals:
    void detailsChanged();
    // played / favorite / positionMs / resumable. Emitted only when one of
    // them actually moved.
    void userStateChanged();
    void collectionsChanged();
    void personChanged();
    void similarStatusChanged();
    void upcomingEpisodesChanged();
    // The details reply's user data, when nothing newer has arrived since it
    // was requested. The loaded item is often in no model ItemActions holds,
    // so without this the page's watched / favourite button and the verb it
    // fires would each fall back to a different snapshot (Application.cpp).
    void itemUserDataLoaded(const QString &itemId, bool played, bool favorite,
                            qint64 positionTicks);

private:
    void cancelRequests();
    void setSimilarLoading(bool loading);
    void onUserDataPatched(const QVariantList &entries);
    void setUserState(bool played, bool favorite, qint64 positionMs);

    emby::EmbyClient *m_client;
    MediaItemModel *m_similar;
    QString m_itemId;
    bool m_itemLoading = false;
    QString m_tagline;
    QString m_logoUrl;
    QVariantList m_mediaSources;
    QVariantList m_chapters;
    QVariantList m_people;
    QVariantList m_cast;
    QVariantList m_crew;
    QVariantList m_genres;
    QVariantList m_studios;
    QVariantList m_externalLinks;
    QVariantList m_collections;
    QVariantList m_trailers;
    QString m_premiereDate;
    QVariantMap m_person;
    double m_criticRating = 0.0;
    bool m_similarLoading = false;
    int m_generation = 0;
    emby::RequestHandle m_detailsRequest;
    emby::RequestHandle m_collectionsRequest;
    emby::RequestHandle m_similarRequest;
    MediaItemModel *m_upcomingEpisodes = nullptr;
    bool m_upcomingEpisodesLoading = false;
    QVariantMap m_nextEpisode;
    bool m_isSeries = false;
    int m_upcomingEpisodesGeneration = 0;
    emby::RequestHandle m_upcomingEpisodesRequest;
    bool m_played = false;
    bool m_favorite = false;
    qint64 m_positionMs = 0;
    // The load generation in which user state last arrived from somewhere
    // newer than that load's own REST reply (a live record, or a change made
    // here). The reply was requested first, so it must not seed over it.
    int m_userStateGeneration = -1;
};

} // namespace strmqt
