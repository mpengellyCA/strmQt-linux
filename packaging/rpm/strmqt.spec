# StrmQt RPM packaging (spec 2026-09-27 §7.3). Built in, and for, one Fedora
# release's container by scripts/ci/package-rpm.sh; not a Fedora-review spec.
Name:           strmqt
Version:        0.9.3
Release:        1%{?dist}
Summary:        Couch-first Emby client for the Linux desktop
License:        GPL-3.0-or-later AND OFL-1.1 AND MIT AND LGPL-3.0-or-later
URL:            https://github.com/mpengellyCA/strmQt-linux
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++ cmake ninja-build pkgconf git-core
BuildRequires:  qt6-rpm-macros
BuildRequires:  qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel
BuildRequires:  qt6-qtwebsockets-devel qt6-qtsvg-devel qt6-qtwebengine-devel sqlite-devel
BuildRequires:  mpv-devel vlc-devel SDL3-devel openssl-devel
# Runtime pieces for %%check's ctest and for the C(fedora-*) self-test (deps.sh installs these via dnf builddep):
BuildRequires:  qt6-qtsvg qt6-qtwayland vlc-plugins-base vlc-plugin-ffmpeg ffmpeg-free mesa-dri-drivers
BuildRequires:  vlc-plugins-extra

# The binary imports a Qt_6_PRIVATE_API symbol and carries qmlcachegen code
# for exactly this Qt: pin it. Fedora rebases Qt inside a release, so this
# holds the Qt update back until a rebuilt StrmQt exists (README).
Requires:       qt6-qtdeclarative%{?_isa} = %{_qt6_version}
Requires:       qt6-qtsvg%{?_isa} qt6-qtwayland%{?_isa}
# The YouTube tab: its QML module is loaded at run time, by URL, so nothing
# links against it for rpm to find.
Requires:       qt6-qtwebengine%{?_isa}
Requires:       vlc-plugins-base vlc-plugin-ffmpeg
# The VLC engine draws subtitles with libVLC's freetype text renderer, which
# Fedora ships in vlc-plugins-extra; without it a subtitle loads unseen.
Requires:       vlc-plugins-extra
# kf6-kwallet ships ksecretd/kwalletd6; Fedora's "kwallet" is the KDE 4 one.
Recommends:     (kf6-kwallet or gnome-keyring or keepassxc)
# HDR detection shells out to kscreen-doctor, which Fedora ships in libkscreen
# (the .deb recommends kscreen, Arch lists it as an optdepend).
Recommends:     libkscreen

%description
StrmQt is a native Qt 6 / QML client for Emby media servers, built to be
driven from the sofa: keyboard, gamepad, TV remote, KDE Connect or the
built-in web remote. Playback uses libmpv, with libvlc as a fallback.

%prep
%autosetup -n %{name}-%{version}

%build
# qt_standard_project_setup() gives the binaries an $ORIGIN RUNPATH for
# relocatable bundles; a system package links only system libraries (rpmlint
# binary-or-shlib-defines-rpath), so skip it.
%cmake -G Ninja -DSTRMQT_WERROR=OFF -DCMAKE_SKIP_INSTALL_RPATH=ON
%cmake_build

%install
%cmake_install

%check
export QT_QPA_PLATFORM=offscreen
%ctest

%files
%license COPYING assets/fonts/OFL-*.txt shaders/*.LICENSE
%doc README.md
%{_bindir}/strmqt
%{_bindir}/strmqt-cli
%{_datadir}/strmqt/
%{_datadir}/applications/ca.mikesdev.StrmQt.desktop
%{_metainfodir}/ca.mikesdev.StrmQt.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/ca.mikesdev.StrmQt.*

%changelog
* Mon Oct 19 2026 Mike Pengelly <mike@leadrix.io> - 0.9.3-1
- A TV that joins while the host's screen is still gets its picture at
  once, not green for up to 20 s.

* Sun Oct 18 2026 Mike Pengelly <mike@leadrix.io> - 0.9.2-1
- TVs watching the streaming host together play in step: the host keeps
  time for them and tells every StrmLight how far behind it to play.

* Sun Oct 18 2026 Mike Pengelly <mike@leadrix.io> - 0.9.1-1
- Only StrmLight streams from the host, from StrmQt's own ports (8330-8356);
  picture, sound and screen scale for StrmLight set in StrmQt; Settings
  reorganised; the host keeps its display settings and tray icon to itself.

* Sat Oct 17 2026 Mike Pengelly <mike@leadrix.io> - 0.9.0-1
- The streaming host: StrmQt on a hidden desktop with a Sunshine and a
  PipeWire of its own, for StrmLight on a Fire TV or Android TV; one-code
  pairing and discovery; the remote works signed out; an on-screen keyboard.

* Tue Oct 13 2026 Mike Pengelly <mike@leadrix.io> - 0.8.5-1
- YouTube and YouTube Music as destinations (Qt WebEngine), each of which
  can be switched off; trailers play in the app; a full-screen toggle.

* Mon Oct 12 2026 Mike Pengelly <mike@leadrix.io> - 0.8.0-1
- One input layer for keyboard, mouse, controller, remote and phone; library
  filters; idle pointer hiding; video upscaling.

* Thu Oct 01 2026 Mike Pengelly <mike@leadrix.io> - 0.7.5-1
- Distro compatibility release: native Fedora 43/44 package.
