# packaging/appimage/Deploy.cmake — AppImage-only Qt/QML runtime bundling.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Included from src/CMakeLists.txt ONLY when -DSTRMQT_APPIMAGE_DEPLOY=ON. Distro
# packages (PKGBUILD, Flatpak) must never see this file run: they link the host's
# Qt and a private copy would be both wasteful and wrong.
#
# ==============================================================================
# WHY PRE_EXCLUDE_REGEXES IS LOAD-BEARING, NOT POLISH
# ==============================================================================
#
# Qt advertises a guard that stops qt_deploy_runtime_dependencies() from copying
# system libraries out of the host. It is INERT on a distro Qt. See
# /usr/lib/cmake/Qt6Core/Qt6CoreMacros.cmake:3201-3216:
#
#     foreach(link_dir IN LISTS CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES)
#         file(RELATIVE_PATH relative_dir "${QT6_INSTALL_PREFIX}" "${link_dir}")
#         ...
#         if(IS_ABSOLUTE "${relative_dir}" OR relative_dir MATCHES "^\\.\\./")
#             list(APPEND deploy_ignored_lib_dirs "${link_dir}")   # only then
#         endif()
#     endforeach()
#
# A link dir is ignored only when it lies OUTSIDE the Qt install prefix. On Arch
# that prefix is /usr (measured, not assumed: `QT6_INSTALL_PREFIX=/usr`,
# `CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES=/usr/lib/gcc/x86_64-pc-linux-gnu/16;/usr/lib;/lib`).
# Walk it:
#     /usr/lib/gcc/.../16 -> relative "lib/gcc/.../16"  -> NOT ignored
#     /usr/lib            -> relative "lib"             -> NOT ignored
#     /lib                -> relative "../lib"          -> ignored, but /lib is a
#                            symlink to /usr/lib and GET_RUNTIME_DEPENDENCIES
#                            reports realpaths, so this entry never matches.
#
# Net effect: QT_DEPLOY_IGNORED_LIB_DIRS is effectively empty. Left at defaults,
# the deploy step happily copies libc, ld-linux, libGL, libEGL, libdrm, libva,
# libvulkan, libwayland-client and libfontconfig into the AppDir. The result is
# an AppImage that cannot create a GL context (bundled libGL vs host DRI driver),
# cannot hardware-decode (bundled libva vs host /usr/lib/dri/*_drv_video.so), and
# renders no text (bundled fontconfig vs host font cache). The lists below are
# the ONLY thing standing between us and that. Do not trim them for tidiness.
#
# BUNDLING RULE (apply this when adding anything):
#   BUNDLE   pure-userspace codec/render code: libmpv, the ffmpeg libs,
#            libplacebo, libass, libzimg, dav1d and friends, Qt itself,
#            libstdc++/libgcc_s.
#   NEVER    anything that talks to a kernel device, the display server, the
#   BUNDLE   audio server, or the font database. Those must be the host's,
#            because the *other half* of the conversation is the host's.
#
# ==============================================================================
# WHY `INCLUDE_PLUGINS qwayland` IS MANDATORY
# ==============================================================================
#
# /usr/lib/qt6/modules/Gui.json declares:
#     "qpa": { "platforms": ["xcb"], "default_platform": "xcb" }
#
# and Qt6CoreDeploySupport.cmake:360 removes `platforms` from bulk plugin
# selection outright:
#     list(REMOVE_ITEM selected_plugin_types ${arg_EXCLUDE_BY_TYPE} platforms)
#
# QPA plugins are then re-added (line 397-400) from *only* that JSON `platforms`
# array, prefixed with "q" -> {qxcb}. So without an explicit INCLUDE_PLUGINS the
# AppImage ships libqxcb.so and nothing else, and on a Wayland session it starts
# silently under XWayland: blurry on fractional scaling, no native fullscreen
# handoff, no direct scanout. There is no warning. build-appimage.sh asserts on
# this because a silent regression here is invisible until a user reports "it
# looks fuzzy".
#
# ==============================================================================

# Fail early and legibly rather than three minutes into `cmake --install`.
# Qt fixes up deployed plugin RPATHs through _qt_internal_set_rpath(), which
# defaults to CMake's internal file(RPATH_SET). That can only REWRITE an
# existing DT_RPATH/DT_RUNPATH, never create one, and several Qt Wayland plugins
# (e.g. wayland-shell-integration/liblayer-shell.so) ship with no RPATH entry.
# The install then aborts with "No valid ELF RPATH or RUNPATH entry exists in
# the file". QT_DEPLOY_USE_PATCHELF=ON switches that code path to
# `patchelf --set-rpath`, which adds the entry when it is missing.
if(NOT QT_DEPLOY_USE_PATCHELF)
    message(FATAL_ERROR
        "STRMQT_APPIMAGE_DEPLOY requires -DQT_DEPLOY_USE_PATCHELF=ON.\n"
        "Without it, `cmake --install` fails partway through Qt's deploy step "
        "on any plugin that has no existing DT_RPATH/DT_RUNPATH.\n"
        "Use packaging/appimage/build-appimage.sh, which passes it.")
endif()

# The upstream Arch package ships 27 KImageFormats plugins (kimg_*.so) in Qt's
# imageformats dir. Deploying them drags in libheif, libavif, OpenEXR, libjxl,
# libraw and more, for formats Emby never serves — its artwork is JPEG/PNG/WebP.
#
# GOTCHA, verified experimentally: EXCLUDE_PLUGINS entries are run through
# get_filename_component(... NAME_WE) before being used as a regex
# (Qt6CoreDeploySupport.cmake:293-304). NAME_WE strips the LONGEST extension, so
# the natural-looking "kimg_.*" is silently mangled to "kimg_" and then matched
# as /kimg_$/, which matches NOTHING. Use a dot-free character class instead.
set(_strmqt_exclude_plugins
    kimg_[a-z0-9_]+       # all 27 KImageFormats plugins (see note above)
    KDEPlasmaPlatformTheme6  # would pull the whole Plasma/KF6 stack
    qgtk3                 # would pull GTK3 + glib + gdk-pixbuf
    # Image formats Emby does not serve. qjpeg/qpng/qgif/qwebp/qsvg/qico stay.
    qmng qjp2 qtiff qicns qtga qwbmp qpdf
)

# Whole plugin categories we never load.
#   qmltooling            - QML debugger/profiler backends; RelWithDebInfo ships
#                           no debug server and they pull Qt::QmlDebug.
#   egldeviceintegrations - EGLFS device backends: kernel/vendor specific, only
#                           meaningful on the host that owns the GPU stack.
#   generic               - legacy evdev/tslib/libinput input plugins; Wayland
#                           and xcb deliver input through the compositor.
#   networkinformation    - would pull NetworkManager/glib for a signal we never
#                           read.
#   accessiblebridge      - AT-SPI bridge, pulls dbus + atspi.
set(_strmqt_exclude_plugin_types
    qmltooling
    egldeviceintegrations
    generic
    networkinformation
    accessiblebridge
)

# Sonames that must resolve to the HOST at runtime. Matched by
# file(GET_RUNTIME_DEPENDENCIES) against the dependency name before resolution.
# Grouped by *why*, because the reason is what survives a Qt upgrade.
set(_strmqt_pre_exclude_regexes
    # --- libmpv: excluded for a TOOLING reason, not a bundling reason ---------
    # We very much DO want libmpv and its codec closure in the AppImage. It is
    # excluded here because Arch's libmpv.so.2 carries an absolute DT_NEEDED:
    #
    #     0x0000000000000001 (NEEDED)  Shared library: [/usr/lib/libmujs.so]
    #
    # and CMake's file(GET_RUNTIME_DEPENDENCIES) refuses to scan any object with
    # a path-valued DT_NEEDED. It does not warn or skip -- it aborts the whole
    # install with:
    #
    #     CMake Error at Qt6CoreDeploySupport.cmake:507 (file):
    #       file Paths to dependencies are not supported
    #
    # Measured, not assumed: this is reproducible with a bare `cmake -P` calling
    # GET_RUNTIME_DEPENDENCIES on the strmqt binary, and pre-staging a patched
    # copy via the DIRECTORIES argument does NOT avoid it (CMake reaches the
    # host /usr/lib/libmpv.so.2 before consulting DIRECTORIES). Adding
    # "^libmpv" makes the same call succeed with 79 resolved dependencies.
    #
    # So mpv and its closure are bundled by build-appimage.sh instead, with an
    # explicit denylist-pruned BFS that applies the same rules as this file.
    # If you remove this line, `cmake --install` will fail outright.
    "^libmpv"

    # --- dynamic loader + glibc ------------------------------------------------
    # Bundling ld-linux or libc is instant, total breakage: the AppImage's libc
    # and the host's NSS/locale/vDSO must be the same libc.
    "^ld-linux.*"
    "^libc\\.so.*"
    "^libm\\.so.*"
    "^libmvec\\.so.*"
    "^libdl\\.so.*"
    "^libpthread\\.so.*"
    "^librt\\.so.*"
    "^libutil\\.so.*"
    "^libresolv\\.so.*"
    "^libanl\\.so.*"
    "^libnsl.*"
    "^libnss_.*"

    # --- GPU / GL stack --------------------------------------------------------
    # libGL is a thin dispatch layer over the host's DRI driver in /usr/lib/dri.
    # A bundled libGL cannot load the host's driver -> no GL context -> no video.
    "^libGL\\.so.*"
    "^libGLX.*"
    "^libGLdispatch.*"
    "^libOpenGL.*"
    "^libGLESv[0-9].*"
    "^libEGL.*"
    "^libglapi.*"
    "^libgbm.*"
    "^libdrm.*"
    "^libLLVM.*"          # llvmpipe's backing store; never ours to ship

    # --- hardware video decode / compute --------------------------------------
    # libva dlopens the host's <driver>_drv_video.so and hands it kernel fds.
    #
    # ANCHORED DELIBERATELY. The obvious "^libva.*" is WRONG: it also swallows
    # libvapoursynth-script.so.0, which is a direct DT_NEEDED of Arch's
    # libmpv.so.2. Excluding it turns a 47 KB stub into a hard host dependency
    # on the `vapoursynth` package -- and the AppImage then refuses to start
    # with "cannot open shared object file" on any host without it. Match only
    # the real VA-API sonames: libva.so.2, libva-drm, libva-x11, libva-wayland.
    "^libva\\.so.*"
    "^libva-.*"
    "^libvdpau.*"
    "^libvulkan.*"
    "^libOpenCL.*"
    "^libnvidia.*"
    "^libcuda.*"
    "^libnvcuvid.*"
    "^libnvidia-ml.*"

    # --- display server --------------------------------------------------------
    # These carry a wire protocol to a running compositor/X server. The peer is
    # always the host's.
    "^libwayland-.*"
    "^libdecor.*"
    "^libxkbcommon.*"
    "^libxkbfile.*"
    "^libxcb.*"
    # The character class MUST include the hyphen. "^libX[A-Za-z0-9]*\\.so"
    # matches libX11.so.6 but NOT libX11-xcb.so.1 -- which libQt6XcbQpa.so needs,
    # so the bridge library leaked into the AppDir on the first build while
    # libX11 itself was correctly excluded. Any pattern of the form
    # lib<Foo>[class]*\.so in this file must allow '-' and '_'.
    "^libX[A-Za-z0-9_-]*\\.so.*"
    "^libICE.*"
    "^libSM.*"

    # --- audio / input servers -------------------------------------------------
    # Same argument: PulseAudio/PipeWire/JACK are IPC clients to a host daemon,
    # and SDL3 (our gamepad backend) talks to evdev and the compositor.
    "^libasound.*"
    "^libpulse.*"
    "^libpulsecommon.*"
    "^libpipewire.*"
    "^libspa-.*"
    "^libjack.*"
    "^libSDL.*"

    # --- font stack (excluded as a SET, never piecemeal) -----------------------
    # fontconfig/freetype/harfbuzz/fribidi share struct layouts and a cache
    # format. Bundle one and you get a mismatched pair; bundle fontconfig and it
    # reads the host's /var/cache/fontconfig with the wrong version stamp and
    # returns zero fonts -> an app that renders no text at all.
    "^libfontconfig.*"
    "^libfreetype.*"
    "^libharfbuzz.*"
    "^libfribidi.*"
    "^libgraphite2.*"
    "^libexpat.*"         # fontconfig's XML parser; part of that set

    # --- glib / GTK universe ---------------------------------------------------
    # Pulled in transitively by portals and theme plugins. Bundling glib breaks
    # GIO module loading and dbus session discovery.
    "^libglib-.*"
    "^libgobject-.*"
    "^libgio-.*"
    "^libgmodule-.*"
    "^libgthread-.*"
    "^libgdk_pixbuf.*"
    "^libgdk-.*"
    "^libgtk-.*"
    "^libcairo.*"
    "^libpango.*"
    "^libatk.*"

    # --- TLS / crypto / auth ---------------------------------------------------
    # A bundled libssl uses a bundled (or missing) trust store and silently
    # diverges from the host CA bundle. TLS errors are fatal in release builds
    # (AGENTS.md) so a wrong trust store is a hard outage, not a warning.
    "^libssl.*"
    "^libcrypto.*"
    "^libgnutls.*"
    "^libnettle.*"
    "^libhogweed.*"
    "^libgmp\\.so.*"
    "^libp11-kit.*"
    "^libtasn1.*"
    "^libgcrypt.*"
    "^libgpg-error.*"
    "^libkrb5.*"
    "^libk5crypto.*"
    "^libgssapi_krb5.*"
    "^libkrb5support.*"
    "^libcom_err.*"
    "^libkeyutils.*"
    "^libsasl2.*"
    "^libldap.*"
    "^liblber.*"

    # --- init / device / IPC ---------------------------------------------------
    "^libsystemd.*"
    "^libudev.*"
    "^libdbus-1.*"
    "^libcap\\.so.*"
    "^libcap-.*"          # libcap-ng; same hyphen trap as libX11-xcb
    "^libselinux.*"
    "^libapparmor.*"
    "^libseccomp.*"
    "^libelf.*"

    # --- ABI-frozen compression ------------------------------------------------
    # Present and ABI-stable on every target host for a decade. Bundling them
    # buys nothing and risks a version skew against host libs we did not bundle.
    # NOTE the "\\.so" anchors: they keep libzimg (which we DO bundle) out of the
    # libz rule, and libzstd out of it too.
    "^libz\\.so.*"
    "^libzstd.*"
    "^liblzma.*"
    "^libbz2.*"
    "^liblz4.*"
    "^libbrotli.*"
)

# NO_TRANSLATIONS      - the UI is English-only in 0.1.0; Qt's .qm files are dead
#                        weight and qt_deploy_translations() needs lconvert.
# NO_COMPILER_RUNTIME  - MSVC-only concept; a no-op here, passed for clarity.
# INCLUDE_PLUGINS      - see the qwayland rationale above. This is the single
#                        most important argument in this call.
qt_generate_deploy_qml_app_script(
    TARGET strmqt
    OUTPUT_SCRIPT strmqt_deploy_script
    NO_TRANSLATIONS
    NO_COMPILER_RUNTIME
    PRE_EXCLUDE_REGEXES ${_strmqt_pre_exclude_regexes}
    # qwayland  - the whole point; see the Gui.json analysis above.
    # qxcb      - kept deliberately (Qt adds it from Gui.json's default_platform)
    #             as the fallback for an X11 session. It brings libQt6XcbQpa with
    #             it; that is intended, and libX11/libxcb/libX11-xcb stay on the
    #             host where they belong.
    # qoffscreen/qminimal - not for users: they make the bundle smoke-testable
    #             (`QT_QPA_PLATFORM=offscreen ./StrmQt.AppImage`) without a
    #             compositor, which is the only way CI or an agent can prove the
    #             bundled Qt actually initialises. ~100 KB combined. Without
    #             them the AppImage aborts headless with "Could not find the Qt
    #             platform plugin", which looks identical to a real bundling
    #             failure and wastes an afternoon.
    INCLUDE_PLUGINS qwayland qoffscreen qminimal
    EXCLUDE_PLUGIN_TYPES ${_strmqt_exclude_plugin_types}
    EXCLUDE_PLUGINS ${_strmqt_exclude_plugins}
)

install(SCRIPT ${strmqt_deploy_script})

# Layout this produces under DESTDIR=$APPDIR, CMAKE_INSTALL_PREFIX=/usr
# (QT_DEPLOY_* defaults, confirmed against this Qt: QT6_INSTALL_PLUGINS is
# "lib/qt6/plugins", QT6_INSTALL_QML is "lib/qt6/qml", QT6_INSTALL_LIBS "lib"):
#
#   $APPDIR/usr/bin/strmqt, strmqt-cli, qt.conf   (qt.conf from GENERATE_QT_CONF)
#   $APPDIR/usr/lib/*.so*                         <- Qt + mpv + codec libs
#   $APPDIR/usr/lib/qt6/plugins/**                <- RPATH fixed up by Qt
#   $APPDIR/usr/lib/qt6/qml/**                    <- RPATH fixed up by Qt
#
# Qt rewrites the RPATH of *plugins* (Qt6CoreDeploySupport.cmake:592-602) but
# copies the *libraries* with a plain file(INSTALL ... FOLLOW_SYMLINK_CHAIN)
# (line 566) and never touches their RPATH. build-appimage.sh's patchelf pass 2
# exists precisely to close that gap.
