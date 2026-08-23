#pragma once

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(logApp)
Q_DECLARE_LOGGING_CATEGORY(logCore)
Q_DECLARE_LOGGING_CATEGORY(logServer)
Q_DECLARE_LOGGING_CATEGORY(logPlayback)

namespace strmqt {

// Installs the message pattern used across the app. Call once, before any logging.
void initLogging();

} // namespace strmqt
