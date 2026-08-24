#pragma once

#include <QGuiApplication>
#include <QString>
#include <QUrl>

namespace strmqt {

class Settings;
class SecretsStore;
class SessionController;
class HomeController;
class LibraryController;
class PlayerController;
class SearchController;
class SeriesController;
class DetailsController;
class ItemActions;
class LiveUpdateService;
class GamepadManager;
class InputMap;
class MusicController;
class PlaylistController;
class RemoteControlService;
class EmbyImageFetcher;
class CoverTintService;
class PlayerBackend;
class PowerInhibit;
class MprisPlayer;
namespace emby {
class EmbyClient;
}

// Owns the application object graph (settings, secrets, Emby client, controllers).
// QML sees only the controllers, wired up as context properties in main.cpp.
class Application : public QGuiApplication
{
    Q_OBJECT
    Q_PROPERTY(QString interactionContext READ interactionContext WRITE setInteractionContext
                   NOTIFY interactionContextChanged)
    // How far one mouse-wheel notch moves a scrollable surface, in pixels.
    // Published so the horizontal rails — which handle the wheel themselves —
    // move by exactly as much as the vertical views Qt scrolls for us.
    Q_PROPERTY(int wheelStepPx READ wheelStepPx CONSTANT)

public:
    Application(int &argc, char **argv);
    ~Application() override;

    Settings *settings() const { return m_settings; }
    SessionController *session() const { return m_session; }
    HomeController *home() const { return m_home; }
    LibraryController *library() const { return m_library; }
    PlayerController *player() const { return m_player; }
    SearchController *search() const { return m_search; }
    SeriesController *series() const { return m_series; }
    DetailsController *details() const { return m_details; }
    EmbyImageFetcher *imageFetcher() const { return m_imageFetcher; }
    CoverTintService *coverTint() const { return m_coverTint; }
    ItemActions *actions() const { return m_actions; }
    InputMap *input() const { return m_input; }
    RemoteControlService *remote() const { return m_remote; }
    PlaylistController *playlists() const { return m_playlists; }
    MusicController *music() const { return m_music; }
    LiveUpdateService *live() const { return m_live; }
    QString interactionContext() const { return m_interactionContext; }
    int wheelStepPx() const { return m_wheelStepPx; }
    void setInteractionContext(const QString &context);

signals:
    void interactionContextChanged();

private:
    void wirePlaybackIntegrations();
    void applyWheelScrollPolicy();
    void recomputeLiveUpdatePolicy();
    void teardownAuthenticatedSession();
    // Rebuilds the MPRIS track from the queue entry under the playhead and, when
    // the sleeve changed, kicks off the async export that gives it a file:// URI.
    void pushNowPlayingToMpris();

    Settings *m_settings = nullptr;
    SecretsStore *m_secrets = nullptr;
    emby::EmbyClient *m_client = nullptr;
    SessionController *m_session = nullptr;
    HomeController *m_home = nullptr;
    LibraryController *m_library = nullptr;
    PlayerBackend *m_engine = nullptr;
    PlayerController *m_player = nullptr;
    SearchController *m_search = nullptr;
    SeriesController *m_series = nullptr;
    DetailsController *m_details = nullptr;
    EmbyImageFetcher *m_imageFetcher = nullptr;
    CoverTintService *m_coverTint = nullptr;
    ItemActions *m_actions = nullptr;
    InputMap *m_input = nullptr;
#ifdef STRMQT_HAVE_SDL3
    GamepadManager *m_gamepad = nullptr;
#endif
    LiveUpdateService *m_live = nullptr;
    RemoteControlService *m_remote = nullptr;
    PlaylistController *m_playlists = nullptr;
    MusicController *m_music = nullptr;
    PowerInhibit *m_powerInhibit = nullptr;
    MprisPlayer *m_mpris = nullptr;
    // Image id ("{itemId}/{imageType}/{tag}") of the sleeve MPRIS is showing or
    // waiting on. Doubles as the generation guard for the export: a reply whose
    // id is no longer this one belongs to a track that has already been
    // replaced, and publishing it would put the wrong cover on the lock screen.
    QString m_mprisArtId;
    QUrl m_mprisArtUrl;
    // Published by Main.qml from the surface the user can actually interact
    // with. Playback activity is deliberately not an input context: playback
    // may continue underneath an ordinary browse page.
    QString m_interactionContext = QStringLiteral("login");
    int m_wheelStepPx = 0;
    bool m_lastPlaybackActive = false;
};

} // namespace strmqt
