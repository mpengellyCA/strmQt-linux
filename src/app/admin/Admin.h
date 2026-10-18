#pragma once

#include "platform/streaming/StreamingHost.h"

#include <QString>
#include <QStringList>

class QIODevice;
class QTextStream;

namespace strmqt {
class Settings;
}

// `strmqt admin …`: the streaming host's administration from a terminal, for
// a host nobody sits at — a mini PC, a container — and for strmqt-headless,
// whose `strmqtctl` runs these over SSH or `docker compose exec`.
//
// No window and no display: it runs before any GUI exists (main.cpp). What a
// running host's StrmQt must do itself — pairing a StrmLight, taking a
// YouTube Music sign-in into its web view — goes to it over its HostLink;
// the rest is the settings file and the host's folder, which the running
// StrmQts read again on their own.
namespace strmqt::admin {

struct Context
{
    Settings *settings = nullptr;
    QString linkPath;
    StreamingHost::Environment host;
    QTextStream *out = nullptr;
    QTextStream *err = nullptr;
    // What `youtube-music import -` reads.
    QIODevice *in = nullptr;
};

// The exit codes.
inline constexpr int kOk = 0;
inline constexpr int kFailed = 1;
inline constexpr int kUsage = 2;

// `arguments` are those after "admin". Needs a running QCoreApplication for
// the commands that wait (host, stream --height).
int run(const QStringList &arguments, Context &context);

// main()'s, for `strmqt admin …`.
int main(int argc, char **argv);

QString usage();

} // namespace strmqt::admin
