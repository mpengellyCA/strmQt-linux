#include "DBusTransport.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QObject>
#include <QPointer>

#include <algorithm>
#include <array>
#include <utility>

namespace strmqt::secrets {

namespace {

// Holds a signal handler for as long as its context lives (it is the context's
// child). QDBusConnection::connect's string overload needs a real slot.
class SignalReceiver final : public QObject
{
    Q_OBJECT

public:
    SignalReceiver(DBusTransport::SignalHandler handler, QObject *parent)
        : QObject(parent), m_handler(std::move(handler))
    {
    }

public slots:
    void deliver(const QDBusMessage &message) { m_handler(message); }

private:
    DBusTransport::SignalHandler m_handler;
};

} // namespace

bool SessionBusTransport::connected() const
{
    return QDBusConnection::sessionBus().isConnected();
}

void SessionBusTransport::call(const QDBusMessage &message, QObject *context, Done done,
                               int timeoutMs)
{
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(message, timeoutMs), context);
    const QPointer<QObject> self(context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
                     [self, watcher, done = std::move(done)]() {
                         const QDBusMessage reply = watcher->reply();
                         // Future settlement below is a reentrancy point: a
                         // context-free continuation may synchronously delete
                         // the store and its children. Detach and retire the
                         // watcher before invoking any completion, then never
                         // touch its raw pointer again.
                         watcher->setParent(QCoreApplication::instance());
                         watcher->deleteLater();
                         if (self)
                             done(reply);
                     });
}

bool SessionBusTransport::connectSignal(const QString &service, const QString &path,
                                        const QString &interface, const QString &name,
                                        QObject *context, SignalHandler handler)
{
    auto *receiver = new SignalReceiver(std::move(handler), context);
    const bool connected = QDBusConnection::sessionBus().connect(
        service, path, interface, name, receiver, SLOT(deliver(QDBusMessage)));
    if (!connected)
        delete receiver;
    return connected;
}

bool isUnavailableError(const QDBusMessage &reply)
{
    if (reply.type() != QDBusMessage::ErrorMessage)
        return false;
    static const std::array<QString, 6> kUnavailable = {
        QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown"),
        QStringLiteral("org.freedesktop.DBus.Error.NoReply"),
        QStringLiteral("org.freedesktop.DBus.Error.UnknownObject"),
        QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"),
        QStringLiteral("org.freedesktop.DBus.Error.NameHasNoOwner"),
        QStringLiteral("org.freedesktop.DBus.Error.Timeout"),
    };
    const QString name = reply.errorName();
    return std::find(kUnavailable.begin(), kUnavailable.end(), name) != kUnavailable.end();
}

QString applicationId()
{
    const QString name = QCoreApplication::applicationName();
    return name.isEmpty() ? QStringLiteral("strmqt") : name;
}

} // namespace strmqt::secrets

#include "DBusTransport.moc"
