#pragma once

#include "app/models/MediaItemModel.h"
#include "server/emby/RequestHandle.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace strmqt {

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

public:
    explicit DetailsController(emby::EmbyClient *client, QObject *parent = nullptr);
    ~DetailsController() override;

    QString itemId() const { return m_itemId; }
    bool itemLoading() const { return m_itemLoading; }
    QString tagline() const { return m_tagline; }
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

signals:
    void detailsChanged();
    void collectionsChanged();
    void personChanged();
    void similarStatusChanged();

private:
    void cancelRequests();
    void setSimilarLoading(bool loading);

    emby::EmbyClient *m_client;
    MediaItemModel *m_similar;
    QString m_itemId;
    bool m_itemLoading = false;
    QString m_tagline;
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
};

} // namespace strmqt
