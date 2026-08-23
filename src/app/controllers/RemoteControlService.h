#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>

namespace strmqt {

class ItemActions;
class LiveUpdateService;
class PlayerController;

namespace emby {
class EmbyClient;
}

// Lets another Emby client drive this one (ARCHITECTURE.md): the phone app's
// "cast to" list, Emby Web's remote tab, a second StrmQt.
//
// Two halves, and BOTH are required — this is the part that is easy to get
// half-right. Handling the messages is useless if the server never advertises
// the session, and advertising it is worse than useless if the messages are
// ignored, because the client then appears as a target that silently does
// nothing. Verified against the live server before writing this: StrmQt's
// session reported SupportsRemoteControl=false with 0 SupportedCommands, while
// Emby Web reported 39, which is why StrmQt never appeared as a cast target.
//
// Commands arrive on the same WebSocket the live updates use, as three message
// types: "Playstate" (transport), "Play" (start something), "GeneralCommand"
// (everything else). Only commands with a real verb behind them are declared —
// advertising a command we ignore is the same silent-failure trap.
class RemoteControlService : public QObject
{
    Q_OBJECT

public:
    RemoteControlService(emby::EmbyClient *client, LiveUpdateService *live,
                         PlayerController *player, ItemActions *actions,
                         QObject *parent = nullptr);

    // Commands declared to the server. Public so a test can assert that every
    // one of them is actually handled.
    static QStringList supportedCommands();

signals:
    // Something asked us to show a message ("DisplayMessage"). The UI surfaces
    // it as a toast; there is nowhere else for it to go.
    void messageRequested(const QString &header, const QString &text);
    // Commands whose verb lives in the UI, not in a controller.
    void navigationRequested(const QString &destination); // "home"|"search"|"settings"|"back"
    void osdToggleRequested();

private:
    void announceCapabilities();
    void onMessage(const QString &type, const QJsonObject &data);
    void handlePlaystate(const QJsonObject &data);
    void handlePlay(const QJsonObject &data);
    void handleGeneralCommand(const QJsonObject &data);
    // Emby addresses tracks by the ABSOLUTE MediaStream index of the source;
    // the engine addresses them by its own per-type track id. These map one to
    // the other by position within the type, which is the only correspondence
    // the two lists actually share.
    void selectStreamByServerIndex(const QString &type, int serverIndex);

    emby::EmbyClient *m_client;
    LiveUpdateService *m_live;
    PlayerController *m_player;
    ItemActions *m_actions;
};

} // namespace strmqt
