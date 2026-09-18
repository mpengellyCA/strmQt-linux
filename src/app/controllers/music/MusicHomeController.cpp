#include "app/controllers/music/MusicHomeController.h"

#include <QLocale>

#include "app/music/MusicFormat.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"

namespace strmqt::music {

namespace {

// A refresh that answers the same ids in the same order leaves the model
// alone: a reset would drop the shelf's focus and scroll for no visible change.
template<class Model, class T>
void replaceItems(Model *model, QList<T> items, int total = -1)
{
    if (model->count() == items.size() && (total < 0 || total == model->totalRecordCount())) {
        bool same = true;
        for (int row = 0; row < items.size() && same; ++row)
            same = model->idAt(row) == items.at(row).id;
        if (same)
            return;
    }
    model->setItems(std::move(items), total);
}

} // namespace

MusicHomeController::MusicHomeController(MusicRepository *repository, MusicPlayback *playback,
                                         QObject *parent)
    : QObject(parent)
    , m_repository(repository)
    , m_playback(playback)
    , m_heroAlbum(new AlbumGridModel(this))
    , m_heroRecent(new AlbumGridModel(this))
    , m_recent(new AlbumGridModel(this))
    , m_new(new AlbumGridModel(this))
    , m_stations(new StationModel(this))
    , m_genres(new GenreBinModel(this))
    , m_artists(new ArtistGridModel(this))
    , m_forgotten(new AlbumGridModel(this))
    , m_pull(new AlbumGridModel(this))
    , m_heroLane(new MusicLane(m_heroRecent, this))
    , m_recentLane(new MusicLane(m_recent, this))
    , m_newLane(new MusicLane(m_new, this))
    , m_stationLane(new MusicLane(m_stations, this))
    , m_genreLane(new MusicLane(m_genres, this))
    , m_artistLane(new MusicLane(m_artists, this))
    , m_forgottenLane(new MusicLane(m_forgotten, this))
    , m_pullLane(new MusicLane(m_pull, this))
{
    connect(m_heroAlbum, &QAbstractItemModel::dataChanged, this, &MusicHomeController::syncHeroFavourite);

    const auto whenOpen = [this](void (MusicHomeController::*load)(Load)) {
        return [this, load] {
            if (!m_libraryId.isEmpty())
                (this->*load)(Load::First);
        };
    };
    connect(m_heroLane, &MusicLane::retryRequested, this, whenOpen(&MusicHomeController::loadHero));
    connect(m_recentLane, &MusicLane::retryRequested, this, whenOpen(&MusicHomeController::loadRecent));
    connect(m_newLane, &MusicLane::retryRequested, this, whenOpen(&MusicHomeController::loadNew));
    connect(m_genreLane, &MusicLane::retryRequested, this, whenOpen(&MusicHomeController::loadGenres));
    connect(m_artistLane, &MusicLane::retryRequested, this, whenOpen(&MusicHomeController::loadArtists));
    connect(m_forgottenLane, &MusicLane::retryRequested, this, whenOpen(&MusicHomeController::loadForgotten));
    connect(m_pullLane, &MusicLane::retryRequested, this, whenOpen(&MusicHomeController::loadPull));
    connect(m_stationLane, &MusicLane::retryRequested, this, [this] {
        if (m_poolSettled && m_artistsSettled)
            rebuildStations();
    });
}

QList<MusicModelBase *> MusicHomeController::models() const
{
    return {m_heroAlbum, m_heroRecent, m_recent, m_new, m_stations,
            m_genres,    m_artists,    m_forgotten, m_pull};
}

QStringList MusicHomeController::sectionKeys()
{
    return {QStringLiteral("home"),  QStringLiteral("albums"), QStringLiteral("artists"),
            QStringLiteral("songs"), QStringLiteral("genres"), QStringLiteral("playlists")};
}

template<class T>
void MusicHomeController::track(MusicLane *lane, Load load, QFuture<Result<T>> future,
                                std::function<void(T &&)> apply, std::function<void()> settled)
{
    const quint64 generation = load == Load::Refresh ? lane->beginRefresh() : lane->begin();
    future.then(this, [lane, generation, apply = std::move(apply),
                       settled = std::move(settled)](Result<T> result) {
        if (!lane->isCurrent(generation))
            return;
        if (result.ok()) {
            apply(std::move(result.value));
            lane->succeed(generation);
        } else {
            lane->fail(generation, result.error);
        }
        if (settled)
            settled();
    });
}

void MusicHomeController::open(const QString &libraryId)
{
    if (libraryId.isEmpty())
        return;
    if (libraryId == m_libraryId) {
        refreshStale();
        return;
    }
    clearAll();
    m_libraryId = libraryId;
    emit libraryIdChanged();

    m_stationGeneration = m_stationLane->begin();
    loadHero(Load::First);
    loadRecent(Load::First);
    loadNew(Load::First);
    loadGenres(Load::First);
    loadArtists(Load::First);
    loadForgotten(Load::First);
    loadPull(Load::First);
}

void MusicHomeController::refreshStale()
{
    if (m_libraryId.isEmpty())
        return;
    // Pull one out is random by design and the stations follow it, so neither
    // is refreshed behind the user's back. A lane still on its first load is
    // skipped: the page refreshes on activation in the same turn as open(), and
    // refetching then would only drop the replies already on their way.
    const auto refresh = [this](MusicLane *lane, void (MusicHomeController::*load)(Load)) {
        if (!lane->loading())
            (this->*load)(Load::Refresh);
    };
    // An "Another one" pull in flight is quiet (loading stays false), but a
    // refresh would supersede it and drop the record the user asked for.
    if (m_heroPullGeneration == 0 || !m_heroLane->isCurrent(m_heroPullGeneration))
        refresh(m_heroLane, &MusicHomeController::loadHero);
    refresh(m_recentLane, &MusicHomeController::loadRecent);
    refresh(m_newLane, &MusicHomeController::loadNew);
    refresh(m_genreLane, &MusicHomeController::loadGenres);
    refresh(m_artistLane, &MusicHomeController::loadArtists);
    refresh(m_forgottenLane, &MusicHomeController::loadForgotten);
}

void MusicHomeController::loadHero(Load load)
{
    const quint64 generation = load == Load::Refresh ? m_heroLane->beginRefresh() : m_heroLane->begin();
    m_repository->continueListening(m_libraryId).then(this, [this, generation](Result<ContinueListening> result) {
        if (!m_heroLane->isCurrent(generation))
            return;
        if (!result.ok()) {
            m_heroLane->fail(generation, result.error);
            return;
        }
        if (result.value.isValid()) {
            const ContinueListening &resume = result.value;
            setHero(resume.album, QStringLiteral("resume"), resume.resumeIndex, resume.progress);
            m_heroLane->succeed(generation);
            return;
        }
        if (m_hero.value(QStringLiteral("mode")).toString() == QLatin1String("pullOne")) {
            // Still no history: keep the record already on screen.
            m_heroLane->succeed(generation);
            return;
        }
        pullOneHero(generation);
    });
}

void MusicHomeController::pullOneHero(quint64 generation)
{
    m_repository->randomAlbums(m_libraryId, 1).then(this, [this, generation](Result<QList<Album>> result) {
        if (!m_heroLane->isCurrent(generation))
            return;
        m_heroPullGeneration = 0;
        if (!result.ok()) {
            m_heroLane->fail(generation, result.error);
            return;
        }
        if (result.value.isEmpty())
            clearHero();
        else
            setHero(result.value.first(), QStringLiteral("pullOne"), 0, 0.0);
        m_heroLane->succeed(generation);
    });
}

void MusicHomeController::loadRecent(Load load)
{
    track<QList<Album>>(m_recentLane, load, m_repository->recentAlbums(m_libraryId, kShelfLimit),
                        [this](QList<Album> &&albums) {
                            replaceItems(m_recent, std::move(albums));
                            fillHeroRecent();
                        });
}

void MusicHomeController::loadNew(Load load)
{
    track<NewAlbums>(m_newLane, load, m_repository->newAlbums(m_libraryId, kShelfLimit),
                     [this](NewAlbums &&result) {
                         replaceItems(m_new, std::move(result.albums));
                         setShelfText(m_addedThisWeekText,
                                      result.addedThisWeek > 0
                                          ? tr("%n added this week", nullptr, result.addedThisWeek)
                                          : QString());
                     });
}

void MusicHomeController::loadGenres(Load load)
{
    track<Page<GenreBin>>(m_genreLane, load, m_repository->genreBins(m_libraryId, kGenreBins),
                          [this](Page<GenreBin> &&page) {
                              QList<GenreBin> bins = std::move(page.items);
                              QString allText;
                              if (!bins.isEmpty()) {
                                  allText = tr("All %1 genres").arg(QLocale().toString(page.totalRecordCount));
                                  // The last bin opens Genres. An empty id is how
                                  // the page tells it from a real genre.
                                  GenreBin all;
                                  all.name = allText;
                                  const QList<GenreBin> firstThree = bins.first(qMin(qsizetype(3), bins.size()));
                                  for (const GenreBin &bin : firstThree) {
                                      if (!bin.covers.isEmpty())
                                          all.covers.append(bin.covers.first());
                                  }
                                  bins.append(all);
                              }
                              setShelfText(m_allGenresText, allText);
                              replaceItems(m_genres, std::move(bins));
                          });
}

void MusicHomeController::loadArtists(Load load)
{
    track<QList<Artist>>(
        m_artistLane, load, m_repository->topArtists(m_libraryId, kShelfLimit),
        [this](QList<Artist> &&artists) {
            m_topArtist = artists.isEmpty() ? Artist{} : artists.first();
            replaceItems(m_artists, std::move(artists));
            if (m_stationsBuilt && m_topArtist.id != m_stationTopId)
                rebuildStations();
        },
        [this] {
            m_artistsSettled = true;
            maybeBuildStations();
        });
}

void MusicHomeController::loadForgotten(Load load)
{
    track<QList<Album>>(m_forgottenLane, load, m_repository->forgottenFavourites(m_libraryId, kShelfLimit),
                        [this](QList<Album> &&albums) { replaceItems(m_forgotten, std::move(albums)); });
}

void MusicHomeController::loadPull(Load load)
{
    track<QList<Album>>(
        m_pullLane, load, m_repository->randomAlbums(m_libraryId, kShelfLimit),
        [this](QList<Album> &&albums) {
            m_pool = albums;
            replaceItems(m_pull, std::move(albums));
        },
        [this] {
            m_poolSettled = true;
            maybeBuildStations();
        });
}

void MusicHomeController::maybeBuildStations()
{
    if (!m_stationsBuilt && m_poolSettled && m_artistsSettled)
        buildStations();
}

void MusicHomeController::buildStations()
{
    m_stations->setStations(MusicRepository::stations(m_pool, m_topArtist));
    m_stationTopId = m_topArtist.id;
    m_stationsBuilt = true;
    m_stationLane->succeed(m_stationGeneration);
}

void MusicHomeController::rebuildStations()
{
    m_stationGeneration = m_stationLane->beginRefresh();
    buildStations();
}

void MusicHomeController::setHero(const Album &album, const QString &mode, int resumeIndex, double progress)
{
    m_heroAlbum->setItems({album});

    const QString artist = joinNames(album.albumArtists);
    QStringList parts;
    if (!artist.isEmpty())
        parts.append(artist);
    if (album.year > 0)
        parts.append(QString::number(album.year));
    if (album.trackCount > 0)
        parts.append(formatTrackCount(album.trackCount));
    const QString runtime = formatRuntime(album.runtimeMs);
    if (!runtime.isEmpty())
        parts.append(runtime);

    const bool resume = mode == QLatin1String("resume");
    QVariantMap hero;
    hero.insert(QStringLiteral("mode"), mode);
    hero.insert(QStringLiteral("albumId"), album.id);
    hero.insert(QStringLiteral("title"), album.title);
    hero.insert(QStringLiteral("artist"), artist);
    hero.insert(QStringLiteral("artistId"), album.albumArtists.isEmpty() ? QString() : album.albumArtists.first().id);
    hero.insert(QStringLiteral("year"), album.year);
    hero.insert(QStringLiteral("summary"), parts.join(QStringLiteral(" · ")));
    hero.insert(QStringLiteral("coverUrl"), coverUrl(album.coverRef));
    hero.insert(QStringLiteral("progress"), progress);
    hero.insert(QStringLiteral("resumeIndex"), resumeIndex);
    hero.insert(QStringLiteral("resumeLabel"), resume ? tr("Resume track %1").arg(resumeIndex + 1) : tr("Play"));
    hero.insert(QStringLiteral("favourite"), album.favourite);
    hero.insert(QStringLiteral("albumItem"), m_heroAlbum->get(0));
    if (hero != m_hero) {
        m_hero = hero;
        emit heroChanged();
    }
    fillHeroRecent();
}

void MusicHomeController::clearHero()
{
    m_heroAlbum->clear();
    if (!m_hero.isEmpty()) {
        m_hero.clear();
        emit heroChanged();
    }
}

void MusicHomeController::syncHeroFavourite()
{
    if (m_hero.isEmpty() || m_heroAlbum->count() == 0)
        return;
    const QVariantMap item = m_heroAlbum->get(0);
    const bool favourite = item.value(QStringLiteral("favourite")).toBool();
    if (m_hero.value(QStringLiteral("favourite")).toBool() == favourite)
        return;
    m_hero.insert(QStringLiteral("favourite"), favourite);
    m_hero.insert(QStringLiteral("albumItem"), item);
    emit heroChanged();
}

void MusicHomeController::fillHeroRecent()
{
    const QString heroId = m_hero.value(QStringLiteral("albumId")).toString();
    QList<Album> beside;
    for (const Album &album : m_recent->items()) {
        if (album.id == heroId)
            continue;
        beside.append(album);
        if (beside.size() == kHeroRecent)
            break;
    }
    replaceItems(m_heroRecent, std::move(beside));
}

void MusicHomeController::setShelfText(QString &field, const QString &value)
{
    if (field == value)
        return;
    field = value;
    emit shelfTextChanged();
}

std::optional<Station> MusicHomeController::stationAt(int row) const
{
    const QString key = m_stations->idAt(row);
    return key.isEmpty() ? std::nullopt : m_stations->stationFor(key);
}

void MusicHomeController::resumeHero()
{
    const QString albumId = m_hero.value(QStringLiteral("albumId")).toString();
    if (albumId.isEmpty())
        return;
    m_playback->playAlbum(albumId, m_hero.value(QStringLiteral("title")).toString(),
                          m_hero.value(QStringLiteral("resumeIndex")).toInt());
}

void MusicHomeController::shuffleHero()
{
    const QString albumId = m_hero.value(QStringLiteral("albumId")).toString();
    if (!albumId.isEmpty())
        m_playback->shuffleAlbum(albumId, m_hero.value(QStringLiteral("title")).toString());
}

void MusicHomeController::anotherOne()
{
    if (m_libraryId.isEmpty())
        return;
    m_heroPullGeneration = m_heroLane->beginRefresh();
    pullOneHero(m_heroPullGeneration);
}

void MusicHomeController::reshuffle()
{
    if (!m_libraryId.isEmpty())
        loadPull(Load::Refresh);
}

void MusicHomeController::playStation(int row)
{
    if (const auto station = stationAt(row))
        m_playback->playStation(m_libraryId, *station);
}

void MusicHomeController::shuffleStation(int row)
{
    if (const auto station = stationAt(row))
        m_playback->shuffleStation(m_libraryId, *station);
}

void MusicHomeController::queueStation(int row)
{
    if (const auto station = stationAt(row))
        m_playback->queueStation(m_libraryId, *station);
}

QString MusicHomeController::cycleSection(int step) const
{
    const QStringList keys = sectionKeys();
    const int count = static_cast<int>(keys.size());
    return keys.at(((step % count) + count) % count); // Home is index 0
}

void MusicHomeController::clearAll()
{
    for (MusicLane *lane : {m_heroLane, m_recentLane, m_newLane, m_stationLane, m_genreLane, m_artistLane,
                            m_forgottenLane, m_pullLane}) {
        lane->reset();
    }
    for (AlbumGridModel *albums : {m_heroRecent, m_recent, m_new, m_forgotten, m_pull})
        albums->clear();
    m_stations->setStations({});
    m_artists->clear();
    m_genres->clear();
    clearHero();
    setShelfText(m_addedThisWeekText, QString());
    setShelfText(m_allGenresText, QString());
    m_pool.clear();
    m_topArtist = Artist{};
    m_stationTopId.clear();
    m_heroPullGeneration = 0;
    m_poolSettled = false;
    m_artistsSettled = false;
    m_stationsBuilt = false;
}

void MusicHomeController::resetSessionState()
{
    clearAll();
    if (!m_libraryId.isEmpty()) {
        m_libraryId.clear();
        emit libraryIdChanged();
    }
}

} // namespace strmqt::music
