#include "LibraryController.h"

#include "app/controllers/LiveUpdateService.h"
#include "app/models/MediaItemModel.h"
#include "core/Log.h"
#include "server/emby/EmbyClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QVariantMap>

#include <algorithm>

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

// ── Metadata filters ────────────────────────────────────────────────────────
// One row per axis. `path` is the server list the values come from, scoped by
// the same ParentId / IncludeItemTypes as the grid so a library is only offered
// what it holds; an empty path means the values are the fixed `choices` below.
// `byId` says which field of a listed value the items query wants back.
//
// Every axis here was measured against the server before it was added
// (ItemsQuery.h). Runtime is absent on purpose: the server has no runtime
// bound under any spelling, only a sort key — which the sort menu has.
enum class FacetKinds { Any, Films, Shows };

struct FacetSpec
{
    const char *key;
    const char *label;
    bool multi;
    FacetKinds kinds;
    const char *path;
    bool byId;
};

const FacetSpec kFacets[] = {
    {"genre", QT_TRANSLATE_NOOP("strmqt::LibraryController", "Genre"), true, FacetKinds::Any,
     "/Genres", true},
    {"year", QT_TRANSLATE_NOOP("strmqt::LibraryController", "Year"), true, FacetKinds::Any,
     "/Years", false},
    {"studio", QT_TRANSLATE_NOOP("strmqt::LibraryController", "Studio"), true, FacetKinds::Any,
     "/Studios", true},
    {"rating", QT_TRANSLATE_NOOP("strmqt::LibraryController", "Age rating"), true,
     FacetKinds::Any, "/OfficialRatings", false},
    {"score", QT_TRANSLATE_NOOP("strmqt::LibraryController", "Community rating"), false,
     FacetKinds::Any, "", false},
    {"status", QT_TRANSLATE_NOOP("strmqt::LibraryController", "Status"), false, FacetKinds::Shows,
     "", false},
    {"resolution", QT_TRANSLATE_NOOP("strmqt::LibraryController", "Resolution"), false,
     FacetKinds::Films, "", false},
    {"range", QT_TRANSLATE_NOOP("strmqt::LibraryController", "Dynamic range"), true,
     FacetKinds::Films, "/ExtendedVideoTypes", true},
    {"videoCodec", QT_TRANSLATE_NOOP("strmqt::LibraryController", "Video codec"), true,
     FacetKinds::Films, "/VideoCodecs", false},
    {"audio", QT_TRANSLATE_NOOP("strmqt::LibraryController", "Audio channels"), true,
     FacetKinds::Films, "/AudioLayouts", false},
};

const FacetSpec *facetSpec(const QString &key)
{
    for (const FacetSpec &spec : kFacets) {
        if (key == QLatin1String(spec.key))
            return &spec;
    }
    return nullptr;
}

QVariant facetOption(const QString &value, const QString &label)
{
    QVariantMap map;
    map.insert(QStringLiteral("value"), value);
    map.insert(QStringLiteral("label"), label);
    return map;
}

// Width, not the server's IsHD / Is4K: those overlap (every 4K film is also
// HD), so they cannot say "1080p and nothing else". The thresholds sit under
// the nominal widths because a cropped or anamorphic encode is narrower than
// its name.
struct WidthBand
{
    const char *value;
    int min;
    int max;
};
const WidthBand kWidthBands[] = {
    {"4k", 3800, 0}, {"1080", 1900, 3799}, {"720", 1200, 1899}, {"sd", 0, 1199}};

// What a codec or layout is called on a box, where the server's own name is a
// lower-case ffmpeg identifier.
QString prettyStreamName(const QString &name)
{
    static const QHash<QString, QString> known{
        {QStringLiteral("h264"), QStringLiteral("H.264")},
        {QStringLiteral("hevc"), QStringLiteral("HEVC (H.265)")},
        {QStringLiteral("av1"), QStringLiteral("AV1")},
        {QStringLiteral("vc1"), QStringLiteral("VC-1")},
        {QStringLiteral("vp9"), QStringLiteral("VP9")},
        {QStringLiteral("mpeg2video"), QStringLiteral("MPEG-2")},
        {QStringLiteral("mpeg4"), QStringLiteral("MPEG-4")},
        {QStringLiteral("mono"), QStringLiteral("Mono")},
        {QStringLiteral("stereo"), QStringLiteral("Stereo")},
        {QStringLiteral("DolbyVision"), QStringLiteral("Dolby Vision")},
    };
    return known.value(name, name);
}

} // namespace

LibraryController::LibraryController(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_model(new MediaItemModel(this))
{
}

void LibraryController::resetSessionState()
{
    ++m_generation;
    m_model->clear();

    m_libraryId.clear();
    m_title.clear();
    m_collectionType.clear();
    m_favoritesOnly = false;
    m_genreId.clear();
    m_personId.clear();
    m_studioId.clear();
    m_collectionScope = false;

    m_sortBy = QStringLiteral("SortName");
    m_sortDescending = false;
    m_watchedFilter = QStringLiteral("all");
    m_nameStartsWith.clear();
    resetFacets();

    setLoading(false);
    setError({});
    setPending(false, 0);
    if (!m_autoApplyUpdates) {
        m_autoApplyUpdates = true;
        emit autoApplyUpdatesChanged();
    }
    emit titleChanged();
    emit scopeChanged();
    emit facetsChanged();
    emit queryChanged();
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
    connect(service, &LiveUpdateService::userDataInvalidated, this,
            &LibraryController::onUserDataInvalidated);
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
    applyFacets(query);
    query.fields = {QStringLiteral("Overview")};
    return query;
}

void LibraryController::applyFacets(ItemsQuery &query) const
{
    const auto picked = [this](const char *key) {
        return m_facetSelection.value(QLatin1String(key));
    };
    // Appended, not assigned: a genre or studio VIEW already carries its own
    // id, and that facet is not offered there (facets()).
    query.genreIds += picked("genre");
    query.studioIds += picked("studio");
    for (const QString &year : picked("year")) {
        bool ok = false;
        const int value = year.toInt(&ok);
        if (ok)
            query.years.append(value);
    }
    query.officialRatings = picked("rating");
    query.extendedVideoTypes = picked("range");
    query.videoCodecs = picked("videoCodec");
    query.audioLayouts = picked("audio");
    query.seriesStatus = picked("status").value(0);
    query.minCommunityRating = picked("score").value(0).toDouble();
    const QString resolution = picked("resolution").value(0);
    for (const WidthBand &band : kWidthBands) {
        if (resolution == QLatin1String(band.value)) {
            query.minWidth = band.min;
            query.maxWidth = band.max;
        }
    }
}

// ── Metadata filters ────────────────────────────────────────────────────────

QVariantList LibraryController::facets() const
{
    QVariantList out;
    // A collection lists its direct members in the server's own order and is
    // a handful of titles; music browses through pages of its own.
    if (!hasQuery() || m_collectionScope || m_collectionType == QLatin1String("music"))
        return out;
    const bool films = m_collectionType == QLatin1String("movies");
    const bool shows = m_collectionType == QLatin1String("tvshows");
    for (const FacetSpec &spec : kFacets) {
        if (spec.kinds == FacetKinds::Films && !films)
            continue;
        if (spec.kinds == FacetKinds::Shows && !shows)
            continue;
        const QString key = QLatin1String(spec.key);
        // The axis a scoped view is already pinned to is not also a filter.
        if ((key == QLatin1String("genre") && !m_genreId.isEmpty())
            || (key == QLatin1String("studio") && !m_studioId.isEmpty()))
            continue;
        const QStringList picked = m_facetSelection.value(key);
        QStringList names;
        const QVariantList options = m_facetOptions.value(key);
        for (const QString &value : picked) {
            QString label = value;
            for (const QVariant &option : options) {
                const QVariantMap map = option.toMap();
                if (map.value(QStringLiteral("value")).toString() == value) {
                    label = map.value(QStringLiteral("label")).toString();
                    break;
                }
            }
            names.append(label);
        }
        QVariantMap map;
        map.insert(QStringLiteral("key"), key);
        // Television files a show under the network that aired it.
        map.insert(QStringLiteral("label"), key == QLatin1String("studio") && shows
                                                ? tr("Network")
                                                : tr(spec.label));
        map.insert(QStringLiteral("multi"), spec.multi);
        map.insert(QStringLiteral("count"), static_cast<int>(picked.size()));
        map.insert(QStringLiteral("summary"), names.join(QStringLiteral(", ")));
        out.append(map);
    }
    return out;
}

int LibraryController::activeFacetCount() const
{
    int count = 0;
    for (auto it = m_facetSelection.cbegin(); it != m_facetSelection.cend(); ++it) {
        if (!it.value().isEmpty())
            ++count;
    }
    return count;
}

QVariantList LibraryController::facetOptions(const QString &key)
{
    const FacetSpec *spec = facetSpec(key);
    if (!spec)
        return {};
    if (!m_facetOptions.contains(key)) {
        if (spec->path[0] != '\0') {
            loadFacetOptions(key);
            return {};
        }
        QVariantList fixed;
        if (key == QLatin1String("resolution")) {
            fixed = {facetOption(QStringLiteral("4k"), tr("4K")),
                     facetOption(QStringLiteral("1080"), tr("1080p")),
                     facetOption(QStringLiteral("720"), tr("720p")),
                     facetOption(QStringLiteral("sd"), tr("SD"))};
        } else if (key == QLatin1String("status")) {
            fixed = {facetOption(QStringLiteral("Continuing"), tr("Continuing")),
                     facetOption(QStringLiteral("Ended"), tr("Ended"))};
        } else if (key == QLatin1String("score")) {
            for (int floor = 9; floor >= 5; --floor)
                fixed.append(facetOption(QString::number(floor), tr("%1 and up").arg(floor)));
        }
        m_facetOptions.insert(key, fixed);
    }
    const QStringList picked = m_facetSelection.value(key);
    QVariantList out;
    const QVariantList options = m_facetOptions.value(key);
    out.reserve(options.size());
    for (const QVariant &option : options) {
        QVariantMap map = option.toMap();
        map.insert(QStringLiteral("selected"),
                   picked.contains(map.value(QStringLiteral("value")).toString()));
        out.append(map);
    }
    return out;
}

bool LibraryController::facetLoading(const QString &key) const
{
    return m_facetLoading.contains(key);
}

void LibraryController::loadFacetOptions(const QString &key)
{
    const FacetSpec *spec = facetSpec(key);
    if (!spec || !m_client || m_facetLoading.contains(key))
        return;
    m_facetLoading.insert(key);

    // The same scope as the grid, so a library is offered only what it holds.
    const ItemsQuery scope = currentQuery();
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("UserId"), QStringLiteral("{uid}"));
    if (!scope.parentId.isEmpty())
        params.addQueryItem(QStringLiteral("ParentId"), scope.parentId);
    if (!scope.includeItemTypes.isEmpty())
        params.addQueryItem(QStringLiteral("IncludeItemTypes"),
                            scope.includeItemTypes.join(QLatin1Char(',')));
    params.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
    params.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("SortName"));
    // Names only: a studio list is well over a thousand rows on a film
    // library and each would otherwise carry its artwork tags and user data.
    params.addQueryItem(QStringLiteral("EnableImages"), QStringLiteral("false"));
    params.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("false"));

    const int generation = m_facetGeneration;
    const bool byId = spec->byId;
    m_client->getJson(QLatin1String(spec->path), params)
        .then(this, [this, key, generation, byId](const Result<QJsonDocument> &result) {
            if (generation != m_facetGeneration)
                return;
            m_facetLoading.remove(key);
            QVariantList options;
            if (result.ok()) {
                const QJsonArray items =
                    result.value.object().value(QLatin1String("Items")).toArray();
                options.reserve(items.size());
                for (const QJsonValue &entry : items) {
                    const QJsonObject object = entry.toObject();
                    const QString name = object.value(QLatin1String("Name")).toString();
                    const QJsonValue idValue = object.value(QLatin1String("Id"));
                    const QString id = idValue.isString() ? idValue.toString()
                                       : idValue.isDouble()
                                           ? QString::number(idValue.toVariant().toLongLong())
                                           : QString();
                    const QString value = byId ? id : name;
                    if (name.isEmpty() || value.isEmpty())
                        continue;
                    options.append(facetOption(value, prettyStreamName(name)));
                }
                // Newest first: nobody opens a year list looking for 1938.
                if (key == QLatin1String("year"))
                    std::reverse(options.begin(), options.end());
                m_facetOptions.insert(key, options);
            } else {
                // Not cached, so opening the facet again asks again.
                qCWarning(logApp) << "filter values for" << key << "failed:" << result.error;
            }
            emit facetOptionsChanged(key);
            // A summary names what is picked, and may have had only ids.
            if (!m_facetSelection.value(key).isEmpty())
                emit facetsChanged();
        });
}

void LibraryController::toggleFacetValue(const QString &key, const QString &value)
{
    const FacetSpec *spec = facetSpec(key);
    if (!spec || value.isEmpty())
        return;
    QStringList picked = m_facetSelection.value(key);
    if (picked.contains(value))
        picked.removeAll(value);
    else if (spec->multi)
        picked.append(value);
    else
        picked = {value};
    if (picked.isEmpty())
        m_facetSelection.remove(key);
    else
        m_facetSelection.insert(key, picked);
    emit facetsChanged();
    emit facetOptionsChanged(key);
    applyQueryChange();
}

void LibraryController::clearFacet(const QString &key)
{
    if (!m_facetSelection.contains(key))
        return;
    m_facetSelection.remove(key);
    emit facetsChanged();
    emit facetOptionsChanged(key);
    applyQueryChange();
}

void LibraryController::resetFacets()
{
    ++m_facetGeneration;
    m_facetSelection.clear();
    m_facetOptions.clear();
    m_facetLoading.clear();
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
    return m_watchedFilter != QLatin1String("all") || !m_nameStartsWith.isEmpty()
           || !m_facetSelection.isEmpty();
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
    const QStringList keys = m_facetSelection.keys();
    m_facetSelection.clear();
    emit facetsChanged();
    for (const QString &key : keys)
        emit facetOptionsChanged(key);
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
    // The lists go too: they were fetched for the library being left.
    resetFacets();
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
    emit facetsChanged();
    emit queryChanged();
    resetFor(title);
    fetchPage(0);
}

void LibraryController::openFavorites()
{
    clearScope();
    m_favoritesOnly = true;
    emit scopeChanged();
    emit facetsChanged();
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
    emit facetsChanged();
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
    emit facetsChanged();
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
    emit facetsChanged();
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
    emit facetsChanged();
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
    probeForUpdates();
}

void LibraryController::probeForUpdates(bool announceEvenWithoutGrowth)
{
    const int generation = m_generation;
    const int knownTotal = m_model->totalRecordCount();

    ItemsQuery query = currentQuery();
    query.fields.clear(); // the count is all we want
    query.startIndex = 0;
    query.limit = 1;

    m_client->items(query).then(
        this, [this, generation, knownTotal,
               announceEvenWithoutGrowth](const Result<ItemsPage> &result) {
            if (generation != m_generation || !result.ok())
                return;
            const int delta = result.value.totalRecordCount - knownTotal;
            if (delta <= 0 && !announceEvenWithoutGrowth) {
                // Items were removed or only edited: nothing new to announce, and
                // nothing worth yanking the grid for.
                return;
            }
            setPending(true, qMax(0, delta));
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
                                entry.value(QStringLiteral("favorite")).toBool(),
                                entry.value(QStringLiteral("positionTicks")).toLongLong(),
                                entry.value(QStringLiteral("playCount")).toInt());
    }
}

void LibraryController::onUserDataInvalidated(const QStringList &itemIds)
{
    Q_UNUSED(itemIds);
    if (!hasQuery() || m_loading)
        return;

    // User data changes membership only for these server-side filters, and can
    // reorder DatePlayed. Every other query is fully reconciled by the rich
    // in-place patch above.
    const bool membershipSensitive = m_favoritesOnly
                                  || m_watchedFilter != QLatin1String("all")
                                  || m_sortBy == QLatin1String("DatePlayed");
    if (!membershipSensitive)
        return;
    if (m_autoApplyUpdates && m_model->rowCount() <= kPageSize) {
        reload();
        return;
    }
    // A later page must not be reset underneath the cursor. Even an unchanged
    // total can hide a remove+add or a reorder, so announce an "Updated" pill.
    probeForUpdates(/*announceEvenWithoutGrowth=*/true);
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
                m_model->appendItems(result.value.items, result.value.totalRecordCount);
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
