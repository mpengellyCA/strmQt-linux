#pragma once

#include <QGuiApplication>

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
    ItemActions *actions() const { return m_actions; }
    InputMap *input() const { return m_input; }
    RemoteControlService *remote() const { return m_remote; }
    PlaylistController *playlists() const { return m_playlists; }
    MusicController *music() const { return m_music; }
    LiveUpdateService *live() const { return m_live; }

private:
    void wirePlaybackIntegrations();

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
};

} // namespace strmqt
