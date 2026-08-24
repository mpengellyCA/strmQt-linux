#include "PowerInhibit.h"

#include "core/Log.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QPointer>

namespace strmqt {

namespace {

struct Endpoint
{
    QString service;
    QString path;
    QString interface;
};

Endpoint endpointFor(PowerInhibit::Backend backend)
{
    if (backend == PowerInhibit::Backend::ScreenSaver) {
        return {QStringLiteral("org.freedesktop.ScreenSaver"),
                QStringLiteral("/org/freedesktop/ScreenSaver"),
                QStringLiteral("org.freedesktop.ScreenSaver")};
    }
    return {QStringLiteral("org.freedesktop.PowerManagement"),
            QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"),
            QStringLiteral("org.freedesktop.PowerManagement.Inhibit")};
}

QString backendName(PowerInhibit::Backend backend)
{
    return endpointFor(backend).service;
}

void releaseCookie(PowerInhibit::Backend backend, quint32 cookie)
{
    const Endpoint endpoint = endpointFor(backend);
    QDBusMessage message = QDBusMessage::createMethodCall(
        endpoint.service, endpoint.path, endpoint.interface, QStringLiteral("UnInhibit"));
    message << cookie;
    auto *watcher =
        new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message));
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
                     [watcher, backend, cookie] {
                         if (watcher->isError()) {
                             qCDebug(logApp) << "could not release" << backendName(backend)
                                             << "inhibit cookie" << cookie << ':'
                                             << watcher->error().message();
                         }
                         watcher->deleteLater();
                     });
}

} // namespace

PowerInhibit::PowerInhibit(QObject *parent) : QObject(parent) {}

PowerInhibit::~PowerInhibit()
{
    release();
}

void PowerInhibit::acquire(const QString &reason)
{
    if (m_wanted && (m_acquirePending || m_cookie.has_value()))
        return;

    m_wanted = true;
    m_acquirePending = true;
    m_reason = reason;
    const quint64 generation = ++m_generation;
    requestAcquire(Backend::ScreenSaver, reason, generation);
}

void PowerInhibit::release()
{
    if (!m_wanted && !m_acquirePending && !m_cookie.has_value())
        return;

    m_wanted = false;
    m_acquirePending = false;
    m_reason.clear();
    ++m_generation;
    if (!m_cookie.has_value())
        return;

    requestRelease(*m_backend, *m_cookie);
    m_backend.reset();
    m_cookie.reset();
}

void PowerInhibit::requestAcquire(Backend backend, const QString &reason, quint64 generation)
{
    const Endpoint endpoint = endpointFor(backend);
    QDBusMessage message = QDBusMessage::createMethodCall(
        endpoint.service, endpoint.path, endpoint.interface, QStringLiteral("Inhibit"));
    message << QCoreApplication::applicationName() << reason;
    auto *watcher =
        new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message));
    const QPointer<PowerInhibit> self(this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
                     [self, watcher, backend, generation] {
                         const QDBusPendingReply<quint32> reply = *watcher;
                         if (self) {
                             self->completeAcquire(backend, generation, reply.isValid(),
                                                   reply.isValid() ? reply.value() : 0,
                                                   reply.isValid() ? QString()
                                                                   : reply.error().message());
                         } else if (reply.isValid()) {
                             // The owner can disappear while a desktop service
                             // is still answering. Do not orphan the late cookie.
                             releaseCookie(backend, reply.value());
                         }
                         watcher->deleteLater();
                     });
}

void PowerInhibit::requestRelease(Backend backend, quint32 cookie)
{
    releaseCookie(backend, cookie);
}

void PowerInhibit::completeAcquire(Backend backend, quint64 generation, bool success,
                                   quint32 cookie, const QString &error)
{
    if (generation != m_generation || !m_wanted) {
        if (success)
            requestRelease(backend, cookie);
        return;
    }

    if (!success) {
        if (backend == Backend::ScreenSaver) {
            requestAcquire(Backend::PowerManagement, m_reason, generation);
            return;
        }
        m_acquirePending = false;
        qCInfo(logApp) << "display inhibit unavailable:" << error;
        return;
    }

    m_acquirePending = false;
    m_backend = backend;
    m_cookie = cookie;
    qCDebug(logApp) << "display inhibited through" << backendName(backend) << "cookie" << cookie;
}

} // namespace strmqt
