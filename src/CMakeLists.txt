# strmqt_core: settings/session/logging + backend seams. Must stay free of QtGui
# (AGENTS.md layout rules) so tests and the future CLI probe can link it headless.
qt_add_library(strmqt_core STATIC
    core/Log.h core/Log.cpp
    core/Settings.h core/Settings.cpp
    server/MediaServerBackend.h server/MediaServerBackend.cpp
    server/dto/ItemsPage.h
    server/dto/Library.h
    server/dto/MediaItem.h
    server/dto/MediaSource.h
    server/dto/SessionInfo.h
    server/dto/UserProfile.h
    server/dto/ItemDetails.h
    server/dto/ItemsQuery.h
    server/dto/PlaybackTicket.h server/dto/PlaybackTicket.cpp
    server/dto/ServerInfo.h
    server/emby/EmbyDtoMapper.h server/emby/EmbyDtoMapper.cpp
    server/emby/EmbyClient.h server/emby/EmbyClient.cpp
    server/emby/EmbyWebSocket.h server/emby/EmbyWebSocket.cpp
    core/Result.h
    platform/HdrSupport.h platform/HdrSupport.cpp
    platform/MprisPlayer.h platform/MprisPlayer.cpp
    platform/PowerInhibit.h platform/PowerInhibit.cpp
    platform/SecretsStore.h platform/SecretsStore.cpp
    playback/PlayerBackend.h playback/PlayerBackend.cpp
)
target_include_directories(strmqt_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(strmqt_core PUBLIC Qt6::Core Qt6::Network Qt6::DBus Qt6::WebSockets)
target_compile_definitions(strmqt_core PUBLIC STRMQT_VERSION="${PROJECT_VERSION}")

# App services layer: QML-facing models and controllers (QtCore only, testable).
qt_add_library(strmqt_app STATIC
    app/models/HomeRailModel.h app/models/HomeRailModel.cpp
    app/models/MediaItemModel.h app/models/MediaItemModel.cpp
    app/models/LibraryListModel.h app/models/LibraryListModel.cpp
    app/controllers/SessionController.h app/controllers/SessionController.cpp
    app/controllers/HomeController.h app/controllers/HomeController.cpp
    app/controllers/LibraryController.h app/controllers/LibraryController.cpp
    app/controllers/PlayerController.h app/controllers/PlayerController.cpp
    app/controllers/SearchController.h app/controllers/SearchController.cpp
    app/controllers/SeriesController.h app/controllers/SeriesController.cpp
    app/controllers/DetailsController.h app/controllers/DetailsController.cpp
    app/ItemActions.h app/ItemActions.cpp
    app/PlayQueue.h app/PlayQueue.cpp
    app/controllers/LiveUpdateService.h app/controllers/LiveUpdateService.cpp
    app/controllers/RemoteControlService.cpp
    app/controllers/PlaylistController.cpp
    app/controllers/MusicController.cpp
    input/InputMap.h input/InputMap.cpp
)
target_link_libraries(strmqt_app PUBLIC strmqt_core)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
# libVLC is the fallback engine only (PLAN §3.2): mpv is primary, so a distro that
# does not want a VLC dependency can drop it and still get a working player.
option(STRMQT_WITH_VLC "Build the libVLC fallback playback engine" ON)
if(STRMQT_WITH_VLC)
    pkg_check_modules(LIBVLC IMPORTED_TARGET libvlc)
endif()
# SDL3 is optional (PLAN §3.2): the app must build and run keyboard-only without it.
option(STRMQT_WITH_SDL3 "Build SDL3 gamepad support" ON)
if(STRMQT_WITH_SDL3)
    pkg_check_modules(SDL3 IMPORTED_TARGET sdl3)
endif()

qt_add_executable(strmqt
    app/main.cpp
    app/Application.h app/Application.cpp
    app/EmbyImageProvider.h app/EmbyImageProvider.cpp
    # QtGui (QImage/QColor), so deliberately NOT in strmqt_app, which stays
    # QtCore-only. tests/CMakeLists.txt compiles CoverTint.cpp into its own
    # target for the same reason.
    app/CoverTint.h app/CoverTint.cpp
    app/CoverTintService.h app/CoverTintService.cpp
    playback/mpv/MpvPlayer.h playback/mpv/MpvPlayer.cpp
    playback/mpv/MpvVideoItem.h playback/mpv/MpvVideoItem.cpp
)
if(LIBVLC_FOUND)
    target_sources(strmqt PRIVATE
        playback/vlc/VlcPlayer.h playback/vlc/VlcPlayer.cpp
        playback/vlc/VlcVideoItem.h playback/vlc/VlcVideoItem.cpp)
    target_compile_definitions(strmqt PRIVATE STRMQT_HAVE_VLC)
endif()
if(SDL3_FOUND)
    target_sources(strmqt PRIVATE input/GamepadManager.h input/GamepadManager.cpp)
    target_compile_definitions(strmqt PRIVATE STRMQT_HAVE_SDL3)
endif()

# Bundled typefaces (SIL OFL): Archivo / Public Sans / IBM Plex Mono. Compiled
# into the binary so the Flatpak and AppImage render identically to a native
# build, which cannot be assumed of host fontconfig.
qt_add_resources(strmqt "strmqt_fonts"
    PREFIX "/fonts"
    BASE ${CMAKE_SOURCE_DIR}/assets/fonts
    FILES
        ${CMAKE_SOURCE_DIR}/assets/fonts/Archivo[wdth,wght].ttf
        ${CMAKE_SOURCE_DIR}/assets/fonts/PublicSans[wght].ttf
        ${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexMono-Regular.ttf
        ${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexMono-Medium.ttf
        ${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexMono-SemiBold.ttf
)

# UI icon set (hand-authored SVGs, assets/icons/ui → qrc:/icons). Globbed with
# CONFIGURE_DEPENDS so adding an icon does not mean editing this file; a missing
# icon then shows as an empty Image rather than a configure error, which is why
# StrmIcon logs unknown names.
file(GLOB STRMQT_UI_ICONS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/icons/ui/*.svg)
if(STRMQT_UI_ICONS)
    qt_add_resources(strmqt "strmqt_icons"
        PREFIX "/icons"
        BASE ${CMAKE_SOURCE_DIR}/assets/icons/ui
        FILES ${STRMQT_UI_ICONS}
    )
endif()

set_source_files_properties(ui/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(ui/MediaKinds.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(ui/NowPlayingInfo.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(strmqt
    URI StrmQt
    VERSION 1.0
    QML_FILES
        ui/Main.qml
        ui/Theme.qml
        ui/MediaKinds.qml
        ui/NowPlayingInfo.qml
        ui/components/CoverWash.qml
        ui/components/MpvVideoPlane.qml
        ui/pages/LoginPage.qml
        ui/pages/HomePage.qml
        ui/pages/LibraryPage.qml
        ui/pages/DetailsPage.qml
        ui/pages/PlayerPage.qml
        ui/pages/SearchPage.qml
        ui/pages/SeriesPage.qml
        ui/pages/SettingsPage.qml
        ui/pages/ArtistPage.qml
        ui/pages/AlbumPage.qml
        ui/pages/MusicPage.qml
        ui/pages/PlaylistPage.qml
        ui/pages/PersonPage.qml
)

# Shared control library (ARCHITECTURE.md). Owned as its own CMake fragment so the
# component set can grow without touching this file.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ui/controls/Controls.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/ui/controls/Controls.cmake)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ui/player/Player.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/ui/player/Player.cmake)
endif()

qt_target_qml_sources(strmqt QML_FILES
    ui/settings/SettingsSections.qml
)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ui/shell/Shell.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/ui/shell/Shell.cmake)
endif()

# The VLC video plane is a separate QML file so that a build without libvlc simply
# lacks it: PlayerPage loads the plane by URL and never names VlcVideo directly.
if(LIBVLC_FOUND)
    qt_target_qml_sources(strmqt QML_FILES ui/components/VlcVideoPlane.qml)
endif()

# qmltyperegistrar includes QML_ELEMENT headers by bare file name.
target_include_directories(strmqt PRIVATE playback/mpv)
target_link_libraries(strmqt PRIVATE strmqt_app Qt6::Gui Qt6::Quick Qt6::QuickControls2
    Qt6::OpenGL PkgConfig::MPV)
if(LIBVLC_FOUND)
    target_include_directories(strmqt PRIVATE playback/vlc)
    target_link_libraries(strmqt PRIVATE PkgConfig::LIBVLC)
endif()
if(SDL3_FOUND)
    target_link_libraries(strmqt PRIVATE PkgConfig::SDL3)
endif()

# Headless Emby probe tool (M1 acceptance). Core-only: no QtGui.
qt_add_executable(strmqt-cli cli/main.cpp)
target_link_libraries(strmqt-cli PRIVATE strmqt_core)

# ── Install ────────────────────────────────────────────────────────────────────
# Both QML modules are compiled into the strmqt executable's resources
# (qmlcachegen + rcc), so the binaries are self-contained: no qmldir, no .qml,
# no qmltypes need installing. strmqt_core / strmqt_app are internal static
# libraries with no public headers and are deliberately NOT installed.
install(TARGETS strmqt strmqt-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# AppImage-only Qt bundling. OFF for distro packages (PKGBUILD, Flatpak) so a
# native install never ships a private copy of Qt.
option(STRMQT_APPIMAGE_DEPLOY "Bundle Qt/QML runtime for the AppImage" OFF)
if(STRMQT_APPIMAGE_DEPLOY)
    include(${CMAKE_SOURCE_DIR}/packaging/appimage/Deploy.cmake)
endif()
