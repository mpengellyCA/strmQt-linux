#include "Application.h"
#include "ApplicationPolicy.h"

#include <QStyleHints>

#include "CoverTintService.h"
#include "EmbyImageProvider.h"
#include "ItemActions.h"
#include "controllers/DetailsController.h"
#include "controllers/HomeController.h"
#include "controllers/LibraryController.h"
#include "controllers/LiveUpdateService.h"
#include "controllers/PlayerController.h"
#include "controllers/MusicController.h"
#include "controllers/PlaylistController.h"
#include "controllers/RemoteControlService.h"
#include "controllers/SearchController.h"
#include "controllers/SeriesController.h"
#include "controllers/SessionController.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "input/InputMap.h"
#include "platform/HdrSupport.h"
#include "platform/MprisPlayer.h"
#include "platform/PowerInhibit.h"
#include "platform/SecretsStore.h"
#ifdef STRMQT_HAVE_SDL3
#include "input/GamepadManager.h"
#endif
#include "playback/mpv/MpvPlayer.h"
#ifdef STRMQT_HAVE_VLC
#include "playback/vlc/VlcPlayer.h"
#endif
#include "app/models/MediaItemModel.h"
#include "server/emby/EmbyClient.h"

#include <QSysInfo>

namespace strmqt {

Application::Application(int &argc, char **argv) : QGuiApplication(argc, argv)
{
    setOrganizationName(QStringLiteral("StrmQt"));
    setOrganizationDomain(QStringLiteral("mikesdev.ca"));
    setApplicationName(QStringLiteral("strmqt"));
    // Wayland app_id == desktop file basename, so KWin can match window ↔ .desktop.
    setDesktopFileName(QStringLiteral("ca.mikesdev.StrmQt"));
    setApplicationVersion(QStringLiteral(STRMQT_VERSION));
    setApplicationDisplayName(QStringLiteral("StrmQt"));

    initLogging();

    m_settings = new Settings(this);
    m_secrets = new SecretsStore(this);
    m_client = new emby::EmbyClient(this);
    m_client->setDeviceId(m_settings->deviceId());
    m_client->setDeviceName(QSysInfo::machineHostName());

    m_session = new SessionController(m_settings, m_secrets, m_client, this);
    m_home = new HomeController(m_client, this);
    m_library = new LibraryController(m_client, this);
    m_imageFetcher = new EmbyImageFetcher(m_client, this);
    // Samples the covers the fetcher above decodes; no fetching of its own.
    m_coverTint = new CoverTintService(m_imageFetcher, this);
    m_search = new SearchController(m_client, this);
    m_series = new SeriesController(m_client, this);
    m_details = new DetailsController(m_client, this);
    m_playlists = new PlaylistController(m_client, this);
    m_music = new MusicController(m_client, this);
    // Engine choice (Settings playback/engine): mpv primary, vlc escape hatch.
    // Without the VLC build a stale "vlc" setting silently falls back to mpv; the
    // engineName() logged below always reports what was actually constructed.
#ifdef STRMQT_HAVE_VLC
    if (m_settings->playbackEngine() == QLatin1String("vlc"))
        m_engine = new VlcPlayer(this);
    else
#endif
        m_engine = new MpvPlayer(m_settings->toneMapping(), this);
    auto *hdr = new HdrSupport(this);
    hdr->probe(); // informational: logs whether an HDR display is active
    qCInfo(logApp) << "playback engine:" << m_engine->engineName();
    m_player = new PlayerController(m_client, m_engine, m_settings, this);

    // One implementation of every item verb (play / played / favorite), shared by
    // cards, context menus, the details page and the player; and one source of
    // truth for every binding (ARCHITECTURE.md).
    m_input = new InputMap(this);
#ifdef STRMQT_HAVE_SDL3
    // Constructed after InputMap because every pad button resolves through it:
    // the pad synthesizes whatever key an action is *currently* bound to, so a
    // rebind moves the gamepad with it. Degrades to keyboard-only when SDL
    // cannot initialise.
    m_gamepad = new GamepadManager(m_input, this);
#endif
    m_actions = new ItemActions(m_client, m_player, this);
    for (MediaItemModel *model : { m_home->resume(), m_home->nextUp(), m_home->favorites(),
                                   m_library->model(), m_search->model(), m_series->episodes(),
                                   m_details->similar(), m_music->albums(), m_music->artists(),
                                   m_music->tracks(), m_music->songs(),
                                   m_music->artistAlbums(),
                                   m_music->artistTracks(), m_music->playlists(),
                                   m_playlists->items() })
        m_actions->registerModel(model);
    // Two lists of playlists exist on purpose (see MusicController::playlists):
    // PlaylistController's is every playlist the user has, for the "add to…"
    // picker, and MusicController's is the music library's alone, for the tab.
    // Only the first is refreshed by the verbs that change the set, so the
    // second is told here — otherwise a playlist made from a track never
    // appears in the tab whose whole job is to list it. Wired in C++ rather
    // than relayed through QML because it is a real dependency between two
    // controllers and no page should have to remember to carry it.
    connect(m_playlists, &PlaylistController::playlistsMutated, m_music,
            &MusicController::invalidatePlaylists);
    // The queue verbs live in ItemActions (ARCHITECTURE.md rule 3), and
    // MusicController::playAlbum() needs them: it fetches an album's tracks
    // into a scratch list of its own and hands the ordered items over, so that
    // playing an album never has to navigate the open-album state to do it.
    m_music->setActions(m_actions);
    // SeriesController keeps a whole-series episode list to answer "next
    // unwatched"; it has to hear about played toggles to stay right. Connected
    // here rather than forwarded through QML because the dependency is a real
    // one and the page should not have to remember to relay it.
    connect(m_actions, &ItemActions::playedChanged, m_series, &SeriesController::notePlayed);

    // Latest rails are built lazily by HomeController::refresh(); registerModel dedupes.
    const auto registerRailModels = [this](const QVariantList &rails) {
        for (const QVariant &rail : rails)
            m_actions->registerModel(rail.toMap().value(QStringLiteral("model"))
                                         .value<MediaItemModel *>());
    };
    connect(m_home, &HomeController::latestRailsChanged, this,
            [this, registerRailModels] { registerRailModels(m_home->latestRails()); });
    // Genre rails arrive later and separately; without this their cards have no
    // verbs, because ItemActions can only act on models it knows.
    connect(m_home, &HomeController::genreRailsChanged, this,
            [this, registerRailModels] { registerRailModels(m_home->genreRails()); });

    // Live updates (ARCHITECTURE.md). Without this the UI is a snapshot taken at
    // startup: watch something on a phone and Continue Watching stays wrong until
    // the app is restarted.
    m_live = new LiveUpdateService(m_client, m_settings, this);
    m_home->bindLiveUpdates(m_live);
    m_library->bindLiveUpdates(m_live);
    connect(m_session, &SessionController::authenticatedChanged, this, [this] {
        if (m_session->authenticated())
            m_live->start();
        else
            m_live->stop();
    });
    // One policy owns every suspension cause. Polling stands down only when
    // the app is backgrounded or a VIDEO decoder is active; audio playback is
    // cheap and may last for hours. Recompute on every input so an audio/video
    // queue boundary cannot leave the old decision latched.
    connect(m_player, &PlayerController::activeChanged, this,
            &Application::recomputeLiveUpdatePolicy);
    connect(m_player, &PlayerController::isAudioChanged, this,
            &Application::recomputeLiveUpdatePolicy);
    connect(this, &QGuiApplication::applicationStateChanged, this,
            [this] { recomputeLiveUpdatePolicy(); });
    recomputeLiveUpdatePolicy();
    if (m_session->authenticated())
        m_live->start();

    // Quality preferences reach the server through the DeviceProfile on every
    // PlaybackInfo, so they only take effect on the next start — which is why
    // they are pushed on change rather than read at request time.
    const auto pushQuality = [this] {
        m_client->setQualityPreferences(m_settings->maxBitrateKbps(),
                                        m_settings->playbackMode());
    };
    pushQuality();
    connect(m_settings, &Settings::maxBitrateKbpsChanged, this, pushQuality);
    connect(m_settings, &Settings::playbackModeChanged, this, pushQuality);

    // Remote control (ARCHITECTURE.md): another Emby client can now see and drive
    // this session. Constructed after m_live because it listens on that socket.
    m_remote = new RemoteControlService(m_client, m_live, m_player, m_actions, this);

    m_powerInhibit = new PowerInhibit(this);
    m_mpris = new MprisPlayer(this);
    m_mpris->registerOnBus();
    wirePlaybackIntegrations();
    // SessionController emits this before it replaces the server or
    // credentials. Keeping teardown here makes logout one application-level
    // transaction instead of a navigation-only event in QML.
    connect(m_session, &SessionController::sessionBoundaryChanged, this,
            [this] { teardownAuthenticatedSession(); });

    applyWheelScrollPolicy();

    qCInfo(logApp) << "StrmQt" << applicationVersion() << "starting, platform:" << platformName();
}

// A desktop's wheel setting is written for text: three lines, and Qt scrolls a
// Flickable by exactly wheelScrollLines x 24 px per notch — 72 px, measured. A
// poster is four times that tall, so a notch moved a grid by a quarter of one
// card and crossing a library was a wrist exercise. The multiplier is applied
// to whatever the user chose rather than replacing it, so someone who has
// already tuned their wheel keeps the proportion they asked for, and it is
// clamped so an unusual setting cannot turn one notch into a page-jump.
void Application::applyWheelScrollPolicy()
{
    constexpr int kWheelMultiplier = 4;
    constexpr int kMaxWheelLines = 40;
    constexpr int kPixelsPerLine = 24; // Qt's own figure, in QQuickFlickable
    const int configured = qMax(1, styleHints()->wheelScrollLines());
    const int lines = qBound(kWheelMultiplier, configured * kWheelMultiplier, kMaxWheelLines);
    styleHints()->setWheelScrollLines(lines);
    m_wheelStepPx = lines * kPixelsPerLine;
    qCInfo(logApp) << "wheel scroll:" << configured << "->" << lines << "lines ("
                   << m_wheelStepPx << "px per notch)";
}

void Application::teardownAuthenticatedSession()
{
    // Stop all producers before clearing their presentation. The explicit
    // boundary shutdown also covers a resolving controller and invalidates a
    // deferred suspended-audio resume before old credentials disappear.
    m_live->stop();
    m_player->shutdownForSessionBoundary();

    // ItemActions owns the registry of every user-facing media model. It also
    // retires optimistic mutations and asynchronous queue builders here.
    m_actions->resetSessionState();
    m_search->clearRecentQueries();

    // No old user's title, art, queue capabilities, or position may survive on
    // the desktop media-control surface while the login page is visible.
    m_mprisArtId.clear();
    m_mprisArtUrl.clear();
    m_mpris->setBusy(false);
    m_mpris->setPlaybackActive(false, false);
    m_mpris->setQueueState(false, false);
    m_mpris->setPositionMs(0);
    m_mpris->setNowPlaying({});
    m_powerInhibit->release();
}

void Application::setInteractionContext(const QString &context)
{
    static const QStringList valid{QStringLiteral("login"), QStringLiteral("browse"),
                                   QStringLiteral("music"), QStringLiteral("player"),
                                   QStringLiteral("overlay")};
    const QString wanted = valid.contains(context) ? context : QStringLiteral("browse");
    if (m_interactionContext == wanted) {
#ifdef STRMQT_HAVE_SDL3
        // GamepadManager is constructed with a conservative browse default,
        // while Application starts in login. The first QML publication may
        // therefore repeat our value but still has hardware state to sync.
        if (m_gamepad && m_gamepad->context() != wanted)
            m_gamepad->setContext(wanted);
#endif
        return;
    }
    m_interactionContext = wanted;
#ifdef STRMQT_HAVE_SDL3
    if (m_gamepad)
        m_gamepad->setContext(wanted);
#endif
    emit interactionContextChanged();
}

void Application::recomputeLiveUpdatePolicy()
{
    if (!m_live || !m_player)
        return;
    const bool foreground = applicationState() == Qt::ApplicationActive;
    const bool active = m_player->active();
    const bool playbackEnded = m_lastPlaybackActive && !active;
    m_lastPlaybackActive = active;
    const bool wasSuspended = m_live->suspended();
    const bool suspended =
        shouldSuspendLiveUpdates(foreground, active, m_player->isAudio());
    m_live->setSuspended(suspended);
    // Crossing from any composed suspension state to none is the safe point
    // to reconcile immediately: video stopped/became audio, or the app came
    // back to the foreground.
    if (!suspended && (wasSuspended || playbackEnded))
        m_live->refreshNow();
}

void Application::wirePlaybackIntegrations()
{
    // Playback state → screensaver inhibit + MPRIS surface.
    auto syncState = [this] {
        const bool active = m_player->active();
        const bool paused = m_player->paused();
        m_mpris->setBusy(m_player->busy());
        m_mpris->setPlaybackActive(active, paused);
        // Screen/display inhibition belongs to visible video. Audio playback
        // should allow ordinary display blanking, even for an hours-long queue.
        if (shouldInhibitDisplay(active, paused, m_player->isAudio()))
            m_powerInhibit->acquire(QStringLiteral("Playing video"));
        else
            m_powerInhibit->release();
    };
    connect(m_player, &PlayerController::activeChanged, this, syncState);
    connect(m_player, &PlayerController::pausedChanged, this, syncState);
    connect(m_player, &PlayerController::busyChanged, this, syncState);
    connect(m_player, &PlayerController::isAudioChanged, this, syncState);
    syncState();

    // MPRIS Volume is a linear multiplier with 1.0 as nominal volume; the
    // controller exposes the same range as integer percent, including mpv's
    // intentional amplification up to 130%. Muting publishes effective zero
    // without throwing away the level that unmute restores.
    const auto syncVolume = [this] {
        const double volume = m_player->muted() ? 0.0 : m_player->volume() / 100.0;
        m_mpris->setVolume(volume);
    };
    connect(m_player, &PlayerController::volumeChanged, this, syncVolume);
    connect(m_player, &PlayerController::mutedChanged, this, syncVolume);
    connect(m_mpris, &MprisPlayer::volumeRequested, m_player, [this](double volume) {
        const int percent = qRound(volume * 100.0);
        if (percent > 0 && m_player->muted())
            m_player->setMuted(false);
        m_player->setVolume(percent);
    });
    syncVolume();
    const auto syncRate = [this] { m_mpris->setRate(m_player->playbackSpeed()); };
    connect(m_player, &PlayerController::playbackSpeedChanged, this, syncRate);
    connect(m_mpris, &MprisPlayer::rateRequested, m_player,
            [this](double rate) { m_player->setPlaybackSpeed(rate); });
    syncRate();
    // Title and duration land on separate signals and the queue entry carries
    // everything else, so all three funnel into one rebuild; MprisPlayer drops
    // the repeats rather than putting them on the bus.
    connect(m_player, &PlayerController::titleChanged, this,
            &Application::pushNowPlayingToMpris);
    connect(m_player, &PlayerController::durationChanged, this,
            &Application::pushNowPlayingToMpris);
    connect(m_player->queue(), &PlayQueue::currentChanged, this,
            &Application::pushNowPlayingToMpris);
    connect(m_player, &PlayerController::positionChanged, this,
            [this] { m_mpris->setPositionMs(m_player->positionMs()); });
    // CanGoNext / CanGoPrevious have to be re-announced, not just answered: an
    // applet reads the property when the player appears and never again.
    connect(m_player, &PlayerController::queueStateChanged, this, [this] {
        m_mpris->setQueueState(m_player->hasNext(), m_player->hasPrevious());
    });
    m_mpris->setQueueState(m_player->hasNext(), m_player->hasPrevious());
    // The sleeve arrives long after the rest of the track. Anything but the
    // export we are currently waiting on belongs to a superseded item.
    connect(m_imageFetcher, &EmbyImageFetcher::fileExported, this,
            [this](const QString &id, const QUrl &fileUrl) {
                if (id != m_mprisArtId)
                    return;
                if (fileUrl.isEmpty()) {
                    // A failure must not latch. Every track on an album shares
                    // one coverSource(), so leaving the failed id in place makes
                    // the artId != m_mprisArtId guard below false for the rest of
                    // the record and the export is never retried — one dropped
                    // request would cost the whole album its lock-screen cover.
                    m_mprisArtId.clear();
                    return;
                }
                m_mprisArtUrl = fileUrl;
                m_mpris->setArtUrl(fileUrl);
            });

    // MPRIS remote commands (KDE Connect phone, Plasma applet) → controller.
    // Play, Pause and PlayPause are three distinct verbs in the spec and only
    // the last of them toggles. `playerctl play`, XF86AudioPlay on GNOME and
    // most notification-daemon buttons send Play, so mapping it to the toggle
    // paused a track that was already playing.
    connect(m_mpris, &MprisPlayer::playPauseRequested, m_player, &PlayerController::togglePause);
    connect(m_mpris, &MprisPlayer::playRequested, m_player, [this] { m_player->setPaused(false); });
    connect(m_mpris, &MprisPlayer::pauseRequested, m_player, [this] { m_player->setPaused(true); });
    connect(m_mpris, &MprisPlayer::stopRequested, m_player, &PlayerController::stop);
    connect(m_mpris, &MprisPlayer::nextRequested, m_player, &PlayerController::playNext);
    connect(m_mpris, &MprisPlayer::previousRequested, m_player, &PlayerController::playPrevious);
    connect(m_mpris, &MprisPlayer::seekRequested, m_player, &PlayerController::seekRelative);
    connect(m_mpris, &MprisPlayer::setPositionRequested, m_player, &PlayerController::seekTo);
    // Seeked is the spec's answer to a client that extrapolates position between
    // polls: without it the applet's bar keeps running from where the playhead
    // was and visibly snaps back at the next poll. It fires for seeks made in
    // the app as well as ones that arrived over the bus — the client has no
    // other way to learn about the former.
    connect(m_player, &PlayerController::seeked, m_mpris, &MprisPlayer::notifySeeked);
}

void Application::pushNowPlayingToMpris()
{
    // The controller knows the title and the runtime; everything a media applet
    // draws besides those — the sleeve, the album, the performers, the track
    // number — only exists on the queue entry, which is why this reads both.
    const MediaItem item = m_player->queue()->current();

    MprisPlayer::TrackInfo track;
    track.itemId = item.id;
    // The queue entry wins over the engine, and the order matters. This runs
    // from PlayQueue::currentChanged, which advance() emits BEFORE
    // startQueueCurrent() → startItem() assigns the new title, and the backend
    // still reports the old file's duration until the new one loads. So at the
    // moment a new mpris:trackid goes out, m_player->title() and
    // m_player->durationMs() both still describe the track that just ended:
    // taking them would pop the previous song's name in every notification
    // keyed on trackid and scale the applet's scrub bar to the previous
    // runtime for the whole of the load.
    track.title = item.name.isEmpty() ? m_player->title() : item.name;
    track.durationMs = item.runtimeMs() > 0 ? item.runtimeMs() : m_player->durationMs();
    track.album = item.album;
    if (!item.artists.isEmpty())
        track.artists = item.artists;
    else if (!item.albumArtist.isEmpty())
        track.artists = {item.albumArtist};
    if (!item.albumArtist.isEmpty())
        track.albumArtists = {item.albumArtist};
    track.trackNumber = item.indexNumber;
    // playCount is 0 both for "never played" and for a queue entry that never
    // carried UserData, so only a positive count is a fact worth publishing.
    if (item.playCount > 0)
        track.useCount = item.playCount;
    // xesam:userRating is the *user's* rating, so communityRating is the wrong
    // number for it. The only per-user verdict this client carries is the
    // favourite flag; anything else stays unrated, because publishing 0.0 reads
    // as "rated zero stars" rather than "not rated".
    if (item.favorite)
        track.userRating = 1.0;

    // coverSource() is the square: for a track it resolves to the ALBUM's cover
    // rather than whatever the ripper embedded in the file.
    const MediaItem::ImageRef cover = item.coverSource();
    const QString artId =
        cover.isValid()
            ? cover.itemId + QLatin1Char('/') + cover.imageType + QLatin1Char('/') + cover.tag
            : QString();
    if (artId != m_mprisArtId) {
        m_mprisArtId = artId;
        m_mprisArtUrl.clear();
        if (!artId.isEmpty())
            m_imageFetcher->exportToFile(artId, QStringLiteral("mpris"));
    }
    track.artUrl = m_mprisArtUrl;

    m_mpris->setNowPlaying(track);
}

Application::~Application() = default;

} // namespace strmqt
