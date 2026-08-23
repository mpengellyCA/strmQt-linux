#include "SearchController.h"

#include <QSettings>

#include "core/Log.h"
#include "server/emby/EmbyClient.h"

namespace strmqt {

namespace {
constexpr int kDebounceMs = 350;
constexpr int kResultLimit = 60;
constexpr int kFacetLimit = 12;
constexpr int kRecentLimit = 8;
const auto kRecentKey = QStringLiteral("search/recent");

// Person and genre rows carry an image tag but arrive as plain items, so the
// provider URL is built the same way MediaItemModel does it.
QVariantList facetList(const QList<MediaItem> &items)
{
    QVariantList out;
    for (const MediaItem &item : items) {
        if (item.name.isEmpty())
            continue;
        QVariantMap map;
        map.insert(QStringLiteral("id"), item.id);
        map.insert(QStringLiteral("name"), item.name);
        // Both forms: PersonCard builds its own provider URL from id + tag, and
        // chips want the finished URL. Handing over only the URL forced the page
        // to parse the tag back out of it.
        map.insert(QStringLiteral("primaryImageTag"), item.primaryImageTag);
        map.insert(QStringLiteral("imageUrl"),
                   item.primaryImageTag.isEmpty()
                       ? QString()
                       : QStringLiteral("image://emby/%1/Primary/%2")
                             .arg(item.id, item.primaryImageTag));
        out.append(map);
    }
    return out;
}
} // namespace

SearchController::SearchController(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_model(new MediaItemModel(this))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs);
    QSettings store;
    m_recentQueries = store.value(kRecentKey).toStringList();
    connect(&m_debounce, &QTimer::timeout, this, &SearchController::runSearch);
}

void SearchController::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    emit queryChanged();
    if (query.trimmed().isEmpty()) {
        ++m_generation;
        m_model->clear();
        // Facets are separate lists and would otherwise survive an emptied
        // query, leaving people and genres on screen for a search that is gone.
        m_people.clear();
        m_genres.clear();
        emit facetsChanged();
        m_debounce.stop();
        return;
    }
    m_debounce.start();
}

void SearchController::runSearch()
{
    const int generation = ++m_generation;
    m_searching = true;
    emit searchingChanged();

    ItemsQuery query;
    query.searchTerm = m_query.trimmed();
    query.recursive = true;
    // BoxSet included so collections are findable by name. Without it the
    // search page's Collections section can never populate, however correct the
    // QML is.
    query.includeItemTypes = {QStringLiteral("Movie"),      QStringLiteral("Series"),
                              QStringLiteral("Episode"),    QStringLiteral("BoxSet"),
                              QStringLiteral("MusicAlbum"), QStringLiteral("MusicArtist"),
                              QStringLiteral("Audio")};
    // ArtistItems carries the ids "go to artist" navigates by; without it a
    // track result knows the artist's NAME and cannot open their page, so the
    // row silently does not appear.
    query.fields = {QStringLiteral("Overview"), QStringLiteral("ArtistItems")};
    query.limit = kResultLimit;

    // People and genres are their own endpoints and land independently; the item
    // results must not wait on them.
    m_people.clear();
    m_genres.clear();
    emit facetsChanged();
    const QString term = m_query.trimmed();
    m_client->persons(term, kFacetLimit)
        .then(this, [this, generation](const Result<QList<MediaItem>> &result) {
            if (generation != m_generation || !result.ok())
                return;
            m_people = facetList(result.value);
            emit facetsChanged();
        });
    m_client->genres(term, kFacetLimit)
        .then(this, [this, generation](const Result<QList<MediaItem>> &result) {
            if (generation != m_generation || !result.ok())
                return;
            m_genres = facetList(result.value);
            emit facetsChanged();
        });

    m_client->items(query).then(this, [this, generation](const Result<ItemsPage> &result) {
        if (generation != m_generation)
            return;
        m_searching = false;
        emit searchingChanged();
        if (result.ok())
            m_model->setItems(result.value.items, result.value.totalRecordCount);
        else
            qCWarning(logApp) << "search failed:" << result.error;
    });
}

void SearchController::noteQueryUsed(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return;
    m_recentQueries.removeAll(trimmed);
    m_recentQueries.prepend(trimmed);
    while (m_recentQueries.size() > kRecentLimit)
        m_recentQueries.removeLast();
    QSettings().setValue(kRecentKey, m_recentQueries);
    emit recentQueriesChanged();
}

void SearchController::clearRecentQueries()
{
    if (m_recentQueries.isEmpty())
        return;
    m_recentQueries.clear();
    QSettings().remove(kRecentKey);
    emit recentQueriesChanged();
}

} // namespace strmqt
