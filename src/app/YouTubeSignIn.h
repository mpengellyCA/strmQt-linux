#pragma once

#include "app/FirefoxCookies.h"

#include <QObject>
#include <QVariantList>

// Signing in to YouTube Music by borrowing a browser's session
// (FirefoxCookies.h on why it cannot be done in the tab itself).
//
// Settings lists the places a sign-in can be copied from and asks for one;
// the shell, which owns the web view, hands over its profile; this copies
// the cookies in. It is the only code that touches both the browser's files
// and the web engine's, and it does so only when asked.
class YouTubeSignIn : public QObject
{
    Q_OBJECT
    // [{ label, signedIn, lastUsed }], in the order `request` indexes.
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
    // What the last request came to, in words. Empty before the first.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool failed READ failed NOTIFY statusChanged)

public:
    explicit YouTubeSignIn(QObject *parent = nullptr);

    QVariantList sources() const;
    QString status() const { return m_status; }
    bool failed() const { return m_failed; }

    // Looks at the browser profiles again.
    Q_INVOKABLE void refresh();
    // Settings: copy from sources[index] / forget the sign-in. The shell
    // answers with importInto / clear and its view's profile.
    Q_INVOKABLE void request(int index);
    Q_INVOKABLE void requestSignOut();
    // `profile` is a WebEngineProfile. What it held is replaced, so copying
    // from another source is how the account is changed.
    Q_INVOKABLE void importInto(QObject *profile, int index);
    Q_INVOKABLE void clear(QObject *profile);

signals:
    void sourcesChanged();
    void statusChanged();
    void importRequested(int index);
    void signOutRequested();
    // The profile's cookies changed: the page should be loaded again.
    void changed();

private:
    void setStatus(const QString &status, bool failed);

    QList<strmqt::firefox::Source> m_sources;
    QString m_status;
    bool m_failed = false;
};
