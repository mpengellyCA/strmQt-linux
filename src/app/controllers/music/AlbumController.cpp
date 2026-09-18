#include "app/controllers/music/AlbumController.h"

#include <QLocale>

#include <algorithm>

#include "app/models/MediaItemModel.h"
#include "app/music/MusicFormat.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"
#include "server/dto/music/MusicMediaItem.h"

namespace strmqt::music {

namespace {

const QString kDot = QStringLiteral(" · ");

} // namespace

AlbumController::AlbumController(MusicRepository *repository, MusicPlayback *playback,
                                 QObject *parent)
    : QObject(parent)
    , m_repository(repository)
    , m_playback(playback)
    , m_tracks(new TrackListModel(this))
    , m_moreBy(new AlbumGridModel(this))
{
}

QString AlbumController::artist() const
{
    return joinNames(m_album.albumArtists);
}

QString AlbumController::artistId() const
{
    return m_album.albumArtists.isEmpty() ? QString() : m_album.albumArtists.first().id;
}

QString AlbumController::kicker() const
{
    if (!m_loaded)
        return {};
    QStringList parts{releaseTypeName(m_album.releaseType)};
    if (m_album.year > 0)
        parts.append(QString::number(m_album.year));
    return parts.join(kDot);
}

QString AlbumController::coverUrl() const
{
    return music::coverUrl(m_album.coverRef);
}

bool AlbumController::isHiRes() const
{
    // formatBadge is the album's dominant badge (EmbyMusicMapper::dominantFormat,
    // a plurality over badge strings), and isHiRes colours that same badge. When
    // the badge encodes depth and rate (e.g. "FLAC 24/96") that is unambiguous.
    // But a lossless track missing BitDepth or SampleRate degrades its badge to
    // the bare codec label (EmbyMusicMapper::deriveAudioFormat), and a bare
    // label promises nothing about the rate: two tracks can both wear "FLAC"
    // while only one is hi-res. Picking any-one-or-first would let track order
    // decide whether the badge paints amber, so the rule is all-or-nothing:
    // every track carrying the summary badge must be hi-res for the badge to be.
    if (m_album.formatSummary.isEmpty())
        return false;
    bool sawMatch = false;
    for (const Track &track : m_tracks->items()) {
        if (track.format.badge != m_album.formatSummary)
            continue;
        sawMatch = true;
        if (!track.format.isHiRes)
            return false;
    }
    return sawMatch;
}

QString AlbumController::discBadge() const
{
    return m_discs.size() > 1 ? tr("%1 DISCS").arg(m_discs.size()) : QString();
}

QString AlbumController::trackSummary() const
{
    if (!m_loaded)
        return {};
    qint64 runtime = 0;
    for (const Track &track : m_tracks->items())
        runtime += track.runtimeMs;
    const QString length = formatRuntime(runtime);
    const QString count = formatTrackCount(static_cast<int>(m_tracks->items().size()));
    return length.isEmpty() ? count : count + kDot + length;
}

QVariantList AlbumController::discs() const
{
    QVariantList rows;
    if (m_discs.size() <= 1)
        return rows;
    int firstRow = 0;
    for (qsizetype i = 0; i < m_discs.size(); ++i) {
        const Disc &disc = m_discs.at(i);
        const QString title = i < 26
            ? tr("Side %1").arg(QChar(char16_t(u'A' + i)))
            : tr("Disc %1").arg(disc.number);
        QString detail = tr("Disc %1").arg(disc.number);
        const QString length = formatRuntime(disc.runtimeMs);
        if (!length.isEmpty())
            detail += kDot + length;
        rows.append(QVariantMap{{QStringLiteral("number"), disc.number},
                                {QStringLiteral("title"), title},
                                {QStringLiteral("detail"), detail},
                                {QStringLiteral("firstRow"), firstRow}});
        firstRow += static_cast<int>(disc.tracks.size());
    }
    return rows;
}

bool AlbumController::showArtistColumn() const
{
    const QList<Track> &tracks = m_tracks->items();
    return std::any_of(tracks.cbegin(), tracks.cend(),
                       [](const Track &track) { return track.differsFromAlbumArtist; });
}

QStringList AlbumController::trackIds() const
{
    QStringList ids;
    for (const Track &track : m_tracks->items())
        ids.append(track.id);
    return ids;
}

QVariantList AlbumController::linerNotes() const
{
    QVariantList rows;
    const auto add = [&rows](const QString &label, const QString &value,
                             const QVariantList &links = {}) {
        if (value.isEmpty())
            return;
        rows.append(QVariantMap{{QStringLiteral("label"), label},
                                {QStringLiteral("value"), value},
                                {QStringLiteral("links"), links}});
    };
    if (!m_loaded)
        return rows;

    if (m_album.premiereDate.isValid()) {
        add(tr("Released"),
            QLocale().toString(m_album.premiereDate.toUTC().date(), QStringLiteral("d MMMM yyyy")));
    } else if (m_album.year > 0) {
        add(tr("Released"), QString::number(m_album.year));
    }

    QStringList genreNames;
    QVariantList genreLinks;
    for (const GenreRef &genre : m_album.genres) {
        if (genre.name.isEmpty())
            continue;
        genreNames.append(genre.name);
        if (!genre.id.isEmpty()) {
            genreLinks.append(QVariantMap{{QStringLiteral("id"), genre.id},
                                          {QStringLiteral("name"), genre.name}});
        }
    }
    add(tr("Genre"), genreNames.join(kDot), genreLinks);
    add(tr("Label"), m_album.studios.join(kDot));
    add(tr("Format"), m_album.formatSummary);

    const QString plays = m_album.playCount > 0 ? tr("played %1×").arg(m_album.playCount) : QString();
    if (m_album.dateAdded.isValid()) {
        QString value = relativeAge(m_album.dateAdded, now());
        if (!plays.isEmpty())
            value += kDot + plays;
        add(tr("Added"), value);
    } else if (m_album.playCount > 0) {
        add(tr("Played"), tr("%1×").arg(m_album.playCount));
    }
    return rows;
}

QVariantMap AlbumController::albumItem() const
{
    if (m_album.id.isEmpty())
        return {};
    return MediaItemModel::mapForItem(toMediaItem(m_album));
}

QString AlbumController::moreByTitle() const
{
    const QString name = artist();
    return name.isEmpty() ? tr("More like this") : tr("More by %1").arg(name);
}

void AlbumController::open(const QString &albumId, const QString &name)
{
    if (albumId.isEmpty())
        return;
    if (albumId == m_album.id && (m_loading || (m_loaded && m_error.isEmpty())))
        return;
    if (albumId != m_album.id) {
        m_album = Album{};
        m_album.id = albumId;
        m_album.title = name;
        m_discs.clear();
        m_tracks->clear();
        m_moreBy->clear();
        m_loaded = false;
        emit albumChanged();
        emit favouriteChanged();
    }
    load();
}

void AlbumController::retry()
{
    if (!m_album.id.isEmpty())
        load();
}

void AlbumController::load()
{
    const quint64 generation = ++m_generation;
    m_loading = true;
    m_error.clear();
    emit stateChanged();
    m_repository->albumSleeve(m_album.id).then(this, [this, generation](const Result<AlbumSleeve> &result) {
        if (generation != m_generation)
            return;
        m_loading = false;
        if (!result.ok()) {
            m_error = result.error;
            emit stateChanged();
            return;
        }
        apply(result.value);
        emit stateChanged();
    });
}

void AlbumController::apply(const AlbumSleeve &sleeve)
{
    m_album = sleeve.album;
    m_discs = sleeve.discs;
    QList<Track> tracks;
    for (const Disc &disc : m_discs)
        tracks.append(disc.tracks);
    m_tracks->setItems(std::move(tracks));
    m_moreBy->setItems(sleeve.moreByArtist);
    m_loaded = true;
    emit albumChanged();
    emit favouriteChanged();
}

void AlbumController::play(int fromIndex)
{
    const QList<Track> &tracks = m_tracks->items();
    if (tracks.isEmpty())
        return;
    const int last = static_cast<int>(tracks.size()) - 1;
    // ItemActions::playAllFromIfCurrent clamps too; this clamp is deliberate
    // defence, because the index is into a list this controller owns. Keep it.
    m_playback->playTracks(tracks, std::clamp(fromIndex, 0, last), m_album.title);
}

void AlbumController::shuffle()
{
    if (!m_album.id.isEmpty())
        m_playback->shuffleAlbum(m_album.id, m_album.title);
}

void AlbumController::radio()
{
    if (!m_album.id.isEmpty())
        m_playback->radio(m_album.id, m_album.title);
}

void AlbumController::noteFavourite(const QString &itemId, bool favourite)
{
    if (itemId.isEmpty() || itemId != m_album.id || m_album.favourite == favourite)
        return;
    m_album.favourite = favourite;
    emit favouriteChanged();
    emit albumChanged(); // albumItem carries the flag for ItemMenu
}

void AlbumController::resetSessionState()
{
    ++m_generation;
    m_album = Album{};
    m_discs.clear();
    m_tracks->clear();
    m_moreBy->clear();
    m_loaded = false;
    m_loading = false;
    m_error.clear();
    emit albumChanged();
    emit favouriteChanged();
    emit stateChanged();
}

QDateTime AlbumController::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
}

QString AlbumController::relativeAge(const QDateTime &then, const QDateTime &now)
{
    const qint64 days = then.daysTo(now);
    if (days <= 0)
        return tr("today");
    if (days == 1)
        return tr("yesterday");
    if (days < 7)
        return tr("%1 days ago").arg(days);
    if (days < 30) {
        const qint64 weeks = days / 7;
        return weeks == 1 ? tr("1 week ago") : tr("%1 weeks ago").arg(weeks);
    }
    if (days < 365) {
        const qint64 months = days / 30;
        return months == 1 ? tr("1 month ago") : tr("%1 months ago").arg(months);
    }
    const qint64 years = days / 365;
    return years == 1 ? tr("1 year ago") : tr("%1 years ago").arg(years);
}

} // namespace strmqt::music
