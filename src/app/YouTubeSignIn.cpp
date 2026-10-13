#include "app/YouTubeSignIn.h"

#include <QDateTime>
#include <QUrl>
#include <QVariantMap>
#include <QWebEngineCookieStore>
#include <QtWebEngineQuick/QQuickWebEngineProfile>

using strmqt::firefox::Source;

YouTubeSignIn::YouTubeSignIn(QObject *parent) : QObject(parent) {}

QVariantList YouTubeSignIn::sources() const
{
    QVariantList list;
    for (const Source &source : m_sources) {
        list << QVariantMap {
            { QStringLiteral("label"), source.label() },
            { QStringLiteral("signedIn"), source.signedIn },
            { QStringLiteral("lastUsed"),
              source.lastUsed > 0 ? QDateTime::fromSecsSinceEpoch(source.lastUsed) : QDateTime() },
        };
    }
    return list;
}

void YouTubeSignIn::refresh()
{
    m_sources = strmqt::firefox::sources(
        strmqt::firefox::profileDirectories(strmqt::firefox::defaultRoots()));
    emit sourcesChanged();
}

void YouTubeSignIn::request(int index)
{
    if (index >= 0 && index < m_sources.size())
        emit importRequested(index);
}

void YouTubeSignIn::requestSignOut()
{
    emit signOutRequested();
}

void YouTubeSignIn::importInto(QObject *profile, int index)
{
    auto *webProfile = qobject_cast<QQuickWebEngineProfile *>(profile);
    if (!webProfile || index < 0 || index >= m_sources.size()) {
        setStatus(tr("YouTube Music is not available to sign in to."), true);
        return;
    }
    const Source source = m_sources.at(index);
    QString error;
    const QList<QNetworkCookie> cookies = strmqt::firefox::cookies(source, &error);
    if (cookies.isEmpty()) {
        setStatus(error, true);
        return;
    }
    QWebEngineCookieStore *store = webProfile->cookieStore();
    store->deleteAllCookies();
    for (const QNetworkCookie &cookie : cookies) {
        QString host = cookie.domain();
        if (host.startsWith(QLatin1Char('.')))
            host.remove(0, 1);
        store->setCookie(cookie, QUrl(QStringLiteral("https://") + host + cookie.path()));
    }
    setStatus(source.signedIn
                  ? tr("Signed in from %1.").arg(source.label())
                  : tr("Copied from %1, which is not signed in to YouTube.").arg(source.label()),
              !source.signedIn);
    emit changed();
}

void YouTubeSignIn::clear(QObject *profile)
{
    auto *webProfile = qobject_cast<QQuickWebEngineProfile *>(profile);
    if (!webProfile)
        return;
    webProfile->cookieStore()->deleteAllCookies();
    setStatus(tr("Signed out of YouTube Music."), false);
    emit changed();
}

void YouTubeSignIn::setStatus(const QString &status, bool failed)
{
    m_status = status;
    m_failed = failed;
    emit statusChanged();
}
