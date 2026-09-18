#pragma once

#include <QFuture>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include "core/Result.h"
#include "server/dto/music/MusicQuery.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt {
class ItemActions;
}

namespace strmqt::music {

class MusicRepository;

// Every music play verb in one place (exposed to QML as MusicPlay). Resolves
// tracks through the repository, converts them with toMediaItem and hands them
// to ItemActions with a source label, so ItemActions and PlayQueue never learn
// about music DTOs (Crate spec §3.6.1–2).
class MusicPlayback : public QObject
{
    Q_OBJECT

public:
    MusicPlayback(MusicRepository *repository, ItemActions *actions, QObject *parent = nullptr);

    void playTracks(const QList<Track> &tracks, int startIndex, const QString &sourceLabel);
    Q_INVOKABLE void playAlbum(const QString &albumId, const QString &title, int startIndex = 0);
    Q_INVOKABLE void shuffleAlbum(const QString &albumId, const QString &title);
    Q_INVOKABLE void radio(const QString &seedId, const QString &seedName);
    void playStation(const QString &libraryId, const Station &station);
    void shuffleStation(const QString &libraryId, const Station &station);
    // Appends without replacing what plays, so it reserves no intent.
    void queueStation(const QString &libraryId, const Station &station);
    Q_INVOKABLE void playStationTile(const QString &libraryId, const QVariantMap &tile);
    void shuffleQuery(const MusicQuery &query, const QString &label);
    // ▶ Play on a filtered view: the Songs scope in its own order (spec §5.1).
    void playQuery(const MusicQuery &query, const QString &label, int limit = 500);

    static QVariantList toMaps(const QList<Track> &tracks);

private:
    enum class Order { AsGiven, Shuffled };

    void playResolved(QFuture<Result<QList<Track>>> future, quint64 generation, int startIndex,
                      Order order, const QString &sourceLabel);
    void reportFailure(const QString &error);

    MusicRepository *m_repository;
    ItemActions *m_actions;
};

} // namespace strmqt::music
