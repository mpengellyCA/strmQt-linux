cmake_minimum_required(VERSION 3.25)

project(StrmQt VERSION 0.7.5 LANGUAGES CXX)

option(STRMQT_WERROR "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CMAKE_INSTALL_BINDIR / _DATADIR for the install rules below and in src/.
include(GNUInstallDirs)

# Canonical run path is ./build/<preset>/strmqt (AGENTS.md).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Qt 6.4.2 is the floor: Ubuntu 24.04 LTS and Debian 12 ship exactly that, and
# no supported distro ships 6.5-6.7 (spec 2026-09-27 §2). Qt 6.8+ is the full
# tier; older Qt builds the compat QML shims (src/ui/shims, spec §3).
find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core DBus Gui Network OpenGL Quick QuickControls2 Test WebSockets)
# REQUIRES and the QTP policies are Qt 6.5+. Guarding at 6.8 keeps the full
# tier's policy set exactly what it was.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt_standard_project_setup(REQUIRES 6.8)
else()
    qt_standard_project_setup()
endif()
include(${CMAKE_SOURCE_DIR}/cmake/StrmQtQmlTier.cmake)

find_package(OpenSSL REQUIRED)

add_compile_options(-Wall -Wextra)
if(STRMQT_WERROR)
    add_compile_options(-Werror)
endif()

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)

# ── Desktop integration ────────────────────────────────────────────────────────
# Reverse-DNS app id: desktop file basename, AppStream component id, icon name
# and Wayland app_id must all match for KWin to associate window ↔ desktop entry.
set(STRMQT_APPID "ca.mikesdev.StrmQt")

install(FILES packaging/desktop/${STRMQT_APPID}.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/appstream/${STRMQT_APPID}.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES assets/icons/${STRMQT_APPID}.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
# Explicit sizes, not a glob: a missing raster icon must be a hard install error.
foreach(size IN ITEMS 32 48 64 128 256 512)
    install(FILES assets/icons/hicolor/${size}x${size}/apps/${STRMQT_APPID}.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
endforeach()
