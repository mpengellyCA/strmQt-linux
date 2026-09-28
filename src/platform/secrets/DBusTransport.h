#pragma once

#include <QDBusMessage>
#include <QString>

#include <functional>

class QObject;

namespace strmqt::secrets {

// QtDBus's own default reply timeout (25 s).
inline constexpr int kDefaultTimeoutMs = -1;
// For calls that wait on the user: KWallet's open answers only once the unlock
// dialog is answered, and a slow user must not look like a vanished daemon.
inline constexpr int kInteractiveTimeoutMs = 5 * 60 * 1000;

// The one way the secret backends reach D-Bus (spec 2026-09-27 §6.5). Production
// uses the session bus; tests inject tests/mocks/FakeDBusTransport.h and answer
// each recorded message by hand, so no unit test needs a bus.
class DBusTransport
{
public:
    using Done = std::function<void(const QDBusMessage &reply)>;
    using SignalHandler = std::function<void(const QDBusMessage &signal)>;
    virtual ~DBusTransport() = default;
    virtual bool connected() const = 0;
    // `done` runs once, only while `context` is alive. `timeoutMs` is the reply
    // timeout; kDefaultTimeoutMs keeps QtDBus's default.
    virtual void call(const QDBusMessage &message, QObject *context, Done done,
                      int timeoutMs = kDefaultTimeoutMs) = 0;
    // `handler` runs for each matching signal while `context` is alive.
    virtual bool connectSignal(const QString &service, const QString &path,
                               const QString &interface, const QString &name, QObject *context,
                               SignalHandler handler) = 0;
};

class SessionBusTransport final : public DBusTransport
{
public:
    bool connected() const override;
    void call(const QDBusMessage &message, QObject *context, Done done,
              int timeoutMs = kDefaultTimeoutMs) override;
    bool connectSignal(const QString &service, const QString &path, const QString &interface,
                       const QString &name, QObject *context, SignalHandler handler) override;
};

// ServiceUnknown, NoReply, UnknownObject, UnknownMethod, NameHasNoOwner, Timeout (spec §6.2).
bool isUnavailableError(const QDBusMessage &reply);

// The application id the keyrings show the user when they ask for access.
QString applicationId();

} // namespace strmqt::secrets
