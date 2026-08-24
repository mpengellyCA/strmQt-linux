#include "DetailsController.h"

#include "app/models/MediaItemModel.h"
#include "core/Log.h"
#include "server/dto/ItemsQuery.h"
#include "server/emby/EmbyClient.h"

namespace strmqt {

namespace {
constexpr int kCastLimit = 8;
const auto kSeparator = QStringLiteral(", ");
} // namespace

DetailsController::DetailsController(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_similar(new MediaItemModel(this))
{
}

DetailsController::~DetailsController()
{
    ++m_generation;
    cancelRequests();
}

void DetailsController::resetSessionState()
{
    ++m_generation;
    cancelRequests();
    m_similar->clear();
    m_tagline.clear();
    m_genresLine.clear();
    m_castLine.clear();
    m_directorLine.clear();
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
}

void DetailsController::loadPerson(const QString &personId)
{
    const int generation = ++m_generation;
    cancelRequests();
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
    cancelRequests();
    m_tagline.clear();
    m_genresLine.clear();
    m_castLine.clear();
    m_directorLine.clear();
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
    emit detailsChanged();
    m_similar->clear();

    m_client->itemDetails(itemId, &m_detailsRequest).then(this, [this, generation](const Result<ItemDetails> &result) {
        if (generation != m_generation)
            return;
        if (!result.ok()) {
            qCWarning(logApp) << "item details load failed:" << result.error;
            return;
        }
        const ItemDetails &details = result.value;
        m_tagline = details.tagline;
        m_genresLine = details.genres.join(kSeparator);
        m_castLine = details.cast.mid(0, kCastLimit).join(kSeparator);
        m_directorLine = details.directors.join(kSeparator);

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

        emit detailsChanged();
    });

    // Collection membership. A separate request because the item payload has no
    // field for it: ListItemIds against BoxSets is the reverse lookup, and the
    // plausible-looking ContainsItemId is silently ignored by the server.
    m_collections.clear();
    emit collectionsChanged();
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
        });
}

void DetailsController::cancelRequests()
{
    m_detailsRequest.cancel();
    m_collectionsRequest.cancel();
    m_similarRequest.cancel();
}

} // namespace strmqt
