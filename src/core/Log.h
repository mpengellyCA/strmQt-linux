#pragma once

#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(logApp)
Q_DECLARE_LOGGING_CATEGORY(logCore)
Q_DECLARE_LOGGING_CATEGORY(logServer)
Q_DECLARE_LOGGING_CATEGORY(logPlayback)
// Every key the window receives, with its native codes: how an unknown remote
// button is identified. Off unless asked for (debug is below its default
// threshold): QT_LOGGING_RULES="strmqt.input.keys.debug=true".
Q_DECLARE_LOGGING_CATEGORY(logKeys)

namespace strmqt {

// Installs the message pattern and redaction message handler used across the app. Call once, before any logging.
void initLogging();

// External libraries may include complete request URLs or headers in diagnostics.
// Keep credential stripping at the boundary where those strings enter our logs.
QString redactSensitiveText(const QString &text);

} // namespace strmqt
