#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace strmqt::streaming {

inline constexpr char kHostUnit[] = "strmqt-host.service";
inline constexpr char kSunshineUnit[] = "strmqt-host-sunshine.service";
// Set in the screen's unit: the StrmQt it starts is the streaming host's, and
// runs its remote locked on, on a port of its own (Settings::setStreamingHostInstance).
inline constexpr char kHostMarker[] = "STRMQT_STREAMING_HOST";

// The streaming host is two systemd user services StrmQt writes and owns:
// a headless KWin with StrmQt on it, and a Sunshine of its own that captures
// that screen. Nothing here touches a Sunshine the user runs for their desktop:
// this one has its own configuration, state, certificate and ports.
//
// This file is the text of those files and nothing else, so it can be tested
// without systemd, Sunshine or a display.
struct HostConfig
{
    // sunshine.conf, apps.json and everything Sunshine writes beside them.
    QString stateDir;
    // Absolute paths. None may contain whitespace or a quote: KWin takes the
    // session command as one string and splits it itself.
    QString strmqt;
    QString kwin;
    QString sunshine;
    QString socket = QStringLiteral("strmqt-host");
    // The screen's unit, which Sunshine's unit is tied to.
    QString hostUnit = QLatin1String(kHostUnit);
    int width = 3840;
    int height = 2160;
    // Sunshine's base port; every other port is an offset from it.
    int port = 48989;
    QString name = QStringLiteral("StrmQt");
    // Sunshine's own web page: this machine only, unless switched on.
    bool webUiOnNetwork = false;
    // KEY=VALUE pairs for the StrmQt in the session. Tests point it at a
    // scratch profile with these.
    QStringList environment;
};


QString sunshineConf(const HostConfig &config);
QByteArray appsJson();
QString hostUnit(const HostConfig &config);
QString sunshineUnit(const HostConfig &config);

// What `config` cannot be written as, in the user's words; empty when it can.
QStringList configProblems(const HostConfig &config);

// The ports a Sunshine on `base` listens on.
QList<int> tcpPorts(int base);
QList<int> udpPorts(int base);
int webUiPort(int base);

// The first base, stepping from `first`, whose every port `isFree` says is
// free; 0 when none of `tries` is. A second Sunshine on a machine that already
// runs one cannot take the defaults, and one busy port is enough to leave it
// running with no stream.
int freePortBase(const std::function<bool(int port, bool udp)> &isFree, int first = 48989,
                 int tries = 20, int step = 100);

} // namespace strmqt::streaming
