#pragma once

#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(logApp)
Q_DECLARE_LOGGING_CATEGORY(logCore)
Q_DECLARE_LOGGING_CATEGORY(logServer)
Q_DECLARE_LOGGING_CATEGORY(logPlayback)

namespace strmqt {

// Installs the message pattern used across the app. Call once, before any logging.
void initLogging();

// External libraries may include complete request URLs or headers in diagnostics.
// Keep credential stripping at the boundary where those strings enter our logs.
QString redactSensitiveText(const QString &text);

} // namespace strmqt
