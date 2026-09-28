# SDL3 for the gamepad path (spec 2026-09-27 §5). The distro's SDL3 by default;
# with STRMQT_BUNDLE_SDL3=ON (Ubuntu 24.04 and Debian 12 packages, the
# AppImage) a pinned 3.4.16 tarball, built static with the gamepad subsystems
# only. A distro build never fetches anything it did not ask for.
option(STRMQT_BUNDLE_SDL3 "Fetch and statically link SDL3 3.4.16 (for distros without SDL3)" OFF)

set(STRMQT_HAVE_SDL3 OFF)
if(NOT STRMQT_WITH_SDL3)
    return()
endif()

if(STRMQT_BUNDLE_SDL3)
    include(FetchContent)
    # The gamepad path needs JOYSTICK, HAPTIC, HIDAPI, SENSOR and EVENTS; the
    # rest is off so the static archive stays near 2 MB and pulls no X11,
    # Wayland, audio or GPU dependency into StrmQt's link.
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    # SDL's sources are not ours to hold to -Werror (STRMQT_WERROR).
    set(SDL_WERROR OFF CACHE BOOL "" FORCE)
    foreach(_sub AUDIO VIDEO RENDER GPU CAMERA DIALOG TRAY)
        set(SDL_${_sub} OFF CACHE BOOL "" FORCE)
    endforeach()
    # With no X11 or Wayland backend SDL's configure stops unless told this is
    # deliberate (its docs/README-cmake.md): StrmQt never opens an SDL window.
    set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
        URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68)
    # The top-level add_compile_options(-Werror) is directory-scoped, so SDL
    # would inherit it. Strip it for the subproject only, then put it back.
    get_directory_property(_strmqt_saved_compile_options COMPILE_OPTIONS)
    set(_strmqt_sdl3_compile_options ${_strmqt_saved_compile_options})
    list(REMOVE_ITEM _strmqt_sdl3_compile_options -Werror)
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_strmqt_sdl3_compile_options}")
    # qt_standard_project_setup() turns AUTOMOC/AUTOUIC on for every target,
    # SDL's included. SDL has no Qt, and on CMake 3.25 (Debian 12) the autogen
    # mocs_compilation.cpp then wants a C++ PCH its C target has no rule for.
    foreach(_auto AUTOMOC AUTOUIC AUTORCC)
        set(_strmqt_saved_${_auto} ${CMAKE_${_auto}})
        set(CMAKE_${_auto} OFF)
    endforeach()
    # No EXCLUDE_FROM_ALL argument: it needs CMake 3.28 and the floor is 3.25.
    # SDL_INSTALL=OFF keeps it out of the install tree instead.
    FetchContent_MakeAvailable(SDL3)
    foreach(_auto AUTOMOC AUTOUIC AUTORCC)
        set(CMAKE_${_auto} ${_strmqt_saved_${_auto}})
    endforeach()
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_strmqt_saved_compile_options}")
    set(STRMQT_SDL3_TARGET SDL3::SDL3-static)
    set(STRMQT_HAVE_SDL3 ON)
    # share/doc/strmqt, the package-named directory, not CMAKE_INSTALL_DOCDIR
    # (share/doc/StrmQt, from project()).
    install(FILES ${sdl3_SOURCE_DIR}/LICENSE.txt
            DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/strmqt RENAME SDL3-LICENSE.txt)
    message(STATUS "SDL3: bundled 3.4.16 (static)")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 IMPORTED_TARGET sdl3)
    if(SDL3_FOUND)
        set(STRMQT_SDL3_TARGET PkgConfig::SDL3)
        set(STRMQT_HAVE_SDL3 ON)
        message(STATUS "SDL3: system ${SDL3_VERSION}")
    else()
        message(STATUS "SDL3: not found; gamepad support disabled "
                       "(set STRMQT_BUNDLE_SDL3=ON to bundle it)")
    endif()
endif()
