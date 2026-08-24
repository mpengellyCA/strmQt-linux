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

enum class ItemKind
{
    // A type the server stated and this build does not recognise: refused
    // wherever a container would be, because an unrecognised kind may well be
    // one. Distinct from Unspecified, which is the absence of any claim.
    Unknown,
    // No `type` at all. The remote-control lane addresses items by id alone —
    // an Emby "Play on" command carries ItemIds and nothing else — and the
    // queue row fills in when the item's own details arrive. Refusing these
    // would take remote playback out entirely, and an absent type is not the
    // server telling us this is a folder.
    Unspecified,
    Movie,
    Episode,
    Series,
    Season,
    BoxSet,
    Audio,
    MusicAlbum,
    MusicArtist,
    Playlist,
    Video,
    Folder,
    Genre,
};

// The sole item taxonomy used by ItemActions. A container is not necessarily
// playable: artists expand only through Instant Mix, while playlists must be
// opened because their member-entry identity and order belong to PlaylistCtl.
struct ClassifiedItem
{
    QVariantMap item;
    ItemKind kind = ItemKind::Unknown;
    QString id;
    QString name;

    bool isContainer() const
    {
        switch (kind) {
        case ItemKind::Series:
        case ItemKind::Season:
        case ItemKind::BoxSet:
        case ItemKind::MusicAlbum:
        case ItemKind::MusicArtist:
        case ItemKind::Playlist:
        case ItemKind::Folder:
        case ItemKind::Genre:
            return true;
        default:
            return false;
        }
    }

    bool isMusic() const
    {
        return kind == ItemKind::Audio || kind == ItemKind::MusicAlbum ||
               kind == ItemKind::MusicArtist;
    }

    bool isLeaf() const
    {
        return kind == ItemKind::Movie || kind == ItemKind::Episode || kind == ItemKind::Audio ||
               kind == ItemKind::Video || kind == ItemKind::Unspecified;
    }

    bool expandsForPlayback() const
    {
        return kind == ItemKind::Series || kind == ItemKind::Season || kind == ItemKind::BoxSet ||
               kind == ItemKind::MusicAlbum;
    }
};

ItemKind itemKindFor(const QString &type)
{
    if (type.isEmpty())
        return ItemKind::Unspecified;
    if (type.compare(QLatin1String("Movie"), Qt::CaseInsensitive) == 0)
        return ItemKind::Movie;
    if (type.compare(QLatin1String("Episode"), Qt::CaseInsensitive) == 0)
        return ItemKind::Episode;
    if (type.compare(QLatin1String("Series"), Qt::CaseInsensitive) == 0)
        return ItemKind::Series;
    if (type.compare(QLatin1String("Season"), Qt::CaseInsensitive) == 0)
        return ItemKind::Season;
    if (type.compare(QLatin1String("BoxSet"), Qt::CaseInsensitive) == 0)
        return ItemKind::BoxSet;
    // AudioBook rides with Audio, the way it already does everywhere else that
    // asks: PlayerController::typeIsAudio(), PlayerController::isAudio() and
    // MediaItem::artwork() all group the two. Splitting them here would take
    // every verb off a spoken-word row that used to play.
    if (type.compare(QLatin1String("Audio"), Qt::CaseInsensitive) == 0 ||
        type.compare(QLatin1String("AudioBook"), Qt::CaseInsensitive) == 0)
        return ItemKind::Audio;
    if (type.compare(QLatin1String("MusicAlbum"), Qt::CaseInsensitive) == 0)
        return ItemKind::MusicAlbum;
    if (type.compare(QLatin1String("MusicArtist"), Qt::CaseInsensitive) == 0)
        return ItemKind::MusicArtist;
    if (type.compare(QLatin1String("Playlist"), Qt::CaseInsensitive) == 0)
        return ItemKind::Playlist;
    if (type.compare(QLatin1String("Video"), Qt::CaseInsensitive) == 0 ||
        type.compare(QLatin1String("MusicVideo"), Qt::CaseInsensitive) == 0)
        return ItemKind::Video;
    if (type.compare(QLatin1String("Folder"), Qt::CaseInsensitive) == 0 ||
        type.compare(QLatin1String("CollectionFolder"), Qt::CaseInsensitive) == 0)
        return ItemKind::Folder;
    if (type.compare(QLatin1String("Genre"), Qt::CaseInsensitive) == 0 ||
        type.compare(QLatin1String("MusicGenre"), Qt::CaseInsensitive) == 0)
        return ItemKind::Genre;
    return ItemKind::Unknown;
}

ClassifiedItem classifyItem(QVariantMap item)
{
    ClassifiedItem classified;
    classified.kind = itemKindFor(item.value(QStringLiteral("type")).toString());
    classified.id = item.value(kItemIdKey).toString();
    classified.name = item.value(QStringLiteral("name")).toString();
    classified.item = std::move(item);
    return classified;
}

// One shuffle or "play all" fetches at most this many items. A whole library is
// not a queue: Emby happily returns tens of thousands of rows, and every one of
// them would be parsed, kept in memory and shuffled for a session the user will
// abandon after three episodes. 500 comfortably covers a series, a season, a
// collection and any realistic "shuffle this library" sample, and — because the
// shuffle fetch is SortBy=Random — a truncated fetch is still a fair sample of
// the whole library rather than its first 500 items alphabetically.
constexpr int kQueueFetchLimit = 500;
constexpr int kCollectionExpansionLimit = 64;

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
QString containerCollectionType(ItemKind kind)
{
    if (kind == ItemKind::MusicAlbum || kind == ItemKind::MusicArtist || kind == ItemKind::Genre)
        return QStringLiteral("music");
    if (kind == ItemKind::Series || kind == ItemKind::Season)
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
    if (m_client) {
        // Application normally announces the session boundary first, but the
        // client is also used directly by tests and embedders. The identity
        // signal is the final ownership boundary: no cached state or optimistic
        // request from A may be consulted after B becomes current.
        connect(m_client, &emby::EmbyClient::identityChanged, this,
                &ItemActions::resetSessionState);
    }
}

void ItemActions::resetSessionState()
{
    ++m_playbackIntentGeneration;
    m_collectionRequest.cancel();
    ++m_sessionGeneration;
    m_state.clear();
    m_stateOrder.clear();
    m_playedRequests.clear();
    m_favoriteRequests.clear();
    m_admittingUserStates.clear();
    m_models.removeIf([](const QPointer<MediaItemModel> &model) { return model.isNull(); });
    for (const QPointer<MediaItemModel> &model : std::as_const(m_models)) {
        if (model->rowCount() > 0)
            model->clear();
    }
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

    const auto syncAll = [this, model] { syncCachedUserState(model, 0, model->rowCount() - 1); };
    connect(model, &QAbstractItemModel::dataChanged, this,
            [this, model](const QModelIndex &topLeft, const QModelIndex &bottomRight,
                          const QList<int> &roles) {
                if (!roles.isEmpty() && !roles.contains(MediaItemModel::PlayedRole) &&
                    !roles.contains(MediaItemModel::FavoriteRole))
                    return;
                syncCachedUserState(model, topLeft.row(), bottomRight.row());
            });
    connect(model, &QAbstractItemModel::modelReset, this, syncAll);
    connect(model, &QAbstractItemModel::rowsInserted, this,
            [this, model](const QModelIndex &, int first, int last) {
                syncCachedUserState(model, first, last);
            });
    syncAll();
}

void ItemActions::unregisterModel(MediaItemModel *model)
{
    if (model)
        disconnect(model, nullptr, this, nullptr);
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
    QVariantMap item;
    item.insert(QStringLiteral("type"), type);
    return classifyItem(std::move(item)).isContainer();
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
    const ClassifiedItem classified = classifyItem(map);
    if (classified.kind == ItemKind::MusicAlbum) {
        emit orderedAlbumPlayRequested(itemId);
        return;
    }
    if (classified.expandsForPlayback()) {
        if (classified.kind == ItemKind::BoxSet) {
            playCollection(itemId); // curated order, not SortName
            return;
        }
        playAll(itemId, containerCollectionType(classified.kind));
        return;
    }
    if (classified.kind == ItemKind::Playlist) {
        // Playlist order and duplicate-entry identity are owned by the playlist
        // page. Treating its id as a leaf asks PlaybackInfo for a folder; using
        // ParentId invents an expansion contract the server does not promise.
        openDetails(map);
        return;
    }
    if (!classified.isLeaf()) {
        // Say so. Every other refusal in this file toasts, and the container
        // path this replaced surfaced "nothing to play here" from its empty
        // fetch — a Play button that goes quiet reads as a broken build.
        qCWarning(logApp) << "ItemActions: item kind has no direct play verb"
                          << map.value(QStringLiteral("type")).toString();
        emit actionFailed(tr("There is nothing to play here."));
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
    // Keep the complete card payload. In addition to making the media type
    // available synchronously, this retains its tagged artwork instead of
    // throwing it away and issuing a second, tagless image request.
    m_player->playItem(PlayQueue::itemFromVariant(map), titleFor(map),
                       std::max<qint64>(0, startMs));
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
    const ClassifiedItem classified = classifyItem(map);
    const QString itemId = classified.id;
    if (itemId.isEmpty()) {
        qCWarning(logApp) << "ItemActions: resume requested without an item id";
        return;
    }
    if (!classified.isLeaf()) {
        qCWarning(logApp) << "ItemActions: resume requires a playable leaf";
        emit actionFailed(tr("There is nothing to play here."));
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
    m_player->playItem(PlayQueue::itemFromVariant(map), titleFor(map),
                       std::max<qint64>(0, startMs));
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
    const quint64 generation = ++m_playbackIntentGeneration;
    // RequestHandle cancellation may settle its future synchronously. Retire
    // the old generation first so that completion can never surface an error
    // or rebuild the queue for an intent the user already replaced.
    m_collectionRequest.cancel();
    return generation;
}

void ItemActions::playNext(const QVariant &item)
{
    const QVariantMap map = resolve(item);
    const ClassifiedItem classified = classifyItem(map);
    if (classified.id.isEmpty()) {
        qCWarning(logApp) << "ItemActions: playNext without an item id";
        return;
    }
    if (!classified.isLeaf()) {
        qCWarning(logApp) << "ItemActions: playNext requires a playable leaf";
        emit actionFailed(tr("There is nothing to queue here."));
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
    const ClassifiedItem classified = classifyItem(map);
    if (classified.id.isEmpty()) {
        qCWarning(logApp) << "ItemActions: addToQueue without an item id";
        return;
    }
    if (!classified.isLeaf()) {
        qCWarning(logApp) << "ItemActions: addToQueue requires a playable leaf";
        emit actionFailed(tr("There is nothing to queue here."));
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
    QVariantList playable;
    playable.reserve(items.size());
    int playableBeforeStart = 0;
    for (int index = 0; index < items.size(); ++index) {
        const QVariantMap map = resolve(items.at(index));
        const ClassifiedItem classified = classifyItem(map);
        if (classified.id.isEmpty() || !classified.isLeaf())
            continue;
        if (index < startIndex)
            ++playableBeforeStart;
        playable.append(map);
    }
    if (playable.isEmpty()) {
        emit actionFailed(tr("There is nothing to play here."));
        return;
    }
    if (!requireQueueTarget())
        return;
    const int playableStart =
        std::clamp(playableBeforeStart, 0, static_cast<int>(playable.size()) - 1);
    m_player->playQueue(playable, playableStart);
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
        const ClassifiedItem classified = classifyItem(map);
        if (classified.id.isEmpty() || !classified.isLeaf())
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
        const ClassifiedItem classified = classifyItem(map);
        if (!classified.id.isEmpty() && classified.isLeaf())
            queueItems.append(PlayQueue::itemFromVariant(map));
    }
    if (m_player->queue()->playNext(queueItems) > 0)
        emit queueChanged();
}

void ItemActions::setFavoriteAll(const QStringList &itemIds, bool favorite)
{
    for (const QString &itemId : itemIds) {
        // A bulk gesture reports capacity pressure once and stops admitting
        // work; it must not turn the bounded table into an unbounded burst of
        // rejected signals after all slots become pinned.
        if (!admitUserState(itemId)) {
            qCWarning(logApp) << "ItemActions: pending user-state limit reached";
            emit actionFailed(
                tr("Too many item updates are still pending. Try again in a moment."));
            break;
        }
        setFavorite(itemId, favorite);
    }
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
    if (!requireQueueTarget())
        return;
    if (!m_client) {
        qCWarning(logApp) << "ItemActions: no server client; cannot expand collection";
        emit actionFailed(tr("Not connected to the server."));
        return;
    }

    const quint64 generation = beginPlaybackIntent();
    ItemsQuery query;
    query.parentId = collectionId;
    // The first page is the collection's curated top-level order. Series and
    // Season rows are expanded one at a time below, in this same slot.
    query.recursive = false;
    query.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series"),
                              QStringLiteral("Season"), QStringLiteral("Episode"),
                              QStringLiteral("Video")};
    query.limit = kQueueFetchLimit;
    m_client->items(query, &m_collectionRequest)
        .then(this, [this, generation](const Result<ItemsPage> &result) {
            if (generation != m_playbackIntentGeneration)
                return;
            if (!result.ok()) {
                emit actionFailed(tr("Could not build the collection queue: %1").arg(result.error));
                return;
            }
            auto walk = std::make_shared<CollectionWalk>();
            walk->members = result.value.items;
            walk->generation = generation;
            continueCollectionWalk(walk);
        });
}

void ItemActions::continueCollectionWalk(const std::shared_ptr<CollectionWalk> &walk)
{
    if (!walk || walk->generation != m_playbackIntentGeneration)
        return;

    while (walk->memberIndex < walk->members.size()) {
        if (walk->playable.size() >= kQueueFetchLimit) {
            walk->memberIndex = static_cast<int>(walk->members.size());
            break;
        }
        const MediaItem member = walk->members.at(walk->memberIndex++);
        const ItemKind kind = itemKindFor(member.type);
        if (kind == ItemKind::Movie || kind == ItemKind::Episode || kind == ItemKind::Video) {
            walk->playable.append(member); // duplicates are distinct curated slots
            continue;
        }
        if (kind != ItemKind::Series && kind != ItemKind::Season)
            continue;
        if (member.id.isEmpty()) {
            emit actionFailed(tr("A collection member is missing its item id."));
            return;
        }
        if (++walk->expansionCount > kCollectionExpansionLimit) {
            emit actionFailed(tr("This collection has too many nested shows to queue safely."));
            return;
        }

        ItemsQuery query;
        query.parentId = member.id;
        query.recursive = true;
        query.includeItemTypes = {QStringLiteral("Episode")};
        query.sortBy = QStringLiteral("PremiereDate,SortName");
        query.limit = kQueueFetchLimit - walk->playable.size();
        m_client->items(query, &m_collectionRequest)
            .then(this, [this, walk](const Result<ItemsPage> &result) {
                if (walk->generation != m_playbackIntentGeneration)
                    return;
                if (!result.ok()) {
                    emit actionFailed(
                        tr("Could not expand a collection member: %1").arg(result.error));
                    return;
                }
                for (const MediaItem &child : result.value.items) {
                    if (walk->playable.size() >= kQueueFetchLimit)
                        break;
                    if (itemKindFor(child.type) == ItemKind::Episode)
                        walk->playable.append(child);
                }
                continueCollectionWalk(walk);
            });
        return;
    }

    if (walk->playable.isEmpty()) {
        emit actionFailed(tr("There is nothing to play here."));
        return;
    }
    m_player->playQueueItems(walk->playable, 0, false);
    emit queueChanged();
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
    UserState state;
    bool found = false;
    const auto cached = m_state.constFind(itemId);
    if (cached != m_state.cend()) {
        state = *cached;
        found = true;
    } else {
        for (const QPointer<MediaItemModel> &model : m_models) {
            if (model.isNull())
                continue;
            for (const MediaItem &item : model->items()) {
                if (item.id != itemId)
                    continue;
                state = {item.played, item.favorite};
                found = true;
                break;
            }
            if (found)
                break;
        }
    }

    // A bounded cache may evict an item while its request is still running.
    // The request tables therefore remain the source for optimistic fields;
    // the queued value is the newest intent when one exists.
    const auto playedRequest = m_playedRequests.constFind(itemId);
    if (playedRequest != m_playedRequests.cend())
        state.played = playedRequest->hasQueued ? playedRequest->queued : playedRequest->requested;
    const auto favoriteRequest = m_favoriteRequests.constFind(itemId);
    if (favoriteRequest != m_favoriteRequests.cend())
        state.favorite =
            favoriteRequest->hasQueued ? favoriteRequest->queued : favoriteRequest->requested;
    return state;
}

void ItemActions::rememberState(const QString &itemId, const UserState &state)
{
    if (itemId.isEmpty())
        return;
    if (!m_state.contains(itemId) && !admitUserState(itemId)) {
        qCCritical(logApp) << "ItemActions: user-state admission invariant failed for" << itemId;
        return;
    }
    m_state.insert(itemId, state);
    m_stateOrder.removeOne(itemId);
    m_stateOrder.append(itemId);
    while (m_state.size() > m_userStateCacheLimit && evictOldestSettledUserState()) {
    }
}

void ItemActions::setUserStateCacheLimitForTests(int maximum)
{
    if (!m_playedRequests.isEmpty() || !m_favoriteRequests.isEmpty() ||
        !m_admittingUserStates.isEmpty()) {
        qCWarning(logApp) << "ItemActions: cannot change the user-state limit with pending work";
        return;
    }
    m_userStateCacheLimit = qBound(1, maximum, kMaxCachedUserStates);
    while (m_state.size() > m_userStateCacheLimit && evictOldestSettledUserState()) {
    }
}

bool ItemActions::hasPendingUserState(const QString &itemId) const
{
    return m_admittingUserStates.contains(itemId) || m_playedRequests.contains(itemId) ||
           m_favoriteRequests.contains(itemId);
}

bool ItemActions::evictOldestSettledUserState()
{
    for (qsizetype index = 0; index < m_stateOrder.size(); ++index) {
        const QString &candidate = m_stateOrder.at(index);
        if (hasPendingUserState(candidate))
            continue;
        m_state.remove(candidate);
        m_stateOrder.removeAt(index);
        return true;
    }
    return false;
}

bool ItemActions::admitUserState(const QString &itemId)
{
    if (itemId.isEmpty())
        return false;
    if (m_state.contains(itemId) || hasPendingUserState(itemId))
        return true;
    while (m_state.size() >= m_userStateCacheLimit) {
        if (!evictOldestSettledUserState())
            return false;
    }
    return true;
}

void ItemActions::syncCachedUserState(MediaItemModel *model, int firstRow, int lastRow)
{
    if (m_patchingModels || !model || firstRow < 0 || lastRow < firstRow)
        return;
    const QList<MediaItem> &items = model->items();
    const int boundedLast = qMin(lastRow, items.size() - 1);
    for (int row = firstRow; row <= boundedLast; ++row) {
        const MediaItem &item = items.at(row);
        if (item.id.isEmpty())
            continue;
        auto playedRequest = m_playedRequests.find(item.id);
        auto favoriteRequest = m_favoriteRequests.find(item.id);
        if (!m_state.contains(item.id) && playedRequest == m_playedRequests.end() &&
            favoriteRequest == m_favoriteRequests.end())
            continue;

        UserState state = knownState(item.id);
        if (playedRequest == m_playedRequests.end())
            state.played = item.played;
        else
            playedRequest->baseline = item.played;
        if (favoriteRequest == m_favoriteRequests.end())
            state.favorite = item.favorite;
        else
            favoriteRequest->baseline = item.favorite;
        rememberState(item.id, state);
    }
}

void ItemActions::patchModels(const QString &itemId, const UserState &state)
{
    m_models.removeIf([](const QPointer<MediaItemModel> &model) { return model.isNull(); });
    m_patchingModels = true;
    for (const QPointer<MediaItemModel> &model : std::as_const(m_models))
        model->updateUserData(itemId, state.played, state.favorite);
    m_patchingModels = false;
}

void ItemActions::applyPlayed(const QString &itemId, bool played)
{
    UserState state = knownState(itemId);
    state.played = played;
    rememberState(itemId, state);
    patchModels(itemId, state);
    emit playedChanged(itemId, played);
}

void ItemActions::applyFavorite(const QString &itemId, bool favorite)
{
    UserState state = knownState(itemId);
    state.favorite = favorite;
    rememberState(itemId, state);
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
    if (!admitUserState(itemId)) {
        qCWarning(logApp) << "ItemActions: pending user-state limit reached";
        emit actionFailed(tr("Too many item updates are still pending. Try again in a moment."));
        return;
    }
    const quint64 sessionGeneration = m_sessionGeneration;
    m_admittingUserStates.insert(itemId);
    const bool previous = knownState(itemId).played;
    applyPlayed(itemId, played); // optimistic: the UI moves now
    m_admittingUserStates.remove(itemId);
    if (sessionGeneration != m_sessionGeneration)
        return;

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
            if (finished.hasQueued && finished.queued != finished.requested) {
                sendPlayed(itemId, finished.queued, finished.requested);
                return;
            }
            emit playedCommitted(itemId, finished.requested);
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
    if (m_state.contains(itemId) || m_playedRequests.contains(itemId))
        current = knownState(itemId).played; // newest intent wins over a stale map
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
    if (!admitUserState(itemId)) {
        qCWarning(logApp) << "ItemActions: pending user-state limit reached";
        emit actionFailed(tr("Too many item updates are still pending. Try again in a moment."));
        return;
    }
    const quint64 sessionGeneration = m_sessionGeneration;
    m_admittingUserStates.insert(itemId);
    const bool previous = knownState(itemId).favorite;
    applyFavorite(itemId, favorite);
    m_admittingUserStates.remove(itemId);
    if (sessionGeneration != m_sessionGeneration)
        return;

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
    if (m_state.contains(itemId) || m_favoriteRequests.contains(itemId))
        current = knownState(itemId).favorite;
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

QVariantMap ItemActions::itemCapabilities(const QVariant &item) const
{
    const ClassifiedItem classified = classifyItem(resolve(item));
    const bool valid = !classified.id.isEmpty();
    const bool leaf = valid && classified.isLeaf();
    const bool expandable = valid && classified.expandsForPlayback();
    const bool artist = classified.kind == ItemKind::MusicArtist;
    const bool playlist = classified.kind == ItemKind::Playlist;
    const bool known = classified.kind != ItemKind::Unknown;
    const bool passiveContainer =
        classified.kind == ItemKind::Folder || classified.kind == ItemKind::Genre;
    const bool markPlayed = valid && known && !artist && !playlist && !passiveContainer;

    QVariantMap capabilities;
    capabilities.insert(QStringLiteral("valid"), valid);
    capabilities.insert(QStringLiteral("episode"), classified.kind == ItemKind::Episode);
    capabilities.insert(QStringLiteral("series"), classified.kind == ItemKind::Series);
    capabilities.insert(QStringLiteral("collection"), classified.kind == ItemKind::BoxSet);
    capabilities.insert(QStringLiteral("music"), classified.isMusic());
    capabilities.insert(QStringLiteral("leaf"), leaf);
    capabilities.insert(QStringLiteral("expandable"), expandable);
    capabilities.insert(QStringLiteral("play"), leaf || expandable);
    capabilities.insert(QStringLiteral("resume"),
                        leaf && classified.item.value(QStringLiteral("resumable")).toBool());
    capabilities.insert(QStringLiteral("queue"), leaf);
    capabilities.insert(
        QStringLiteral("shuffle"),
        valid && (classified.kind == ItemKind::Series || classified.kind == ItemKind::Season ||
                  classified.kind == ItemKind::BoxSet || classified.kind == ItemKind::MusicAlbum));
    capabilities.insert(QStringLiteral("instantMix"),
                        valid && (artist || classified.kind == ItemKind::MusicAlbum ||
                                  classified.kind == ItemKind::Audio));
    capabilities.insert(QStringLiteral("markPlayed"), markPlayed);
    capabilities.insert(QStringLiteral("favorite"), valid && known && !passiveContainer);
    capabilities.insert(
        QStringLiteral("seriesNavigation"),
        valid && (classified.kind == ItemKind::Series ||
                  (classified.kind == ItemKind::Episode &&
                   !classified.item.value(QStringLiteral("seriesId")).toString().isEmpty())));
    capabilities.insert(QStringLiteral("collectionNavigation"),
                        valid && classified.kind == ItemKind::BoxSet);
    capabilities.insert(QStringLiteral("details"), valid);
    capabilities.insert(QStringLiteral("addToPlaylist"),
                        valid && (leaf || classified.kind == ItemKind::MusicAlbum));
    capabilities.insert(
        QStringLiteral("playlistEntry"),
        !classified.item.value(QStringLiteral("playlistItemId")).toString().isEmpty());
    return capabilities;
}

QVariantList ItemActions::itemMenuPolicy(const QVariant &item, bool allowDetails,
                                         bool allowAddToPlaylist, bool allowRemoveFromPlaylist,
                                         bool allowMusicNavigation, const QString &profile) const
{
    const ClassifiedItem classified = classifyItem(resolve(item));
    if (classified.id.isEmpty())
        return {};

    const QVariantMap capabilities = itemCapabilities(classified.item);
    const bool played = isPlayed(classified.id);
    const bool favorite = isFavorite(classified.id);
    QVariantList descriptors;

    const auto append = [&descriptors](const QString &verb, const QString &presentation,
                                       const QVariantMap &target = QVariantMap{},
                                       const QString &routeKind = QString(), bool checked = false,
                                       bool destructive = false) {
        QVariantMap descriptor;
        descriptor.insert(QStringLiteral("verb"), verb);
        descriptor.insert(QStringLiteral("presentation"), presentation);
        descriptor.insert(QStringLiteral("checked"), checked);
        descriptor.insert(QStringLiteral("destructive"), destructive);
        if (!target.isEmpty())
            descriptor.insert(QStringLiteral("target"), target);
        if (!routeKind.isEmpty())
            descriptor.insert(QStringLiteral("routeKind"), routeKind);
        descriptors.append(descriptor);
    };
    const auto separator = [&descriptors] {
        if (descriptors.isEmpty() ||
            descriptors.constLast().toMap().value(QStringLiteral("separator")).toBool())
            return;
        descriptors.append(QVariantMap{{QStringLiteral("separator"), true}});
    };
    const auto itemTarget = [&classified] {
        return QVariantMap{{kItemIdKey, classified.id}, {QStringLiteral("name"), classified.name}};
    };
    const auto appendFavorite = [&] {
        append(QStringLiteral("favorite"),
               favorite ? QStringLiteral("removeFavorite") : QStringLiteral("addFavorite"),
               itemTarget(), {}, favorite);
    };

    // Music's browse grids deliberately have a narrower menu and a distinct
    // order. The semantic descriptors still come from the same classifier;
    // only their presentation profile differs.
    if (profile == QLatin1String("musicBrowse")) {
        if (classified.kind == ItemKind::MusicAlbum) {
            append(QStringLiteral("play"), QStringLiteral("play"), itemTarget());
            append(QStringLiteral("shuffle"), QStringLiteral("shuffleAlbum"), itemTarget());
            append(QStringLiteral("instantMix"), QStringLiteral("instantMix"), itemTarget());
            if (allowDetails) {
                separator();
                append(QStringLiteral("details"), QStringLiteral("openAlbum"), classified.item,
                       QStringLiteral("album"));
            }
            if (allowAddToPlaylist) {
                append(QStringLiteral("addToPlaylist"), QStringLiteral("addToPlaylist"),
                       itemTarget());
            }
        } else if (classified.kind == ItemKind::MusicArtist) {
            append(QStringLiteral("instantMix"), QStringLiteral("instantMix"), itemTarget());
            if (allowDetails) {
                append(QStringLiteral("details"), QStringLiteral("openArtist"), classified.item,
                       QStringLiteral("artist"));
            }
        } else if (classified.kind == ItemKind::Playlist) {
            if (allowDetails) {
                append(QStringLiteral("details"), QStringLiteral("openPlaylist"), classified.item,
                       QStringLiteral("playlist"));
            }
        } else {
            return itemMenuPolicy(classified.item, allowDetails, allowAddToPlaylist,
                                  allowRemoveFromPlaylist, allowMusicNavigation, {});
        }
        separator();
        appendFavorite();
        const QString entryId =
            classified.item.value(QStringLiteral("playlistItemId")).toString();
        if (allowRemoveFromPlaylist && !entryId.isEmpty()) {
            separator();
            append(QStringLiteral("removeFromPlaylist"), QStringLiteral("removeFromPlaylist"),
                   QVariantMap{{QStringLiteral("playlistItemId"), entryId}}, {}, false, true);
        }
        return descriptors;
    }

    // Playlists are opened, never guessed into a ParentId query or queued as a
    // leaf. Keep the compact policy used by the dedicated music grid too.
    if (classified.kind == ItemKind::Playlist) {
        if (allowDetails) {
            append(QStringLiteral("details"), QStringLiteral("openPlaylist"), classified.item,
                   QStringLiteral("playlist"));
        }
        separator();
        appendFavorite();
        const QString entryId =
            classified.item.value(QStringLiteral("playlistItemId")).toString();
        if (allowRemoveFromPlaylist && !entryId.isEmpty()) {
            separator();
            append(QStringLiteral("removeFromPlaylist"), QStringLiteral("removeFromPlaylist"),
                   QVariantMap{{QStringLiteral("playlistItemId"), entryId}}, {}, false, true);
        }
        return descriptors;
    }

    if (classified.kind == ItemKind::MusicArtist) {
        // The generic menu deliberately has no primary artist verb. Search's
        // card activation opens the artist, while Music's browse profile uses
        // Instant Mix + Open Artist. Adding either here would silently change
        // every Home/Library/Search context menu into one of those profiles.
    } else if (classified.kind == ItemKind::MusicAlbum) {
        append(QStringLiteral("play"), QStringLiteral("playAlbum"), itemTarget());
        append(QStringLiteral("shuffle"), QStringLiteral("shuffleAlbum"), itemTarget());
    } else if (capabilities.value(QStringLiteral("expandable")).toBool()) {
        append(QStringLiteral("play"), QStringLiteral("playAll"), itemTarget());
        append(QStringLiteral("shuffle"), QStringLiteral("shuffle"), itemTarget());
        if (classified.kind == ItemKind::Series) {
            separator();
            append(QStringLiteral("series"), QStringLiteral("episodes"), itemTarget(),
                   QStringLiteral("series"));
        }
    } else if (capabilities.value(QStringLiteral("resume")).toBool()) {
        append(QStringLiteral("resume"), QStringLiteral("resume"), itemTarget());
        append(QStringLiteral("playFromStart"), QStringLiteral("playFromStart"), itemTarget());
    } else if (capabilities.value(QStringLiteral("play")).toBool()) {
        append(QStringLiteral("play"), QStringLiteral("play"), itemTarget());
    }

    if (capabilities.value(QStringLiteral("queue")).toBool()) {
        separator();
        append(QStringLiteral("playNext"), QStringLiteral("playNext"), itemTarget());
        append(QStringLiteral("addToQueue"), QStringLiteral("addToQueue"), itemTarget());
    }

    if (capabilities.value(QStringLiteral("markPlayed")).toBool() ||
        capabilities.value(QStringLiteral("favorite")).toBool()) {
        separator();
    }
    if (capabilities.value(QStringLiteral("markPlayed")).toBool()) {
        const QString presentation =
            classified.isMusic()
                ? (played ? QStringLiteral("markUnplayed") : QStringLiteral("markPlayed"))
                : (played ? QStringLiteral("markUnwatched") : QStringLiteral("markWatched"));
        append(QStringLiteral("played"), presentation, itemTarget(), {}, played);
    }
    if (capabilities.value(QStringLiteral("favorite")).toBool())
        appendFavorite();

    const QString playlistEntryId =
        classified.item.value(QStringLiteral("playlistItemId")).toString();
    const bool canAdd =
        allowAddToPlaylist && capabilities.value(QStringLiteral("addToPlaylist")).toBool();
    const bool canRemove = allowRemoveFromPlaylist && !playlistEntryId.isEmpty();
    if (canAdd || canRemove)
        separator();
    if (canAdd)
        append(QStringLiteral("addToPlaylist"), QStringLiteral("addToPlaylist"), itemTarget());
    if (canRemove) {
        append(QStringLiteral("removeFromPlaylist"), QStringLiteral("removeFromPlaylist"),
               QVariantMap{{QStringLiteral("playlistItemId"), playlistEntryId}}, {}, false, true);
    }

    separator();
    if (capabilities.value(QStringLiteral("seriesNavigation")).toBool() &&
        classified.kind == ItemKind::Episode) {
        QVariantMap target;
        target.insert(kItemIdKey, classified.item.value(QStringLiteral("seriesId")));
        target.insert(QStringLiteral("name"), classified.item.value(QStringLiteral("seriesName")));
        target.insert(QStringLiteral("type"), QStringLiteral("Series"));
        append(QStringLiteral("series"), QStringLiteral("goToSeries"), target,
               QStringLiteral("series"));
    }
    if (allowMusicNavigation && classified.kind == ItemKind::Audio) {
        const QString albumId = classified.item.value(QStringLiteral("albumId")).toString();
        if (!albumId.isEmpty()) {
            const QVariantMap target{
                {kItemIdKey, albumId},
                {QStringLiteral("name"), classified.item.value(QStringLiteral("album"))},
                {QStringLiteral("type"), QStringLiteral("MusicAlbum")}};
            append(QStringLiteral("album"), QStringLiteral("goToAlbum"), target,
                   QStringLiteral("album"));
        }
    }
    if (allowMusicNavigation &&
        (classified.kind == ItemKind::Audio || classified.kind == ItemKind::MusicAlbum)) {
        const QVariantMap target = artistTarget(classified.item);
        if (!target.value(kItemIdKey).toString().isEmpty()) {
            append(QStringLiteral("artist"), QStringLiteral("goToArtist"), target,
                   QStringLiteral("artist"));
        }
    }
    if (allowDetails && !classified.isMusic()) {
        append(QStringLiteral("details"), QStringLiteral("details"), classified.item,
               QStringLiteral("details"));
    }
    append(QStringLiteral("refresh"), QStringLiteral("refreshMetadata"), itemTarget());
    return descriptors;
}

void ItemActions::performItemVerb(const QString &verb, const QVariant &item)
{
    const QVariantMap map = resolve(item);
    const ClassifiedItem classified = classifyItem(map);
    if (classified.id.isEmpty()) {
        qCWarning(logApp) << "ItemActions: verb requested without an item id" << verb;
        return;
    }
    const QVariantMap capabilities = itemCapabilities(map);

    if (verb == QLatin1String("play")) {
        if (capabilities.value(QStringLiteral("play")).toBool())
            play(map);
    } else if (verb == QLatin1String("resume")) {
        if (capabilities.value(QStringLiteral("resume")).toBool())
            resume(map);
    } else if (verb == QLatin1String("playFromStart")) {
        if (capabilities.value(QStringLiteral("leaf")).toBool())
            playFromStart(map);
    } else if (verb == QLatin1String("playNext")) {
        if (capabilities.value(QStringLiteral("queue")).toBool())
            playNext(map);
    } else if (verb == QLatin1String("addToQueue")) {
        if (capabilities.value(QStringLiteral("queue")).toBool())
            addToQueue(map);
    } else if (verb == QLatin1String("shuffle")) {
        if (!capabilities.value(QStringLiteral("shuffle")).toBool())
            return;
        if (classified.kind == ItemKind::Series)
            shuffleSeries(classified.id);
        else
            shuffle(classified.id, containerCollectionType(classified.kind));
    } else if (verb == QLatin1String("instantMix")) {
        if (capabilities.value(QStringLiteral("instantMix")).toBool())
            instantMix(map);
    } else if (verb == QLatin1String("played")) {
        if (capabilities.value(QStringLiteral("markPlayed")).toBool())
            setPlayed(classified.id, !isPlayed(classified.id));
    } else if (verb == QLatin1String("favorite")) {
        if (capabilities.value(QStringLiteral("favorite")).toBool())
            setFavorite(classified.id, !isFavorite(classified.id));
    } else if (verb == QLatin1String("series")) {
        if (capabilities.value(QStringLiteral("seriesNavigation")).toBool())
            openSeries(map);
    } else if (verb == QLatin1String("album")) {
        if (classified.kind == ItemKind::Audio) {
            const QString albumId = map.value(QStringLiteral("albumId")).toString();
            if (!albumId.isEmpty())
                openAlbum(albumId, map.value(QStringLiteral("album")).toString());
        }
    } else if (verb == QLatin1String("artist")) {
        if (classified.kind == ItemKind::Audio || classified.kind == ItemKind::MusicAlbum) {
            const QVariantMap target = artistTarget(map);
            openArtist(target.value(kItemIdKey).toString(),
                       target.value(QStringLiteral("name")).toString());
        }
    } else if (verb == QLatin1String("browseCollection")) {
        if (capabilities.value(QStringLiteral("collectionNavigation")).toBool())
            browseCollection(classified.id, classified.name);
    } else if (verb == QLatin1String("details")) {
        openDetails(map);
    } else if (verb == QLatin1String("refresh")) {
        refreshMetadata(classified.id);
    }
}

void ItemActions::openDetails(const QVariant &item)
{
    const QVariantMap map = resolve(item);
    const ClassifiedItem classified = classifyItem(map);
    if (classified.id.isEmpty()) {
        qCWarning(logApp) << "ItemActions: openDetails without an item id";
        return;
    }
    if (classified.kind == ItemKind::MusicAlbum) {
        emit routeRequested(QStringLiteral("album"), map);
    } else if (classified.kind == ItemKind::MusicArtist) {
        emit routeRequested(QStringLiteral("artist"), map);
    } else if (classified.kind == ItemKind::Audio &&
               !map.value(QStringLiteral("albumId")).toString().isEmpty()) {
        QVariantMap target;
        target.insert(kItemIdKey, map.value(QStringLiteral("albumId")));
        target.insert(QStringLiteral("name"), map.value(QStringLiteral("album")));
        target.insert(QStringLiteral("type"), QStringLiteral("MusicAlbum"));
        emit routeRequested(QStringLiteral("album"), target);
    } else if (classified.kind == ItemKind::Playlist) {
        emit routeRequested(QStringLiteral("playlist"), map);
    } else {
        emit routeRequested(QStringLiteral("details"), map);
    }
}

void ItemActions::openSeries(const QVariant &item)
{
    const QVariantMap map = resolve(item);
    const ClassifiedItem classified = classifyItem(map);
    QString seriesId = map.value(QStringLiteral("seriesId")).toString();
    QString seriesName = map.value(QStringLiteral("seriesName")).toString();
    if (seriesId.isEmpty() && classified.kind == ItemKind::Series) {
        seriesId = map.value(kItemIdKey).toString();
        seriesName = map.value(QStringLiteral("name")).toString();
    }
    if (seriesId.isEmpty()) {
        qCWarning(logApp) << "ItemActions: openSeries has no series for"
                          << map.value(kItemIdKey).toString();
        return;
    }
    emit routeRequested(QStringLiteral("series"),
                        QVariantMap{{kItemIdKey, seriesId},
                                    {QStringLiteral("name"), seriesName},
                                    {QStringLiteral("type"), QStringLiteral("Series")}});
}

void ItemActions::openAlbum(const QString &albumId, const QString &name)
{
    if (albumId.isEmpty())
        return;
    emit routeRequested(QStringLiteral("album"),
                        QVariantMap{{kItemIdKey, albumId},
                                    {QStringLiteral("name"), name},
                                    {QStringLiteral("type"), QStringLiteral("MusicAlbum")}});
}

void ItemActions::openArtist(const QString &artistId, const QString &name)
{
    if (artistId.isEmpty())
        return;
    emit routeRequested(QStringLiteral("artist"),
                        QVariantMap{{kItemIdKey, artistId},
                                    {QStringLiteral("name"), name},
                                    {QStringLiteral("type"), QStringLiteral("MusicArtist")}});
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
