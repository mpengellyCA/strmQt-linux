# StrmQt RPM packaging (spec 2026-09-27 §7.3). Built in, and for, one Fedora
# release's container by scripts/ci/package-rpm.sh; not a Fedora-review spec.
Name:           strmqt
Version:        0.7.5
Release:        1%{?dist}
Summary:        Couch-first Emby client for the Linux desktop
License:        GPL-3.0-or-later AND OFL-1.1
URL:            https://github.com/mpengellyCA/strmQt-linux
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++ cmake ninja-build pkgconf git-core
BuildRequires:  qt6-rpm-macros
BuildRequires:  qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel
BuildRequires:  qt6-qtwebsockets-devel qt6-qtsvg-devel
BuildRequires:  mpv-devel vlc-devel SDL3-devel openssl-devel
# Runtime pieces for %%check's ctest and for the C(fedora-*) self-test (deps.sh installs these via dnf builddep):
BuildRequires:  qt6-qtsvg qt6-qtwayland vlc-plugins-base vlc-plugin-ffmpeg ffmpeg-free mesa-dri-drivers

# The binary imports a Qt_6_PRIVATE_API symbol and carries qmlcachegen code
# for exactly this Qt: pin it. Fedora rebases Qt inside a release, so this
# holds the Qt update back until a rebuilt StrmQt exists (README).
Requires:       qt6-qtdeclarative%{?_isa} = %{_qt6_version}
Requires:       qt6-qtsvg%{?_isa} qt6-qtwayland%{?_isa}
Requires:       vlc-plugins-base vlc-plugin-ffmpeg
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
%license COPYING assets/fonts/OFL-*.txt
%doc README.md
%{_bindir}/strmqt
%{_bindir}/strmqt-cli
%{_datadir}/applications/ca.mikesdev.StrmQt.desktop
%{_metainfodir}/ca.mikesdev.StrmQt.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/ca.mikesdev.StrmQt.*

%changelog
* Sun Sep 27 2026 Mike Pengelly <mike@leadrix.io> - 0.7.5-1
- Distro compatibility release: native Fedora 43/44 package.
