#include "PlayQueue.h"

#include <QMetaType>
#include <QRandomGenerator>

#include <algorithm>
#include <utility>

namespace strmqt {

namespace {

const auto kItemIdKey = QStringLiteral("itemId");

// image://emby/<id>/<type>/<tag> → the triple. The model turns an item into a
// URL; a map handed back from QML carries only the URL, so take it apart again
// rather than losing every poster the moment an item passes through the queue.
//
// The id matters as much as the tag: MediaItem::coverSource() points a track's
// square at its ALBUM, so a poster URL routinely names an item that is not this
// one, and keeping only the tag would rebuild the album's tag against the
// track's id — a URL for an image that does not exist.
MediaItem::ImageRef refFromImageSource(const QString &source)
{
    if (source.isEmpty())
        return {};
    const QStringList parts = source.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 3)
        return {};
    return {parts.at(parts.size() - 3), parts.at(parts.size() - 2), parts.last()};
}

int intOf(const QVariantMap &map, const QString &key, int fallback = -1)
{
    const auto it = map.constFind(key);
    return it == map.cend() ? fallback : it->toInt();
}

} // namespace

PlayQueue::PlayQueue(QObject *parent) : QAbstractListModel(parent) {}

MediaItem PlayQueue::itemFromVariant(const QVariant &value)
{
    MediaItem item;
    if (value.typeId() == QMetaType::QString) {
        item.id = value.toString();
        return item;
    }

    const QVariantMap map = value.toMap();
    if (map.isEmpty())
        return item;

    item.id = map.value(kItemIdKey, map.value(QStringLiteral("id"))).toString();
    item.name = map.value(QStringLiteral("name")).toString();
    if (item.name.isEmpty())
        item.name = map.value(QStringLiteral("label")).toString();
    item.type = map.value(QStringLiteral("type")).toString();
    item.overview = map.value(QStringLiteral("overview")).toString();
    item.seriesId = map.value(QStringLiteral("seriesId")).toString();
    item.seriesName = map.value(QStringLiteral("seriesName")).toString();
    item.seasonId = map.value(QStringLiteral("seasonId")).toString();
    item.indexNumber = intOf(map, QStringLiteral("indexNumber"));
    item.parentIndexNumber = intOf(map, QStringLiteral("parentIndexNumber"));
    item.productionYear = intOf(map, QStringLiteral("year"), 0);
    item.officialRating = map.value(QStringLiteral("officialRating")).toString();
    item.communityRating = map.value(QStringLiteral("communityRating")).toDouble();
    item.runtimeTicks = map.value(QStringLiteral("runtimeMs")).toLongLong() * kTicksPerMs;
    item.status = map.value(QStringLiteral("status")).toString();
    item.unplayedItemCount = intOf(map, QStringLiteral("unplayedCount"), 0);
    item.playbackPositionTicks = map.value(QStringLiteral("positionMs")).toLongLong() * kTicksPerMs;
    item.played = map.value(QStringLiteral("played")).toBool();
    // Restored because the DTO path is not the only way into the queue: an album
    // or a playlist plays through playQueue(QVariantList), and without this every
    // one of those tracks reaches MPRIS with no xesam:useCount. Absent means 0,
    // which MediaItem already documents as "never, or nobody asked".
    item.playCount = intOf(map, QStringLiteral("playCount"), 0);
    item.favorite = map.value(QStringLiteral("favorite")).toBool();
    // Music identity travels with the entry. Without it a queued track knows
    // only its own name, which is why the bar had to reconstruct context by
    // splitting a display string, and why the poster below cannot tell an
    // album's cover from an artist's photo by id alone.
    item.album = map.value(QStringLiteral("album")).toString();
    item.albumId = map.value(QStringLiteral("albumId")).toString();
    item.albumArtist = map.value(QStringLiteral("albumArtist")).toString();
    item.artists = map.value(QStringLiteral("artists")).toStringList();
    item.artistIds = map.value(QStringLiteral("artistIds")).toStringList();

    // A poster URL that names another item is restored under whichever field
    // put it there, so coverSource() rebuilds the identical URL and an entry can
    // make the round trip any number of times without drifting.
    const MediaItem::ImageRef poster =
        refFromImageSource(map.value(QStringLiteral("posterUrl")).toString());
    if (poster.itemId == item.id) {
        item.primaryImageTag = poster.tag;
    } else if (poster.isValid() && poster.itemId == item.albumId) {
        item.albumPrimaryImageTag = poster.tag;
    } else if (poster.isValid()) {
        item.parentPrimaryImageItemId = poster.itemId;
        item.parentPrimaryImageTag = poster.tag;
    }
    const QString backdrop =
        refFromImageSource(map.value(QStringLiteral("backdropUrl")).toString()).tag;
    if (!backdrop.isEmpty())
        item.backdropImageTags = {backdrop};
    return item;
}

// ── Model surface ─────────────────────────────────────────────────────────────

int PlayQueue::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant PlayQueue::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    switch (role) {
    case QueueIndexRole:
        return index.row();
    case IsCurrentRole:
        return index.row() == m_currentIndex;
    default:
        return MediaItemModel::dataForItem(m_entries.at(index.row()).item, role);
    }
}

QHash<int, QByteArray> PlayQueue::roleNames() const
{
    QHash<int, QByteArray> roles = MediaItemModel::mediaRoleNames();
    roles.insert(QueueIndexRole, "queueIndex");
    roles.insert(IsCurrentRole, "isCurrent");
    return roles;
}

// QueueIndexRole and IsCurrentRole are positional: every structural change and
// every cursor move restates them for the whole queue.
void PlayQueue::emitRowMetaChanged()
{
    if (m_entries.isEmpty())
        return;
    emit dataChanged(index(0), index(static_cast<int>(m_entries.size()) - 1),
                     {QueueIndexRole, IsCurrentRole});
}

quint64 PlayQueue::currentKey() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_entries.size())
        return 0;
    return m_entries.at(m_currentIndex).key;
}

// currentChanged() is the property signal: hasNext/hasPrevious hang off it and
// both can flip on a pure re-index, so it fires for every structural change.
// The cursor signals are the narrow ones — they fire only when the entry under
// the cursor is a *different* entry, and identity here is the per-entry key,
// never the row, because a removal above the cursor moves the row without
// changing the item (and the same item may legitimately be queued twice).
void PlayQueue::notifyCursor(bool displaced)
{
    const quint64 key = currentKey();
    const bool moved = key != m_currentKey;
    m_currentKey = key;
    emit currentChanged();
    if (!moved || key == 0)
        return; // an emptied queue is exhausted(), not a new item to play
    if (displaced)
        emit currentItemDisplaced();
    else
        emit currentItemChanged();
}

QVariantMap PlayQueue::itemAt(int row) const
{
    QVariantMap map;
    const QModelIndex idx = index(row);
    if (!idx.isValid())
        return map;
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(idx, it.key()));
    return map;
}

QVariantMap PlayQueue::currentItem() const
{
    return itemAt(m_currentIndex);
}

MediaItem PlayQueue::current() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_entries.size())
        return {};
    return m_entries.at(m_currentIndex).item;
}

QString PlayQueue::contextLabel() const
{
    if (m_entries.isEmpty())
        return {};

    QString album;
    QString artist;
    bool first = true;
    for (const Entry &entry : m_entries) {
        // Music only. A run of episodes has a provenance too, but "up next in
        // context" is a music surface and a series queue is not what it labels.
        if (entry.item.type.compare(QLatin1String("Audio"), Qt::CaseInsensitive) != 0)
            return {};

        // The album artist is the credit a record is filed under; the first
        // performer is the fallback, and it is what a compilation has instead.
        const QString entryArtist = entry.item.albumArtist.isEmpty()
                                        ? entry.item.artists.value(0)
                                        : entry.item.albumArtist;
        if (first) {
            album = entry.item.album;
            artist = entryArtist;
            first = false;
            continue;
        }
        if (entry.item.album != album)
            album.clear();
        if (entryArtist != artist)
            artist.clear();
        if (album.isEmpty() && artist.isEmpty())
            return {};
    }

    // One record beats one artist: "from Lift Yr Skinny Fists" says more than
    // "from Godspeed You! Black Emperor" when both are true.
    if (!album.isEmpty())
        return tr("from %1").arg(album);
    if (!artist.isEmpty())
        return tr("from %1").arg(artist);
    return {};
}

// ── Contents ──────────────────────────────────────────────────────────────────

void PlayQueue::setItems(QList<MediaItem> items, int startIndex)
{
    beginResetModel();
    m_entries.clear();
    m_originalKeys.clear();
    m_entries.reserve(items.size());
    m_originalKeys.reserve(items.size());
    for (MediaItem &item : items) {
        const quint64 key = m_nextKey++;
        m_entries.append({std::move(item), key});
        m_originalKeys.append(key);
    }
    m_currentIndex = m_entries.isEmpty()
                         ? -1
                         : qBound(0, startIndex, static_cast<int>(m_entries.size()) - 1);
    const bool wasShuffled = std::exchange(m_shuffled, false);
    endResetModel();

    if (wasShuffled)
        emit shuffledChanged();
    emit queueChanged();
    notifyCursor();
}

PlayQueue::Snapshot PlayQueue::snapshot() const
{
    Snapshot snap;
    snap.playOrder.reserve(m_entries.size());
    QHash<quint64, int> positionsByKey;
    positionsByKey.reserve(m_entries.size());
    for (int row = 0; row < m_entries.size(); ++row) {
        const Entry &entry = m_entries.at(row);
        snap.playOrder.append(entry.item);
        positionsByKey.insert(entry.key, row);
    }
    snap.originalPositions.reserve(m_originalKeys.size());
    for (const quint64 key : std::as_const(m_originalKeys)) {
        const auto position = positionsByKey.constFind(key);
        if (position != positionsByKey.cend())
            snap.originalPositions.append(*position);
    }
    snap.currentIndex = m_currentIndex;
    snap.shuffled = m_shuffled;
    snap.repeatMode = m_repeatMode;
    return snap;
}

void PlayQueue::restore(const Snapshot &snapshot)
{
    const bool shuffledChanging = m_shuffled != snapshot.shuffled;
    const bool repeatChanging = m_repeatMode != snapshot.repeatMode;

    beginResetModel();
    m_entries.clear();
    m_originalKeys.clear();
    m_entries.reserve(snapshot.playOrder.size());
    m_originalKeys.reserve(snapshot.playOrder.size());
    for (const MediaItem &item : snapshot.playOrder)
        m_entries.append({item, m_nextKey++});
    for (const int position : snapshot.originalPositions) {
        if (position >= 0 && position < m_entries.size())
            m_originalKeys.append(m_entries.at(position).key);
    }
    // A snapshot whose given order does not describe every entry cannot be
    // trusted to un-shuffle correctly, so fall back to the play order rather
    // than restoring a queue that would reorder itself on the next toggle.
    if (m_originalKeys.size() != m_entries.size()) {
        m_originalKeys.clear();
        for (const Entry &entry : std::as_const(m_entries))
            m_originalKeys.append(entry.key);
    }
    m_currentIndex = m_entries.isEmpty()
                         ? -1
                         : qBound(0, snapshot.currentIndex,
                                  static_cast<int>(m_entries.size()) - 1);
    m_shuffled = snapshot.shuffled;
    m_repeatMode = snapshot.repeatMode;
    endResetModel();

    if (shuffledChanging)
        emit shuffledChanged();
    if (repeatChanging)
        emit repeatModeChanged();
    emit queueChanged();
    notifyCursor();
}

void PlayQueue::enrichEntry(const MediaItem &item)
{
    if (item.id.isEmpty())
        return;
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).item.id != item.id)
            continue;
        // The identity survives: shuffling and re-ordering track the key, not
        // the payload, so replacing what an entry HOLDS must not disturb where
        // it sits or what it is.
        m_entries[row].item = item;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx);
        if (row == m_currentIndex)
            emit currentItemChanged();
        // What the surfaces bind through: they read itemAt(currentIndex), so a
        // changed payload has to announce itself as a queue change or nothing
        // re-reads it.
        emit queueChanged();
        return;
    }
}

int PlayQueue::originalPositionOf(quint64 key) const
{
    return static_cast<int>(m_originalKeys.indexOf(key));
}

void PlayQueue::playNext(const QVariant &item)
{
    const MediaItem media = itemFromVariant(item);
    if (media.id.isEmpty())
        return;
    playNext(QList<MediaItem>{media});
}

void PlayQueue::addToQueue(const QVariant &item)
{
    const MediaItem media = itemFromVariant(item);
    if (media.id.isEmpty())
        return;
    addToQueue(QList<MediaItem>{media});
}

int PlayQueue::addToQueue(const QList<MediaItem> &items)
{
    QList<MediaItem> valid;
    valid.reserve(items.size());
    for (const MediaItem &item : items) {
        if (!item.id.isEmpty())
            valid.append(item);
    }
    if (valid.isEmpty())
        return 0;

    const int first = static_cast<int>(m_entries.size());
    const int last = first + static_cast<int>(valid.size()) - 1;
    const bool wasEmpty = m_entries.isEmpty();
    beginInsertRows(QModelIndex(), first, last);
    m_entries.reserve(m_entries.size() + valid.size());
    m_originalKeys.reserve(m_originalKeys.size() + valid.size());
    for (MediaItem &item : valid) {
        const quint64 key = m_nextKey++;
        m_entries.append({std::move(item), key});
        m_originalKeys.append(key);
    }
    if (wasEmpty)
        m_currentIndex = 0;
    endInsertRows();

    emit queueChanged();
    notifyCursor();
    return static_cast<int>(valid.size());
}

int PlayQueue::playNext(const QList<MediaItem> &items)
{
    QList<MediaItem> valid;
    valid.reserve(items.size());
    for (const MediaItem &item : items) {
        if (!item.id.isEmpty())
            valid.append(item);
    }
    if (valid.isEmpty())
        return 0;

    const bool wasEmpty = m_entries.isEmpty();
    const int first = m_currentIndex < 0 ? 0 : m_currentIndex + 1;
    const int last = first + static_cast<int>(valid.size()) - 1;
    int originalRow = static_cast<int>(m_originalKeys.size());
    if (!wasEmpty) {
        const int currentOriginal = originalPositionOf(m_entries.at(m_currentIndex).key);
        if (currentOriginal >= 0)
            originalRow = currentOriginal + 1;
    }

    QList<Entry> inserted;
    QList<quint64> insertedKeys;
    inserted.reserve(valid.size());
    insertedKeys.reserve(valid.size());
    for (MediaItem &item : valid) {
        const quint64 key = m_nextKey++;
        inserted.append({std::move(item), key});
        insertedKeys.append(key);
    }

    beginInsertRows(QModelIndex(), first, last);
    QList<Entry> mergedEntries;
    mergedEntries.reserve(m_entries.size() + inserted.size());
    for (int row = 0; row < first; ++row)
        mergedEntries.append(std::move(m_entries[row]));
    mergedEntries.append(std::move(inserted));
    for (int row = first; row < m_entries.size(); ++row)
        mergedEntries.append(std::move(m_entries[row]));
    m_entries = std::move(mergedEntries);

    QList<quint64> mergedOriginal;
    mergedOriginal.reserve(m_originalKeys.size() + insertedKeys.size());
    for (int row = 0; row < originalRow; ++row)
        mergedOriginal.append(m_originalKeys.at(row));
    mergedOriginal.append(insertedKeys);
    for (int row = originalRow; row < m_originalKeys.size(); ++row)
        mergedOriginal.append(m_originalKeys.at(row));
    m_originalKeys = std::move(mergedOriginal);
    if (wasEmpty)
        m_currentIndex = 0;
    endInsertRows();

    // Existing rows after the insertion moved, so only their positional role
    // changed. Inserted delegates read both roles correctly on creation.
    const int shiftedFirst = last + 1;
    if (shiftedFirst < m_entries.size())
        emit dataChanged(index(shiftedFirst), index(static_cast<int>(m_entries.size()) - 1),
                         {QueueIndexRole});
    emit queueChanged();
    notifyCursor();
    return static_cast<int>(valid.size());
}

void PlayQueue::removeAt(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    const bool wasCurrent = row == m_currentIndex;

    beginRemoveRows(QModelIndex(), row, row);
    m_originalKeys.removeOne(m_entries.at(row).key);
    m_entries.removeAt(row);
    endRemoveRows();

    if (m_entries.isEmpty()) {
        m_currentIndex = -1;
        emit queueChanged();
        notifyCursor();
        emit exhausted();
        return;
    }

    if (row < m_currentIndex) {
        --m_currentIndex;
    } else if (wasCurrent) {
        // The next item slid into this row and becomes current; at the tail
        // there is no next, so the new last row takes over.
        m_currentIndex = qMin(m_currentIndex, static_cast<int>(m_entries.size()) - 1);
    }

    emitRowMetaChanged();
    emit queueChanged();
    notifyCursor(wasCurrent);
}

void PlayQueue::moveItem(int from, int to)
{
    const int size = static_cast<int>(m_entries.size());
    if (from < 0 || from >= size || to < 0 || to >= size || from == to)
        return;

    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
    const Entry entry = m_entries.takeAt(from);
    m_entries.insert(to, entry);
    // A manual move while shuffled reorders the shuffle only: un-shuffling is
    // meant to give the user back the order the items arrived in.
    if (!m_shuffled) {
        const int originalFrom = originalPositionOf(entry.key);
        if (originalFrom >= 0) {
            m_originalKeys.removeAt(originalFrom);
            m_originalKeys.insert(qBound(0, to, static_cast<int>(m_originalKeys.size())),
                                  entry.key);
        }
    }
    endMoveRows();

    if (m_currentIndex == from)
        m_currentIndex = to;
    else if (from < m_currentIndex && to >= m_currentIndex)
        --m_currentIndex;
    else if (from > m_currentIndex && to <= m_currentIndex)
        ++m_currentIndex;

    emitRowMetaChanged();
    emit queueChanged();
    notifyCursor();
}

void PlayQueue::clear()
{
    if (m_entries.isEmpty() && m_currentIndex < 0)
        return;
    beginResetModel();
    m_entries.clear();
    m_originalKeys.clear();
    m_currentIndex = -1;
    endResetModel();
    emit queueChanged();
    notifyCursor();
}

// ── Cursor ────────────────────────────────────────────────────────────────────

void PlayQueue::setCurrentIndex(int row)
{
    if (row == m_currentIndex)
        return;
    m_currentIndex = row;
    emitRowMetaChanged();
    notifyCursor();
}

int PlayQueue::nextIndex() const
{
    if (m_entries.isEmpty() || m_currentIndex < 0)
        return -1;
    if (m_repeatMode == RepeatOne)
        return m_currentIndex;
    if (m_currentIndex + 1 < m_entries.size())
        return m_currentIndex + 1;
    return m_repeatMode == RepeatAll ? 0 : -1;
}

int PlayQueue::previousIndex() const
{
    if (m_entries.isEmpty() || m_currentIndex < 0)
        return -1;
    if (m_repeatMode == RepeatOne)
        return m_currentIndex;
    if (m_currentIndex > 0)
        return m_currentIndex - 1;
    return m_repeatMode == RepeatAll ? static_cast<int>(m_entries.size()) - 1 : -1;
}

bool PlayQueue::advance()
{
    const int next = nextIndex();
    if (next < 0) {
        emit exhausted();
        return false;
    }
    setCurrentIndex(next); // no-op under RepeatOne: the same item plays again
    return true;
}

bool PlayQueue::goBack()
{
    const int previous = previousIndex();
    if (previous < 0)
        return false;
    setCurrentIndex(previous);
    return true;
}

void PlayQueue::jumpTo(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    setCurrentIndex(row);
}

// ── Shuffle & repeat ──────────────────────────────────────────────────────────

void PlayQueue::setShuffled(bool shuffled)
{
    if (shuffled == m_shuffled)
        return;
    m_shuffled = shuffled;

    if (m_entries.size() > 1) {
        beginResetModel();
        if (shuffled) {
            // Everything except the current item is redealt into the slots the
            // current item does not occupy, so what is playing keeps playing
            // and keeps its row.
            QList<Entry> others;
            others.reserve(m_entries.size() - 1);
            for (int row = 0; row < m_entries.size(); ++row) {
                if (row != m_currentIndex)
                    others.append(m_entries.at(row));
            }
            std::shuffle(others.begin(), others.end(), *QRandomGenerator::global());
            int taken = 0;
            for (int row = 0; row < m_entries.size(); ++row) {
                if (row != m_currentIndex)
                    m_entries[row] = others.at(taken++);
            }
        } else {
            // Restore, never re-sort: the original order is kept as a key list
            // precisely so that un-shuffling is exact even for a queue the user
            // has since added to.
            QHash<quint64, Entry> byKey;
            byKey.reserve(m_entries.size());
            for (const Entry &entry : std::as_const(m_entries))
                byKey.insert(entry.key, entry);
            const quint64 currentKey =
                m_currentIndex >= 0 ? m_entries.at(m_currentIndex).key : quint64(0);

            QList<Entry> restored;
            restored.reserve(m_entries.size());
            for (quint64 key : std::as_const(m_originalKeys)) {
                const auto it = byKey.constFind(key);
                if (it != byKey.cend())
                    restored.append(*it);
            }
            m_entries = std::move(restored);
            for (int row = 0; row < m_entries.size(); ++row) {
                if (m_entries.at(row).key == currentKey) {
                    m_currentIndex = row;
                    break;
                }
            }
        }
        endResetModel();
        notifyCursor();
    }

    emit shuffledChanged();
}

void PlayQueue::setRepeatMode(RepeatMode mode)
{
    if (mode == m_repeatMode)
        return;
    m_repeatMode = mode;
    emit repeatModeChanged();
    emit currentChanged(); // hasNext/hasPrevious depend on the mode
}

void PlayQueue::cycleRepeatMode()
{
    switch (m_repeatMode) {
    case RepeatOff:
        setRepeatMode(RepeatAll);
        break;
    case RepeatAll:
        setRepeatMode(RepeatOne);
        break;
    case RepeatOne:
        setRepeatMode(RepeatOff);
        break;
    }
}

} // namespace strmqt
