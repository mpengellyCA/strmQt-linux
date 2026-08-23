#include "Log.h"

Q_LOGGING_CATEGORY(logApp, "strmqt.app")
Q_LOGGING_CATEGORY(logCore, "strmqt.core")
Q_LOGGING_CATEGORY(logServer, "strmqt.server")
Q_LOGGING_CATEGORY(logPlayback, "strmqt.playback")

namespace strmqt {

void initLogging()
{
    qSetMessagePattern(QStringLiteral("%{time hh:mm:ss.zzz} %{category} %{type}: %{message}"));
}

} // namespace strmqt
