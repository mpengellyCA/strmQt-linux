#include "ItemActions.h"

#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "app/models/MediaItemModel.h"
#include "core/Log.h"
#include "core/Result.h"
#include "server/dto/ItemsPage.h"
#include "server/emby/EmbyClient.h"

#include <QMetaType>
#include <QRandomGenerator>
#include <QSet>

#include <algorithm>
#include <utility>

namespace strmqt {

namespace {

const auto kItemIdKey = QStringLiteral("itemId");

// One shuffle or "play all" fetches at most this many items. A whole library is
// not a queue: Emby happily returns tens of thousands of rows, and every one of
// them would be parsed, kept in memory and shuffled for a session the user will
// abandon after three episodes. 500 comfortably covers a series, a season, a
// collection and any realistic "shuffle this library" sample, and — because the
// shuffle fetch is SortBy=Random — a truncated fetch is still a fair sample of
// the whole library rather than its first 500 items alphabetically.
constexpr int kQueueFetchLimit = 500;

// Which item types are actually playable in a library of this kind. Without
// this a shuffle of a TV library queues series folders, which play nothing.
QStringList playableTypesFor(const QString &collectionType)
{
    const QString kind = collectionType.toLower();
    if (kind == QLatin1String("tvshows"))
        return {QStringLiteral("Episode")};
    if (kind == QLatin1String("movies"))
        return {QStringLiteral("Movie")};
    if (kind == QLatin1String("music"))
        return {QStringLiteral("Audio")};
    if (kind == QLatin1String("musicvideos"))
        return {QStringLiteral("MusicVideo")};
    if (kind == QLatin1String("homevideos") || kind == QLatin1String("photos"))
        return {QStringLiteral("Video")};
    // Collections and unknown kinds: every leaf that can be played, no folders.
    return {QStringLiteral("Movie"), QStringLiteral("Episode"), QStringLiteral("Video")};
}

// Air order for episodes, name order for everything else. PremiereDate is used
// rather than IndexNumber because it is a sort key every Emby 4.x server
// accepts and it orders a multi-season "play all" correctly.
// The collectionType playAll() expects, derived from the container's own kind.
// An album plays in track order, a series in air order, and playableTypesFor()
// needs to know which kinds of child to ask for.
QString containerCollectionType(const QString &type)
{
    if (type.compare(QLatin1String("MusicAlbum"), Qt::CaseInsensitive) == 0
        || type.compare(QLatin1String("MusicArtist"), Qt::CaseInsensitive) == 0
        || type.compare(QLatin1String("MusicGenre"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("music");
    if (type.compare(QLatin1String("Series"), Qt::CaseInsensitive) == 0
        || type.compare(QLatin1String("Season"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("tvshows");
    return {};
}

QString playAllSortFor(const QStringList &types)
{
    if (types.size() == 1 && types.first() == QLatin1String("Episode"))
        return QStringLiteral("PremiereDate,SortName");
    // Music plays in TRACK order. SortName would play an album alphabetically
    // by title, which is never what anyone means.
    //
    // Deliberately NOT "ParentIndexNumber,IndexNumber": measured over 4,500
    // tracks of the target library, 2,700 have NO disc number and not one has
    // a disc above 1. Sorting on disc first therefore sorts most albums by a
    // field that is null, and a null disc sorts ahead of disc 1 — which turned
    // a 1,2,2 album into 2,1,2. Track number alone is right for every
    // single-disc album, and merely interleaves a genuine multi-disc one,
    // which is the smaller failure and unavoidable while 60% of the data
    // lacks the field.
    if (types.size() == 1 && types.first() == QLatin1String("Audio"))
        return QStringLiteral("IndexNumber,SortName");
    return QStringLiteral("SortName");
}

QString titleFor(const QVariantMap &item)
{
    const QString label = item.value(QStringLiteral("label")).toString();
    if (!label.isEmpty())
        return label;
    const QString name = item.value(QStringLiteral("name")).toString();
    if (!name.isEmpty())
        return name;
    return item.value(kItemIdKey).toString();
}

} // namespace

ItemActions::ItemActions(emby::EmbyClient *client, PlayerController *player, QObject *parent)
    : QObject(parent), m_client(client), m_player(player)
{
}

void ItemActions::resetSessionState()
{
    ++m_playbackIntentGeneration;
    ++m_sessionGeneration;
    m_state.clear();
    m_playedRequests.clear();
    m_favoriteRequests.clear();
    m_models.removeIf([](const QPointer<MediaItemModel> &model) { return model.isNull(); });
    for (const QPointer<MediaItemModel> &model : std::as_const(m_models))
        model->clear();
}

void ItemActions::registerModel(MediaItemModel *model)
{
    if (!model)
        return;
    for (const QPointer<MediaItemModel> &known : std::as_const(m_models)) {
        if (known == model)
            return;
    }
    m_models.append(model);
}

void ItemActions::unregisterModel(MediaItemModel *model)
{
    m_models.removeIf([model](const QPointer<MediaItemModel> &known) {
        return known.isNull() || known == model;
    });
}

// ── Item resolution ───────────────────────────────────────────────────────────

QVariantMap ItemActions::itemFor(const QString &itemId) const
{
    if (itemId.isEmpty())
        return {};
    for (const QPointer<MediaItemModel> &model : m_models) {
        if (model.isNull())
            continue;
        const QList<MediaItem> &items = model->items();
        for (int row = 0; row < items.size(); ++row) {
            if (items.at(row).id != itemId)
                continue;
            QVariantMap map = model->get(row);
            // Not exposed as model roles, but openSeries() needs them.
            map.insert(QStringLiteral("seriesId"), items.at(row).seriesId);
            map.insert(QStringLiteral("seasonId"), items.at(row).seasonId);
            return map;
        }
    }
    return {};
}

QVariantMap ItemActions::resolve(const QVariant &item) const
{
    if (item.typeId() == QMetaType::QString)
        return itemFor(item.toString());

    QVariantMap map = item.toMap();
    if (map.isEmpty())
        return {};
    // Tolerate {id: ...} as well as the model's {itemId: ...}.
    if (!map.contains(kItemIdKey) && map.contains(QStringLiteral("id")))
        map.insert(kItemIdKey, map.value(QStringLiteral("id")));

    const QString itemId = map.value(kItemIdKey).toString();
    if (itemId.isEmpty())
        return map;
    // A map handed over from QML carries only the model roles; fill in what a
    // registered model knows and the caller could not have (series ids), and
    // let the live user state win over the caller's snapshot.
    if (!map.contains(QStringLiteral("seriesId"))) {
        const QVariantMap known = itemFor(itemId);
        if (!known.isEmpty()) {
            map.insert(QStringLiteral("seriesId"), known.value(QStringLiteral("seriesId")));
            map.insert(QStringLiteral("seasonId"), known.value(QStringLiteral("seasonId")));
        }
    }
    return map;
}

// ── Playback ──────────────────────────────────────────────────────────────────

bool ItemActions::isContainer(const QString &type)
{
    // Emby item kinds that HOLD media rather than being media. Asking the
    // server for PlaybackInfo on one is a 500, not an empty answer:
    //
    //   Unable to cast object of type '…Audio.MusicAlbum'
    //   to type '…IHasMediaSources'
    //
    // Measured on the live server: Series, BoxSet, MusicArtist and MusicAlbum
    // all return HTTP 500. So "play" on any of these has to mean "play what is
    // inside it", which is what playAll already does.
    static const QStringList kContainers{
        QStringLiteral("MusicAlbum"), QStringLiteral("MusicArtist"),
        QStringLiteral("Series"),     QStringLiteral("Season"),
        QStringLiteral("BoxSet"),     QStringLiteral("Playlist"),
        QStringLiteral("Folder"),     QStringLiteral("CollectionFolder"),
        QStringLiteral("MusicGenre"), QStringLiteral("Genre"),
    };
    return kContainers.contains(type, Qt::CaseInsensitive);
}

void ItemActions::startPlayback(const QVariant &item, bool fromStart)
{
    const QVariantMap map = resolve(item);
    const QString itemId = map.value(kItemIdKey).toString();
    if (itemId.isEmpty()) {
        qCWarning(logApp) << "ItemActions: play requested without an item id";
        return;
    }

    // A folder cannot be played, only its contents can. Without this, pressing
    // Play on an album, a series, a collection or an artist asked the server
    // for a media source the item does not have and surfaced a raw HTTP 500.
    const QString type = map.value(QStringLiteral("type")).toString();
    if (isContainer(type)) {
        if (type.compare(QLatin1String("BoxSet"), Qt::CaseInsensitive) == 0) {
            playCollection(itemId); // curated order, not SortName
            return;
        }
        playAll(itemId, containerCollectionType(type));
        return;
    }
    if (!m_player) {
        qCWarning(logApp) << "ItemActions: no player controller; cannot play" << itemId;
        emit actionFailed(tr("Playback is not available."));
        return;
    }
    beginPlaybackIntent();
    qint64 startMs = 0;
    if (!fromStart && map.value(QStringLiteral("resumable")).toBool())
        startMs = map.value(QStringLiteral("positionMs")).toLongLong();
    // The type travels with the item. A bare play seeds a one-item queue, and
    // that seed is what PlayerController::isAudio reads first: without the type
    // the answer waits on the network and the bar lays itself out for whatever
    // was playing before.
    m_player->playItem(itemId, titleFor(map), std::max<qint64>(0, startMs), -1, type);
}

void ItemActions::play(const QVariant &item)
{
    startPlayback(item, false);
}

void ItemActions::playFromStart(const QVariant &item)
{
    startPlayback(item, true);
}

void ItemActions::resume(const QVariant &item)
{
    const QVariantMap map = resolve(item);
    const QString itemId = map.value(kItemIdKey).toString();
    if (itemId.isEmpty()) {
        qCWarning(logApp) << "ItemActions: resume requested without an item id";
        return;
    }
    if (!m_player) {
        qCWarning(logApp) << "ItemActions: no player controller; cannot resume" << itemId;
        emit actionFailed(tr("Playback is not available."));
        return;
    }
    beginPlaybackIntent();
    // Unlike play(), an explicit resume honours a stored position even for an
    // item already marked played (which clears the "resumable" role).
    const qint64 startMs = map.value(QStringLiteral("positionMs")).toLongLong();
    m_player->playItem(itemId, titleFor(map), std::max<qint64>(0, startMs), -1,
                       map.value(QStringLiteral("type")).toString());
}

// ── Queue (ARCHITECTURE.md) ────────────────────────────────────────────────

// Every queue verb needs a player to put the queue in; saying so once keeps the
// four of them honest about it instead of failing silently.
bool ItemActions::requireQueueTarget()
{
    if (m_player)
        return true;
    qCWarning(logApp) << "ItemActions: no player controller; cannot touch the queue";
    emit actionFailed(tr("Playback is not available."));
    return false;
}

quint64 ItemActions::beginPlaybackIntent()
{
    return ++m_playbackIntentGeneration;
}

void ItemActions::playNext(const QVariant &item)
{
    const QVariantMap map = resolve(item);
    if (map.value(kItemIdKey).toString().isEmpty()) {
        qCWarning(logApp) << "ItemActions: playNext without an item id";
        return;
    }
    if (!requireQueueTarget())
        return;
    m_player->queue()->playNext(map);
    emit queueChanged();
}

void ItemActions::addToQueue(const QVariant &item)
{
    const QVariantMap map = resolve(item);
    if (map.value(kItemIdKey).toString().isEmpty()) {
        qCWarning(logApp) << "ItemActions: addToQueue without an item id";
        return;
    }
    if (!requireQueueTarget())
        return;
    m_player->queue()->addToQueue(map);
    emit queueChanged();
}

void ItemActions::playAllFrom(const QVariantList &items, int startIndex)
{
    const quint64 generation = beginPlaybackIntent();
    playAllFromIfCurrent(items, startIndex, generation);
}

quint64 ItemActions::reservePlaybackIntent()
{
    return beginPlaybackIntent();
}

bool ItemActions::isPlaybackIntentCurrent(quint64 generation) const
{
    return generation == m_playbackIntentGeneration;
}

void ItemActions::playAllFromIfCurrent(const QVariantList &items, int startIndex,
                                       quint64 generation)
{
    if (!isPlaybackIntentCurrent(generation))
        return;
    if (items.isEmpty()) {
        emit actionFailed(tr("There is nothing to play here."));
        return;
    }
    if (!requireQueueTarget())
        return;
    m_player->playQueue(items, startIndex);
    emit queueChanged();
}

void ItemActions::addAllToQueue(const QVariantList &items)
{
    if (items.isEmpty()) {
        emit actionFailed(tr("There is nothing to queue here."));
        return;
    }
    if (!requireQueueTarget())
        return;
    QList<MediaItem> queueItems;
    queueItems.reserve(items.size());
    for (const QVariant &entry : items) {
        const QVariantMap map = resolve(entry);
        if (map.value(kItemIdKey).toString().isEmpty())
            continue;
        queueItems.append(PlayQueue::itemFromVariant(map));
    }
    const int added = m_player->queue()->addToQueue(queueItems);
    if (added == 0) {
        qCWarning(logApp) << "ItemActions: addAllToQueue had nothing with an item id";
        emit actionFailed(tr("There is nothing to queue here."));
        return;
    }
    // Once, for the whole gesture. Forty toasts is not forty pieces of
    // feedback, it is one piece of feedback and thirty-nine obstructions.
    emit queueChanged();
}

void ItemActions::playNextAll(const QVariantList &items)
{
    if (items.isEmpty() || !requireQueueTarget())
        return;
    QList<MediaItem> queueItems;
    queueItems.reserve(items.size());
    for (const QVariant &entry : items) {
        const QVariantMap map = resolve(entry);
        if (!map.value(kItemIdKey).toString().isEmpty())
            queueItems.append(PlayQueue::itemFromVariant(map));
    }
    if (m_player->queue()->playNext(queueItems) > 0)
        emit queueChanged();
}

void ItemActions::setFavoriteAll(const QStringList &itemIds, bool favorite)
{
    for (const QString &itemId : itemIds)
        setFavorite(itemId, favorite);
}

void ItemActions::instantMix(const QVariant &item)
{
    // An id is the whole of what this verb needs, so a bare id is taken at face
    // value rather than looked up. resolve() answers a QString by searching the
    // registered models, and **MusicController's four models are not registered
    // here** — so every album, artist and track a music page could name would
    // resolve to an empty map and the mix would silently never happen. Found by
    // the test below, which is what it is for.
    const QString itemId = item.typeId() == QMetaType::QString
                               ? item.toString()
                               : resolve(item).value(kItemIdKey).toString();
    if (itemId.isEmpty()) {
        qCWarning(logApp) << "ItemActions: instantMix without an item id";
        return;
    }
    if (!requireQueueTarget())
        return;
    if (!m_client) {
        qCWarning(logApp) << "ItemActions: no server client; cannot mix" << itemId;
        emit actionFailed(tr("Not connected to the server."));
        return;
    }

    const quint64 generation = beginPlaybackIntent();

    m_client->instantMix(itemId, kQueueFetchLimit)
        .then(this, [this, generation](const Result<ItemsPage> &result) {
            if (generation != m_playbackIntentGeneration)
                return;
            if (!result.ok()) {
                emit actionFailed(tr("Could not build an instant mix: %1").arg(result.error));
                return;
            }
            // De-duplicate, in the order the server sent. Measured: 500 rows
            // asked for came back with 493 distinct ids, and PlayQueue keys
            // entries rather than ids, so the repeats would survive all the way
            // to the queue panel and read as a bug.
            QList<MediaItem> mix;
            QSet<QString> seen;
            mix.reserve(result.value.items.size());
            for (const MediaItem &entry : result.value.items) {
                if (entry.id.isEmpty() || seen.contains(entry.id))
                    continue;
                seen.insert(entry.id);
                mix.append(entry);
            }
            if (mix.isEmpty()) {
                emit actionFailed(tr("There is nothing to play here."));
                return;
            }
            // NOT flagged shuffled. `shuffled` means "there is an original
            // order to give back when the user turns shuffle off", and a mix
            // has none: its order is the station. Flagging it would put a lit
            // shuffle button over a queue that un-shuffles to itself.
            m_player->playQueueItems(mix, 0, false);
            emit queueChanged();
        });
}

void ItemActions::fetchIntoQueue(const ItemsQuery &query, bool shuffled, bool randomStart)
{
    if (!requireQueueTarget())
        return;
    if (!m_client) {
        qCWarning(logApp) << "ItemActions: no server client; cannot build a queue";
        emit actionFailed(tr("Not connected to the server."));
        return;
    }

    const quint64 generation = beginPlaybackIntent();

    m_client->items(query).then(this, [this, shuffled, randomStart,
                                      generation](const Result<ItemsPage> &result) {
        if (generation != m_playbackIntentGeneration)
            return;
        if (!result.ok()) {
            // A queue that silently does not appear is the worst possible
            // outcome here: say so.
            emit actionFailed(tr("Could not build the queue: %1").arg(result.error));
            return;
        }
        if (result.value.items.isEmpty()) {
            emit actionFailed(tr("There is nothing to play here."));
            return;
        }
        const int count = static_cast<int>(result.value.items.size());
        const int startIndex =
            randomStart && count > 1 ? int(QRandomGenerator::global()->bounded(count)) : 0;
        m_player->playQueueItems(result.value.items, startIndex, shuffled);
        emit queueChanged();
    });
}

void ItemActions::playCollection(const QString &collectionId)
{
    if (collectionId.isEmpty()) {
        qCWarning(logApp) << "ItemActions: playCollection without an id";
        return;
    }
    ItemsQuery query;
    query.parentId = collectionId;
    // No sortBy and no recursion: see the header. Both are deliberate and both
    // differ from playAll().
    query.recursive = false;
    query.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series"),
                              QStringLiteral("Episode")};
    query.limit = kQueueFetchLimit;
    fetchIntoQueue(query, false, false);
}

void ItemActions::playAll(const QString &parentId, const QString &collectionType)
{
    if (parentId.isEmpty()) {
        qCWarning(logApp) << "ItemActions: playAll without a parent id";
        return;
    }
    ItemsQuery query;
    query.parentId = parentId;
    query.recursive = true;
    query.includeItemTypes = playableTypesFor(collectionType);
    query.sortBy = playAllSortFor(query.includeItemTypes);
    query.limit = kQueueFetchLimit;
    fetchIntoQueue(query, false, false);
}

void ItemActions::shuffle(const QString &parentId, const QString &collectionType)
{
    if (parentId.isEmpty()) {
        qCWarning(logApp) << "ItemActions: shuffle without a parent id";
        return;
    }
    ItemsQuery query;
    query.parentId = parentId;
    query.recursive = true;
    query.includeItemTypes = playableTypesFor(collectionType);
    // The server does the sampling: SortBy=Random means a capped fetch is still
    // drawn from the whole library, not from its first page.
    query.sortBy = QStringLiteral("Random");
    query.limit = kQueueFetchLimit;
    // The order is already random, so the queue is flagged shuffled for the UI
    // and un-shuffling gives back the order the server returned.
    fetchIntoQueue(query, true, false);
}

void ItemActions::shuffleSeries(const QString &seriesId)
{
    if (seriesId.isEmpty()) {
        qCWarning(logApp) << "ItemActions: shuffleSeries without a series id";
        return;
    }
    if (!requireQueueTarget())
        return;
    if (!m_client) {
        qCWarning(logApp) << "ItemActions: no server client; cannot shuffle" << seriesId;
        emit actionFailed(tr("Not connected to the server."));
        return;
    }

    const quint64 generation = beginPlaybackIntent();

    // /Shows/{id}/Episodes returns every episode of the series in air order, so
    // the queue keeps a real order to restore when the user turns shuffle off —
    // and the random start index is what makes the *first* item random too.
    m_client->episodes(seriesId, {}).then(this, [this, generation](const Result<ItemsPage> &result) {
        if (generation != m_playbackIntentGeneration)
            return;
        if (!result.ok()) {
            emit actionFailed(tr("Could not shuffle this series: %1").arg(result.error));
            return;
        }
        if (result.value.items.isEmpty()) {
            emit actionFailed(tr("There is nothing to play here."));
            return;
        }
        const int count = static_cast<int>(result.value.items.size());
        const int startIndex = count > 1 ? int(QRandomGenerator::global()->bounded(count)) : 0;
        m_player->playQueueItems(result.value.items, startIndex, true);
        emit queueChanged();
    });
}

// ── User state ────────────────────────────────────────────────────────────────

ItemActions::UserState ItemActions::knownState(const QString &itemId) const
{
    const auto cached = m_state.constFind(itemId);
    if (cached != m_state.cend())
        return *cached;
    for (const QPointer<MediaItemModel> &model : m_models) {
        if (model.isNull())
            continue;
        for (const MediaItem &item : model->items()) {
            if (item.id == itemId)
                return {item.played, item.favorite};
        }
    }
    return {};
}

void ItemActions::patchModels(const QString &itemId, const UserState &state)
{
    m_models.removeIf([](const QPointer<MediaItemModel> &model) { return model.isNull(); });
    for (const QPointer<MediaItemModel> &model : std::as_const(m_models))
        model->updateUserData(itemId, state.played, state.favorite);
}

void ItemActions::applyPlayed(const QString &itemId, bool played)
{
    UserState state = knownState(itemId);
    state.played = played;
    m_state.insert(itemId, state);
    patchModels(itemId, state);
    emit playedChanged(itemId, played);
}

void ItemActions::applyFavorite(const QString &itemId, bool favorite)
{
    UserState state = knownState(itemId);
    state.favorite = favorite;
    m_state.insert(itemId, state);
    patchModels(itemId, state);
    emit favoriteChanged(itemId, favorite);
}

bool ItemActions::isPlayed(const QString &itemId) const
{
    return knownState(itemId).played;
}

bool ItemActions::isFavorite(const QString &itemId) const
{
    return knownState(itemId).favorite;
}

void ItemActions::setPlayed(const QString &itemId, bool played)
{
    if (itemId.isEmpty())
        return;
    const bool previous = knownState(itemId).played;
    applyPlayed(itemId, played); // optimistic: the UI moves now

    const auto inFlight = m_playedRequests.find(itemId);
    if (inFlight != m_playedRequests.end()) {
        // Coalesce: one request on the wire per item, one trailing value queued.
        inFlight->hasQueued = true;
        inFlight->queued = played;
        return;
    }
    sendPlayed(itemId, played, previous);
}

void ItemActions::sendPlayed(const QString &itemId, bool played, bool baseline)
{
    if (!m_client) {
        qCWarning(logApp) << "ItemActions: no server client; cannot set played for" << itemId;
        applyPlayed(itemId, baseline);
        emit actionFailed(tr("Not connected to the server."));
        return;
    }

    InFlight request;
    request.requested = played;
    request.baseline = baseline;
    m_playedRequests.insert(itemId, request);
    const quint64 sessionGeneration = m_sessionGeneration;

    m_client->setPlayed(itemId, played)
        .then(this, [this, itemId, sessionGeneration](const Result<bool> &result) {
            if (sessionGeneration != m_sessionGeneration)
                return;
            const auto it = m_playedRequests.find(itemId);
            if (it == m_playedRequests.end())
                return;
            const InFlight finished = *it;
            m_playedRequests.erase(it);
            if (!result.ok()) {
                // Honest rollback: put the UI back where the server actually is.
                applyPlayed(itemId, finished.baseline);
                emit actionFailed(tr("Could not update the watched state: %1").arg(result.error));
                return;
            }
            if (finished.hasQueued && finished.queued != finished.requested)
                sendPlayed(itemId, finished.queued, finished.requested);
        });
}

void ItemActions::togglePlayed(const QVariant &item)
{
    const QVariantMap map = resolve(item);
    const QString itemId = map.value(kItemIdKey).toString();
    if (itemId.isEmpty()) {
        qCWarning(logApp) << "ItemActions: togglePlayed without an item id";
        return;
    }
    bool current = false;
    if (m_state.contains(itemId))
        current = m_state.value(itemId).played; // newest intent wins over a stale map
    else if (map.contains(QStringLiteral("played")))
        current = map.value(QStringLiteral("played")).toBool();
    else
        current = knownState(itemId).played;
    setPlayed(itemId, !current);
}

void ItemActions::setFavorite(const QString &itemId, bool favorite)
{
    if (itemId.isEmpty())
        return;
    const bool previous = knownState(itemId).favorite;
    applyFavorite(itemId, favorite);

    const auto inFlight = m_favoriteRequests.find(itemId);
    if (inFlight != m_favoriteRequests.end()) {
        inFlight->hasQueued = true;
        inFlight->queued = favorite;
        return;
    }
    sendFavorite(itemId, favorite, previous);
}

void ItemActions::sendFavorite(const QString &itemId, bool favorite, bool baseline)
{
    if (!m_client) {
        qCWarning(logApp) << "ItemActions: no server client; cannot set favorite for" << itemId;
        applyFavorite(itemId, baseline);
        emit actionFailed(tr("Not connected to the server."));
        return;
    }

    InFlight request;
    request.requested = favorite;
    request.baseline = baseline;
    m_favoriteRequests.insert(itemId, request);
    const quint64 sessionGeneration = m_sessionGeneration;

    m_client->setFavorite(itemId, favorite)
        .then(this, [this, itemId, sessionGeneration](const Result<bool> &result) {
            if (sessionGeneration != m_sessionGeneration)
                return;
            const auto it = m_favoriteRequests.find(itemId);
            if (it == m_favoriteRequests.end())
                return;
            const InFlight finished = *it;
            m_favoriteRequests.erase(it);
            if (!result.ok()) {
                applyFavorite(itemId, finished.baseline);
                emit actionFailed(tr("Could not update the favourite: %1").arg(result.error));
                return;
            }
            if (finished.hasQueued && finished.queued != finished.requested)
                sendFavorite(itemId, finished.queued, finished.requested);
        });
}

void ItemActions::toggleFavorite(const QVariant &item)
{
    const QVariantMap map = resolve(item);
    const QString itemId = map.value(kItemIdKey).toString();
    if (itemId.isEmpty()) {
        qCWarning(logApp) << "ItemActions: toggleFavorite without an item id";
        return;
    }
    bool current = false;
    if (m_state.contains(itemId))
        current = m_state.value(itemId).favorite;
    else if (map.contains(QStringLiteral("favorite")))
        current = map.value(QStringLiteral("favorite")).toBool();
    else
        current = knownState(itemId).favorite;
    setFavorite(itemId, !current);
}

// ── Navigation ────────────────────────────────────────────────────────────────

void ItemActions::browseGenre(const QString &genreId, const QString &name)
{
    if (genreId.isEmpty())
        return; // a payload too old to carry GenreItems: the chip is not a link
    emit browseRequested(QStringLiteral("genre"), genreId, name);
}

void ItemActions::browsePerson(const QString &personId, const QString &name)
{
    if (personId.isEmpty())
        return;
    emit browseRequested(QStringLiteral("person"), personId, name);
}

void ItemActions::browseStudio(const QString &studioId, const QString &name)
{
    if (studioId.isEmpty())
        return;
    emit browseRequested(QStringLiteral("studio"), studioId, name);
}

void ItemActions::browseCollection(const QString &collectionId, const QString &name)
{
    if (collectionId.isEmpty())
        return;
    emit browseRequested(QStringLiteral("collection"), collectionId, name);
}

void ItemActions::openDetails(const QVariant &item)
{
    const QVariantMap map = resolve(item);
    if (map.value(kItemIdKey).toString().isEmpty()) {
        qCWarning(logApp) << "ItemActions: openDetails without an item id";
        return;
    }
    emit detailsRequested(map);
}

void ItemActions::openSeries(const QVariant &item)
{
    const QVariantMap map = resolve(item);
    QString seriesId = map.value(QStringLiteral("seriesId")).toString();
    QString seriesName = map.value(QStringLiteral("seriesName")).toString();
    if (seriesId.isEmpty() &&
        map.value(QStringLiteral("type")).toString() == QLatin1String("Series")) {
        seriesId = map.value(kItemIdKey).toString();
        seriesName = map.value(QStringLiteral("name")).toString();
    }
    if (seriesId.isEmpty()) {
        qCWarning(logApp) << "ItemActions: openSeries has no series for"
                          << map.value(kItemIdKey).toString();
        return;
    }
    emit seriesRequested(seriesId, seriesName);
}

// The ALBUM artist wins over the first credited performer whenever the item
// names one: an artist page is a discography, and a discography is filed under
// the album artist. Landing a compilation track's "go to artist" on its guest
// vocalist opens a page with no albums on it.
//
// The name and the id always describe the SAME artist, or the id is left out.
// EmbyDtoMapper keeps `artists` and `artistIds` index-aligned — an id is empty
// where a credited name has no artist item on the server — so a pair is only
// ever read out of one index, never one from each list.
QVariantMap ItemActions::artistTarget(const QVariant &item) const
{
    const QVariantMap map = resolve(item);
    const QStringList names = map.value(QStringLiteral("artists")).toStringList();
    const QStringList ids = map.value(QStringLiteral("artistIds")).toStringList();
    const QString albumArtist = map.value(QStringLiteral("albumArtist")).toString();

    qsizetype index = names.isEmpty() ? -1 : 0;
    if (!albumArtist.isEmpty()) {
        const qsizetype credited = names.indexOf(albumArtist);
        if (credited >= 0)
            index = credited;
    }
    // No performer list at all: the album artist is still a name worth printing,
    // and an album page reached without one is better than a dead line.
    const QString name = index >= 0 ? names.at(index) : albumArtist;
    const QString id = (index >= 0 && index < ids.size()) ? ids.at(index) : QString();
    if (name.isEmpty() && id.isEmpty())
        return {};

    QVariantMap target;
    target.insert(kItemIdKey, id);
    target.insert(QStringLiteral("name"), name);
    target.insert(QStringLiteral("type"), QStringLiteral("MusicArtist"));
    return target;
}

bool ItemActions::refreshMetadata(const QString &itemId)
{
    if (itemId.isEmpty())
        return false;
    if (!m_client) {
        qCWarning(logApp) << "ItemActions: no server client; cannot refresh" << itemId;
        return false;
    }
    // The server answers 204 and does the work in the background, so a success
    // here means "accepted", not "finished" — there is nothing to update in the
    // models yet, and the user finds out by the item changing later.
    m_client->refreshMetadata(itemId).then(this, [this, itemId](const Result<bool> &result) {
        if (!result.ok()) {
            qCWarning(logApp) << "metadata refresh failed for" << itemId << ":" << result.error;
            emit actionFailed(tr("Could not refresh metadata: %1").arg(result.error));
        }
    });
    return true;
}

} // namespace strmqt
