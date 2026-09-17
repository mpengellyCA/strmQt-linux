#pragma once

#include <QString>

#include "server/dto/music/MusicTypes.h"

// Display strings for the music UI. QML never formats (Crate plan, Global Constraints).
namespace strmqt::music {

QString formatDuration(qint64 ms);
QString formatRuntime(qint64 ms);
QString formatRecordCount(int count);
QString formatTrackCount(int count);
QString joinNames(const QList<NamedRef> &names);
QString coverUrl(const ImageRef &ref);

} // namespace strmqt::music
