#include "Application.h"
#include "WindowFocusKeeper.h"
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
#include "remote/WebRemoteController.h"
#include "core/Settings.h"
#include "input/InputMap.h"
#include "input/RemoteBackKeyFilter.h"
#include "input/RemoteOkKeyFilter.h"

#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
#include <QWindow>

#include <utility>

int main(int argc, char *argv[])
{
    // ARCHITECTURE.md: pin the Qt scene graph to OpenGL so the libmpv render API can
    // share the GL context when video lands (M3). Explicit env override wins.
    if (qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND"))
        qputenv("QSG_RHI_BACKEND", "opengl");

    strmqt::Application app(argc, argv);
    app.session()->restore();

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
    // Self-test mode (STRMQT_SELFTEST=1): Main.qml constructs every page
    // component and exits non-zero if any fails.
    //
    // This exists because the ordinary offscreen smoke run does NOT prove the
    // pages are constructible. StackView only ever builds its initialItem, so a
    // page reachable by a push is never instantiated and a QML type error in it
    // survives a clean-looking startup — which is exactly how M7 shipped a
    // broken PlayerPage.
    engine.rootContext()->setContextProperty(
        QStringLiteral("SelfTest"),
        qEnvironmentVariableIsSet("STRMQT_SELFTEST"));

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("StrmQt", "Main");

    // The remote and the gamepad drive this window while another one is
    // active; it has to keep its focused item for their keys to land.
    const QList<QObject *> roots = engine.rootObjects();
    // A Bluetooth remote's OK button arrives as Select or XF86OK, not Return,
    // and its Back as Qt::Key_Back, which only the page history listened for.
    if (auto *window = qobject_cast<QWindow *>(roots.value(0))) {
        window->installEventFilter(new strmqt::WindowFocusKeeper(window));
        window->installEventFilter(new strmqt::RemoteOkKeyFilter(window));
        window->installEventFilter(new strmqt::RemoteBackKeyFilter(app.input(), window));
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
