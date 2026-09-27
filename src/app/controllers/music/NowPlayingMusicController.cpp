#include "app/controllers/music/NowPlayingMusicController.h"

#include <algorithm>
#include <iterator>

#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "app/music/MusicFormat.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"
#include "app/music/models/TrackListModel.h"
#include "core/Log.h"
#include "server/emby/EmbyMusicMapper.h"
#include "server/emby/MusicServerCapabilities.h"

namespace strmqt::music {

namespace {

bool isLyricsCodec(const QString &codec)
{
    const QString lower = codec.toLower();
    return lower == QLatin1String("lrc") || lower == QLatin1String("txt")
           || lower == QLatin1String("text");
}

} // namespace

NowPlayingMusicController::NowPlayingMusicController(MusicRepository *repository,
                                                     MusicPlayback *playback, QObject *parent)
    : QObject(parent),
      m_repository(repository),
      m_playback(playback),
      m_tracks(new TrackListModel(this)),
      m_lyricsEnabled(emby::caps::kLyricsAvailable)
{
}

void NowPlayingMusicController::bind(PlayerController *player, ItemActions *actions)
{
    Q_ASSERT(player && actions && !m_player);
    m_player = player;
    m_actions = actions;

    PlayQueue *queue = player->queue();
    connect(queue, &PlayQueue::currentChanged, this, &NowPlayingMusicController::refreshTrack);
    connect(queue, &PlayQueue::currentItemChanged, this, &NowPlayingMusicController::refreshTrack);
    connect(queue, &PlayQueue::queueChanged, this, &NowPlayingMusicController::refreshTrack);
    connect(queue, &PlayQueue::sourceLabelChanged, this, &NowPlayingMusicController::refreshSourceLabel);

    connect(player, &PlayerController::activeChanged, this, &NowPlayingMusicController::refreshRecordState);
    connect(player, &PlayerController::pausedChanged, this, &NowPlayingMusicController::refreshRecordState);
    connect(player, &PlayerController::busyChanged, this, &NowPlayingMusicController::refreshRecordState);
    connect(player, &PlayerController::bufferingChanged, this, &NowPlayingMusicController::refreshRecordState);
    connect(player, &PlayerController::activeChanged, this, &NowPlayingMusicController::refreshTime);
    // Forced for every new ticket, so it also marks "this track's streams are in".
    // durationMs() falls back to the ticket's RunTimeTicks until the backend
    // reports its own duration (PlayerController::durationChanged fires only
    // for the latter), so the fallback needs its own refresh here too — the
    // ticket resolves strictly after activeChanged, and nothing else is
    // guaranteed to touch positionChanged/durationChanged before playback
    // actually starts moving.
    connect(player, &PlayerController::sourceIndexChanged, this, [this] {
        refreshReadout();
        refreshTime();
        loadLyrics();
    });
    connect(player, &PlayerController::streamMethodChanged, this, &NowPlayingMusicController::refreshReadout);
    connect(player, &PlayerController::positionChanged, this, [this] {
        refreshTime();
        refreshCurrentLyricRow();
    });
    connect(player, &PlayerController::durationChanged, this, &NowPlayingMusicController::refreshTime);

    connect(actions, &ItemActions::favoriteChanged, this, [this](const QString &itemId, bool favourite) {
        m_favouriteOverrides.insert(itemId, favourite);
        if (itemId == m_trackId)
            emit favouriteChanged();
    });

    refreshTrack();
}

bool NowPlayingMusicController::favourite() const
{
    return m_active && m_favouriteOverrides.value(m_trackId, m_itemFavourite);
}

QObject *NowPlayingMusicController::albumTracks() const
{
    return m_tracks;
}

void NowPlayingMusicController::refreshTrack()
{
    const PlayQueue *queue = m_player ? m_player->queue() : nullptr;
    const MediaItem item = queue ? queue->current() : MediaItem{};
    const bool active = !item.id.isEmpty() && item.type == QLatin1String("Audio");

    const QString previousTrackId = m_trackId;
    const QString previousAlbumId = m_albumId;
    const bool wasActive = m_active;
    const QVariantMap previousItem = m_trackItem;

    m_active = active;
    m_trackId = active ? item.id : QString();
    m_trackItem = active ? queue->currentItem() : QVariantMap{};
    m_title = active ? item.name : QString();
    if (active && !item.artists.isEmpty()) {
        m_artist = item.artists.join(QStringLiteral(", "));
        m_artistId = item.artistIds.value(0);
    } else {
        m_artist = active ? item.albumArtist : QString();
        m_artistId.clear();
    }
    m_album = active ? item.album : QString();
    m_albumId = active ? item.albumId : QString();
    QString cover = active ? music::coverUrl(item.coverSource()) : QString();
    if (active && cover.isEmpty())
        cover = m_trackItem.value(QStringLiteral("posterUrl")).toString();
    m_coverUrl = cover;
    // A newer value from the server (a live patch on the queue item) replaces
    // whatever this session last toggled.
    if (active && item.favorite != m_itemFavourite && m_trackId == previousTrackId)
        m_favouriteOverrides.remove(m_trackId);
    m_itemFavourite = active && item.favorite;

    const bool trackMoved = m_trackId != previousTrackId || m_active != wasActive;
    if (trackMoved) {
        if (!previousAlbumId.isEmpty() && !m_albumId.isEmpty() && previousAlbumId != m_albumId)
            emit albumChanging();
        clearLyrics();
    }
    if (trackMoved || m_trackItem != previousItem) {
        emit trackChanged();
        emit favouriteChanged();
    }

    if (m_albumId != m_tracksAlbumId)
        loadAlbumTracks(m_albumId);
    else
        refreshCurrentAlbumRow();
    refreshSourceLabel();
    refreshRecordState();
    refreshReadout();
    refreshTime();
    // Deliberately NOT loadLyrics() here: PlayQueue's currentChanged/
    // currentItemChanged fire on advance() before PlayerController::
    // startQueueCurrent() resets the ticket (its comment says the ticket is
    // still the OUTGOING item's at that point), so m_trackId would already be
    // the new track while currentSource()/subtitleStreams() still answer for
    // the old one — a lyrics request for the new track keyed by the old
    // track's source/stream index. sourceIndexChanged is what tells the truth:
    // PlayerController emits it once immediately when the ticket is reset (so
    // loadLyrics() sees no source and clears) and again once the new ticket
    // actually resolves (so it sees the right source and stream index).
}

void NowPlayingMusicController::refreshSourceLabel()
{
    const PlayQueue *queue = m_player ? m_player->queue() : nullptr;
    QString label;
    QString kicker;
    if (queue && m_active) {
        if (!queue->sourceLabel().isEmpty()) {
            label = queue->sourceLabel();
            kicker = tr("Playing from · %1").arg(label);
        } else if (!queue->contextLabel().isEmpty()) {
            // contextLabel is already the translated "from %1".
            label = queue->contextLabel();
            kicker = tr("Playing %1").arg(label);
        }
    }
    if (label == m_sourceLabel && kicker == m_sourceKicker)
        return;
    m_sourceLabel = label;
    m_sourceKicker = kicker;
    emit sourceLabelChanged();
}

void NowPlayingMusicController::refreshRecordState()
{
    QString state = QStringLiteral("stopped");
    if (m_active && m_player && m_player->active()) {
        if (m_player->busy() || m_player->buffering())
            state = QStringLiteral("buffering");
        else if (m_player->paused())
            state = QStringLiteral("paused");
        else
            state = QStringLiteral("playing");
    }
    if (state == m_recordState)
        return;
    m_recordState = state;
    emit recordStateChanged();
}

void NowPlayingMusicController::refreshReadout()
{
    QStringList parts;
    if (m_active && m_player) {
        const QVariantList streams = m_player->audioStreams();
        if (!streams.isEmpty()) {
            const QVariantMap stream = streams.constFirst().toMap();
            const AudioFormat format = emby::deriveAudioFormat(
                stream.value(QStringLiteral("codec")).toString(),
                stream.value(QStringLiteral("bitDepth")).toInt(),
                stream.value(QStringLiteral("sampleRate")).toInt(),
                static_cast<int>(stream.value(QStringLiteral("bitRate")).toLongLong()),
                stream.value(QStringLiteral("channels")).toInt());
            if (!format.badge.isEmpty())
                parts.append(format.badge);
        }
        const QString method = m_player->streamMethod();
        if (method == QLatin1String("DirectPlay"))
            parts.append(tr("DIRECT PLAY"));
        else if (method == QLatin1String("DirectStream"))
            parts.append(tr("DIRECT STREAM"));
        else if (method == QLatin1String("Transcode"))
            parts.append(tr("TRANSCODE"));
    }
    const QString readout = parts.join(QStringLiteral(" · "));
    if (readout == m_readout)
        return;
    m_readout = readout;
    emit readoutChanged();
}

void NowPlayingMusicController::refreshTime()
{
    QString text;
    if (m_active && m_player && m_player->active()) {
        const QString elapsed = formatDuration(m_player->positionMs());
        const QString total = formatDuration(m_player->durationMs());
        text = (elapsed.isEmpty() ? QStringLiteral("0:00") : elapsed) + QStringLiteral(" / ")
               + (total.isEmpty() ? QStringLiteral("--:--") : total);
    }
    if (text == m_timeText)
        return;
    m_timeText = text;
    emit timeTextChanged();
}

void NowPlayingMusicController::refreshCurrentAlbumRow()
{
    const int row = m_trackId.isEmpty()
                        ? -1
                        : m_tracks->indexOfNavigationIdentity(QStringLiteral("i:") + m_trackId);
    if (row == m_currentAlbumRow)
        return;
    m_currentAlbumRow = row;
    emit currentAlbumRowChanged();
}

void NowPlayingMusicController::setAlbumSummary(const QString &summary)
{
    if (summary == m_albumSummary)
        return;
    m_albumSummary = summary;
    emit albumSummaryChanged();
}

void NowPlayingMusicController::loadAlbumTracks(const QString &albumId)
{
    m_tracksAlbumId = albumId;
    const quint64 generation = ++m_albumGeneration;
    if (albumId.isEmpty()) {
        m_tracks->clear();
        setAlbumSummary(QString());
        refreshCurrentAlbumRow();
        return;
    }
    m_repository->albumTracks(albumId).then(this, [this, generation](Result<QList<Track>> result) {
        if (generation != m_albumGeneration)
            return;
        if (!result.ok()) {
            qCWarning(logApp) << "Now playing: album tracks unavailable:" << result.error;
            m_tracks->clear();
            setAlbumSummary(QString());
            refreshCurrentAlbumRow();
            m_tracksAlbumId.clear(); // the next track of this album tries again
            return;
        }
        qint64 runtimeMs = 0;
        for (const Track &track : std::as_const(result.value))
            runtimeMs += track.runtimeMs;
        const QString count = formatTrackCount(static_cast<int>(result.value.size()));
        const QString runtime = formatRuntime(runtimeMs);
        m_tracks->setItems(result.value);
        setAlbumSummary(runtime.isEmpty() ? count : count + QStringLiteral(" · ") + runtime);
        refreshCurrentAlbumRow();
    });
}

void NowPlayingMusicController::playAlbumFrom(int row)
{
    if (!m_playback || row < 0 || row >= m_tracks->rowCount())
        return;
    m_playback->playTracks(m_tracks->items(), row, m_album);
}

void NowPlayingMusicController::toggleFavourite()
{
    if (m_actions && m_active)
        m_actions->setFavorite(m_trackId, !favourite());
}

void NowPlayingMusicController::setLyricsEnabledForTests(bool enabled)
{
    m_lyricsEnabled = enabled;
    clearLyrics();
    loadLyrics();
}

void NowPlayingMusicController::clearLyrics()
{
    ++m_lyricsGeneration;
    m_lyricsKey.clear();
    if (!m_lyrics.isEmpty()) {
        m_lyrics.clear();
        m_lyricsVariant.clear();
        emit lyricsChanged();
    }
    refreshCurrentLyricRow();
}

void NowPlayingMusicController::loadLyrics()
{
    // Every branch below that concludes "there is no lyrics identity to load
    // for the current state" clears whatever is currently shown and bumps the
    // generation, so a reply already in flight for a now-abandoned identity
    // (a different track, or the same track's source switched to one with no
    // lyrics stream) cannot land afterwards and display the wrong thing.
    if (!m_lyricsEnabled || !m_active || !m_player) {
        clearLyrics();
        return;
    }
    const QString sourceId = m_player->currentSource().value(QStringLiteral("id")).toString();
    int streamIndex = -1;
    const QVariantList subtitles = m_player->subtitleStreams();
    for (const QVariant &value : subtitles) {
        const QVariantMap stream = value.toMap();
        if (isLyricsCodec(stream.value(QStringLiteral("codec")).toString())) {
            streamIndex = stream.value(QStringLiteral("index")).toInt();
            break;
        }
    }
    if (sourceId.isEmpty() || streamIndex < 0) {
        clearLyrics();
        return;
    }
    const QString key = m_trackId + QLatin1Char('|') + sourceId + QLatin1Char('|')
                        + QString::number(streamIndex);
    // Identity unchanged from what is already loaded or already in flight:
    // this is the normal no-op path (e.g. sourceIndexChanged firing again for
    // a reason that did not change the chosen lyrics stream), not a stale
    // reply to invalidate, so this one must NOT clear or bump the generation —
    // doing so would orphan a legitimate in-flight request for this same key.
    if (key == m_lyricsKey)
        return;
    m_lyricsKey = key;
    const quint64 generation = ++m_lyricsGeneration;
    m_repository->lyrics(m_trackId, sourceId, streamIndex)
        .then(this, [this, generation](Result<QList<LyricLine>> result) {
            if (generation != m_lyricsGeneration)
                return;
            if (!result.ok()) {
                qCWarning(logApp) << "Now playing: lyrics unavailable:" << result.error;
                return;
            }
            m_lyrics = result.value;
            m_lyricsVariant.clear();
            for (const LyricLine &line : std::as_const(m_lyrics))
                m_lyricsVariant.append(QVariantMap{{QStringLiteral("timeMs"), line.timeMs},
                                                   {QStringLiteral("text"), line.text}});
            emit lyricsChanged();
            refreshCurrentLyricRow();
        });
}

void NowPlayingMusicController::refreshCurrentLyricRow()
{
    int row = -1;
    if (m_player && lyricsTimed()) {
        const qint64 position = m_player->positionMs();
        const auto next = std::upper_bound(m_lyrics.cbegin(), m_lyrics.cend(), position,
                                           [](qint64 at, const LyricLine &line) { return at < line.timeMs; });
        row = static_cast<int>(std::distance(m_lyrics.cbegin(), next)) - 1;
    }
    if (row == m_currentLyricRow)
        return;
    m_currentLyricRow = row;
    emit currentLyricRowChanged();
}

} // namespace strmqt::music
