#include "Application.h"
#include "WindowFocusKeeper.h"
#include "platform/streaming/HostFiles.h"
#include "platform/streaming/StreamingHost.h"
#include "input/FocusNavigator.h"
#include <QQuickWindow>
#include "controllers/PlaylistController.h"
#include "controllers/music/AlbumController.h"
#include "controllers/music/ArtistController.h"
#include "controllers/music/MusicBrowseController.h"
#include "controllers/music/MusicHomeController.h"
#include "controllers/music/NowPlayingMusicController.h"
#include "controllers/RemoteControlService.h"
#include "CoverTintService.h"
#include "EmbyImageProvider.h"
#include "ItemActions.h"
#include "controllers/DetailsController.h"
#include "controllers/HomeController.h"
#include "controllers/LibraryController.h"
#include "controllers/LiveUpdateService.h"
#include "controllers/PlayerController.h"
#include "controllers/SearchController.h"
#include "controllers/SeriesController.h"
#include "controllers/SessionController.h"
#include "music/MusicPlayback.h"
#include "remote/DiscoveryResponder.h"
#include "remote/HostLink.h"
#include "remote/TimeService.h"
#include "remote/WebRemoteController.h"
#include "remote/WebRemoteServer.h"
#include "core/Settings.h"
#include "input/InputMap.h"
#include "input/KeyEventLogger.h"
#include "input/KeyDelivery.h"
#include "input/OnScreenKeyboard.h"
#include "input/PointerIdle.h"
#include "input/RemoteBackKeyFilter.h"
#include "input/RemoteOkKeyFilter.h"
#include "input/SkipKeyFilter.h"

#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QStringList>
#include <QJsonObject>
#include <QSysInfo>
#include <QUrl>
#include <QWindow>
#ifdef STRMQT_HAVE_YOUTUBE
#include "app/YouTubeSignIn.h"

#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#endif

#include <utility>

int main(int argc, char *argv[])
{
    // ARCHITECTURE.md: pin the Qt scene graph to OpenGL so the libmpv render API can
    // share the GL context when video lands (M3). Explicit env override wins.
    if (qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND"))
        qputenv("QSG_RHI_BACKEND", "opengl");

#ifdef STRMQT_HAVE_YOUTUBE
    // Has to precede the application object: it shares the scene graph's GL
    // context with Chromium's. Nothing of the engine starts until the YouTube
    // page is first opened.
    QtWebEngineQuick::initialize();
#endif

    strmqt::Application app(argc, argv);
    // QT_LOGGING_RULES="strmqt.input.keys.debug=true": every key, with its
    // native codes, for identifying a remote's buttons. Installed only when
    // asked for, so an ordinary run does not filter every event for nothing.
    if (logKeys().isDebugEnabled())
        app.installEventFilter(new strmqt::KeyEventLogger(&app));
    app.session()->restore();

    // Controllers QML reads are declared before the engine so that they are
    // destroyed after it: QML still evaluating during teardown found one
    // already gone.
    strmqt::StreamingHost streamingHost;
    strmqt::OnScreenKeyboard onScreenKeyboard;

    // A StrmLight pairs with the streaming host's StrmQt, with one code for
    // its stream and its remote. That StrmQt answers StrmLights looking for
    // it, takes codes typed into the desktop StrmQt, and gives its Sunshine
    // the same code; the desktop StrmQt passes codes on to it.
    strmqt::HostLink hostLink;
    // The host's clock, which StrmLights playing together line up on.
    strmqt::TimeService timeService;
    strmqt::DiscoveryResponder discovery([&app, &streamingHost, &timeService] {
        QJsonObject reply{
            {QStringLiteral("name"), QSysInfo::machineHostName()},
            {QStringLiteral("remotePort"), app.settings()->webRemotePort()},
            {QStringLiteral("sunshinePort"), streamingHost.port()},
            {QStringLiteral("fingerprint"), app.webRemote()->certFingerprint()},
        };
        if (timeService.isRunning())
            reply.insert(QStringLiteral("timePort"), timeService.port());
        return reply;
    });
    if (app.settings()->isStreamingHostInstance()) {
        app.webRemote()->setSunshinePairing([&streamingHost](const QString &code) {
            streamingHost.enterPinWhenAsked(code, strmqt::WebRemoteServer::kPairingCodeSeconds);
        });
        // A StrmLight streams only from the Sunshine this StrmQt vouches for,
        // with the settings it gives, and this StrmQt keeps nothing but
        // StrmLights paired with it.
        app.webRemote()->setStrmLightHost([&streamingHost, &timeService] {
            QJsonObject host = streamingHost.strmLightHost();
            if (timeService.isRunning())
                host.insert(QStringLiteral("timePort"), timeService.port());
            return host;
        });
        // KWin keeps the screen's scale; set again in case its folder is new.
        QTimer::singleShot(3 * 1000, &streamingHost, &strmqt::StreamingHost::applyScale);
        auto *pairingCheck = new QTimer(&app);
        QObject::connect(pairingCheck, &QTimer::timeout, &streamingHost, &strmqt::StreamingHost::checkPairing);
        pairingCheck->start(60 * 1000);
        QTimer::singleShot(15 * 1000, &streamingHost, &strmqt::StreamingHost::checkPairing);
        hostLink.listen([&app](const QString &code) { return app.webRemote()->pairStrmLight(code); });
        timeService.start(quint16(strmqt::streaming::kTimePort));
        discovery.start(quint16(app.settings()->webRemotePort()));
    } else {
        app.webRemote()->setHostLink(&hostLink);
    }
    // Both go before the application object does.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&app] {
        app.webRemote()->setHostLink(nullptr);
        app.webRemote()->setSunshinePairing({});
    });

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("emby"),
                            new strmqt::EmbyImageProvider(app.imageFetcher()));
    engine.rootContext()->setContextProperty(QStringLiteral("Images"), app.imageFetcher());
    engine.rootContext()->setContextProperty(QStringLiteral("Session"), app.session());
    engine.rootContext()->setContextProperty(QStringLiteral("HomeCtl"), app.home());
    engine.rootContext()->setContextProperty(QStringLiteral("LibraryCtl"), app.library());
    engine.rootContext()->setContextProperty(QStringLiteral("PlayerCtl"), app.player());
    engine.rootContext()->setContextProperty(QStringLiteral("SearchCtl"), app.search());
    engine.rootContext()->setContextProperty(QStringLiteral("SeriesCtl"), app.series());
    engine.rootContext()->setContextProperty(QStringLiteral("DetailsCtl"), app.details());
    engine.rootContext()->setContextProperty(QStringLiteral("Actions"), app.actions());
    engine.rootContext()->setContextProperty(QStringLiteral("Input"), app.input());
    // Main.qml owns the visible interaction surface and publishes it back to
    // Application, which routes hardware input from that answer.
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &app);
    // Theme reads density and accent from here; SettingsPage writes them.
    engine.rootContext()->setContextProperty(QStringLiteral("Prefs"), app.settings());
    engine.rootContext()->setContextProperty(QStringLiteral("LiveCtl"), app.live());
    engine.rootContext()->setContextProperty(QStringLiteral("RemoteCtl"), app.remote());
    engine.rootContext()->setContextProperty(QStringLiteral("WebRemoteCtl"), app.webRemote());
    engine.rootContext()->setContextProperty(QStringLiteral("PlaylistCtl"), app.playlists());
    // Every music play verb (Crate spec §3.6): album, shuffle, radio, stations.
    engine.rootContext()->setContextProperty(QStringLiteral("MusicPlay"), app.musicPlayback());
    // The music player's display values: record state, readout, album, lyrics.
    engine.rootContext()->setContextProperty(QStringLiteral("NowPlayingMusicCtl"), app.nowPlayingMusic());
    engine.rootContext()->setContextProperty(QStringLiteral("MusicHomeCtl"), app.musicHome());
    engine.rootContext()->setContextProperty(QStringLiteral("MusicBrowseCtl"), app.musicBrowse());
    engine.rootContext()->setContextProperty(QStringLiteral("AlbumCtl"), app.albumController());
    engine.rootContext()->setContextProperty(QStringLiteral("ArtistCtl"), app.artistController());
    // The cover wash (MUSIC.md §4): Theme re-exports its opacity ceiling, and
    // CoverWash.qml reads the tints themselves.
    engine.rootContext()->setContextProperty(QStringLiteral("CoverTint"), app.coverTint());
    // Settings' "Streaming host" panel. It looks at nothing until asked.
    engine.rootContext()->setContextProperty(QStringLiteral("StreamingHostCtl"), &streamingHost);
    // Typing for a pad, a TV remote and the phone (input/OnScreenKeyboard.h);
    // it watches the window once there is one, below.
    engine.rootContext()->setContextProperty(QStringLiteral("OnScreenKeyboardCtl"),
                                             &onScreenKeyboard);
    // Self-test mode (STRMQT_SELFTEST=1): Main.qml constructs every page
    // component and exits non-zero if any fails.
    //
    // This exists because the ordinary offscreen smoke run does NOT prove the
    // pages are constructible. StackView only ever builds its initialItem, so a
    // page reachable by a push is never instantiated and a QML type error in it
    // survives a clean-looking startup — which is exactly how M7 shipped a
    // broken PlayerPage.
    // Whether this build carries the YouTube tab (STRMQT_WITH_YOUTUBE). The
    // rail entry and YouTubePage ask.
#ifdef STRMQT_HAVE_YOUTUBE
    const bool youtube = true;
    // Settings' "YouTube Music" panel and the shell (Main.qml) meet here.
    YouTubeSignIn youtubeSignIn;
    engine.rootContext()->setContextProperty(QStringLiteral("YouTubeSignIn"), &youtubeSignIn);
#else
    const bool youtube = false;
#endif
    // In the build AND switched on (Settings → YouTube): a trailer
    // or a search asked for while it is off goes where it went before there
    // was a YouTube tab.
    const auto applyYouTube = [&app, youtube] {
        app.actions()->setYouTubeAvailable(youtube && app.settings()->youtubeEnabled());
    };
    applyYouTube();
    QObject::connect(app.settings(), &strmqt::Settings::youtubeEnabledChanged, &app, applyYouTube);
    engine.rootContext()->setContextProperty(QStringLiteral("YouTubeAvailable"), youtube);
    engine.rootContext()->setContextProperty(
        QStringLiteral("SelfTest"),
        qEnvironmentVariableIsSet("STRMQT_SELFTEST"));

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule("StrmQt", "Main");
#else
    // loadFromModule is Qt 6.5, and 6.4's default import path lacks
    // qrc:/qt/qml. RESOURCE_PREFIX pins the module there on every Qt
    // (src/CMakeLists.txt), so one URL serves (spec 2026-09-27 §4.1).
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/StrmQt/ui/Main.qml")));
#endif

    // The remote and the gamepad drive this window while another one is
    // active; it has to keep its focused item for their keys to land.
    const QList<QObject *> roots = engine.rootObjects();
    // A Bluetooth remote's OK button arrives as Select or XF86OK, not Return,
    // and its Back as Qt::Key_Back, which only the page history listened for.
    if (auto *window = qobject_cast<QWindow *>(roots.value(0))) {
        // The streaming host's screen is all StrmQt; Main.qml's 1600×900 left
        // most of the streamed picture empty.
        if (app.settings()->isStreamingHostInstance())
            window->showFullScreen();
        window->installEventFilter(new strmqt::WindowFocusKeeper(window));
        // A pointer left where it was while the user picked up a pad stops
        // hovering things and is no longer drawn (input/PointerIdle.h).
        new strmqt::PointerIdle(window, window);
        // Answers the arrow no control claimed and re-seats focus that is on
        // nothing (ARCHITECTURE.md §5, "Where an arrow goes").
        if (auto *quickWindow = qobject_cast<QQuickWindow *>(window)) {
            auto *navigator = new strmqt::FocusNavigator(quickWindow, quickWindow);
            app.setNavigationModal(navigator->modalActive());
            QObject::connect(navigator, &strmqt::FocusNavigator::modalActiveChanged, &app,
                             [&app, navigator] { app.setNavigationModal(navigator->modalActive()); });
        }
        // Installed before the remote's filters so that it runs after them:
        // it sees a remote's OK and Back as the Return and Esc they become.
        onScreenKeyboard.attach(window);
        window->installEventFilter(new strmqt::RemoteOkKeyFilter(window));
        window->installEventFilter(new strmqt::RemoteBackKeyFilter(app.input(), window));
        window->installEventFilter(
            new strmqt::keydelivery::DeliveredKeyStamp(app.input(), window));
        // ⏭ / ⏮: chapter first, then the queue — the same rule MPRIS
        // Next/Previous run (Application::wirePlaybackIntegrations) — and ⏭
        // held fast-forwards.
        strmqt::PlayerController *player = app.player();
        auto *skipKeys = new strmqt::SkipKeyFilter(
            app.input(),
            {[player] { return player->active(); }, [player] { player->skipForward(); },
             [player] { player->skipBack(); }, [player] { player->skipForwardPressed(); },
             [player] { player->skipForwardReleased(); },
             [player] { player->cancelSkipForwardHold(); }},
            window);
        window->installEventFilter(skipKeys);
        app.input()->registerHandler(skipKeys);
    }

    // Named wiring guard (P4-R11). No test constructs strmqt::Application, so
    // deleting the album or artist context property above, or either
    // ItemActions::favoriteChanged connect in Application.cpp, leaves the whole
    // suite and the page self-test green while the heart on the Crate album and
    // artist pages silently stops following a favourite toggle. The self-test
    // is the only run that builds the real object graph, so it is the only
    // place that can see this wiring at all.
    if (qEnvironmentVariableIsSet("STRMQT_SELFTEST")) {
        QStringList wiring;
        const auto requireContextObject = [&](const char *name) {
            const QVariant value = engine.rootContext()->contextProperty(QLatin1String(name));
            if (!value.isValid() || value.value<QObject *>() == nullptr)
                wiring << QStringLiteral("context property %1 is not an object")
                              .arg(QLatin1String(name));
        };
        requireContextObject("AlbumCtl");
        requireContextObject("ArtistCtl");
        requireContextObject("NowPlayingMusicCtl");

        // Qt::UniqueConnection answers "is this exact connection already there?"
        // without disturbing it: connect() returns an invalid handle when it is.
        // A missing connect is therefore made *and* reported — the run still
        // exits non-zero, so the repair never hides the deletion.
        if (QObject::connect(app.actions(), &strmqt::ItemActions::favoriteChanged,
                             app.albumController(),
                             &strmqt::music::AlbumController::noteFavourite,
                             Qt::UniqueConnection))
            wiring << QStringLiteral("ItemActions::favoriteChanged is not connected to "
                                     "AlbumController::noteFavourite");
        if (QObject::connect(app.actions(), &strmqt::ItemActions::favoriteChanged,
                             app.artistController(),
                             &strmqt::music::ArtistController::noteFavourite,
                             Qt::UniqueConnection))
            wiring << QStringLiteral("ItemActions::favoriteChanged is not connected to "
                                     "ArtistController::noteFavourite");

        if (!wiring.isEmpty()) {
            for (const QString &failure : std::as_const(wiring))
                qWarning().noquote() << "selftest FAIL music wiring:" << failure;
            return 1;
        }
        qInfo().noquote() << "selftest ok   music wiring";
    }

    return app.exec();
}
