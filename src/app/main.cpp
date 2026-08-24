#include "Application.h"
#include "controllers/MusicController.h"
#include "controllers/PlaylistController.h"
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
#include "core/Settings.h"
#include "input/InputMap.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>

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
    engine.rootContext()->setContextProperty(QStringLiteral("PlaylistCtl"), app.playlists());
    engine.rootContext()->setContextProperty(QStringLiteral("MusicCtl"), app.music());
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

    return app.exec();
}
