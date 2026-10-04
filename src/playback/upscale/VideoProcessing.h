#pragma once

#include <QString>
#include <QStringList>

namespace strmqt {

// What an engine does to the decoded picture on its way to the screen (spec
// 2026-10-03 §4.1). A value: PlayerBackend::setVideoProcessing() takes all of
// it in one call, so a change lands atomically rather than as a flicker of
// half-applied filters. The viewing-condition and animal-vision sub-projects
// extend this struct; they do not add engine calls of their own.
struct VideoProcessing
{
    QStringList shaderPaths; // absolute paths, in hook order
    QString scale;           // mpv `scale` filter; empty = the engine's own default
    QString cscale;          // mpv `cscale` filter; empty = the engine's own default

    bool isDefault() const { return shaderPaths.isEmpty() && scale.isEmpty() && cscale.isEmpty(); }
    friend bool operator==(const VideoProcessing &, const VideoProcessing &) = default;
};

} // namespace strmqt
