#include "PowerInhibit.h"

#include "core/Log.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

namespace strmqt {

namespace {
const auto kService = QStringLiteral("org.freedesktop.ScreenSaver");
const auto kPath = QStringLiteral("/org/freedesktop/ScreenSaver");
const auto kInterface = QStringLiteral("org.freedesktop.ScreenSaver");
} // namespace

PowerInhibit::PowerInhibit(QObject *parent) : QObject(parent) {}

PowerInhibit::~PowerInhibit()
{
    release();
}

void PowerInhibit::acquire(const QString &reason)
{
    if (m_cookie != 0)
        return;

    QDBusInterface screensaver(kService, kPath, kInterface, QDBusConnection::sessionBus());
    if (!screensaver.isValid()) {
        qCInfo(logApp) << "screensaver inhibit unavailable (no" << kService << ")";
        return;
    }

    const QDBusReply<quint32> cookie =
        screensaver.call(QStringLiteral("Inhibit"), QCoreApplication::applicationName(), reason);
    if (cookie.isValid()) {
        m_cookie = cookie.value();
        qCDebug(logApp) << "screensaver inhibited, cookie" << m_cookie;
    }
}

void PowerInhibit::release()
{
    if (m_cookie == 0)
        return;

    QDBusInterface screensaver(kService, kPath, kInterface, QDBusConnection::sessionBus());
    if (screensaver.isValid())
        screensaver.call(QStringLiteral("UnInhibit"), m_cookie);
    m_cookie = 0;
}

} // namespace strmqt
