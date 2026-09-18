#pragma once

#include <QFuture>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <optional>

#include "app/controllers/music/MusicLane.h"
#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/ArtistGridModel.h"
#include "app/music/models/GenreBinModel.h"
#include "app/music/models/StationModel.h"
#include "core/Result.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class MusicPlayback;
class MusicRepository;

// Music Home (Crate spec §4): a hero and eight shelves, each an independent
// MusicLane over a typed model. Every shelf loads, fails and retries on its
// own; a reply for a library the user has already left is dropped by the
// lane's generation. refreshStale() refetches through the repository cache,
// so within the TTL it sends nothing.
class MusicHomeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString libraryId READ libraryId NOTIFY libraryIdChanged)
    Q_PROPERTY(QVariantMap hero READ hero NOTIFY heroChanged)
    Q_PROPERTY(strmqt::music::MusicLane *heroLane READ heroLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *recentLane READ recentLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *newLane READ newLane CONSTANT)
    Q_PROPERTY(QString addedThisWeekText READ addedThisWeekText NOTIFY shelfTextChanged)
    Q_PROPERTY(strmqt::music::MusicLane *stationLane READ stationLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *genreLane READ genreLane CONSTANT)
    Q_PROPERTY(QString allGenresText READ allGenresText NOTIFY shelfTextChanged)
    Q_PROPERTY(strmqt::music::MusicLane *artistLane READ artistLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *forgottenLane READ forgottenLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *pullLane READ pullLane CONSTANT)
    Q_PROPERTY(QStringList sectionKeys READ sectionKeys CONSTANT)

public:
    static constexpr int kShelfLimit = 20;
    static constexpr int kGenreBins = 10;
    static constexpr int kHeroRecent = 3;

    MusicHomeController(MusicRepository *repository, MusicPlayback *playback, QObject *parent = nullptr);

    QString libraryId() const { return m_libraryId; }
    QVariantMap hero() const { return m_hero; }
    MusicLane *heroLane() const { return m_heroLane; }
    MusicLane *recentLane() const { return m_recentLane; }
    MusicLane *newLane() const { return m_newLane; }
    QString addedThisWeekText() const { return m_addedThisWeekText; }
    MusicLane *stationLane() const { return m_stationLane; }
    MusicLane *genreLane() const { return m_genreLane; }
    QString allGenresText() const { return m_allGenresText; }
    MusicLane *artistLane() const { return m_artistLane; }
    MusicLane *forgottenLane() const { return m_forgottenLane; }
    MusicLane *pullLane() const { return m_pullLane; }

    QList<MusicModelBase *> models() const;
    static QStringList sectionKeys();

    Q_INVOKABLE void open(const QString &libraryId);
    Q_INVOKABLE void refreshStale();
    Q_INVOKABLE void resumeHero();
    Q_INVOKABLE void shuffleHero();
    Q_INVOKABLE void anotherOne();
    Q_INVOKABLE void reshuffle();
    Q_INVOKABLE void playStation(int row);
    Q_INVOKABLE void shuffleStation(int row);
    Q_INVOKABLE void queueStation(int row);
    Q_INVOKABLE QString cycleSection(int step) const;

    void resetSessionState();

signals:
    void libraryIdChanged();
    void heroChanged();
    void shelfTextChanged();

private:
    enum class Load { First, Refresh };

    template<class T>
    void track(MusicLane *lane, Load load, QFuture<Result<T>> future, std::function<void(T &&)> apply,
               std::function<void()> settled = {});

    void loadHero(Load load);
    void pullOneHero(quint64 generation);
    void loadRecent(Load load);
    void loadNew(Load load);
    void loadGenres(Load load);
    void loadArtists(Load load);
    void loadForgotten(Load load);
    void loadPull(Load load);
    void maybeBuildStations();
    void buildStations();
    void rebuildStations();

    void setHero(const Album &album, const QString &mode, int resumeIndex, double progress);
    void clearHero();
    void syncHeroFavourite();
    void fillHeroRecent();
    void setShelfText(QString &field, const QString &value);
    std::optional<Station> stationAt(int row) const;
    void clearAll();

    MusicRepository *m_repository;
    MusicPlayback *m_playback;

    AlbumGridModel *m_heroAlbum;
    AlbumGridModel *m_heroRecent;
    AlbumGridModel *m_recent;
    AlbumGridModel *m_new;
    StationModel *m_stations;
    GenreBinModel *m_genres;
    ArtistGridModel *m_artists;
    AlbumGridModel *m_forgotten;
    AlbumGridModel *m_pull;

    MusicLane *m_heroLane;
    MusicLane *m_recentLane;
    MusicLane *m_newLane;
    MusicLane *m_stationLane;
    MusicLane *m_genreLane;
    MusicLane *m_artistLane;
    MusicLane *m_forgottenLane;
    MusicLane *m_pullLane;

    QString m_libraryId;
    QVariantMap m_hero;
    QString m_addedThisWeekText;
    QString m_allGenresText;

    QList<Album> m_pool;
    Artist m_topArtist;
    QString m_stationTopId;
    quint64 m_stationGeneration = 0;
    quint64 m_heroPullGeneration = 0; // an "Another one" pull in flight, else 0
    bool m_poolSettled = false;
    bool m_artistsSettled = false;
    bool m_stationsBuilt = false;
};

} // namespace strmqt::music
