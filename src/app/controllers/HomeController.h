#pragma once

#include "app/models/LibraryListModel.h"
#include "app/models/MediaItemModel.h"
#include "server/dto/Library.h"
#include "server/dto/MediaItem.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

namespace strmqt {

class LiveUpdateService;

namespace emby {
class EmbyClient;
}

// Feeds the Home page: Continue Watching, Next Up, per-library Latest rails, and
// the library list itself.
//
// Live updates (ARCHITECTURE.md): a refresh always fetches into a staging
// snapshot and only then decides whether to swap it in. A user-driven refresh
// applies straight away; a server-driven one applies only when the page says it
// is safe to (autoApplyUpdates), otherwise the snapshot is held and
// pendingNewCount tells the UI how many items are waiting behind an
// "N new items" affordance. Content never reorders under the cursor.
//
// A UserDataChanged patches visible fields immediately, then reconciles the
// server-owned membership of Continue Watching / Next Up / Favorites.
class HomeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(strmqt::MediaItemModel *resume READ resume CONSTANT)
    Q_PROPERTY(strmqt::MediaItemModel *nextUp READ nextUp CONSTANT)
    Q_PROPERTY(strmqt::MediaItemModel *favorites READ favorites CONSTANT)
    Q_PROPERTY(strmqt::LibraryListModel *libraries READ libraries CONSTANT)
    // List of { title: string, model: MediaItemModel* } for the Latest rails.
    Q_PROPERTY(QVariantList latestRails READ latestRails NOTIFY latestRailsChanged)
    // List of { title, genreId, model } for the genre rails (ARCHITECTURE.md).
    // Fetched AFTER the first snapshot applies: Home is the front door and
    // genre rails sit below the fold, so they must not delay first paint.
    // genreId is the /Genres item id LibraryController::openGenre takes.
    Q_PROPERTY(QVariantList genreRails READ genreRails NOTIFY genreRailsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    // Fresh content is fetched and waiting to be swapped in.
    Q_PROPERTY(bool updatesPending READ updatesPending NOTIFY pendingChanged)
    // How many item ids in the waiting snapshot are not on screen yet — the
    // number behind "3 new items".
    Q_PROPERTY(int pendingNewCount READ pendingNewCount NOTIFY pendingChanged)
    // QML sets this false while the user is scrolled away from the top or is
    // interacting with a rail; true (the default) lets updates land silently.
    Q_PROPERTY(bool autoApplyUpdates READ autoApplyUpdates WRITE setAutoApplyUpdates NOTIFY
                   autoApplyUpdatesChanged)

public:
    explicit HomeController(emby::EmbyClient *client, QObject *parent = nullptr);

    MediaItemModel *resume() const { return m_resume; }
    MediaItemModel *nextUp() const { return m_nextUp; }
    MediaItemModel *favorites() const { return m_favorites; }
    LibraryListModel *libraries() const { return m_libraries; }
    QVariantList latestRails() const { return m_latestRails; }
    QVariantList genreRails() const { return m_genreRails; }
    bool busy() const { return m_pending > 0; }
    QString errorMessage() const { return m_errorMessage; }
    bool updatesPending() const { return m_hasPending; }
    int pendingNewCount() const { return m_pendingNewCount; }
    bool autoApplyUpdates() const { return m_autoApplyUpdates; }
    void setAutoApplyUpdates(bool autoApply);

    // User-driven refresh: applies as soon as it lands.
    Q_INVOKABLE void refresh();
    // Swap a held snapshot in. No-op when nothing is pending.
    Q_INVOKABLE void applyPending();
    // Throw a held snapshot away (the user dismissed the pill).
    Q_INVOKABLE void discardPending();

    // Fire-and-forget toggles; models update optimistically on success.
    Q_INVOKABLE void togglePlayed(const QString &itemId, bool played, bool favorite);
    Q_INVOKABLE void toggleFavorite(const QString &itemId, bool played, bool favorite);

    // Wires every live-update signal this controller cares about. Application
    // may call this instead of connecting the slots below by hand.
    void bindLiveUpdates(LiveUpdateService *service);

public slots:
    // Server said these items changed (empty = everything). Fetches into a
    // staging snapshot; dropped while a refresh is already in flight, because
    // that refresh will deliver the same data.
    void onLibraryInvalidated(const QStringList &itemIds);
    // In-place user-data patch; entries as emitted by
    // LiveUpdateService::userDataPatched.
    void onUserDataPatched(const QVariantList &entries);
    void onUserDataInvalidated(const QStringList &itemIds);

public:
    // Minimum spacing between membership refreshes driven by user-data
    // invalidation. Tests drive the burst behaviour directly; 0 disables it.
    void setUserDataRefreshFloorMsForTests(int ms) { m_userDataRefreshFloorMs = ms; }

signals:
    void latestRailsChanged();
    void genreRailsChanged();
    void busyChanged();
    void errorMessageChanged();
    void pendingChanged();
    void autoApplyUpdatesChanged();

private:
    void fetchGenreRails();

public:
    // Asked for by the Home page rather than fired from refresh(). Genre rails
    // sit below the fold, so a user who never scrolls should not pay for them —
    // and refresh() runs on every live update, where spontaneously issuing a
    // fetch is indistinguishable from a refetch (tst_live_updates asserts
    // exactly that). Idempotent: only the first call does anything.
    Q_INVOKABLE void loadGenreRails();

private:
    // Everything one refresh fetched, before it is allowed on screen.
    struct Snapshot
    {
        QList<MediaItem> resume;
        QList<MediaItem> nextUp;
        QList<MediaItem> favorites;
        int resumeTotal = 0;
        int nextUpTotal = 0;
        int favoritesTotal = 0;
        QList<Library> libraries;
        // Ordered as `libraries`, one entry per library that earns a rail.
        QList<QPair<Library, QList<MediaItem>>> rails;
        bool haveLibraries = false;
    };

    void startRefresh(bool applyWhenReady);
    void finishRefresh();
    void applySnapshot(const Snapshot &snapshot);
    int countNewItems(const Snapshot &snapshot) const;
    void setPending(bool pending, int newCount);
    void beginRequest();
    void endRequest();
    void setError(const QString &message);
    void updateAllModels(const QString &itemId, bool played, bool favorite);
    void updateAllModels(const QString &itemId, bool played, bool favorite,
                         qint64 positionTicks, int playCount);
    MediaItemModel *railModelFor(const QString &libraryId);

    emby::EmbyClient *m_client;
    QElapsedTimer m_lastUserDataRefresh;
    int m_userDataRefreshFloorMs = 30'000;
    bool m_userDataRefreshQueued = false;
    MediaItemModel *m_resume;
    MediaItemModel *m_nextUp;
    MediaItemModel *m_favorites;
    LibraryListModel *m_libraries;
    QVariantList m_latestRails;
    // Rail models live as long as their library does, keyed by library id, so a
    // refresh does not invalidate the pointers QML and ItemActions hold.
    QHash<QString, MediaItemModel *> m_railModels;
    // Keyed by genre id and outliving a refresh, so QML and ItemActions
    // pointers stay valid across one.
    QHash<QString, MediaItemModel *> m_genreModels;
    QVariantList m_genreRails;
    bool m_genreRailsFetched = false;
    QString m_errorMessage;
    int m_pending = 0;
    int m_generation = 0; // invalidates in-flight replies across refreshes

    Snapshot m_incoming;      // the refresh currently in flight
    Snapshot m_held;          // fetched, waiting for applyPending()
    bool m_applyWhenReady = true;
    bool m_hasPending = false;
    int m_pendingNewCount = 0;
    bool m_autoApplyUpdates = true;
};

} // namespace strmqt
