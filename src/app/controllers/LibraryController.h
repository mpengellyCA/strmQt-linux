#pragma once

#include "app/models/MediaItemModel.h"
#include "server/dto/ItemsQuery.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace strmqt {

class LiveUpdateService;

namespace emby {
class EmbyClient;
}

// Drives the Library grid page: paged, recursive item listing for one library.
// A single instance backs the page; open() resets it for a new library.
//
// Live updates (ARCHITECTURE.md): a grid the user has paged into must not
// silently snap back to page 0, so a server-side library change does not reload
// the grid. It issues a one-item count probe instead — the same query with
// Limit=1, which costs a single cheap request — and turns the difference against
// the model's totalRecordCount into pendingNewCount for an "N new items"
// affordance. Applying it is an explicit act (applyPending(), or
// autoApplyUpdates while the grid is still on its first page).
//
// UserDataChanged is patched in place and never refetches.
class LibraryController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(strmqt::MediaItemModel *model READ model CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY loadingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(bool updatesPending READ updatesPending NOTIFY pendingChanged)
    Q_PROPERTY(int pendingNewCount READ pendingNewCount NOTIFY pendingChanged)
    Q_PROPERTY(bool autoApplyUpdates READ autoApplyUpdates WRITE setAutoApplyUpdates NOTIFY
                   autoApplyUpdatesChanged)
    // ── Sort and filter (ARCHITECTURE.md) ─────────────────────────────────────
    // Emby's own sort key, e.g. "SortName" | "DateCreated" | "PremiereDate".
    Q_PROPERTY(QString sortBy READ sortBy NOTIFY queryChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending NOTIFY queryChanged)
    // The sort keys that make sense for the *kind* of library on screen:
    // "Date added" belongs everywhere, "Album artist" only in music. One
    // QVariantMap per option, {key, label}.
    Q_PROPERTY(QVariantList availableSorts READ availableSorts NOTIFY scopeChanged)
    // "all" | "unplayed" | "played" | "favorites"
    Q_PROPERTY(QString watchedFilter READ watchedFilter NOTIFY queryChanged)
    // Single letter from the alphabet bar, or empty for no constraint.
    Q_PROPERTY(QString nameStartsWith READ nameStartsWith NOTIFY queryChanged)
    // True when anything narrows the view, so the UI can offer one "clear".
    Q_PROPERTY(bool filtered READ filtered NOTIFY queryChanged)
    // Stable identity for the view on screen, for per-library preferences
    // (ARCHITECTURE.md). A title is not an identity: renaming a library on the
    // server would lose its settings, and two libraries may share a name.
    Q_PROPERTY(QString scopeKey READ scopeKey NOTIFY scopeChanged)

public:
    explicit LibraryController(emby::EmbyClient *client, QObject *parent = nullptr);

    MediaItemModel *model() const { return m_model; }
    QString title() const { return m_title; }
    bool loading() const { return m_loading; }
    bool canLoadMore() const;
    QString errorMessage() const { return m_errorMessage; }
    bool updatesPending() const { return m_hasPending; }
    int pendingNewCount() const { return m_pendingNewCount; }
    bool autoApplyUpdates() const { return m_autoApplyUpdates; }
    void setAutoApplyUpdates(bool autoApply);

    QString sortBy() const { return m_sortBy; }
    bool sortDescending() const { return m_sortDescending; }
    QVariantList availableSorts() const;
    QString watchedFilter() const { return m_watchedFilter; }
    QString nameStartsWith() const { return m_nameStartsWith; }
    bool filtered() const;
    QString scopeKey() const;

    // Each of these re-runs the query from page 0 — a sort or filter change is
    // a new result set, not a modification of the one on screen. They no-op when
    // the value is unchanged so a menu that re-emits on open does not refetch.
    Q_INVOKABLE void setSort(const QString &key, bool descending);
    Q_INVOKABLE void setWatchedFilter(const QString &filter);
    Q_INVOKABLE void setNameStartsWith(const QString &letter);
    Q_INVOKABLE void clearFilters();

    Q_INVOKABLE void open(const QString &libraryId, const QString &title,
                          const QString &collectionType);

    // Scoped browse views reached from a details page (ARCHITECTURE.md).
    // Each is the same grid with one server-side axis pinned, which is why they
    // share this controller instead of growing a page of their own.
    Q_INVOKABLE void openGenre(const QString &genreId, const QString &name);
    Q_INVOKABLE void openPerson(const QString &personId, const QString &name);
    Q_INVOKABLE void openStudio(const QString &studioId, const QString &name);
    // A collection is just a parent, but it must keep the server's own order
    // (a franchise reads in release order, not alphabetically).
    Q_INVOKABLE void openCollection(const QString &collectionId, const QString &name);
    // Favorites is a filter across every library, not a library of its own, so it
    // has no parentId. Without this the UI can only send the user back to Home and
    // hope they find the Favorites rail.
    Q_INVOKABLE void openFavorites();
    Q_INVOKABLE void loadMore();
    // Re-run the current query from page 0. loadMore() cannot serve as a retry:
    // a failed first page leaves the model empty, which zeroes totalRecordCount
    // and makes canLoadMore false, so the only re-fetch QML had was a no-op in
    // exactly the case that needed it.
    Q_INVOKABLE void reload();
    // Take the waiting update: reloads from page 0.
    Q_INVOKABLE void applyPending();
    Q_INVOKABLE void discardPending();

    // Wires every live-update signal this controller cares about.
    void bindLiveUpdates(LiveUpdateService *service);

public slots:
    void onLibraryInvalidated(const QStringList &itemIds);
    void onUserDataPatched(const QVariantList &entries);
    void onUserDataInvalidated(const QStringList &itemIds);

signals:
    void titleChanged();
    // The query moved (sort, filter, letter): the grid is being refilled.
    void queryChanged();
    // The *scope* changed (a different library, genre, person): the set of
    // sensible sort keys may differ.
    void scopeChanged();
    void loadingChanged();
    void errorMessageChanged();
    void pendingChanged();
    void autoApplyUpdatesChanged();

private:
    void fetchPage(int startIndex);
    void probeForUpdates(bool announceEvenWithoutGrowth = false);
    void setLoading(bool loading);
    void setError(const QString &message);
    void resetFor(const QString &title);
    // Clears every scope axis so the next open*() starts from a known state
    // rather than inheriting the last view's genre or person.
    void clearScope();
    void applyQueryChange();
    void setPending(bool pending, int newCount);
    bool hasQuery() const
    {
        return !m_libraryId.isEmpty() || m_favoritesOnly || !m_genreId.isEmpty()
               || !m_personId.isEmpty() || !m_studioId.isEmpty();
    }
    ItemsQuery currentQuery() const;

    emby::EmbyClient *m_client;
    MediaItemModel *m_model;
    QString m_libraryId;
    QString m_title;
    QString m_collectionType;
    // Empty parentId + this flag is the Favorites view; see openFavorites().
    bool m_favoritesOnly = false;
    QString m_genreId;
    QString m_personId;
    QString m_studioId;
    bool m_collectionScope = false;

    QString m_sortBy = QStringLiteral("SortName");
    bool m_sortDescending = false;
    QString m_watchedFilter = QStringLiteral("all");
    QString m_nameStartsWith;
    QString m_errorMessage;
    bool m_loading = false;
    int m_generation = 0;
    bool m_hasPending = false;
    int m_pendingNewCount = 0;
    bool m_autoApplyUpdates = true;
};

} // namespace strmqt
