#include "LibraryController.h"

#include "app/controllers/LiveUpdateService.h"
#include "app/models/MediaItemModel.h"
#include "core/Log.h"
#include "server/emby/EmbyClient.h"

#include <QVariantMap>

namespace strmqt {

namespace {

constexpr int kPageSize = 100;

// The item kinds a poster grid can actually render. Any view that is not scoped
// to one library (favourites, a genre, a person, a studio) spans everything the
// server holds, so without this a folder or a playlist lands in the tiles.
const QStringList kBrowsableTypes{QStringLiteral("Movie"), QStringLiteral("Series"),
                                  QStringLiteral("Episode"), QStringLiteral("BoxSet")};

// Grid contents per library kind: leaf-ish types the user actually browses.
QStringList itemTypesFor(const QString &collectionType)
{
    if (collectionType == QLatin1String("movies"))
        return {QStringLiteral("Movie")};
    if (collectionType == QLatin1String("tvshows"))
        return {QStringLiteral("Series")};
    if (collectionType == QLatin1String("boxsets"))
        return {QStringLiteral("BoxSet")};
    // A music library browses by ALBUM. Listing Audio would put 56,283 rows
    // (measured on the target server) behind one grid, which is a file
    // listing, not a library.
    if (collectionType == QLatin1String("music"))
        return {QStringLiteral("MusicAlbum")};
    return {}; // unclassified: let the server return whatever the folder holds
}

} // namespace

LibraryController::LibraryController(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_model(new MediaItemModel(this))
{
}

void LibraryController::bindLiveUpdates(LiveUpdateService *service)
{
    if (!service)
        return;
    connect(service, &LiveUpdateService::refreshRequested, this, &LibraryController::reload);
    connect(service, &LiveUpdateService::libraryInvalidated, this,
            &LibraryController::onLibraryInvalidated);
    connect(service, &LiveUpdateService::userDataPatched, this,
            &LibraryController::onUserDataPatched);
}

bool LibraryController::canLoadMore() const
{
    return !m_loading && m_model->rowCount() < m_model->totalRecordCount();
}

void LibraryController::setAutoApplyUpdates(bool autoApply)
{
    if (m_autoApplyUpdates == autoApply)
        return;
    m_autoApplyUpdates = autoApply;
    emit autoApplyUpdatesChanged();
    if (m_autoApplyUpdates && m_hasPending)
        applyPending();
}

void LibraryController::resetFor(const QString &title)
{
    ++m_generation;
    m_title = title;
    emit titleChanged();
    setError(QString());
    setPending(false, 0);
    m_model->clear();
}

ItemsQuery LibraryController::currentQuery() const
{
    ItemsQuery query;
    query.parentId = m_libraryId;
    // A collection lists its direct members; recursing would pull in every
    // episode of a series that happens to be in it.
    query.recursive = !m_collectionScope;
    query.sortBy = m_sortBy;
    query.sortDescending = m_sortDescending;
    query.includeItemTypes = itemTypesFor(m_collectionType);
    if (m_favoritesOnly) {
        // Server-side filter across every library. Restricted to the types a grid
        // can actually render, so a favourited folder does not land in the tiles.
        query.filters = {QStringLiteral("IsFavorite")};
        query.includeItemTypes = kBrowsableTypes;
    }
    // A genre/person/studio view spans every library, so it carries no parentId
    // and needs the same type restriction favourites does.
    if (!m_genreId.isEmpty()) {
        query.genreIds = {m_genreId};
        query.includeItemTypes = kBrowsableTypes;
    }
    if (!m_personId.isEmpty()) {
        query.personIds = {m_personId};
        query.includeItemTypes = kBrowsableTypes;
    }
    if (!m_studioId.isEmpty()) {
        query.studioIds = {m_studioId};
        query.includeItemTypes = kBrowsableTypes;
    }

    // The watched filter is orthogonal to the scope, so it appends rather than
    // replaces — "favourites, unplayed" is a legitimate view.
    if (m_watchedFilter == QLatin1String("unplayed"))
        query.filters.append(QStringLiteral("IsUnplayed"));
    else if (m_watchedFilter == QLatin1String("played"))
        query.filters.append(QStringLiteral("IsPlayed"));
    else if (m_watchedFilter == QLatin1String("favorites")
             && !query.filters.contains(QStringLiteral("IsFavorite")))
        query.filters.append(QStringLiteral("IsFavorite"));

    query.nameStartsWith = m_nameStartsWith;
    query.fields = {QStringLiteral("Overview")};
    return query;
}

QVariantList LibraryController::availableSorts() const
{
    const auto option = [](const QString &key, const QString &label) {
        QVariantMap map;
        map.insert(QStringLiteral("key"), key);
        map.insert(QStringLiteral("label"), label);
        return QVariant::fromValue(map);
    };

    QVariantList sorts{
        option(QStringLiteral("SortName"), tr("Name")),
        option(QStringLiteral("DateCreated"), tr("Date added")),
        option(QStringLiteral("DatePlayed"), tr("Date played")),
        option(QStringLiteral("PremiereDate"), tr("Release date")),
        option(QStringLiteral("CommunityRating"), tr("Community rating")),
        option(QStringLiteral("Runtime"), tr("Runtime")),
        option(QStringLiteral("Random"), tr("Random")),
    };
    // Critic scores exist for films and effectively nothing else, and an option
    // that silently sorts by a field the whole library leaves at 0 reads as a
    // broken sort rather than an empty one.
    if (m_collectionType == QLatin1String("movies") || m_collectionType.isEmpty())
        sorts.insert(5, option(QStringLiteral("CriticRating"), tr("Critic rating")));
    if (m_collectionType == QLatin1String("music"))
        sorts.append(option(QStringLiteral("AlbumArtist"), tr("Album artist")));
    return sorts;
}

QString LibraryController::scopeKey() const
{
    if (m_collectionScope)
        return QStringLiteral("collection:%1").arg(m_libraryId);
    if (!m_libraryId.isEmpty())
        return m_libraryId;
    if (m_favoritesOnly)
        return QStringLiteral("favorites");
    // Kind-scoped, because the id spaces are unrelated and do overlap: genre
    // 8122 and person 8122 are different destinations.
    if (!m_genreId.isEmpty())
        return QStringLiteral("genre:%1").arg(m_genreId);
    if (!m_personId.isEmpty())
        return QStringLiteral("person:%1").arg(m_personId);
    if (!m_studioId.isEmpty())
        return QStringLiteral("studio:%1").arg(m_studioId);
    return {};
}

bool LibraryController::filtered() const
{
    return m_watchedFilter != QLatin1String("all") || !m_nameStartsWith.isEmpty();
}

void LibraryController::setSort(const QString &key, bool descending)
{
    if (key.isEmpty() || (m_sortBy == key && m_sortDescending == descending))
        return;
    m_sortBy = key;
    m_sortDescending = descending;
    applyQueryChange();
}

void LibraryController::setWatchedFilter(const QString &filter)
{
    const QString wanted = filter.isEmpty() ? QStringLiteral("all") : filter;
    if (m_watchedFilter == wanted)
        return;
    m_watchedFilter = wanted;
    applyQueryChange();
}

void LibraryController::setNameStartsWith(const QString &letter)
{
    // The bar toggles: tapping the active letter clears it rather than
    // re-running the identical query.
    const QString wanted = (letter == m_nameStartsWith) ? QString() : letter;
    if (m_nameStartsWith == wanted)
        return;
    m_nameStartsWith = wanted;
    applyQueryChange();
}

void LibraryController::clearFilters()
{
    if (!filtered())
        return;
    m_watchedFilter = QStringLiteral("all");
    m_nameStartsWith.clear();
    applyQueryChange();
}

void LibraryController::applyQueryChange()
{
    emit queryChanged();
    if (!hasQuery())
        return; // a preference set before anything was opened
    resetFor(m_title);
    fetchPage(0);
}

void LibraryController::clearScope()
{
    m_libraryId.clear();
    m_collectionType.clear();
    m_favoritesOnly = false;
    m_genreId.clear();
    m_personId.clear();
    m_studioId.clear();
    m_collectionScope = false;
    // A narrowed view is not carried into the next one: arriving at "Tom
    // Holland" still filtered to unplayed titles beginning with Q looks broken.
    m_nameStartsWith.clear();
    m_watchedFilter = QStringLiteral("all");
}

void LibraryController::open(const QString &libraryId, const QString &title,
                             const QString &collectionType)
{
    clearScope();
    m_libraryId = libraryId;
    m_collectionType = collectionType;
    // Sort is per-kind, not global: a music library defaulting to SortName is
    // right, a "recently added" TV view is not what a name sort gives you.
    m_sortBy = QStringLiteral("SortName");
    m_sortDescending = false;
    emit scopeChanged();
    emit queryChanged();
    resetFor(title);
    fetchPage(0);
}

void LibraryController::openFavorites()
{
    clearScope();
    m_favoritesOnly = true;
    emit scopeChanged();
    emit queryChanged();
    resetFor(tr("Favorites"));
    fetchPage(0);
}

void LibraryController::openGenre(const QString &genreId, const QString &name)
{
    if (genreId.isEmpty())
        return;
    clearScope();
    m_genreId = genreId;
    emit scopeChanged();
    emit queryChanged();
    resetFor(name);
    fetchPage(0);
}

void LibraryController::openPerson(const QString &personId, const QString &name)
{
    if (personId.isEmpty())
        return;
    clearScope();
    m_personId = personId;
    // Someone's filmography reads as a career, so it opens newest-first rather
    // than alphabetically.
    m_sortBy = QStringLiteral("PremiereDate");
    m_sortDescending = true;
    emit scopeChanged();
    emit queryChanged();
    resetFor(name);
    fetchPage(0);
}

void LibraryController::openCollection(const QString &collectionId, const QString &name)
{
    if (collectionId.isEmpty())
        return;
    clearScope();
    m_libraryId = collectionId;
    m_collectionScope = true;
    // BoxSet members are ordered by the server; SortName would scatter a
    // franchise alphabetically ("Ace Ventura: When Nature Calls" before
    // "...Pet Detective").
    m_sortBy.clear();
    m_sortDescending = false;
    emit scopeChanged();
    emit queryChanged();
    resetFor(name);
    fetchPage(0);
}

void LibraryController::openStudio(const QString &studioId, const QString &name)
{
    if (studioId.isEmpty())
        return;
    clearScope();
    m_studioId = studioId;
    m_sortBy = QStringLiteral("PremiereDate");
    m_sortDescending = true;
    emit scopeChanged();
    emit queryChanged();
    resetFor(name);
    fetchPage(0);
}

void LibraryController::reload()
{
    if (!hasQuery())
        return; // nothing has been opened yet
    resetFor(m_title);
    fetchPage(0);
}

void LibraryController::loadMore()
{
    if (canLoadMore())
        fetchPage(m_model->rowCount());
}

void LibraryController::onLibraryInvalidated(const QStringList &itemIds)
{
    Q_UNUSED(itemIds); // the grid is a server-side query; ids do not narrow it
    if (!hasQuery() || m_loading)
        return;

    // Reloading a grid the user has paged into would throw their position away,
    // so auto-apply is limited to a grid still showing its first page.
    if (m_autoApplyUpdates && m_model->rowCount() <= kPageSize) {
        reload();
        return;
    }
    probeForNewItems();
}

void LibraryController::probeForNewItems()
{
    const int generation = m_generation;
    const int knownTotal = m_model->totalRecordCount();

    ItemsQuery query = currentQuery();
    query.fields.clear(); // the count is all we want
    query.startIndex = 0;
    query.limit = 1;

    m_client->items(query).then(
        this, [this, generation, knownTotal](const Result<ItemsPage> &result) {
            if (generation != m_generation || !result.ok())
                return;
            const int delta = result.value.totalRecordCount - knownTotal;
            if (delta <= 0) {
                // Items were removed or only edited: nothing new to announce, and
                // nothing worth yanking the grid for.
                return;
            }
            setPending(true, delta);
        });
}

void LibraryController::onUserDataPatched(const QVariantList &entries)
{
    for (const QVariant &value : entries) {
        const QVariantMap entry = value.toMap();
        const QString itemId = entry.value(QStringLiteral("itemId")).toString();
        if (itemId.isEmpty())
            continue;
        m_model->updateUserData(itemId, entry.value(QStringLiteral("played")).toBool(),
                                entry.value(QStringLiteral("favorite")).toBool());
    }
}

void LibraryController::applyPending()
{
    if (!m_hasPending)
        return;
    setPending(false, 0);
    reload();
}

void LibraryController::discardPending()
{
    setPending(false, 0);
}

void LibraryController::setPending(bool pending, int newCount)
{
    if (m_hasPending == pending && m_pendingNewCount == newCount)
        return;
    m_hasPending = pending;
    m_pendingNewCount = newCount;
    emit pendingChanged();
}

void LibraryController::fetchPage(int startIndex)
{
    setLoading(true);
    const int generation = m_generation;

    ItemsQuery query = currentQuery();
    query.startIndex = startIndex;
    query.limit = kPageSize;

    m_client->items(query).then(
        this, [this, generation, startIndex](const Result<ItemsPage> &result) {
            if (generation != m_generation)
                return; // a newer open() superseded this reply
            setLoading(false);
            if (!result.ok()) {
                qCWarning(logApp) << "library page load failed:" << result.error;
                setError(result.error);
                return;
            }
            setError(QString());
            if (startIndex == 0)
                m_model->setItems(result.value.items, result.value.totalRecordCount);
            else
                m_model->appendItems(result.value.items);
        });
}

void LibraryController::setError(const QString &message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorMessageChanged();
}

void LibraryController::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

} // namespace strmqt
