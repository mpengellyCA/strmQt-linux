#include "HomeController.h"

#include "app/controllers/LiveUpdateService.h"
#include "app/models/LibraryListModel.h"
#include "app/models/MediaItemModel.h"
#include "core/Log.h"

#include <memory>
#include "server/emby/EmbyClient.h"

#include <QSet>
#include <QTimer>
#include <QVariantMap>

namespace strmqt {

namespace {

// Only these library kinds get a Latest rail on Home; other views (music,
// collections) are reachable through the library grid instead.
bool wantsLatestRail(const Library &library)
{
    return library.collectionType == QLatin1String("movies") ||
           library.collectionType == QLatin1String("tvshows");
}

QSet<QString> idsOf(const QList<MediaItem> &items)
{
    QSet<QString> ids;
    ids.reserve(items.size());
    for (const MediaItem &item : items)
        ids.insert(item.id);
    return ids;
}

QSet<QString> idsOf(const MediaItemModel *model)
{
    return idsOf(model->items());
}

} // namespace

HomeController::HomeController(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_resume(new MediaItemModel(this)),
      m_nextUp(new MediaItemModel(this)), m_favorites(new MediaItemModel(this)),
      m_libraries(new LibraryListModel(this)), m_rails(new HomeRailModel(this))
{
    const auto syncRails = [this] { syncRailDescriptors(); };
    connect(m_resume, &MediaItemModel::countChanged, this, syncRails);
    connect(m_nextUp, &MediaItemModel::countChanged, this, syncRails);
    connect(m_favorites, &MediaItemModel::countChanged, this, syncRails);
    connect(m_libraries, &LibraryListModel::countChanged, this, syncRails);

    m_userDataRefreshTimer.setSingleShot(true);
    connect(&m_userDataRefreshTimer, &QTimer::timeout, this, [this] {
        m_userDataRefreshQueued = false;
        onUserDataInvalidated({});
    });
}

void HomeController::resetSessionState()
{
    const bool wasBusy = busy();
    ++m_generation;
    ++m_genreGeneration;
    ++m_sessionGeneration;
    m_pending = 0;
    if (wasBusy)
        emit busyChanged();

    m_userDataRefreshTimer.stop();
    m_userDataRefreshQueued = false;
    m_lastUserDataRefresh.invalidate();

    m_resume->clear();
    m_nextUp->clear();
    m_favorites->clear();
    m_libraries->setLibraries({});

    const bool hadLatestRails = !m_latestRails.isEmpty();
    m_latestRails.clear();
    for (MediaItemModel *model : std::as_const(m_railModels)) {
        model->clear();
        model->deleteLater();
    }
    m_railModels.clear();
    if (hadLatestRails)
        emit latestRailsChanged();

    const bool hadGenreRails = !m_genreRails.isEmpty();
    m_genreRails.clear();
    for (MediaItemModel *model : std::as_const(m_genreModels)) {
        model->clear();
        model->deleteLater();
    }
    m_genreModels.clear();
    m_genreRailsFetched = false;
    if (hadGenreRails)
        emit genreRailsChanged();
    syncRailDescriptors();

    m_incoming = Snapshot{};
    m_held = Snapshot{};
    m_applyWhenReady = true;
    setPending(false, 0);
    setError({});
    if (!m_autoApplyUpdates) {
        m_autoApplyUpdates = true;
        emit autoApplyUpdatesChanged();
    }
}

void HomeController::bindLiveUpdates(LiveUpdateService *service)
{
    if (!service)
        return;
    connect(service, &LiveUpdateService::refreshRequested, this, &HomeController::refresh);
    connect(service, &LiveUpdateService::libraryInvalidated, this,
            &HomeController::onLibraryInvalidated);
    connect(service, &LiveUpdateService::userDataPatched, this,
            &HomeController::onUserDataPatched);
    connect(service, &LiveUpdateService::userDataInvalidated, this,
            &HomeController::onUserDataInvalidated);
}

void HomeController::setAutoApplyUpdates(bool autoApply)
{
    if (m_autoApplyUpdates == autoApply)
        return;
    m_autoApplyUpdates = autoApply;
    emit autoApplyUpdatesChanged();
    // Turning it back on (the user scrolled home) is an invitation to land
    // whatever has been waiting.
    if (m_autoApplyUpdates && m_hasPending)
        applyPending();
}

void HomeController::refresh()
{
    startRefresh(/*applyWhenReady=*/true);
}

void HomeController::onLibraryInvalidated(const QStringList &itemIds)
{
    Q_UNUSED(itemIds); // Home's rails are "latest across the library", not per item
    if (busy()) {
        // A refresh is already fetching; it will carry these changes.
        qCDebug(logApp) << "home: library invalidation folded into an in-flight refresh";
        return;
    }
    startRefresh(/*applyWhenReady=*/m_autoApplyUpdates);
}

void HomeController::onUserDataPatched(const QVariantList &entries)
{
    // Patch the complete record immediately. Membership is reconciled after
    // the burst by onUserDataInvalidated(): Next Up in particular cannot be
    // inferred from one item's payload.
    for (const QVariant &value : entries) {
        const QVariantMap entry = value.toMap();
        const QString itemId = entry.value(QStringLiteral("itemId")).toString();
        if (itemId.isEmpty())
            continue;
        updateAllModels(itemId, entry.value(QStringLiteral("played")).toBool(),
                        entry.value(QStringLiteral("favorite")).toBool(),
                        entry.value(QStringLiteral("positionTicks")).toLongLong(),
                        entry.value(QStringLiteral("playCount")).toInt());
    }
}

void HomeController::onUserDataInvalidated(const QStringList &itemIds)
{
    Q_UNUSED(itemIds);
    if (busy())
        return;
    // Continue Watching, Next Up and Favorites are three server-side queries.
    // The changed ids do not contain enough type/series context to decide who
    // enters them, so fetch one coherent snapshot and respect the page's
    // existing "do not move under the cursor" policy.
    //
    // Audio playback deliberately leaves live updates running, and every
    // progress report changes user data on the server. Without a floor between
    // snapshots, Home refetches three queries on that cadence for as long as
    // the music plays. The first invalidation is still immediate; anything
    // behind it collapses into one deferred refresh.
    if (m_userDataRefreshFloorMs > 0 && m_lastUserDataRefresh.isValid()) {
        const qint64 waited = m_lastUserDataRefresh.elapsed();
        if (waited < m_userDataRefreshFloorMs) {
            if (!m_userDataRefreshQueued) {
                m_userDataRefreshQueued = true;
                m_userDataRefreshTimer.start(m_userDataRefreshFloorMs - waited);
            }
            return;
        }
    }
    m_lastUserDataRefresh.start();
    startRefresh(/*applyWhenReady=*/m_autoApplyUpdates);
}

void HomeController::startRefresh(bool applyWhenReady)
{
    const bool wasBusy = busy();
    ++m_generation;
    const int generation = m_generation;
    // A user-driven refresh may supersede another one. Its replies carry the
    // old generation and must not decrement the new refresh's request count.
    m_pending = 0;
    m_incoming = Snapshot{};
    m_applyWhenReady = applyWhenReady;
    setError({});

    beginRequest();
    m_client->resumeItems(20).then(this, [this, generation](const Result<ItemsPage> &result) {
        if (generation == m_generation) {
            if (result.ok()) {
                m_incoming.resume = result.value.items;
                m_incoming.resumeTotal = result.value.totalRecordCount;
            } else {
                setError(result.error);
            }
        }
        endRequest(generation);
    });

    beginRequest();
    m_client->nextUp(20).then(this, [this, generation](const Result<ItemsPage> &result) {
        if (generation == m_generation) {
            if (result.ok()) {
                m_incoming.nextUp = result.value.items;
                m_incoming.nextUpTotal = result.value.totalRecordCount;
            } else {
                setError(result.error);
            }
        }
        endRequest(generation);
    });

    beginRequest();
    ItemsQuery favoritesQuery;
    favoritesQuery.recursive = true;
    favoritesQuery.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series")};
    favoritesQuery.filters = {QStringLiteral("IsFavorite")};
    favoritesQuery.limit = 20;
    m_client->items(favoritesQuery).then(this, [this, generation](const Result<ItemsPage> &result) {
        if (generation == m_generation && result.ok()) {
            m_incoming.favorites = result.value.items;
            m_incoming.favoritesTotal = result.value.totalRecordCount;
        }
        endRequest(generation);
    });

    beginRequest();
    m_client->userViews().then(this, [this, generation](const Result<QList<Library>> &result) {
        if (generation != m_generation) {
            endRequest(generation);
            return;
        }
        if (!result.ok()) {
            setError(result.error);
            endRequest(generation);
            return;
        }

        m_incoming.libraries = result.value;
        m_incoming.haveLibraries = true;

        // Reserve one slot per railworthy library up front so the rails keep
        // the server's library order however the replies interleave.
        for (const Library &library : result.value) {
            if (wantsLatestRail(library))
                m_incoming.rails.append({library, {}});
        }

        for (int slot = 0; slot < m_incoming.rails.size(); ++slot) {
            const QString libraryId = m_incoming.rails[slot].first.id;
            beginRequest();
            m_client->latestItems(libraryId, 16)
                .then(this, [this, generation, slot](const Result<QList<MediaItem>> &result) {
                    if (generation == m_generation && result.ok() &&
                        slot < m_incoming.rails.size())
                        m_incoming.rails[slot].second = result.value;
                    endRequest(generation);
                });
        }
        endRequest(generation);
    });
    if (!wasBusy && busy())
        emit busyChanged();
}

void HomeController::finishRefresh()
{
    // Nothing came back at all (offline, or every request failed): leave the
    // screen as it was rather than blanking it.
    if (!m_incoming.haveLibraries && m_incoming.resume.isEmpty() && m_incoming.nextUp.isEmpty() &&
        m_incoming.favorites.isEmpty())
        return;

    if (m_applyWhenReady || m_autoApplyUpdates) {
        applySnapshot(m_incoming);
        setPending(false, 0);
        return;
    }

    const int newCount = countNewItems(m_incoming);
    if (newCount == 0) {
        // Nothing the user can see would move: swapping it in is free.
        applySnapshot(m_incoming);
        setPending(false, 0);
        return;
    }
    m_held = m_incoming;
    setPending(true, newCount);
}

int HomeController::countNewItems(const Snapshot &snapshot) const
{
    QSet<QString> onScreen = idsOf(m_resume);
    onScreen += idsOf(m_nextUp);
    onScreen += idsOf(m_favorites);
    for (auto it = m_railModels.cbegin(); it != m_railModels.cend(); ++it)
        onScreen += idsOf(it.value());

    QSet<QString> incoming = idsOf(snapshot.resume);
    incoming += idsOf(snapshot.nextUp);
    incoming += idsOf(snapshot.favorites);
    for (const auto &rail : snapshot.rails)
        incoming += idsOf(rail.second);

    return static_cast<int>((incoming - onScreen).size());
}

MediaItemModel *HomeController::railModelFor(const QString &libraryId)
{
    auto it = m_railModels.find(libraryId);
    if (it != m_railModels.end())
        return it.value();
    auto *model = new MediaItemModel(this);
    m_railModels.insert(libraryId, model);
    return model;
}

void HomeController::syncRailDescriptors()
{
    QList<HomeRailModel::Descriptor> descriptors;
    if (m_resume->rowCount() > 0) {
        descriptors.append({QStringLiteral("resume"), tr("Continue Watching"), m_resume,
                            false, true, {}});
    }
    if (m_nextUp->rowCount() > 0) {
        descriptors.append(
            {QStringLiteral("next-up"), tr("Next Up"), m_nextUp, false, true, {}});
    }
    if (m_libraries->rowCount() > 0) {
        descriptors.append(
            {QStringLiteral("libraries"), tr("Libraries"), m_libraries, true, false, {}});
    }
    if (m_favorites->rowCount() > 0) {
        descriptors.append(
            {QStringLiteral("favorites"), tr("Favorites"), m_favorites, false, false, {}});
    }

    for (const QVariant &value : std::as_const(m_latestRails)) {
        const QVariantMap rail = value.toMap();
        const QString libraryId = rail.value(QStringLiteral("libraryId")).toString();
        QObject *model = rail.value(QStringLiteral("model")).value<MediaItemModel *>();
        if (libraryId.isEmpty() || !model)
            continue;
        descriptors.append({QStringLiteral("latest:%1").arg(libraryId),
                            rail.value(QStringLiteral("title")).toString(), model, false, false,
                            {}});
    }

    for (const QVariant &value : std::as_const(m_genreRails)) {
        const QVariantMap rail = value.toMap();
        const QString genreId = rail.value(QStringLiteral("genreId")).toString();
        QObject *model = rail.value(QStringLiteral("model")).value<MediaItemModel *>();
        if (genreId.isEmpty() || !model)
            continue;
        descriptors.append({QStringLiteral("genre:%1").arg(genreId),
                            rail.value(QStringLiteral("title")).toString(), model, false, false,
                            genreId});
    }

    m_rails->setDescriptors(std::move(descriptors));
}

void HomeController::applySnapshot(const Snapshot &snapshot)
{
    m_resume->setItems(snapshot.resume, snapshot.resumeTotal);
    m_nextUp->setItems(snapshot.nextUp, snapshot.nextUpTotal);
    m_favorites->setItems(snapshot.favorites, snapshot.favoritesTotal);

    if (!snapshot.haveLibraries)
        return;

    m_libraries->setLibraries(snapshot.libraries);

    QVariantList rails;
    QSet<QString> live;
    for (const auto &entry : snapshot.rails) {
        if (entry.second.isEmpty())
            continue; // an empty rail is noise, not content
        MediaItemModel *model = railModelFor(entry.first.id);
        model->setItems(entry.second);
        live.insert(entry.first.id);

        QVariantMap rail;
        rail.insert(QStringLiteral("libraryId"), entry.first.id);
        rail.insert(QStringLiteral("title"),
                    QStringLiteral("Latest — %1").arg(entry.first.name));
        rail.insert(QStringLiteral("model"), QVariant::fromValue(model));
        rails.append(rail);
    }

    // Libraries that went away take their models with them. The descriptor sync
    // below removes their rows before deleteLater() can destroy either model.
    for (auto it = m_railModels.begin(); it != m_railModels.end();) {
        if (live.contains(it.key())) {
            ++it;
            continue;
        }
        it.value()->deleteLater();
        it = m_railModels.erase(it);
    }

    if (m_latestRails != rails) {
        m_latestRails = rails;
        emit latestRailsChanged();
        syncRailDescriptors();
    }
}

void HomeController::loadGenreRails()
{
    if (m_genreRailsFetched)
        return;
    m_genreRailsFetched = true;
    fetchGenreRails();
}

void HomeController::fetchGenreRails()
{
    // The genres worth a rail are the ones the user's library actually has, so
    // they come from /Genres rather than a hardcoded list.
    constexpr int kGenreCount = 6;
    constexpr int kItemsPerRail = 20;

    const int generation = ++m_genreGeneration;
    m_client->genres(QString(), kGenreCount)
        .then(this, [this, generation](const Result<QList<MediaItem>> &result) {
            if (generation != m_genreGeneration || !result.ok() || result.value.isEmpty())
                return;

            // Rebuilt in place: each rail's items land independently, and the
            // list is republished as each arrives so the first genre shows
            // without waiting for the last.
            const QList<MediaItem> genres = result.value;
            auto pending = std::make_shared<QVariantList>();
            pending->resize(genres.size());

            for (int i = 0; i < genres.size(); ++i) {
                const MediaItem genre = genres.at(i);
                if (genre.id.isEmpty() || genre.name.isEmpty())
                    continue;

                MediaItemModel *&model = m_genreModels[genre.id];
                if (!model)
                    model = new MediaItemModel(this);

                ItemsQuery query;
                query.genreIds = {genre.id};
                query.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series")};
                query.recursive = true;
                // Random rather than SortName: a genre rail exists to surface
                // things the user has forgotten, and alphabetical order shows
                // the same six titles forever.
                query.sortBy = QStringLiteral("Random");
                query.limit = kItemsPerRail;

                m_client->items(query).then(
                    this, [this, generation, genre, model, i,
                           pending](const Result<ItemsPage> &result) {
                        if (generation != m_genreGeneration || !result.ok()
                            || result.value.items.isEmpty())
                            return;
                        model->setItems(result.value.items);

                        QVariantMap rail;
                        rail.insert(QStringLiteral("title"), genre.name);
                        rail.insert(QStringLiteral("genreId"), genre.id);
                        rail.insert(QStringLiteral("model"), QVariant::fromValue(model));
                        (*pending)[i] = rail;

                        QVariantList live;
                        for (const QVariant &entry : std::as_const(*pending)) {
                            if (entry.isValid() && !entry.toMap().isEmpty())
                                live.append(entry);
                        }
                        if (m_genreRails != live) {
                            m_genreRails = live;
                            emit genreRailsChanged();
                            syncRailDescriptors();
                        }
                    });
            }
        });
}

void HomeController::applyPending()
{
    if (!m_hasPending)
        return;
    applySnapshot(m_held);
    m_held = Snapshot{};
    setPending(false, 0);
}

void HomeController::discardPending()
{
    if (!m_hasPending)
        return;
    m_held = Snapshot{};
    setPending(false, 0);
}

void HomeController::setPending(bool pending, int newCount)
{
    if (m_hasPending == pending && m_pendingNewCount == newCount)
        return;
    m_hasPending = pending;
    m_pendingNewCount = newCount;
    emit pendingChanged();
}

void HomeController::togglePlayed(const QString &itemId, bool played, bool favorite)
{
    const int sessionGeneration = m_sessionGeneration;
    m_client->setPlayed(itemId, played)
        .then(this, [this, sessionGeneration, itemId, played,
                     favorite](const Result<bool> &result) {
            if (sessionGeneration != m_sessionGeneration)
                return;
            if (result.ok())
                updateAllModels(itemId, played, favorite);
            else
                setError(result.error);
        });
}

void HomeController::toggleFavorite(const QString &itemId, bool played, bool favorite)
{
    const int sessionGeneration = m_sessionGeneration;
    m_client->setFavorite(itemId, favorite)
        .then(this, [this, sessionGeneration, itemId, played,
                     favorite](const Result<bool> &result) {
            if (sessionGeneration != m_sessionGeneration)
                return;
            if (result.ok())
                updateAllModels(itemId, played, favorite);
            else
                setError(result.error);
        });
}

void HomeController::updateAllModels(const QString &itemId, bool played, bool favorite)
{
    m_resume->updateUserData(itemId, played, favorite);
    m_nextUp->updateUserData(itemId, played, favorite);
    m_favorites->updateUserData(itemId, played, favorite);
    for (MediaItemModel *model : std::as_const(m_railModels))
        model->updateUserData(itemId, played, favorite);
}

void HomeController::updateAllModels(const QString &itemId, bool played, bool favorite,
                                     qint64 positionTicks, int playCount)
{
    m_resume->updateUserData(itemId, played, favorite, positionTicks, playCount);
    m_nextUp->updateUserData(itemId, played, favorite, positionTicks, playCount);
    m_favorites->updateUserData(itemId, played, favorite, positionTicks, playCount);
    for (MediaItemModel *model : std::as_const(m_railModels))
        model->updateUserData(itemId, played, favorite, positionTicks, playCount);
    for (MediaItemModel *model : std::as_const(m_genreModels))
        model->updateUserData(itemId, played, favorite, positionTicks, playCount);
}

void HomeController::beginRequest()
{
    ++m_pending;
}

void HomeController::endRequest(int generation)
{
    if (generation != m_generation)
        return;
    if (--m_pending == 0) {
        finishRefresh();
        emit busyChanged();
    }
}

void HomeController::setError(const QString &message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorMessageChanged();
    if (!message.isEmpty())
        qCWarning(logApp) << "home refresh error:" << message;
}

} // namespace strmqt
