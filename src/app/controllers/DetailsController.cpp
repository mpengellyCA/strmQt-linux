#include "DetailsController.h"
#include "LiveUpdateService.h"

#include "app/models/MediaItemModel.h"
#include "core/Log.h"
#include "server/dto/ItemsQuery.h"
#include "server/emby/EmbyClient.h"

#include <utility>

namespace strmqt {

DetailsController::DetailsController(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_similar(new MediaItemModel(this)),
      m_upcomingEpisodes(new MediaItemModel(this))
{
}

DetailsController::~DetailsController()
{
    ++m_generation;
    ++m_upcomingEpisodesGeneration;
    cancelRequests();
}

void DetailsController::resetSessionState()
{
    ++m_generation;
    ++m_upcomingEpisodesGeneration;
    cancelRequests();
    m_itemId.clear();
    m_itemLoading = false;
    setSimilarLoading(false);
    m_similar->clear();
    m_upcomingEpisodes->clear();
    m_nextEpisode.clear();
    m_upcomingEpisodesLoading = false;
    m_isSeries = false;
    m_tagline.clear();
    m_logoUrl.clear();
    m_mediaSources.clear();
    m_chapters.clear();
    m_people.clear();
    m_cast.clear();
    m_crew.clear();
    m_genres.clear();
    m_studios.clear();
    m_externalLinks.clear();
    m_collections.clear();
    m_trailers.clear();
    m_premiereDate.clear();
    m_person.clear();
    m_criticRating = 0.0;
    emit detailsChanged();
    emit collectionsChanged();
    emit personChanged();
    emit upcomingEpisodesChanged();
    setUserState(false, false, 0);
}

void DetailsController::loadPerson(const QString &personId)
{
    const int generation = ++m_generation;
    ++m_upcomingEpisodesGeneration;
    cancelRequests();
    setSimilarLoading(false);
    m_upcomingEpisodes->clear();
    m_nextEpisode.clear();
    m_upcomingEpisodesLoading = false;
    m_isSeries = false;
    emit upcomingEpisodesChanged();
    const bool retiredItemOwner = !m_itemId.isEmpty() || m_itemLoading;
    m_itemId.clear();
    m_itemLoading = false;
    if (retiredItemOwner)
        emit detailsChanged();
    setUserState(false, false, 0);
    m_person.clear();
    emit personChanged();
    if (personId.isEmpty())
        return;

    m_client->itemDetails(personId, &m_detailsRequest).then(
        this, [this, generation](const Result<ItemDetails> &result) {
            if (generation != m_generation)
                return;
            if (!result.ok()) {
                qCWarning(logApp) << "person load failed:" << result.error;
                return;
            }
            const MediaItem &item = result.value.item;
            QVariantMap map;
            map.insert(QStringLiteral("id"), item.id);
            map.insert(QStringLiteral("name"), item.name);
            map.insert(QStringLiteral("overview"), item.overview);
            // Emby files a person's birth date under PremiereDate and their
            // birthplace under ProductionLocations; neither name is a mistake
            // to correct here, it is the server's vocabulary.
            map.insert(QStringLiteral("birthDate"), item.premiereDate);
            map.insert(QStringLiteral("birthplace"), item.productionLocations.value(0));
            // A death date, when the server has one. The page needs it to say
            // "1949 – 2016" honestly rather than computing an age from a birth
            // date, which is wrong for anyone who has died.
            map.insert(QStringLiteral("deathDate"), item.endDate);
            map.insert(QStringLiteral("imageUrl"),
                       embyImageSource(item.id, QStringLiteral("Primary"),
                                       item.primaryImageTag));
            m_person = map;
            emit personChanged();
        });
}

void DetailsController::load(const QString &itemId)
{
    const int generation = ++m_generation;
    ++m_upcomingEpisodesGeneration;
    cancelRequests();
    const bool sameItem = itemId == m_itemId;
    m_itemId = itemId;
    m_itemLoading = !itemId.isEmpty();
    m_isSeries = false;
    m_upcomingEpisodes->clear();
    m_nextEpisode.clear();
    m_upcomingEpisodesLoading = false;
    emit upcomingEpisodesChanged();
    m_tagline.clear();
    m_mediaSources.clear();
    m_chapters.clear();
    m_people.clear();
    m_cast.clear();
    m_crew.clear();
    m_genres.clear();
    m_studios.clear();
    m_externalLinks.clear();
    m_trailers.clear();
    m_premiereDate.clear();
    m_criticRating = 0.0;
    // Kept across a reload of the same item: the title box is sized by
    // whether there is a logo, and dropping it for the round trip would
    // collapse the box and grow it back.
    if (!sameItem)
        m_logoUrl.clear();
    emit detailsChanged();
    // The previous item's state must not be read as this one's while the
    // reply is in flight. A reload of the same item keeps what is known.
    if (!sameItem)
        setUserState(false, false, 0);
    m_similar->clear();
    setSimilarLoading(!itemId.isEmpty());

    m_collections.clear();
    emit collectionsChanged();
    if (itemId.isEmpty())
        return;

    m_client->itemDetails(itemId, &m_detailsRequest)
        .then(this, [this, generation](const Result<ItemDetails> &result) {
        if (generation != m_generation)
            return;
        m_itemLoading = false;
        if (!result.ok()) {
            // Ownership equality is used to join a restored page to useful
            // state. A failed primary response is not useful state: retire
            // the id so the next ensureLoaded() honestly retries it.
            m_itemId.clear();
            m_logoUrl.clear();
            m_collectionsRequest.cancel();
            m_similarRequest.cancel();
            m_upcomingEpisodesRequest.cancel();
            setSimilarLoading(false);
            m_upcomingEpisodes->clear();
            m_nextEpisode.clear();
            m_upcomingEpisodesLoading = false;
            m_isSeries = false;
            emit upcomingEpisodesChanged();
            qCWarning(logApp) << "item details load failed:" << result.error;
            emit detailsChanged();
            setUserState(false, false, 0);
            return;
        }
        const ItemDetails &details = result.value;
        m_tagline = details.tagline;
        const MediaItem::ImageRef logo = details.item.logoSource();
        m_logoUrl = logo.isValid() ? embyImageSource(logo.itemId, logo.imageType, logo.tag)
                                   : QString();

        // The index travels with the map so a picker can hand it straight back
        // to PlayerController::playItem(..., preferredSourceIndex) — the two
        // orderings are the same one (server order).
        m_mediaSources.clear();
        m_mediaSources.reserve(details.mediaSources.size());
        for (qsizetype i = 0; i < details.mediaSources.size(); ++i) {
            QVariantMap map = details.mediaSources[i].toVariantMap();
            map.insert(QStringLiteral("index"), static_cast<int>(i));
            m_mediaSources.append(map);
        }

        m_chapters.clear();
        m_chapters.reserve(details.chapters.size());
        for (const Chapter &chapter : details.chapters)
            m_chapters.append(chapter.toVariantMap());

        // Server order is billing order for actors, so it is preserved rather
        // than sorted. Cast and crew are split here because the page lays them
        // out differently — headshot cards vs. a credits list — and doing the
        // split in QML would mean filtering the same list twice per repaint.
        m_people.clear();
        m_cast.clear();
        m_crew.clear();
        m_people.reserve(details.people.size());
        for (const Person &person : details.people) {
            const QVariantMap map = person.toVariantMap();
            m_people.append(map);
            if (person.type.compare(QLatin1String("Actor"), Qt::CaseInsensitive) == 0)
                m_cast.append(map);
            else
                m_crew.append(map);
        }

        m_genres.clear();
        for (const NamedId &genre : details.genreItems)
            m_genres.append(genre.toVariantMap());
        // A payload old enough to send Genres but not GenreItems still gets
        // chips; they just cannot be navigated by id.
        if (m_genres.isEmpty()) {
            for (const QString &name : details.genres) {
                NamedId fallback;
                fallback.name = name;
                m_genres.append(fallback.toVariantMap());
            }
        }

        m_studios.clear();
        for (const NamedId &studio : details.studios)
            m_studios.append(studio.toVariantMap());

        m_externalLinks.clear();
        for (const ExternalLink &link : details.externalLinks)
            m_externalLinks.append(link.toVariantMap());

        m_trailers.clear();
        for (const ExternalLink &trailer : details.trailers)
            m_trailers.append(trailer.toVariantMap());

        m_premiereDate = details.premiereDate;
        m_criticRating = details.criticRating;

        m_isSeries = details.item.type.compare(QLatin1String("Series"), Qt::CaseInsensitive) == 0;
        if (m_isSeries)
            refreshUpcomingEpisodes();

        emit detailsChanged();
        // This reply was requested before anything that has since arrived
        // over the socket or been changed here, so it only seeds.
        if (m_userStateGeneration != generation)
            setUserState(details.item.played, details.item.favorite, details.item.positionMs());
    });

    // Collection membership. A separate request because the item payload has no
    // field for it: ListItemIds against BoxSets is the reverse lookup, and the
    // plausible-looking ContainsItemId is silently ignored by the server.
    ItemsQuery collectionsQuery;
    collectionsQuery.includeItemTypes = {QStringLiteral("BoxSet")};
    collectionsQuery.listItemIds = {itemId};
    collectionsQuery.recursive = true;
    collectionsQuery.limit = 12;
    m_client->items(collectionsQuery, &m_collectionsRequest)
        .then(this, [this, generation](const Result<ItemsPage> &result) {
            if (generation != m_generation || !result.ok())
                return;
            m_collections.clear();
            for (const MediaItem &item : result.value.items) {
                QVariantMap map;
                map.insert(QStringLiteral("id"), item.id);
                map.insert(QStringLiteral("name"), item.name);
                m_collections.append(map);
            }
            emit collectionsChanged();
        });

    m_client->similar(itemId, 12, &m_similarRequest)
        .then(this, [this, generation](const Result<QList<MediaItem>> &result) {
            if (generation != m_generation)
                return;
            if (result.ok())
                m_similar->setItems(result.value);
            setSimilarLoading(false);
        });
}

void DetailsController::bindLiveUpdates(LiveUpdateService *service)
{
    if (!service)
        return;
    // The rich socket patch carries the whole record: the rails and the loaded
    // item's own state follow another client without a request.
    connect(service, &LiveUpdateService::userDataPatched, this,
            &DetailsController::onUserDataPatched);
    connect(service, &LiveUpdateService::userDataInvalidated, this,
            [this](const QStringList &) {
                if (m_isSeries && !m_itemId.isEmpty())
                    refreshUpcomingEpisodes();
            });
    connect(service, &LiveUpdateService::refreshRequested, this,
            [this]() {
                if (m_isSeries && !m_itemId.isEmpty())
                    refreshUpcomingEpisodes();
            });
}

void DetailsController::onUserDataPatched(const QVariantList &entries)
{
    for (const QVariant &value : entries) {
        const QVariantMap entry = value.toMap();
        const QString itemId = entry.value(QStringLiteral("itemId")).toString();
        if (itemId.isEmpty())
            continue;
        const bool played = entry.value(QStringLiteral("played")).toBool();
        const bool favorite = entry.value(QStringLiteral("favorite")).toBool();
        const qint64 positionTicks = entry.value(QStringLiteral("positionTicks")).toLongLong();
        const int playCount = entry.value(QStringLiteral("playCount")).toInt();
        m_similar->updateUserData(itemId, played, favorite, positionTicks, playCount);
        m_upcomingEpisodes->updateUserData(itemId, played, favorite, positionTicks, playCount);
        if (itemId == m_itemId) {
            m_userStateGeneration = m_generation;
            setUserState(played, favorite, positionTicks / kTicksPerMs);
        }
    }

    // nextEpisode is a snapshot of the rail's first row, so it has to follow
    // the row. Which episode is next is the server's answer and arrives with
    // the invalidation.
    if (!m_nextEpisode.isEmpty() && m_upcomingEpisodes->rowCount() > 0) {
        QVariantMap next = m_upcomingEpisodes->get(0);
        if (next != m_nextEpisode) {
            m_nextEpisode = std::move(next);
            emit upcomingEpisodesChanged();
        }
    }
}

void DetailsController::notePlayed(const QString &itemId, bool played)
{
    if (itemId.isEmpty() || itemId != m_itemId)
        return;
    m_userStateGeneration = m_generation;
    setUserState(played, m_favorite, m_positionMs);
}

void DetailsController::noteFavorite(const QString &itemId, bool favorite)
{
    if (itemId.isEmpty() || itemId != m_itemId)
        return;
    m_userStateGeneration = m_generation;
    setUserState(m_played, favorite, m_positionMs);
}

void DetailsController::setUserState(bool played, bool favorite, qint64 positionMs)
{
    if (m_played == played && m_favorite == favorite && m_positionMs == positionMs)
        return;
    m_played = played;
    m_favorite = favorite;
    m_positionMs = positionMs;
    emit userStateChanged();
}

namespace {

// The rail shows a dozen. More are fetched so that specials, which the server
// sorts first, can be moved out of the lead without emptying it.
QList<MediaItem> upcomingRail(QList<MediaItem> items)
{
    moveSpecialsLast(items);
    return items.mid(0, 12);
}

} // namespace

void DetailsController::refreshUpcomingEpisodes()
{
    if (!m_isSeries || m_itemId.isEmpty()) {
        m_upcomingEpisodes->clear();
        m_nextEpisode.clear();
        m_upcomingEpisodesLoading = false;
        emit upcomingEpisodesChanged();
        return;
    }

    const int generation = ++m_upcomingEpisodesGeneration;
    m_upcomingEpisodesLoading = true;
    emit upcomingEpisodesChanged();

    ItemsQuery query;
    query.parentId = m_itemId;
    query.recursive = true;
    query.includeItemTypes = {QStringLiteral("Episode")};
    query.filters = {QStringLiteral("IsUnplayed")};
    query.sortBy = episodeOrderSort();
    query.limit = kEpisodeLeadFetch;

    m_client->items(query, &m_upcomingEpisodesRequest)
        .then(this, [this, generation](const Result<ItemsPage> &result) {
            if (generation != m_upcomingEpisodesGeneration)
                return;
            if (result.ok() && !result.value.items.isEmpty()) {
                m_upcomingEpisodes->setItems(upcomingRail(result.value.items),
                                             result.value.totalRecordCount);
                m_nextEpisode = m_upcomingEpisodes->get(0);
                m_upcomingEpisodesLoading = false;
                emit upcomingEpisodesChanged();
                return;
            }

            // Fallback: If no unplayed episodes exist across the series, query the first
            // episodes in broadcast air order so the shelf is not left blank.
            ItemsQuery fallbackQuery;
            fallbackQuery.parentId = m_itemId;
            fallbackQuery.recursive = true;
            fallbackQuery.includeItemTypes = {QStringLiteral("Episode")};
            fallbackQuery.sortBy = episodeOrderSort();
            fallbackQuery.limit = kEpisodeLeadFetch;

            m_client->items(fallbackQuery, &m_upcomingEpisodesRequest)
                .then(this, [this, generation](const Result<ItemsPage> &fallbackResult) {
                    if (generation != m_upcomingEpisodesGeneration)
                        return;
                    if (fallbackResult.ok() && !fallbackResult.value.items.isEmpty()) {
                        m_upcomingEpisodes->setItems(upcomingRail(fallbackResult.value.items),
                                                     fallbackResult.value.totalRecordCount);
                        m_nextEpisode = m_upcomingEpisodes->get(0);
                    } else {
                        m_upcomingEpisodes->clear();
                        m_nextEpisode.clear();
                    }
                    m_upcomingEpisodesLoading = false;
                    emit upcomingEpisodesChanged();
                });
        });
}

void DetailsController::ensureLoaded(const QString &itemId)
{
    if (itemId.isEmpty() || m_itemId == itemId)
        return;
    load(itemId);
}

void DetailsController::cancelRequests()
{
    m_detailsRequest.cancel();
    m_collectionsRequest.cancel();
    m_similarRequest.cancel();
    m_upcomingEpisodesRequest.cancel();
}

void DetailsController::setSimilarLoading(bool loading)
{
    if (m_similarLoading == loading)
        return;
    m_similarLoading = loading;
    emit similarStatusChanged();
}

} // namespace strmqt
