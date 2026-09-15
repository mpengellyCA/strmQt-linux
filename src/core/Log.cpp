#include "Log.h"

#include <cstdio>

#include <QRegularExpression>

Q_LOGGING_CATEGORY(logApp, "strmqt.app")
Q_LOGGING_CATEGORY(logCore, "strmqt.core")
Q_LOGGING_CATEGORY(logServer, "strmqt.server")
Q_LOGGING_CATEGORY(logPlayback, "strmqt.playback")

using namespace strmqt;

namespace {

void strmqtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    const QString redacted = redactSensitiveText(msg);
    const QString formatted = qFormatLogMessage(type, context, redacted);
    fprintf(stderr, "%s\n", formatted.toLocal8Bit().constData());
    fflush(stderr);
}

} // namespace

namespace strmqt {

QString redactSensitiveText(const QString &text)
{
    static const QRegularExpression queryCredential(
        QStringLiteral(
            R"(((?:api(?:_|%5f)key|access(?:_|%5f)token|x(?:-|%2d)emby(?:-|%2d)token|token|authorization|pw|password)(?:=|%3d))([^&\s]*?)(?=(?:&|%26|\s|$)))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression headerCredential(
        QStringLiteral(
            R"(((?:authorization|x(?:-|%2d)emby(?:-|%2d)token)\s*:\s*(?:bearer\s+)?)([^\s,;]+))"),
        QRegularExpression::CaseInsensitiveOption);

    QString redacted = text;
    redacted.replace(queryCredential, QStringLiteral("\\1<redacted>"));
    redacted.replace(headerCredential, QStringLiteral("\\1<redacted>"));
    return redacted;
}

void initLogging()
{
    qSetMessagePattern(QStringLiteral("%{time hh:mm:ss.zzz} %{category} %{type}: %{message}"));
    qInstallMessageHandler(strmqtMessageHandler);
}

} // namespace strmqt
