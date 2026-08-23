#include "RemoteControlService.h"

#include "app/ItemActions.h"
#include "app/controllers/LiveUpdateService.h"
#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "core/Log.h"
#include "server/emby/EmbyClient.h"
#include "server/dto/MediaItem.h" // kTicksPerMs
#include "server/emby/EmbyWebSocket.h"

#include <QJsonArray>

namespace strmqt {

namespace {
// Rewind/FastForward carry no distance, so the receiving client picks one.
// 10 s matches this app's own keyboard seek, so the pad, the keyboard and a
// phone remote all move by the same amount.
constexpr qint64 kRemoteSkipMs = 10'000;
} // namespace

QStringList RemoteControlService::supportedCommands()
{
    // Only what is actually handled below. An advertised command that does
    // nothing is worse than an absent one: the remote shows the button, the
    // user presses it, and nothing happens with no way to tell why.
    return {
        // Transport
        QStringLiteral("Play"),          QStringLiteral("Pause"),
        QStringLiteral("Unpause"),       QStringLiteral("PlayPause"),
        QStringLiteral("Stop"),          QStringLiteral("NextTrack"),
        QStringLiteral("PreviousTrack"), QStringLiteral("Seek"),
        // Skip buttons. Emby sends these as Playstate commands carrying NO
        // amount — the receiving client decides how far — which is why a
        // remote's "skip 30s" did nothing until they were handled.
        QStringLiteral("Rewind"),        QStringLiteral("FastForward"),
        // Sound
        QStringLiteral("SetVolume"),     QStringLiteral("VolumeUp"),
        QStringLiteral("VolumeDown"),    QStringLiteral("Mute"),
        QStringLiteral("Unmute"),        QStringLiteral("ToggleMute"),
        // Streams and playback shape
        QStringLiteral("SetAudioStreamIndex"),
        QStringLiteral("SetSubtitleStreamIndex"),
        QStringLiteral("SetSubtitleOffset"),
        QStringLiteral("SetPlaybackRate"),
        QStringLiteral("SetRepeatMode"), QStringLiteral("SetShuffle"),
        // UI
        QStringLiteral("ToggleOsd"),     QStringLiteral("GoHome"),
        QStringLiteral("GoToSearch"),    QStringLiteral("GoToSettings"),
        QStringLiteral("Back"),          QStringLiteral("DisplayMessage"),
    };
}

RemoteControlService::RemoteControlService(emby::EmbyClient *client, LiveUpdateService *live,
                                           PlayerController *player, ItemActions *actions,
                                           QObject *parent)
    : QObject(parent), m_client(client), m_live(live), m_player(player), m_actions(actions)
{
    if (!m_live || !m_live->socket())
        return;

    connect(m_live->socket(), &emby::EmbyWebSocket::messageReceived, this,
            &RemoteControlService::onMessage);
    // Capabilities are per-session, so they must be re-sent on every reconnect —
    // a resumed socket is a new session to the server, and one announced at
    // startup would be forgotten the first time the network blinked.
    connect(m_live->socket(), &emby::EmbyWebSocket::connectedChanged, this, [this] {
        if (m_live->socket()->isConnected())
            announceCapabilities();
    });
    if (m_live->socket()->isConnected())
        announceCapabilities();
}

void RemoteControlService::announceCapabilities()
{
    if (!m_client)
        return;
    m_client->reportCapabilities(supportedCommands(), true)
        .then(this, [](const Result<bool> &result) {
            if (!result.ok())
                qCWarning(logApp) << "remote: capability report failed:" << result.error;
            else
                qCInfo(logApp) << "remote: session is controllable";
        });
}

void RemoteControlService::onMessage(const QString &type, const QJsonObject &data)
{
    if (type == QLatin1String("Playstate"))
        handlePlaystate(data);
    else if (type == QLatin1String("Play"))
        handlePlay(data);
    else if (type == QLatin1String("GeneralCommand"))
        handleGeneralCommand(data);
}

void RemoteControlService::handlePlaystate(const QJsonObject &data)
{
    if (!m_player)
        return;
    const QString command = data.value(QLatin1String("Command")).toString();
    qCInfo(logApp) << "remote: playstate" << command;

    if (command == QLatin1String("PlayPause")) {
        m_player->togglePause();
    } else if (command == QLatin1String("Pause")) {
        if (!m_player->paused())
            m_player->togglePause();
    } else if (command == QLatin1String("Unpause")) {
        if (m_player->paused())
            m_player->togglePause();
    } else if (command == QLatin1String("Stop")) {
        m_player->stop();
    } else if (command == QLatin1String("NextTrack")) {
        m_player->playNext();
    } else if (command == QLatin1String("PreviousTrack")) {
        m_player->playPrevious();
    } else if (command == QLatin1String("Rewind")) {
        m_player->seekRelative(-kRemoteSkipMs);
    } else if (command == QLatin1String("FastForward")) {
        m_player->seekRelative(kRemoteSkipMs);
    } else if (command == QLatin1String("Seek")) {
        // SeekPositionTicks is a JSON number large enough to lose precision as a
        // double past ~104 days of runtime; toVariant().toLongLong() keeps it.
        const qint64 ticks =
            data.value(QLatin1String("SeekPositionTicks")).toVariant().toLongLong();
        m_player->seekTo(ticks / kTicksPerMs);
    } else {
        qCDebug(logApp) << "remote: unhandled playstate command" << command;
    }
}

void RemoteControlService::handlePlay(const QJsonObject &data)
{
    if (!m_actions)
        return;
    const QString command = data.value(QLatin1String("PlayCommand")).toString();
    const QJsonArray ids = data.value(QLatin1String("ItemIds")).toArray();
    if (ids.isEmpty())
        return;

    QVariantList items;
    for (const QJsonValue &id : ids) {
        const QString itemId = id.toVariant().toString();
        if (itemId.isEmpty())
            continue;
        // The remote sends ids only. ItemActions can resolve one it already
        // knows; anything else is played from the id alone, and the queue row
        // fills in when the item's own details arrive.
        QVariantMap known = m_actions->itemFor(itemId);
        if (known.isEmpty()) {
            known.insert(QStringLiteral("itemId"), itemId);
            known.insert(QStringLiteral("name"), QString());
        }
        items.append(known);
    }
    if (items.isEmpty())
        return;

    qCInfo(logApp) << "remote: play" << command << items.size() << "item(s)";
    if (command == QLatin1String("PlayNext")) {
        // Each playNext() inserts directly after the current item, so walking
        // the list forwards would leave the queue holding it backwards: sending
        // [A, B] queued B then A. Walk it from the end and the sent order is
        // what ends up in the queue.
        for (auto it = items.crbegin(); it != items.crend(); ++it)
            m_actions->playNext(*it);
    } else if (command == QLatin1String("PlayLast")) {
        for (const QVariant &item : items)
            m_actions->addToQueue(item);
    } else {
        // PlayNow, and anything unrecognised: starting playback is the safe
        // reading of a Play command.
        const qint64 startTicks =
            data.value(QLatin1String("StartPositionTicks")).toVariant().toLongLong();
        if (startTicks > 0) {
            // "Play from the resume point" has to travel with the queue entry.
            // Seeking after playAllFrom() cannot work: the PlaybackInfo fetch is
            // async, so the seek runs against an engine with nothing loaded and
            // is silently dropped — the item then restarts from zero. The queue
            // entry's own resume point is what startQueueCurrent() reads, and it
            // gates on isResumable(), which is position AND not-played.
            QVariantMap first = items.constFirst().toMap();
            first.insert(QStringLiteral("positionMs"), startTicks / kTicksPerMs);
            first.insert(QStringLiteral("played"), false);
            items[0] = first;
        }
        m_actions->playAllFrom(items, 0);
    }
}

void RemoteControlService::handleGeneralCommand(const QJsonObject &data)
{
    const QString name = data.value(QLatin1String("Name")).toString();
    const QJsonObject args = data.value(QLatin1String("Arguments")).toObject();
    qCInfo(logApp) << "remote: general" << name;

    if (!m_player)
        return;

    if (name == QLatin1String("SetAudioStreamIndex")) {
        selectStreamByServerIndex(QStringLiteral("Audio"),
                                  args.value(QLatin1String("Index")).toVariant().toInt());
        return;
    }
    if (name == QLatin1String("SetSubtitleStreamIndex")) {
        selectStreamByServerIndex(QStringLiteral("Subtitle"),
                                  args.value(QLatin1String("Index")).toVariant().toInt());
        return;
    }
    if (name == QLatin1String("SetSubtitleOffset")) {
        // Emby sends seconds, possibly fractional.
        const double seconds = args.value(QLatin1String("Value")).toVariant().toDouble();
        m_player->setSubtitleDelayMs(static_cast<int>(seconds * 1000.0));
        return;
    }
    if (name == QLatin1String("SetPlaybackRate")) {
        const double rate = args.value(QLatin1String("Value")).toVariant().toDouble();
        if (rate > 0.0)
            m_player->setPlaybackSpeed(rate);
        return;
    }
    if (name == QLatin1String("SetRepeatMode")) {
        const QString mode = args.value(QLatin1String("RepeatMode")).toString();
        if (PlayQueue *queue = m_player->queue()) {
            if (mode == QLatin1String("RepeatAll"))
                queue->setRepeatMode(PlayQueue::RepeatAll);
            else if (mode == QLatin1String("RepeatOne"))
                queue->setRepeatMode(PlayQueue::RepeatOne);
            else
                queue->setRepeatMode(PlayQueue::RepeatOff);
        }
        return;
    }
    if (name == QLatin1String("SetShuffle")) {
        const QString mode = args.value(QLatin1String("Shuffle")).toString();
        if (PlayQueue *queue = m_player->queue())
            queue->setShuffled(mode != QLatin1String("Sorted"));
        return;
    }
    if (name == QLatin1String("ToggleOsd")) {
        emit osdToggleRequested();
        return;
    }
    if (name == QLatin1String("GoHome")) {
        emit navigationRequested(QStringLiteral("home"));
        return;
    }
    if (name == QLatin1String("GoToSearch")) {
        emit navigationRequested(QStringLiteral("search"));
        return;
    }
    if (name == QLatin1String("GoToSettings")) {
        emit navigationRequested(QStringLiteral("settings"));
        return;
    }
    if (name == QLatin1String("Back")) {
        emit navigationRequested(QStringLiteral("back"));
        return;
    }

    if (name == QLatin1String("SetVolume")) {
        bool ok = false;
        const int volume = args.value(QLatin1String("Volume")).toVariant().toInt(&ok);
        if (ok)
            m_player->setVolume(volume);
    } else if (name == QLatin1String("VolumeUp")) {
        m_player->setVolume(m_player->volume() + 5);
    } else if (name == QLatin1String("VolumeDown")) {
        m_player->setVolume(m_player->volume() - 5);
    } else if (name == QLatin1String("Mute")) {
        m_player->setMuted(true);
    } else if (name == QLatin1String("Unmute")) {
        m_player->setMuted(false);
    } else if (name == QLatin1String("ToggleMute")) {
        m_player->setMuted(!m_player->muted());
    } else if (name == QLatin1String("DisplayMessage")) {
        emit messageRequested(args.value(QLatin1String("Header")).toString(),
                              args.value(QLatin1String("Text")).toString());
    } else {
        qCDebug(logApp) << "remote: unhandled general command" << name;
    }
}

void RemoteControlService::selectStreamByServerIndex(const QString &type, int serverIndex)
{
    if (!m_player)
        return;

    const bool subtitle = type == QLatin1String("Subtitle");

    // -1 is a real instruction ("off"), not a missing value, and only subtitles
    // can honour it — there is no such thing as no audio track.
    if (serverIndex < 0) {
        if (subtitle)
            m_player->setSubtitleTrack(-1);
        return;
    }

    // The server numbers every stream of the source in one sequence; the engine
    // numbers each type separately. The only correspondence the two lists share
    // is ORDER within a type, so find the requested stream's position among its
    // own type and take the engine track at the same position.
    const QVariantList streams = subtitle ? m_player->subtitleStreams()
                                          : m_player->audioStreams();
    int ordinal = -1;
    for (int i = 0; i < streams.size(); ++i) {
        if (streams.at(i).toMap().value(QStringLiteral("index")).toInt() == serverIndex) {
            ordinal = i;
            break;
        }
    }
    if (ordinal < 0) {
        qCWarning(logApp) << "remote: no" << type << "stream with server index" << serverIndex;
        return;
    }

    const QVariantList tracks =
        subtitle ? m_player->backendSubtitleTracks() : m_player->backendAudioTracks();
    if (ordinal >= tracks.size()) {
        // Happens when the engine and the server disagree about the track list
        // — an external subtitle file the server does not know about, say. Say
        // so rather than selecting the wrong track.
        qCWarning(logApp) << "remote:" << type << "ordinal" << ordinal
                          << "beyond the engine's" << tracks.size() << "track(s)";
        return;
    }
    const int trackId = tracks.at(ordinal).toMap().value(QStringLiteral("id")).toInt();
    if (subtitle)
        m_player->setSubtitleTrack(trackId);
    else
        m_player->setAudioTrack(trackId);
}

} // namespace strmqt
