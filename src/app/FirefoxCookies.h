#pragma once

#include <QList>
#include <QNetworkCookie>
#include <QString>
#include <QStringList>

// Reads a sign-in out of Firefox's cookie store, for YouTube Music.
//
// Google will not let an embedded browser sign in ("This browser or app may
// not be secure"), whatever it calls itself, so the YouTube Music tab cannot
// be signed in to from inside the app. A browser the user is already signed
// in with can lend its session instead: the cookies for youtube.com and
// google.com are copied across, once, when the user asks.
//
// Firefox only: its store is a plain SQLite file. Chromium-family browsers
// encrypt theirs with a key in the keyring.
//
// Nothing here writes to Firefox's files, and nothing is read until asked:
// Settings calls sources() when its "Look in Firefox" button is pressed.
namespace strmqt::firefox {

// One place a sign-in can be copied from: a Firefox profile, or one container
// tab group inside it. Each keeps its own cookies, so each is its own set of
// signed-in Google accounts.
struct Source
{
    QString profileName;      // "default-release"
    QString containerName;    // "Work"; empty for the profile's ordinary tabs
    QString database;         // …/cookies.sqlite
    QString originAttributes; // "" or "^userContextId=2"
    bool signedIn = false;    // it holds a YouTube sign-in
    qint64 lastUsed = 0;      // seconds since the epoch; 0 when unknown

    QString label() const;
};

// Firefox profile directories under `roots` (each a directory holding a
// profiles.ini), the installation's default first.
QStringList profileDirectories(const QStringList &roots);
// Where Firefox keeps its profiles on this machine: the distribution's, the
// Snap's and the Flatpak's.
QStringList defaultRoots();

// Every source in those profiles, signed-in ones first.
QList<Source> sources(const QStringList &profileDirs);

// The cookies for youtube.com and google.com in `source`. Empty when the
// store cannot be read; `error` then says why.
QList<QNetworkCookie> cookies(const Source &source, QString *error = nullptr);

} // namespace strmqt::firefox
