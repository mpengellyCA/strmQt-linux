#include "PlayQueue.h"

#include <QMetaType>
#include <QRandomGenerator>

#include <algorithm>
#include <utility>

namespace strmqt {

namespace {

const auto kItemIdKey = QStringLiteral("itemId");

// image://emby/<id>/<type>/<tag> → <tag>. The model turns a tag into a URL; a
// map handed back from QML carries only the URL, so round-trip it rather than
// losing every poster the moment an item passes through the queue.
QString tagFromImageSource(const QString &source)
{
    if (source.isEmpty())
        return {};
    const qsizetype slash = source.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : source.mid(slash + 1);
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
    item.favorite = map.value(QStringLiteral("favorite")).toBool();
    item.primaryImageTag = tagFromImageSource(map.value(QStringLiteral("posterUrl")).toString());
    const QString backdrop = tagFromImageSource(map.value(QStringLiteral("backdropUrl")).toString());
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
        return m_view.data(m_view.index(index.row()), role);
    }
}

QHash<int, QByteArray> PlayQueue::roleNames() const
{
    QHash<int, QByteArray> roles = m_view.roleNames();
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

void PlayQueue::syncView()
{
    QList<MediaItem> items;
    items.reserve(m_entries.size());
    for (const Entry &entry : std::as_const(m_entries))
        items.append(entry.item);
    m_view.setItems(std::move(items));
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
    syncView();
    endResetModel();

    if (wasShuffled)
        emit shuffledChanged();
    emit queueChanged();
    notifyCursor();
}

int PlayQueue::originalPositionOf(quint64 key) const
{
    return static_cast<int>(m_originalKeys.indexOf(key));
}

void PlayQueue::insertEntry(int row, const MediaItem &item, bool originalAfterCurrent)
{
    row = qBound(0, row, static_cast<int>(m_entries.size()));
    const quint64 key = m_nextKey++;

    // Where the item sits in the *unshuffled* order, so un-shuffling later does
    // not teleport it to the end of the queue.
    int originalRow = static_cast<int>(m_originalKeys.size());
    if (originalAfterCurrent && m_currentIndex >= 0 && m_currentIndex < m_entries.size()) {
        const int after = originalPositionOf(m_entries.at(m_currentIndex).key);
        if (after >= 0)
            originalRow = after + 1;
    }

    beginInsertRows(QModelIndex(), row, row);
    m_entries.insert(row, {item, key});
    m_originalKeys.insert(originalRow, key);
    syncView();
    endInsertRows();

    if (m_entries.size() == 1)
        m_currentIndex = 0; // first item in an empty queue starts playing
    else if (row <= m_currentIndex)
        ++m_currentIndex;

    emitRowMetaChanged();
    emit queueChanged();
    // hasNext/hasPrevious hang off currentChanged, and both can flip on an
    // insert. The cursor itself only moved if this was the first item.
    notifyCursor();
}

void PlayQueue::playNext(const QVariant &item)
{
    const MediaItem media = itemFromVariant(item);
    if (media.id.isEmpty())
        return;
    insertEntry(m_currentIndex < 0 ? 0 : m_currentIndex + 1, media, true);
}

void PlayQueue::addToQueue(const QVariant &item)
{
    const MediaItem media = itemFromVariant(item);
    if (media.id.isEmpty())
        return;
    insertEntry(static_cast<int>(m_entries.size()), media, false);
}

void PlayQueue::removeAt(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    const bool wasCurrent = row == m_currentIndex;

    beginRemoveRows(QModelIndex(), row, row);
    m_originalKeys.removeOne(m_entries.at(row).key);
    m_entries.removeAt(row);
    syncView();
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
    syncView();
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
    syncView();
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
        syncView();
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
