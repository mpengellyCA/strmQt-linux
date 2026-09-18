#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <array>
#include <optional>

#include "app/controllers/music/MusicLane.h"
#include "app/music/MusicQueryTranslator.h"
#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/ArtistGridModel.h"
#include "app/music/models/GenreBinModel.h"
#include "app/music/models/PlaylistGridModel.h"
#include "app/music/models/TrackListModel.h"
#include "core/Result.h"
#include "server/dto/music/MusicQuery.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class MusicPlayback;
class MusicRepository;

// Browse (Crate spec §5): one music library read five ways. It owns the whole
// query: filters are shared across sections, while sort and letter are
// remembered per section. Each section is an independent lane.
//
// QML states intent (a section, a filter, a letter). This class decides what to
// fetch, what to invalidate, and every string the page shows. Replies carry the
// lane generation, the session epoch and the query they asked for; anything
// that no longer matches is dropped.
class MusicBrowseController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString libraryId READ libraryId NOTIFY libraryChanged)
    Q_PROPERTY(QString section READ section WRITE setSection NOTIFY sectionChanged)

    Q_PROPERTY(strmqt::music::AlbumGridModel *albums READ albums CONSTANT)
    Q_PROPERTY(strmqt::music::ArtistGridModel *artists READ artists CONSTANT)
    Q_PROPERTY(strmqt::music::TrackListModel *songs READ songs CONSTANT)
    Q_PROPERTY(strmqt::music::GenreBinModel *genres READ genres CONSTANT)
    Q_PROPERTY(strmqt::music::PlaylistGridModel *playlists READ playlists CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *albumsLane READ albumsLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *artistsLane READ artistsLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *songsLane READ songsLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *genresLane READ genresLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *playlistsLane READ playlistsLane CONSTANT)
    Q_PROPERTY(strmqt::music::MusicLane *currentLane READ currentLane NOTIFY sectionChanged)

    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY queryChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY queryChanged)
    Q_PROPERTY(QVariantList availableSorts READ availableSorts NOTIFY sectionChanged)
    Q_PROPERTY(QString sortLabel READ sortLabel NOTIFY queryChanged)
    Q_PROPERTY(QString letter READ letter WRITE setLetter NOTIFY queryChanged)
    Q_PROPERTY(QStringList letters READ letters CONSTANT)
    Q_PROPERTY(bool letterStripVisible READ letterStripVisible NOTIFY queryChanged)

    Q_PROPERTY(QStringList genreIds READ genreIds NOTIFY queryChanged)
    Q_PROPERTY(QString genrePillText READ genrePillText NOTIFY queryChanged)
    Q_PROPERTY(int decade READ decade WRITE setDecade NOTIFY queryChanged)
    Q_PROPERTY(QString decadePillText READ decadePillText NOTIFY queryChanged)
    Q_PROPERTY(QString format READ format WRITE setFormat NOTIFY queryChanged)
    Q_PROPERTY(QString formatPillText READ formatPillText NOTIFY queryChanged)
    Q_PROPERTY(bool formatAvailable READ formatAvailable NOTIFY sectionChanged)
    Q_PROPERTY(bool decadeAvailable READ decadeAvailable NOTIFY sectionChanged)
    Q_PROPERTY(bool filtersAvailable READ filtersAvailable NOTIFY sectionChanged)
    Q_PROPERTY(bool favouritesOnly READ favouritesOnly WRITE setFavouritesOnly NOTIFY queryChanged)
    Q_PROPERTY(bool unplayedOnly READ unplayedOnly WRITE setUnplayedOnly NOTIFY queryChanged)
    Q_PROPERTY(QString artistMode READ artistMode WRITE setArtistMode NOTIFY queryChanged)
    Q_PROPERTY(bool filtered READ filtered NOTIFY queryChanged)
    Q_PROPERTY(int activeFilterCount READ activeFilterCount NOTIFY queryChanged)
    Q_PROPERTY(QString scopeLabel READ scopeLabel NOTIFY queryChanged)
    Q_PROPERTY(QString routeState READ routeState NOTIFY queryChanged)

    Q_PROPERTY(int resultCount READ resultCount NOTIFY countsChanged)
    Q_PROPERTY(int unfilteredCount READ unfilteredCount NOTIFY countsChanged)
    Q_PROPERTY(QString countText READ countText NOTIFY countsChanged)

    Q_PROPERTY(QVariantList genreOptions READ genreOptions NOTIFY genreOptionsChanged)
    Q_PROPERTY(bool genreOptionsLoading READ genreOptionsLoading NOTIFY genreOptionsChanged)
    Q_PROPERTY(bool genreOptionsFailed READ genreOptionsFailed NOTIFY genreOptionsChanged)
    Q_PROPERTY(QVariantList decadeOptions READ decadeOptions CONSTANT)
    Q_PROPERTY(QVariantList formatOptions READ formatOptions NOTIFY sectionChanged)

public:
    MusicBrowseController(MusicRepository *repository, MusicPlayback *playback, QObject *parent = nullptr);

    QString libraryId() const { return m_query.libraryId; }
    QString section() const;
    void setSection(const QString &section);

    AlbumGridModel *albums() const { return m_albums; }
    ArtistGridModel *artists() const { return m_artists; }
    TrackListModel *songs() const { return m_songs; }
    GenreBinModel *genres() const { return m_genres; }
    PlaylistGridModel *playlists() const { return m_playlists; }
    MusicLane *albumsLane() const { return m_lanes[0]; }
    MusicLane *artistsLane() const { return m_lanes[1]; }
    MusicLane *songsLane() const { return m_lanes[2]; }
    MusicLane *genresLane() const { return m_lanes[3]; }
    MusicLane *playlistsLane() const { return m_lanes[4]; }
    MusicLane *currentLane() const;
    QList<MusicModelBase *> models() const;

    QString sortKey() const;
    void setSortKey(const QString &key);
    bool sortDescending() const;
    void setSortDescending(bool descending);
    QVariantList availableSorts() const;
    QString sortLabel() const;
    QString letter() const;
    void setLetter(const QString &letter);
    QStringList letters() const;
    bool letterStripVisible() const;

    QStringList genreIds() const { return m_query.genreIds; }
    QString genrePillText() const;
    int decade() const { return m_query.decade; }
    void setDecade(int decade);
    QString decadePillText() const;
    QString format() const;
    void setFormat(const QString &key);
    QString formatPillText() const;
    bool formatAvailable() const;
    bool decadeAvailable() const;
    bool filtersAvailable() const;
    bool favouritesOnly() const { return m_query.favouritesOnly; }
    void setFavouritesOnly(bool on);
    bool unplayedOnly() const { return m_query.unplayedOnly; }
    void setUnplayedOnly(bool on);
    QString artistMode() const;
    void setArtistMode(const QString &mode);
    bool filtered() const;
    int activeFilterCount() const;
    QString scopeLabel() const;
    QString routeState() const;

    int resultCount() const;
    int unfilteredCount() const;
    QString countText() const;

    QVariantList genreOptions() const;
    bool genreOptionsLoading() const { return m_genreOptionsLoading; }
    bool genreOptionsFailed() const { return m_genreOptionsFailed; }
    QVariantList decadeOptions() const;
    QVariantList formatOptions() const;

    Q_INVOKABLE void open(const QString &libraryId, const QString &section);
    // Back/Forward: the section plus a routeState captured when the route was
    // left. Unreadable state keeps the live query.
    Q_INVOKABLE void restore(const QString &libraryId, const QString &section, const QString &state);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void setGenres(const QStringList &genreIds);
    Q_INVOKABLE void toggleGenre(const QString &genreId);
    Q_INVOKABLE void clearGenres();
    Q_INVOKABLE void clearFilters();
    Q_INVOKABLE void openGenre(const QString &genreId, const QString &genreName);
    Q_INVOKABLE void playFiltered();
    Q_INVOKABLE void shuffleFiltered();
    Q_INVOKABLE void toggleLetter(const QString &letter);
    Q_INVOKABLE bool jumpLetter(int step);
    Q_INVOKABLE QString cycleSection(int step);
    Q_INVOKABLE void ensureGenreOptions();
    Q_INVOKABLE void collectAlbumTracks(const QString &albumId, const QString &name);

    // A playlist made, renamed or deleted from any surface changes the set the
    // Playlists section lists, and PlaylistController — which refreshes only its
    // own list — cannot know about this one. Invalidate rather than refetch: on
    // screen the section reloads now, and from any other section the rows are
    // dropped so the next visit pays for one request instead of one per playlist
    // created while browsing albums.
    //
    // Not the lane's retry(): a fully loaded section has nothing left to page,
    // so retry() returns without asking for anything and the playlist the user
    // just made never appears (measured, Task 10).
    void notePlaylistsMutated();

    void resetSessionState();

signals:
    void libraryChanged();
    void sectionChanged();
    void queryChanged();
    void countsChanged();
    void genreOptionsChanged();
    void albumTracksCollected(const QString &subject, const QStringList &trackIds);
    void actionFailed(const QString &message);

private:
    struct SectionState
    {
        QString sortKey;
        bool descending = false;
        QString letter;
        bool stale = true;
        int resultCount = -1;
        int unfilteredCount = -1;
        quint64 countGeneration = 0;
        bool countPending = false;
    };

    struct Snapshot
    {
        std::array<MusicQuery, 5> queries;
        std::array<MusicQuery, 5> bases;
    };

    const SectionState &stateOf(Section section) const { return m_sections[static_cast<int>(section)]; }
    SectionState &stateOf(Section section) { return m_sections[static_cast<int>(section)]; }
    MusicQuery queryFor(Section section) const;
    MusicQuery baseFor(Section section) const;
    MusicQuery playScope() const;
    bool narrowed(Section section) const;
    Snapshot snapshot() const;
    void commit(const Snapshot &before);
    bool moveTo(const QString &sectionKey);
    void resetQuery(const QString &libraryId);
    void resetForLibrary(const QString &libraryId);
    void applyRouteState(const QString &state);
    void invalidate(Section section);
    void clearModel(Section section);
    void ensureVisible();
    void ensureCounts();
    void fetch(Section section, int startIndex);
    void fetchGenres(quint64 generation, quint64 epoch, const MusicQuery &query, int startIndex);
    template<class T, class Model>
    void acceptPage(Section section, quint64 generation, quint64 epoch, const MusicQuery &query,
                    int startIndex, const Result<Page<T>> &result, Model *model);
    void acceptCount(Section section, quint64 countGeneration, quint64 epoch, const QString &error, int total);
    void retry(Section section);
    void loadGenreOptions();
    void rememberGenreNames(const QList<GenreBin> &genres);
    std::optional<SortKey> sortFor(Section section, const QString &key) const;
    QString countNoun(Section section, int count) const;
    QString decadeLabel(int decade) const;
    QString formatLabel(FormatFilter format) const;

    MusicRepository *m_repository;
    MusicPlayback *m_playback;
    AlbumGridModel *m_albums;
    ArtistGridModel *m_artists;
    TrackListModel *m_songs;
    GenreBinModel *m_genres;
    PlaylistGridModel *m_playlists;
    std::array<MusicModelBase *, 5> m_models{};
    std::array<MusicLane *, 5> m_lanes{};

    MusicQuery m_query; // library, shared filters, artist mode; sort lives per section
    Section m_section = Section::Albums;
    std::array<SectionState, 5> m_sections;
    quint64 m_epoch = 0;
    quint64 m_collectGeneration = 0;

    QList<GenreBin> m_genreOptions;
    QHash<QString, QString> m_genreNames; // id → name, from options and openGenre
    bool m_genreOptionsLoaded = false;
    bool m_genreOptionsLoading = false;
    bool m_genreOptionsFailed = false;
};

} // namespace strmqt::music
