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
// MediaItemModel's roles, with the same names and the same values. Both model
// surfaces use MediaItemModel's shared role conversion, so the queue does not
// need a second copy of every item. Two extra roles (queueIndex, isCurrent) are
// all that a queue adds on top.
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
    // Where this queue came from, for the now-playing pane's "up next in
    // context" (MUSIC.md §4): "from Lift Yr Skinny Fists".
    Q_PROPERTY(QString contextLabel READ contextLabel NOTIFY queueChanged)

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

    // A queue that can be put back exactly as it was, for the one case that
    // needs it: a film interrupting a record (PlayerController). setItems()
    // cannot do this — it defines the given order as the unshuffled one, so a
    // shuffled queue would come back either unshuffled or re-shuffled into a
    // different future, and "resume where I was" would be a lie about what
    // plays next.
    struct Snapshot
    {
        QList<MediaItem> playOrder;   // the order actually played
        QList<int> originalPositions; // indices into playOrder, in given order
        int currentIndex = -1;
        bool shuffled = false;
        RepeatMode repeatMode = RepeatOff;

        bool isValid() const { return !playOrder.isEmpty(); }
    };

    Snapshot snapshot() const;
    void restore(const Snapshot &snapshot);

    // The server's full record for an entry that was seeded from a click. A
    // bare play knows an id, a title and a type — not the artwork, the series
    // or the episode number — so every now-playing surface had nothing to draw
    // for anything played straight from a card, while music (which arrives as
    // a queue of complete items) looked right. Matched by id, never by row.
    void enrichEntry(const MediaItem &item);

    // Derived from what the queue HOLDS, not remembered from the verb that
    // filled it. A label carried in from a click can outlive the queue it
    // described — play an album, then queue three more things onto it, and the
    // remembered label is a lie — whereas this cannot be wrong about a queue it
    // is reading. Empty when there is no single answer: a hand-assembled queue,
    // a shuffle across a whole library, or anything that is not music.
    QString contextLabel() const;

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
    // One structural operation for a whole user gesture (for example, adding
    // an album). Invalid entries are ignored; callers can detect an empty
    // result from the returned count.
    int addToQueue(const QList<MediaItem> &items);
    int playNext(const QList<MediaItem> &items);
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
    // A *different* entry is now under the cursor and is meant to be played: a
    // jumpTo, an advance, a new queue, or the first item dropped into an empty
    // one. A row merely shifting under the cursor — an insert, or a removal
    // above it — is not this, which is what keeps a queue edit from restarting
    // whatever is playing.
    void currentItemChanged();
    // The entry that was current was REMOVED and the row below slid into its
    // place. Deliberately separate from currentItemChanged(): it is not a
    // request to play. stop() leaves the queue intact, so an edit made while
    // nothing is playing must not resurrect the session; playback continues
    // into the promoted row only when it was already running.
    void currentItemDisplaced();
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
    // Identity of the entry under the cursor, 0 when there is none.
    quint64 currentKey() const;
    // Emits currentChanged() and, when the entry under the cursor is a
    // different one, exactly one of the two cursor signals above.
    void notifyCursor(bool displaced = false);
    void setCurrentIndex(int row);
    int originalPositionOf(quint64 key) const;

    QList<Entry> m_entries;        // play order (shuffled or not)
    QList<quint64> m_originalKeys; // the order items were given in
    quint64 m_nextKey = 1;
    int m_currentIndex = -1;
    // Key of the entry the cursor was last reported on, so a re-index can be
    // told apart from an actual change of item.
    quint64 m_currentKey = 0;
    bool m_shuffled = false;
    RepeatMode m_repeatMode = RepeatOff;
};

} // namespace strmqt
