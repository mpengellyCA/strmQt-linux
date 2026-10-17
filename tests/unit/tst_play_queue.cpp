#include <QSet>
#include <QSignalSpy>
#include <QtTest>

#include "app/PlayQueue.h"
#include "app/models/MediaItemModel.h"

using namespace strmqt;

namespace {

MediaItem episode(int number)
{
    MediaItem item;
    item.id = QStringLiteral("ep%1").arg(number);
    item.name = QStringLiteral("Episode %1").arg(number);
    item.type = QStringLiteral("Episode");
    item.seriesName = QStringLiteral("Fixture Series");
    item.parentIndexNumber = 1;
    item.indexNumber = number;
    item.runtimeTicks = 20 * 60 * kTicksPerSecond;
    return item;
}

MediaItem track(const QString &name, const QString &album, const QString &albumArtist)
{
    MediaItem item;
    item.id = name;
    item.name = name;
    item.type = QStringLiteral("Audio");
    item.album = album;
    item.albumArtist = albumArtist;
    return item;
}

QList<MediaItem> episodes(int count)
{
    QList<MediaItem> items;
    for (int i = 1; i <= count; ++i)
        items.append(episode(i));
    return items;
}

QStringList idsOf(const PlayQueue &queue)
{
    QStringList ids;
    for (int row = 0; row < queue.rowCount(); ++row)
        ids.append(queue.itemAt(row).value(QStringLiteral("itemId")).toString());
    return ids;
}

} // namespace

class PlayQueueTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void rolesMatchMediaItemModel();
    void setItemsEstablishesTheCursor();
    void shuffleKeepsTheCurrentItemAndRestoresTheOriginalOrder();
    void shuffleSurvivesAnInsertAndUnShufflesInPlace();
    void advanceHonoursEveryRepeatMode();
    void goBackHonoursEveryRepeatMode();
    void removingTheCurrentRowPromotesTheNextOne();
    void removingTheLastRowEmptiesAndExhausts();
    void playNextInsertsAfterCurrentAndAddToQueueAppends();
    void insertingIntoAnEmptyQueueStartsIt();
    void moveItemKeepsTheCursorOnTheSameItem();
    void entryKeysFollowEntriesNotRowsOrIds();
    void jumpToAndClear();
    void snapshotRestoresOrderCursorAndModes();
    void batchAppendIsOneModelOperation();
    void largeDuplicateQueueSnapshotsByEntryIdentity();
    void cursorSignalsTellAMoveFromAReIndex();
    void itemFromVariantRoundTripsAModelMap();
    void playCountSurvivesTheRoundTrip();
    void contextLabelNamesTheRecordTheQueueCameFrom();
    void sourceLabelFollowsTheQueueItDescribes();
    void enrichEntryReachesEveryOccurrenceAndKeepsTheirEntryIds();
    void updateUserDataPatchesEveryOccurrenceAndNeverTheCursor();
    void updateUserDataLeavesAbsentFieldsAndRepeatsAlone();
};

void PlayQueueTest::initTestCase()
{
    setEmbyImageSourceNamespace(QStringLiteral("test-session"));
}

// The whole point of the queue being a model is that the queue panel can use the
// same delegates as every rail: identical role names *and* identical values.
void PlayQueueTest::rolesMatchMediaItemModel()
{
    MediaItemModel model;
    model.setItems(episodes(2));
    PlayQueue queue;
    queue.setItems(episodes(2));

    const QHash<int, QByteArray> modelRoles = model.roleNames();
    const QHash<int, QByteArray> queueRoles = queue.roleNames();
    for (auto it = modelRoles.cbegin(); it != modelRoles.cend(); ++it) {
        QVERIFY2(queueRoles.contains(it.key()), it.value().constData());
        QCOMPARE(queueRoles.value(it.key()), it.value());
        QCOMPARE(queue.data(queue.index(0), it.key()), model.data(model.index(0), it.key()));
    }
    QCOMPARE(queueRoles.value(PlayQueue::QueueIndexRole), QByteArray("queueIndex"));
    QCOMPARE(queueRoles.value(PlayQueue::IsCurrentRole), QByteArray("isCurrent"));
    QCOMPARE(queue.itemAt(0).value(QStringLiteral("isCurrent")).toBool(), true);
    QCOMPARE(queue.itemAt(1).value(QStringLiteral("isCurrent")).toBool(), false);
    QCOMPARE(queue.itemAt(1).value(QStringLiteral("queueIndex")).toInt(), 1);
    // The label the OSD shows comes through unchanged.
    QCOMPARE(queue.currentItem().value(QStringLiteral("label")).toString(),
             QStringLiteral("Fixture Series — S1E1 — Episode 1"));
}

void PlayQueueTest::setItemsEstablishesTheCursor()
{
    PlayQueue queue;
    QCOMPARE(queue.rowCount(), 0);
    QCOMPARE(queue.currentIndex(), -1);
    QVERIFY(queue.current().id.isEmpty());
    QVERIFY(queue.currentItem().isEmpty());
    QVERIFY(!queue.hasNext());
    QVERIFY(!queue.hasPrevious());

    QSignalSpy currentSpy(&queue, &PlayQueue::currentChanged);
    QSignalSpy queueSpy(&queue, &PlayQueue::queueChanged);
    queue.setItems(episodes(5), 2);
    QCOMPARE(queue.rowCount(), 5);
    QCOMPARE(queue.currentIndex(), 2);
    QCOMPARE(queue.current().id, QStringLiteral("ep3"));
    QCOMPARE(currentSpy.count(), 1);
    QCOMPARE(queueSpy.count(), 1);
    QVERIFY(queue.hasNext());
    QVERIFY(queue.hasPrevious());

    // Out-of-range start indexes are clamped, never crash.
    queue.setItems(episodes(3), 99);
    QCOMPARE(queue.currentIndex(), 2);
    queue.setItems(episodes(3), -4);
    QCOMPARE(queue.currentIndex(), 0);
    queue.setItems({}, 0);
    QCOMPARE(queue.currentIndex(), -1);
}

void PlayQueueTest::shuffleKeepsTheCurrentItemAndRestoresTheOriginalOrder()
{
    PlayQueue queue;
    queue.setItems(episodes(12), 4);
    const QStringList original = idsOf(queue);
    QCOMPARE(queue.current().id, QStringLiteral("ep5"));

    QSignalSpy shuffledSpy(&queue, &PlayQueue::shuffledChanged);
    queue.setShuffled(true);
    QCOMPARE(shuffledSpy.count(), 1);
    QVERIFY(queue.shuffled());

    // What is playing keeps playing, and keeps its row.
    QCOMPARE(queue.currentIndex(), 4);
    QCOMPARE(queue.current().id, QStringLiteral("ep5"));
    // Nothing is lost or duplicated by the deal.
    QStringList shuffled = idsOf(queue);
    QCOMPARE(shuffled.size(), original.size());
    QStringList sortedShuffled = shuffled;
    QStringList sortedOriginal = original;
    sortedShuffled.sort();
    sortedOriginal.sort();
    QCOMPARE(sortedShuffled, sortedOriginal);

    queue.setShuffled(false);
    QCOMPARE(shuffledSpy.count(), 2);
    QVERIFY(!queue.shuffled());
    // Restored, not re-sorted — and still on the same item.
    QCOMPARE(idsOf(queue), original);
    QCOMPARE(queue.currentIndex(), 4);
    QCOMPARE(queue.current().id, QStringLiteral("ep5"));
}

void PlayQueueTest::shuffleSurvivesAnInsertAndUnShufflesInPlace()
{
    PlayQueue queue;
    queue.setItems(episodes(6), 0);
    queue.setShuffled(true);

    MediaItem extra = episode(99);
    QVariantMap map;
    map.insert(QStringLiteral("itemId"), extra.id);
    map.insert(QStringLiteral("name"), extra.name);
    queue.playNext(map);
    QCOMPARE(queue.rowCount(), 7);
    QCOMPARE(queue.itemAt(queue.currentIndex() + 1).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("ep99"));

    const QString currentId = queue.current().id;
    queue.setShuffled(false);
    // The insert lands right after the current item in the restored order too,
    // rather than being flung to the end of the queue.
    const QStringList restored = idsOf(queue);
    QCOMPARE(restored.size(), 7);
    QCOMPARE(restored.indexOf(QStringLiteral("ep99")), restored.indexOf(currentId) + 1);
    QCOMPARE(queue.current().id, currentId);
}

void PlayQueueTest::advanceHonoursEveryRepeatMode()
{
    PlayQueue queue;
    queue.setItems(episodes(3), 0);
    QSignalSpy exhaustedSpy(&queue, &PlayQueue::exhausted);

    QVERIFY(queue.advance());
    QCOMPARE(queue.currentIndex(), 1);
    QVERIFY(queue.advance());
    QCOMPARE(queue.currentIndex(), 2);
    QVERIFY(!queue.hasNext());
    QVERIFY(!queue.advance());
    QCOMPARE(queue.currentIndex(), 2);
    QCOMPARE(exhaustedSpy.count(), 1);

    // RepeatAll wraps instead of running out.
    queue.setRepeatMode(PlayQueue::RepeatAll);
    QVERIFY(queue.hasNext());
    QVERIFY(queue.advance());
    QCOMPARE(queue.currentIndex(), 0);
    QCOMPARE(exhaustedSpy.count(), 1);

    // RepeatOne never moves, and never runs out.
    queue.jumpTo(1);
    queue.setRepeatMode(PlayQueue::RepeatOne);
    QVERIFY(queue.hasNext());
    QVERIFY(queue.advance());
    QCOMPARE(queue.currentIndex(), 1);
    QVERIFY(queue.advance());
    QCOMPARE(queue.currentIndex(), 1);
    QCOMPARE(exhaustedSpy.count(), 1);

    // A single-item queue: only RepeatOff is a dead end.
    PlayQueue single;
    single.setItems(episodes(1), 0);
    QVERIFY(!single.advance());
    single.setRepeatMode(PlayQueue::RepeatAll);
    QVERIFY(single.advance());
    QCOMPARE(single.currentIndex(), 0);
}

void PlayQueueTest::goBackHonoursEveryRepeatMode()
{
    PlayQueue queue;
    queue.setItems(episodes(3), 2);
    QVERIFY(queue.goBack());
    QCOMPARE(queue.currentIndex(), 1);
    QVERIFY(queue.goBack());
    QCOMPARE(queue.currentIndex(), 0);
    QVERIFY(!queue.hasPrevious());
    QVERIFY(!queue.goBack());

    queue.setRepeatMode(PlayQueue::RepeatAll);
    QVERIFY(queue.hasPrevious());
    QVERIFY(queue.goBack());
    QCOMPARE(queue.currentIndex(), 2);

    queue.setRepeatMode(PlayQueue::RepeatOne);
    QVERIFY(queue.goBack());
    QCOMPARE(queue.currentIndex(), 2);
}

void PlayQueueTest::removingTheCurrentRowPromotesTheNextOne()
{
    PlayQueue queue;
    queue.setItems(episodes(4), 1);
    QSignalSpy currentSpy(&queue, &PlayQueue::currentChanged);
    QSignalSpy exhaustedSpy(&queue, &PlayQueue::exhausted);

    queue.removeAt(1);
    QCOMPARE(queue.rowCount(), 3);
    QCOMPARE(queue.currentIndex(), 1);
    QCOMPARE(queue.current().id, QStringLiteral("ep3")); // what was next now plays
    QCOMPARE(currentSpy.count(), 1);
    QCOMPARE(exhaustedSpy.count(), 0);

    // Removing above the cursor keeps the same item current.
    queue.removeAt(0);
    QCOMPARE(queue.currentIndex(), 0);
    QCOMPARE(queue.current().id, QStringLiteral("ep3"));

    // Removing the current row at the tail falls back to the new last row.
    queue.jumpTo(1);
    queue.removeAt(1);
    QCOMPARE(queue.rowCount(), 1);
    QCOMPARE(queue.currentIndex(), 0);
    QCOMPARE(queue.current().id, QStringLiteral("ep3"));
    QCOMPARE(exhaustedSpy.count(), 0);

    queue.removeAt(9); // out of range: no-op, no signals
    QCOMPARE(queue.rowCount(), 1);
}

void PlayQueueTest::removingTheLastRowEmptiesAndExhausts()
{
    PlayQueue queue;
    queue.setItems(episodes(1), 0);
    QSignalSpy exhaustedSpy(&queue, &PlayQueue::exhausted);

    queue.removeAt(0);
    QCOMPARE(queue.rowCount(), 0);
    QCOMPARE(queue.currentIndex(), -1);
    QVERIFY(queue.current().id.isEmpty());
    QCOMPARE(exhaustedSpy.count(), 1);
}

void PlayQueueTest::playNextInsertsAfterCurrentAndAddToQueueAppends()
{
    PlayQueue queue;
    queue.setItems(episodes(3), 1);

    QVariantMap wanted;
    wanted.insert(QStringLiteral("itemId"), QStringLiteral("movie-1"));
    wanted.insert(QStringLiteral("name"), QStringLiteral("Dune"));
    queue.playNext(wanted);
    QCOMPARE(queue.rowCount(), 4);
    QCOMPARE(idsOf(queue),
             QStringList({QStringLiteral("ep1"), QStringLiteral("ep2"), QStringLiteral("movie-1"),
                          QStringLiteral("ep3")}));
    QCOMPARE(queue.currentIndex(), 1); // still playing what it was

    QVariantMap later;
    later.insert(QStringLiteral("itemId"), QStringLiteral("movie-2"));
    queue.addToQueue(later);
    QCOMPARE(idsOf(queue).last(), QStringLiteral("movie-2"));
    QCOMPARE(queue.currentIndex(), 1);

    // An item with no id is not a queue entry.
    queue.addToQueue(QVariantMap{});
    QCOMPARE(queue.rowCount(), 5);
}

void PlayQueueTest::insertingIntoAnEmptyQueueStartsIt()
{
    PlayQueue queue;
    QSignalSpy currentSpy(&queue, &PlayQueue::currentChanged);
    queue.playNext(QStringLiteral("ep7"));
    QCOMPARE(queue.rowCount(), 1);
    QCOMPARE(queue.currentIndex(), 0);
    QCOMPARE(queue.current().id, QStringLiteral("ep7"));
    QCOMPARE(currentSpy.count(), 1);
}

void PlayQueueTest::moveItemKeepsTheCursorOnTheSameItem()
{
    PlayQueue queue;
    queue.setItems(episodes(4), 0);

    queue.moveItem(3, 1);
    QCOMPARE(idsOf(queue),
             QStringList({QStringLiteral("ep1"), QStringLiteral("ep4"), QStringLiteral("ep2"),
                          QStringLiteral("ep3")}));
    QCOMPARE(queue.currentIndex(), 0);

    // Moving the current item takes the cursor with it.
    queue.moveItem(0, 2);
    QCOMPARE(queue.currentIndex(), 2);
    QCOMPARE(queue.current().id, QStringLiteral("ep1"));

    // A move over the cursor shifts it by one.
    queue.moveItem(0, 3);
    QCOMPARE(queue.currentIndex(), 1);
    QCOMPARE(queue.current().id, QStringLiteral("ep1"));

    queue.moveItem(0, 0);
    queue.moveItem(-1, 2);
    queue.moveItem(0, 99);
    QCOMPARE(queue.rowCount(), 4);
}

// PlayerController's gapless prefetch is for one queue ENTRY: it has to tell
// "the same next track" from "another copy of it" and survive a re-index.
void PlayQueueTest::entryKeysFollowEntriesNotRowsOrIds()
{
    PlayQueue queue;
    QCOMPARE(queue.entryKeyAt(0), quint64(0));
    QList<MediaItem> items = episodes(3);
    items.append(episode(2)); // the same item queued twice
    queue.setItems(items, 0);

    const quint64 second = queue.entryKeyAt(1);
    const quint64 again = queue.entryKeyAt(3);
    QVERIFY(second != 0 && again != 0);
    QVERIFY(second != again);
    QVERIFY(second != queue.entryKeyAt(0) && second != queue.entryKeyAt(2));
    QCOMPARE(queue.entryKeyAt(-1), quint64(0));
    QCOMPARE(queue.entryKeyAt(4), quint64(0));

    // A move re-indexes; the key goes with the entry.
    queue.moveItem(1, 3);
    QCOMPARE(queue.entryKeyAt(3), second);
    QCOMPARE(queue.entryKeyAt(2), again);
    QCOMPARE(queue.itemAt(3).value(QStringLiteral("itemId")).toString(), QStringLiteral("ep2"));

    // So does a shuffle, and turning it off again.
    QSet<quint64> keys;
    for (int row = 0; row < queue.rowCount(); ++row)
        keys.insert(queue.entryKeyAt(row));
    queue.setShuffled(true);
    QSet<quint64> shuffled;
    for (int row = 0; row < queue.rowCount(); ++row)
        shuffled.insert(queue.entryKeyAt(row));
    QCOMPARE(shuffled, keys);
}

void PlayQueueTest::jumpToAndClear()
{
    PlayQueue queue;
    queue.setItems(episodes(3), 0);
    QSignalSpy currentSpy(&queue, &PlayQueue::currentChanged);

    queue.jumpTo(2);
    QCOMPARE(queue.currentIndex(), 2);
    QCOMPARE(currentSpy.count(), 1);
    queue.jumpTo(2); // already there
    QCOMPARE(currentSpy.count(), 1);
    queue.jumpTo(7); // out of range
    QCOMPARE(queue.currentIndex(), 2);

    QSignalSpy exhaustedSpy(&queue, &PlayQueue::exhausted);
    queue.clear();
    QCOMPARE(queue.rowCount(), 0);
    QCOMPARE(queue.currentIndex(), -1);
    // clear() is an explicit act, not the queue running out.
    QCOMPARE(exhaustedSpy.count(), 0);
}

// currentChanged() answers "hasNext/hasPrevious may have flipped" and fires for
// every structural change; the cursor signals answer "a different entry is under
// the cursor". Conflating them is what let a queue edit made while nothing was
// playing start playback, so the split is asserted signal by signal.
void PlayQueueTest::cursorSignalsTellAMoveFromAReIndex()
{
    PlayQueue queue;
    QSignalSpy changedSpy(&queue, &PlayQueue::currentChanged);
    QSignalSpy movedSpy(&queue, &PlayQueue::currentItemChanged);
    QSignalSpy displacedSpy(&queue, &PlayQueue::currentItemDisplaced);

    // The first item of an empty queue is a move: it is how "play next" and
    // "add to queue" mean "play".
    queue.playNext(QStringLiteral("ep9"));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(movedSpy.count(), 1);

    queue.setItems(episodes(4), 1);
    QCOMPARE(movedSpy.count(), 2);

    // Inserting on either side of the cursor re-indexes it without changing what
    // it points at.
    queue.playNext(QStringLiteral("ep-later"));
    queue.addToQueue(QStringLiteral("ep-last"));
    QCOMPARE(queue.current().id, QStringLiteral("ep2"));
    QCOMPARE(movedSpy.count(), 2);
    QCOMPARE(displacedSpy.count(), 0);
    QVERIFY(changedSpy.count() > 2); // hasNext/hasPrevious still restated

    // So does removing a row above it.
    queue.removeAt(0);
    QCOMPARE(queue.current().id, QStringLiteral("ep2"));
    QCOMPARE(movedSpy.count(), 2);
    QCOMPARE(displacedSpy.count(), 0);

    // Removing the row the cursor is on promotes another: a different item, but
    // not one the user asked to play.
    queue.removeAt(queue.currentIndex());
    QCOMPARE(queue.current().id, QStringLiteral("ep-later"));
    QCOMPARE(displacedSpy.count(), 1);
    QCOMPARE(movedSpy.count(), 2);

    // Deliberate cursor moves are the other kind.
    queue.jumpTo(2);
    QCOMPARE(movedSpy.count(), 3);
    queue.advance();
    QCOMPARE(movedSpy.count(), 4);
    queue.goBack();
    QCOMPARE(movedSpy.count(), 5);
    QCOMPARE(displacedSpy.count(), 1);

    // Shuffling and re-ordering keep the same item playing, so neither is one.
    queue.setShuffled(true);
    queue.setShuffled(false);
    queue.moveItem(0, queue.rowCount() - 1);
    QCOMPARE(movedSpy.count(), 5);
    QCOMPARE(displacedSpy.count(), 1);

    // An emptied queue has no new item to play: that is exhausted(), not a move.
    queue.clear();
    QCOMPARE(movedSpy.count(), 5);
    QCOMPARE(displacedSpy.count(), 1);
}

void PlayQueueTest::itemFromVariantRoundTripsAModelMap()
{
    MediaItem source = episode(4);
    source.primaryImageTag = QStringLiteral("tag-abc");
    source.playbackPositionTicks = 90 * kTicksPerSecond;
    source.favorite = true;

    MediaItemModel model;
    model.setItems({source});

    const MediaItem restored = PlayQueue::itemFromVariant(model.get(0));
    QCOMPARE(restored.id, source.id);
    QCOMPARE(restored.name, source.name);
    QCOMPARE(restored.type, source.type);
    QCOMPARE(restored.seriesName, source.seriesName);
    QCOMPARE(restored.indexNumber, source.indexNumber);
    QCOMPARE(restored.parentIndexNumber, source.parentIndexNumber);
    QCOMPARE(restored.runtimeMs(), source.runtimeMs());
    QCOMPARE(restored.positionMs(), source.positionMs());
    QCOMPARE(restored.favorite, true);
    QVERIFY(restored.isResumable());
    // The poster survives the trip through QML as a tag, not as a dead URL.
    QCOMPARE(restored.primaryImageTag, QStringLiteral("tag-abc"));

    // A bare id is a legal item, and anything else is simply not one.
    QCOMPARE(PlayQueue::itemFromVariant(QStringLiteral("ep9")).id, QStringLiteral("ep9"));
    QVERIFY(PlayQueue::itemFromVariant(QVariant()).id.isEmpty());

    // A TRACK's poster names its album, not itself, so the id has to survive
    // the trip as well as the tag: keeping only the tag would rebuild the
    // album's tag against the track's id and every queued track would go blank
    // the moment it passed through QML.
    MediaItem track;
    track.id = QStringLiteral("90210");
    track.name = QStringLiteral("Threnody");
    track.type = QStringLiteral("Audio");
    track.album = QStringLiteral("Lift Yr Skinny Fists");
    track.albumId = QStringLiteral("88001");
    track.artists = {QStringLiteral("Godspeed You! Black Emperor")};
    track.albumPrimaryImageTag = QStringLiteral("album-cover-tag");

    MediaItemModel music;
    music.setItems({track});
    const QVariantMap map = music.get(0);
    const MediaItem restoredTrack = PlayQueue::itemFromVariant(map);
    QCOMPARE(restoredTrack.coverSource().itemId, QStringLiteral("88001"));
    QCOMPARE(restoredTrack.coverSource().tag, QStringLiteral("album-cover-tag"));
    // And it is restored as the ALBUM's cover, not as some anonymous parent
    // image: the entry still knows which record it came from.
    QCOMPARE(restoredTrack.albumPrimaryImageTag, QStringLiteral("album-cover-tag"));
    QCOMPARE(restoredTrack.album, QStringLiteral("Lift Yr Skinny Fists"));
    QCOMPARE(restoredTrack.artists, QStringList{QStringLiteral("Godspeed You! Black Emperor")});

    // The whole point: the URL out is the URL back in, so a track can make the
    // round trip any number of times without drifting.
    MediaItemModel again;
    again.setItems({restoredTrack});
    QCOMPARE(again.get(0).value(QStringLiteral("posterUrl")).toString(),
             map.value(QStringLiteral("posterUrl")).toString());

    // A ClearLogo makes the same trip: an episode's is its series' (a parent
    // id), a film's its own, and the URL out is the URL back in, so the OSD can
    // still set the title as a logo after the entry passed through QML.
    MediaItem withSeriesLogo = episode(5);
    withSeriesLogo.parentLogoItemId = QStringLiteral("400001");
    withSeriesLogo.parentLogoImageTag = QStringLiteral("series-logo");
    MediaItem film;
    film.id = QStringLiteral("301001");
    film.type = QStringLiteral("Movie");
    film.logoImageTag = QStringLiteral("film-logo");
    MediaItemModel logos;
    logos.setItems({withSeriesLogo, film});
    const MediaItem restoredEpisode = PlayQueue::itemFromVariant(logos.get(0));
    QCOMPARE(restoredEpisode.logoSource().itemId, QStringLiteral("400001"));
    QCOMPARE(restoredEpisode.logoSource().tag, QStringLiteral("series-logo"));
    QVERIFY(restoredEpisode.logoImageTag.isEmpty());
    const MediaItem restoredFilm = PlayQueue::itemFromVariant(logos.get(1));
    QCOMPARE(restoredFilm.logoImageTag, QStringLiteral("film-logo"));
    MediaItemModel logosAgain;
    logosAgain.setItems({restoredEpisode, restoredFilm});
    for (int row = 0; row < 2; ++row)
        QCOMPARE(logosAgain.get(row).value(QStringLiteral("logoUrl")).toString(),
                 logos.get(row).value(QStringLiteral("logoUrl")).toString());
}

// An album or a playlist plays through Actions.playAllFrom() → playQueue(), i.e.
// through model maps rather than through DTOs, so a field that the model does not
// publish as a role is a field the queue entry cannot have. playCount was exactly
// that, which made MPRIS's xesam:useCount unreachable on the main music path.
void PlayQueueTest::playCountSurvivesTheRoundTrip()
{
    MediaItem track;
    track.id = QStringLiteral("t1");
    track.name = QStringLiteral("Storm");
    track.type = QStringLiteral("Audio");
    track.playCount = 12;

    MediaItemModel model;
    model.setItems({track});
    const QVariantMap map = model.get(0);
    QCOMPARE(map.value(QStringLiteral("playCount")).toInt(), 12);

    QCOMPARE(PlayQueue::itemFromVariant(map).playCount, 12);

    // Through the queue itself, which is how the real path reaches MPRIS.
    PlayQueue queue;
    queue.setItems({track});
    QCOMPARE(queue.current().playCount, 12);
    queue.setItems({PlayQueue::itemFromVariant(map)});
    QCOMPARE(queue.current().playCount, 12);

    // A map that never carried the key restores the documented default rather
    // than a sentinel: 0 is "never played, or nobody asked", and the MPRIS side
    // is what decides not to publish it.
    QVariantMap bare;
    bare.insert(QStringLiteral("itemId"), QStringLiteral("t2"));
    QCOMPARE(PlayQueue::itemFromVariant(bare).playCount, 0);
}

// "Up next, in context" (MUSIC.md §4): the pane says where a queue came from,
// and the answer is read off the queue rather than remembered from the verb
// that built it — so it cannot go stale when the queue is edited underneath it.
void PlayQueueTest::contextLabelNamesTheRecordTheQueueCameFrom()
{
    PlayQueue queue;
    QCOMPARE(queue.contextLabel(), QString());

    queue.setItems({track(QStringLiteral("Storm"), QStringLiteral("Lift Yr Skinny Fists"),
                          QStringLiteral("Godspeed You! Black Emperor")),
                    track(QStringLiteral("Static"), QStringLiteral("Lift Yr Skinny Fists"),
                          QStringLiteral("Godspeed You! Black Emperor"))});
    QCOMPARE(queue.contextLabel(), QStringLiteral("from Lift Yr Skinny Fists"));

    // One artist, several records — an artist queue names the artist.
    queue.setItems({track(QStringLiteral("Storm"), QStringLiteral("Lift Yr Skinny Fists"),
                          QStringLiteral("Godspeed You! Black Emperor")),
                    track(QStringLiteral("Dead Flag"), QStringLiteral("F#A#oo"),
                          QStringLiteral("Godspeed You! Black Emperor"))});
    QCOMPARE(queue.contextLabel(),
             QStringLiteral("from Godspeed You! Black Emperor"));

    // No single answer: say nothing rather than guess.
    queue.setItems({track(QStringLiteral("Storm"), QStringLiteral("Lift Yr Skinny Fists"),
                          QStringLiteral("Godspeed You! Black Emperor")),
                    track(QStringLiteral("Teardrop"), QStringLiteral("Mezzanine"),
                          QStringLiteral("Massive Attack"))});
    QCOMPARE(queue.contextLabel(), QString());

    // Adding a track from somewhere else takes the label away, which is the
    // whole reason it is derived: a remembered one would still say "from Lift
    // Yr Skinny Fists" over a queue that is no longer that record.
    queue.setItems({track(QStringLiteral("Storm"), QStringLiteral("Lift Yr Skinny Fists"),
                          QStringLiteral("Godspeed You! Black Emperor"))});
    QCOMPARE(queue.contextLabel(), QStringLiteral("from Lift Yr Skinny Fists"));
    queue.addToQueue(QVariantMap{{QStringLiteral("itemId"), QStringLiteral("x")},
                                 {QStringLiteral("type"), QStringLiteral("Audio")},
                                 {QStringLiteral("album"), QStringLiteral("Mezzanine")},
                                 {QStringLiteral("albumArtist"), QStringLiteral("Massive Attack")}});
    QCOMPARE(queue.contextLabel(), QString());

    // Not music: the label is a music surface and does not describe a season.
    queue.setItems(episodes(3));
    QCOMPARE(queue.contextLabel(), QString());
}

// A film interrupting a record has to be able to put the record back exactly as
// it was — which is more than the track and the position. A queue that came
// back unshuffled, or re-shuffled into a different future, would be a lie about
// what plays next, so the play order itself is part of the state.
void PlayQueueTest::snapshotRestoresOrderCursorAndModes()
{
    PlayQueue queue;
    queue.setItems(episodes(6), 0);
    queue.setShuffled(true);
    queue.setRepeatMode(PlayQueue::RepeatAll);
    queue.jumpTo(3);

    const QStringList playOrder = idsOf(queue);
    const QString currentId = queue.currentItem().value(QStringLiteral("itemId")).toString();
    const PlayQueue::Snapshot snapshot = queue.snapshot();
    QVERIFY(snapshot.isValid());

    // Something else takes the queue over entirely.
    queue.setItems({episode(99)}, 0);
    QCOMPARE(queue.rowCount(), 1);
    QVERIFY(!queue.shuffled());

    queue.restore(snapshot);
    QCOMPARE(idsOf(queue), playOrder);
    QCOMPARE(queue.currentIndex(), 3);
    QCOMPARE(queue.currentItem().value(QStringLiteral("itemId")).toString(), currentId);
    QVERIFY(queue.shuffled());
    QCOMPARE(queue.repeatMode(), PlayQueue::RepeatAll);

    // And the order it was given in came back too, so un-shuffling still walks
    // the episodes rather than freezing the shuffle in place.
    queue.setShuffled(false);
    QStringList inOrder;
    for (const MediaItem &item : episodes(6))
        inOrder.append(item.id);
    QCOMPARE(idsOf(queue), inOrder);
    QCOMPARE(queue.currentItem().value(QStringLiteral("itemId")).toString(), currentId);
}

void PlayQueueTest::batchAppendIsOneModelOperation()
{
    PlayQueue queue;
    QSignalSpy rowsInserted(&queue, &QAbstractItemModel::rowsInserted);
    QSignalSpy queueChanged(&queue, &PlayQueue::queueChanged);
    QSignalSpy currentItemChanged(&queue, &PlayQueue::currentItemChanged);

    QList<MediaItem> batch = episodes(100);
    MediaItem invalid;
    batch.insert(50, invalid);
    QCOMPARE(queue.addToQueue(batch), 100);

    QCOMPARE(queue.rowCount(), 100);
    QCOMPARE(queue.currentIndex(), 0);
    QCOMPARE(rowsInserted.count(), 1);
    QCOMPARE(rowsInserted.first().at(1).toInt(), 0);
    QCOMPARE(rowsInserted.first().at(2).toInt(), 99);
    QCOMPARE(queueChanged.count(), 1);
    QCOMPARE(currentItemChanged.count(), 1);

    QCOMPARE(queue.addToQueue(episodes(20)), 20);
    QCOMPARE(queue.rowCount(), 120);
    QCOMPARE(queue.currentIndex(), 0);
    QCOMPARE(rowsInserted.count(), 2);
    QCOMPARE(rowsInserted.last().at(1).toInt(), 100);
    QCOMPARE(rowsInserted.last().at(2).toInt(), 119);
    QCOMPARE(queueChanged.count(), 2);
    QCOMPARE(currentItemChanged.count(), 1);
}

void PlayQueueTest::largeDuplicateQueueSnapshotsByEntryIdentity()
{
    constexpr int kCount = 2'000;
    QList<MediaItem> duplicates;
    duplicates.reserve(kCount);
    QStringList originalNames;
    originalNames.reserve(kCount);
    for (int row = 0; row < kCount; ++row) {
        MediaItem item = episode(row + 1);
        item.id = QStringLiteral("same-item-id");
        item.name = QStringLiteral("Entry %1").arg(row);
        originalNames.append(item.name);
        duplicates.append(item);
    }

    PlayQueue queue;
    queue.setItems(duplicates, 700);
    queue.setShuffled(true);
    queue.jumpTo(1'234);
    const QString currentName = queue.current().name;
    const PlayQueue::Snapshot snapshot = queue.snapshot();

    queue.clear();
    queue.restore(snapshot);
    QCOMPARE(queue.rowCount(), kCount);
    QCOMPARE(queue.current().name, currentName);
    queue.setShuffled(false);

    QStringList restoredNames;
    restoredNames.reserve(kCount);
    for (int row = 0; row < queue.rowCount(); ++row)
        restoredNames.append(
            queue.itemAt(row).value(QStringLiteral("name")).toString());
    QCOMPARE(restoredNames, originalNames);
    QCOMPARE(queue.current().name, currentName);
}

// The header's warning about a remembered label outliving its queue: this
// asserts the label is dropped exactly when the queue stops being the one
// thing it named, and kept through everything that leaves that unchanged.
void PlayQueueTest::sourceLabelFollowsTheQueueItDescribes()
{
    PlayQueue queue;
    QSignalSpy changed(&queue, &PlayQueue::sourceLabelChanged);
    const auto record = [] {
        return QList<MediaItem>{track(QStringLiteral("a"), QStringLiteral("R"), QStringLiteral("X")),
                                track(QStringLiteral("b"), QStringLiteral("R"), QStringLiteral("X")),
                                track(QStringLiteral("c"), QStringLiteral("R"), QStringLiteral("X"))};
    };

    queue.setItems(record());
    queue.setSourceLabel(QStringLiteral("Station · Heavy rotation"));
    QCOMPARE(queue.sourceLabel(), QStringLiteral("Station · Heavy rotation"));
    QCOMPARE(changed.count(), 1);
    queue.setSourceLabel(QStringLiteral("Station · Heavy rotation"));
    QCOMPARE(changed.count(), 1); // no-op sets are silent

    queue.moveItem(2, 1);
    queue.removeAt(2);
    queue.setShuffled(true);
    QCOMPARE(queue.sourceLabel(), QStringLiteral("Station · Heavy rotation"));

    const PlayQueue::Snapshot snap = queue.snapshot();
    queue.setItems({episode(1)});
    QCOMPARE(queue.sourceLabel(), QString()); // a film/TV queue has no music provenance
    queue.restore(snap);
    QCOMPARE(queue.sourceLabel(), QStringLiteral("Station · Heavy rotation"));

    QCOMPARE(queue.addToQueue(QList<MediaItem>{track(QStringLiteral("z"), QStringLiteral("Q"), QStringLiteral("Y"))}), 1);
    QCOMPARE(queue.sourceLabel(), QString());

    queue.setSourceLabel(QStringLiteral("Sunburned Almanac"));
    QCOMPARE(queue.playNext(QList<MediaItem>{}), 0);
    QCOMPARE(queue.sourceLabel(), QStringLiteral("Sunburned Almanac")); // nothing added: kept
    queue.clear();
    QCOMPARE(queue.sourceLabel(), QString());
}

// A playlist may hold the same track twice. Enrichment is matched by item id,
// so both rows are that item and both get the record — and each stays the
// playlist ENTRY it was queued as, which the server's record of the item
// knows nothing about.
void PlayQueueTest::enrichEntryReachesEveryOccurrenceAndKeepsTheirEntryIds()
{
    const auto entry = [](const QString &id, const QString &entryId) {
        MediaItem item;
        item.id = id;
        item.name = QStringLiteral("seed");
        item.type = QStringLiteral("Audio");
        item.playlistItemId = entryId;
        return item;
    };
    PlayQueue queue;
    queue.setItems({entry(QStringLiteral("same"), QStringLiteral("entry-a")),
                    entry(QStringLiteral("other"), QStringLiteral("entry-b")),
                    entry(QStringLiteral("same"), QStringLiteral("entry-c"))},
                   2);

    QSignalSpy queueSpy(&queue, &PlayQueue::queueChanged);
    QSignalSpy cursorSpy(&queue, &PlayQueue::currentItemChanged);
    QSignalSpy dataSpy(&queue, &PlayQueue::dataChanged);

    MediaItem record;
    record.id = QStringLiteral("same");
    record.name = QStringLiteral("The whole record");
    record.type = QStringLiteral("Audio");
    record.album = QStringLiteral("Sunburned Almanac");
    queue.enrichEntry(record);

    for (const int row : {0, 2}) {
        QCOMPARE(queue.itemAt(row).value(QStringLiteral("name")).toString(),
                 QStringLiteral("The whole record"));
        QCOMPARE(queue.itemAt(row).value(QStringLiteral("album")).toString(),
                 QStringLiteral("Sunburned Almanac"));
    }
    QCOMPARE(queue.itemAt(0).value(QStringLiteral("playlistItemId")).toString(),
             QStringLiteral("entry-a"));
    QCOMPARE(queue.itemAt(2).value(QStringLiteral("playlistItemId")).toString(),
             QStringLiteral("entry-c"));
    QCOMPARE(queue.itemAt(1).value(QStringLiteral("name")).toString(), QStringLiteral("seed"));
    QCOMPARE(queue.itemAt(1).value(QStringLiteral("playlistItemId")).toString(),
             QStringLiteral("entry-b"));

    // One gesture: a row signal per occurrence, one announcement for the queue,
    // and one for the cursor because the current row was among them.
    QCOMPARE(dataSpy.count(), 2);
    QCOMPARE(queueSpy.count(), 1);
    QCOMPARE(cursorSpy.count(), 1);
    QCOMPARE(queue.currentIndex(), 2);

    // A record that does name an entry is believed.
    record.playlistItemId = QStringLiteral("entry-z");
    queue.enrichEntry(record);
    QCOMPARE(queue.itemAt(0).value(QStringLiteral("playlistItemId")).toString(),
             QStringLiteral("entry-z"));

    // No occurrence, no announcement.
    record.id = QStringLiteral("absent");
    queue.enrichEntry(record);
    QCOMPARE(queueSpy.count(), 2);
}

// The server's record for the item under the cursor arrives straight after the
// stop report, with the queue intact and nothing playing. currentItemChanged is
// what PlayerController turns into "play this", so a user-data patch must never
// raise it — nor either of the other cursor signals.
void PlayQueueTest::updateUserDataPatchesEveryOccurrenceAndNeverTheCursor()
{
    const auto entry = [](const QString &id, const QString &entryId) {
        MediaItem item;
        item.id = id;
        item.name = id;
        item.type = QStringLiteral("Audio");
        item.playlistItemId = entryId;
        item.runtimeTicks = 200 * kTicksPerSecond;
        return item;
    };
    PlayQueue queue;
    queue.setItems({entry(QStringLiteral("same"), QStringLiteral("entry-a")),
                    entry(QStringLiteral("other"), QStringLiteral("entry-b")),
                    entry(QStringLiteral("same"), QStringLiteral("entry-c"))},
                   2);

    QSignalSpy itemSpy(&queue, &PlayQueue::currentItemChanged);
    QSignalSpy displacedSpy(&queue, &PlayQueue::currentItemDisplaced);
    QSignalSpy cursorSpy(&queue, &PlayQueue::currentChanged);
    QSignalSpy queueSpy(&queue, &PlayQueue::queueChanged);
    QSignalSpy dataSpy(&queue, &PlayQueue::dataChanged);
    QSignalSpy resetSpy(&queue, &PlayQueue::modelReset);

    // The current row (2) holds the item, and so does row 0.
    QCOMPARE(queue.updateUserData(QStringLiteral("same"), true, true, 50 * kTicksPerSecond, 3), 2);

    QCOMPARE(itemSpy.count(), 0);
    QCOMPARE(displacedSpy.count(), 0);
    QCOMPARE(cursorSpy.count(), 0);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(queue.currentIndex(), 2);
    for (const int row : {0, 2}) {
        const QVariantMap map = queue.itemAt(row);
        QCOMPARE(map.value(QStringLiteral("played")).toBool(), true);
        QCOMPARE(map.value(QStringLiteral("favorite")).toBool(), true);
        QCOMPARE(map.value(QStringLiteral("positionMs")).toLongLong(), 50'000);
        QCOMPARE(map.value(QStringLiteral("playCount")).toInt(), 3);
    }
    // Still the entries they were.
    QCOMPARE(queue.itemAt(0).value(QStringLiteral("playlistItemId")).toString(),
             QStringLiteral("entry-a"));
    QCOMPARE(queue.itemAt(2).value(QStringLiteral("playlistItemId")).toString(),
             QStringLiteral("entry-c"));
    QCOMPARE(queue.itemAt(1).value(QStringLiteral("played")).toBool(), false);
    QCOMPARE(queue.itemAt(1).value(QStringLiteral("favorite")).toBool(), false);

    // One row announcement per entry, naming the user-data roles; one queue
    // announcement for the surfaces that read itemAt().
    QCOMPARE(dataSpy.count(), 2);
    QCOMPARE(dataSpy.at(0).at(0).toModelIndex().row(), 0);
    QCOMPARE(dataSpy.at(1).at(0).toModelIndex().row(), 2);
    const QList<int> roles = dataSpy.at(0).at(2).value<QList<int>>();
    QVERIFY(roles.contains(MediaItemModel::PlayedRole));
    QVERIFY(roles.contains(MediaItemModel::FavoriteRole));
    QVERIFY(roles.contains(MediaItemModel::PositionMsRole));
    QCOMPARE(queueSpy.count(), 1);
}

void PlayQueueTest::updateUserDataLeavesAbsentFieldsAndRepeatsAlone()
{
    MediaItem item;
    item.id = QStringLiteral("a");
    item.type = QStringLiteral("Audio");
    item.played = true;
    item.favorite = false;
    item.playbackPositionTicks = 7 * kTicksPerSecond;
    item.playCount = 4;
    PlayQueue queue;
    queue.setItems({item});

    QSignalSpy queueSpy(&queue, &PlayQueue::queueChanged);
    QSignalSpy dataSpy(&queue, &PlayQueue::dataChanged);
    QSignalSpy itemSpy(&queue, &PlayQueue::currentItemChanged);

    // Only the favourite is stated; nothing else may be invented.
    QCOMPARE(queue.updateUserData(QStringLiteral("a"), std::nullopt, true), 1);
    QCOMPARE(queue.current().played, true);
    QCOMPARE(queue.current().favorite, true);
    QCOMPARE(queue.current().playbackPositionTicks, 7 * kTicksPerSecond);
    QCOMPARE(queue.current().playCount, 4);
    QCOMPARE(queueSpy.count(), 1);
    QCOMPARE(dataSpy.count(), 1);

    // The same values again, an unknown id and an empty one: all silent.
    QCOMPARE(queue.updateUserData(QStringLiteral("a"), true, true, 7 * kTicksPerSecond, 4), 0);
    QCOMPARE(queue.updateUserData(QStringLiteral("nobody"), false, false), 0);
    QCOMPARE(queue.updateUserData(QString(), false, false), 0);
    QCOMPARE(queueSpy.count(), 1);
    QCOMPARE(dataSpy.count(), 1);
    QCOMPARE(itemSpy.count(), 0);
}

QTEST_GUILESS_MAIN(PlayQueueTest)
#include "tst_play_queue.moc"
