#pragma once

#include "app/models/MediaItemModel.h"
#include "server/dto/MediaItem.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QVariant>
#include <QVariantMap>

namespace strmqt {

// The keystone abstraction (ARCHITECTURE.md): an ordered list of MediaItems
// with a current position. Shuffle, auto-advance, the Up Next card, prev/next
// and "play all" are five features that were all blocked on this one concept.
//
// It is a QAbstractListModel so the queue panel can reuse the same delegates as
// every rail and grid: the roles below MediaItemModel::SubtitleRole *are*
// MediaItemModel's roles, with the same names and the same values, because
// data() forwards to a private MediaItemModel holding the same items. Two extra
// roles (queueIndex, isCurrent) are all that a queue adds on top.
//
// QtCore only — no QtGui, no network, no player. The queue never starts
// playback itself; PlayerController watches currentChanged() and does that.
class PlayQueue : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY queueChanged)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY currentChanged)
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY currentChanged)
    Q_PROPERTY(bool shuffled READ shuffled WRITE setShuffled NOTIFY shuffledChanged)
    Q_PROPERTY(RepeatMode repeatMode READ repeatMode WRITE setRepeatMode NOTIFY repeatModeChanged)

public:
    enum RepeatMode
    {
        RepeatOff,
        RepeatAll,
        RepeatOne,
    };
    Q_ENUM(RepeatMode)

    // Queue-only roles; everything below these is MediaItemModel's, unchanged.
    enum Role
    {
        QueueIndexRole = MediaItemModel::SubtitleRole + 1,
        IsCurrentRole,
    };
    Q_ENUM(Role)

    explicit PlayQueue(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Replaces the whole queue. The given order becomes the unshuffled order,
    // so a queue always starts unshuffled (callers that want a shuffled queue
    // call setShuffled(true) afterwards and can still get the order back).
    void setItems(QList<MediaItem> items, int startIndex = 0);

    int currentIndex() const { return m_currentIndex; }
    // {} when the queue is empty.
    MediaItem current() const;
    Q_INVOKABLE QVariantMap currentItem() const;
    Q_INVOKABLE QVariantMap itemAt(int row) const;

    bool hasNext() const { return nextIndex() >= 0; }
    bool hasPrevious() const { return previousIndex() >= 0; }
    // Row that advance()/goBack() would land on, honouring repeatMode; -1 when
    // there is none. RepeatOne answers "the current row" to both.
    int nextIndex() const;
    int previousIndex() const;

    // Inserted straight after the current item (or as the whole queue when it
    // is empty, in which case the item becomes current and starts playing).
    Q_INVOKABLE void playNext(const QVariant &item);
    Q_INVOKABLE void addToQueue(const QVariant &item);
    // Removing the current row promotes the next one (playback continues with
    // it); removing the last row left in the queue emits exhausted().
    Q_INVOKABLE void removeAt(int row);
    Q_INVOKABLE void moveItem(int from, int to);
    Q_INVOKABLE void clear();

    // true when there is something to play afterwards. RepeatOne answers true
    // without moving: the same item plays again.
    bool advance();
    bool goBack();
    Q_INVOKABLE void jumpTo(int row);

    bool shuffled() const { return m_shuffled; }
    // Keeps the current item current and where it is, shuffling every other
    // item into the remaining slots. Turning it back off restores the order the
    // items were given in — the original is kept, never reconstructed by sorting.
    void setShuffled(bool shuffled);

    RepeatMode repeatMode() const { return m_repeatMode; }
    void setRepeatMode(RepeatMode mode);
    Q_INVOKABLE void cycleRepeatMode();

    // QML/QVariantMap → MediaItem, tolerant of both MediaItemModel's role map
    // ({itemId, label, ...}) and a bare item id string.
    static MediaItem itemFromVariant(const QVariant &value);

signals:
    void currentChanged();
    void queueChanged();
    void shuffledChanged();
    void repeatModeChanged();
    // The queue ran out: advance() found nothing, or the last row was removed.
    void exhausted();

private:
    // An item plus the identity that survives shuffling and re-ordering. Ids are
    // not usable as that identity: the same episode may legitimately be queued
    // twice.
    struct Entry
    {
        MediaItem item;
        quint64 key = 0;
    };

    void emitRowMetaChanged();
    void syncView();
    void setCurrentIndex(int row);
    void insertEntry(int row, const MediaItem &item, bool originalAfterCurrent);
    int originalPositionOf(quint64 key) const;

    QList<Entry> m_entries;        // play order (shuffled or not)
    QList<quint64> m_originalKeys; // the order items were given in
    quint64 m_nextKey = 1;
    int m_currentIndex = -1;
    bool m_shuffled = false;
    RepeatMode m_repeatMode = RepeatOff;
    // Role parity with rails and grids, for free: the queue delegates data() to
    // a MediaItemModel holding the same rows in the same order.
    MediaItemModel m_view;
};

} // namespace strmqt
