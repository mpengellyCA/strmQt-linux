#pragma once

#include <QRegularExpression>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

// Addresses inside YouTube's TV interface (ui/youtube/YouTubeView.qml).
//
// The server hands out ordinary YouTube links — a trailer is
// "https://www.youtube.com/watch?v=…" — and the TV interface has its own
// address for the same video. These are the only two things the app ever asks
// YouTube for by address: one video, and a search. Everything else the user
// reaches by walking YouTube's own interface.
namespace strmqt::youtube {

// The video a link points at, or empty: not YouTube, or not a single video.
inline QString videoId(const QUrl &url)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    if (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))
        return {};
    QString host = url.host().toLower();
    for (const char *prefix : {"www.", "m.", "music."}) {
        if (host.startsWith(QLatin1String(prefix))) {
            host.remove(0, int(qstrlen(prefix)));
            break;
        }
    }
    const QString path = url.path();
    QString id;
    if (host == QLatin1String("youtu.be")) {
        id = path.section(QLatin1Char('/'), 1, 1);
    } else if (host == QLatin1String("youtube.com")
               || host == QLatin1String("youtube-nocookie.com")) {
        if (path == QLatin1String("/watch")) {
            id = QUrlQuery(url).queryItemValue(QStringLiteral("v"));
        } else {
            for (const char *lead : {"/embed/", "/shorts/", "/v/", "/live/"}) {
                if (path.startsWith(QLatin1String(lead))) {
                    id = path.mid(int(qstrlen(lead))).section(QLatin1Char('/'), 0, 0);
                    break;
                }
            }
        }
    }
    return valid.match(id).hasMatch() ? id : QString();
}

inline QUrl watchAddress(const QString &videoId)
{
    return QUrl(QStringLiteral("https://www.youtube.com/tv#/watch?v=") + videoId);
}

// Empty for a query with nothing in it.
inline QUrl searchAddress(const QString &query)
{
    const QString text = query.simplified();
    if (text.isEmpty())
        return {};
    return QUrl(QStringLiteral("https://www.youtube.com/tv#/search?q=")
                + QString::fromLatin1(QUrl::toPercentEncoding(text)));
}

} // namespace strmqt::youtube
