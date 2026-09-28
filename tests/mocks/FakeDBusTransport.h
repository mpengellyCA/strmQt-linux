#pragma once

#include "platform/secrets/DBusTransport.h"

#include <QDBusMessage>
#include <QList>
#include <QPointer>

#include <utility>

namespace strmqt::test {

// Records every D-Bus call a backend makes and lets the test answer it later,
// in the "hold, then complete" style of FakeSecretsStore. No bus is involved:
// replies are built with QDBusMessage::createReply from plain QVariants, so
// backends must accept both QDBusArgument and plain values (fromDBus).
//
// A callback may make further calls, which append to `calls` (and may grow
// `subscriptions`), so nothing is invoked through a reference into either list.
class FakeDBusTransport final : public secrets::DBusTransport
{
public:
    struct Call
    {
        QDBusMessage message;
        QPointer<QObject> context;
        Done done;
        bool answered = false;
    };
    struct Subscription
    {
        QString service, path, interface, name;
        QPointer<QObject> context;
        SignalHandler handler;
    };

    bool busConnected = true;
    QList<Call> calls;
    QList<Subscription> subscriptions;

    bool connected() const override { return busConnected; }
    void call(const QDBusMessage &message, QObject *context, Done done) override
    {
        calls.append({message, context, std::move(done)});
    }
    bool connectSignal(const QString &service, const QString &path, const QString &interface,
                       const QString &name, QObject *context, SignalHandler handler) override
    {
        subscriptions.append({service, path, interface, name, context, std::move(handler)});
        return true;
    }

    // The oldest unanswered call; tests assert its member before answering.
    int pending() const
    {
        for (int i = 0; i < calls.size(); ++i)
            if (!calls[i].answered)
                return i;
        return -1;
    }
    const QDBusMessage &next() const { return calls[pending()].message; }

    void reply(const QVariantList &arguments)
    {
        const int index = pending();
        calls[index].answered = true;
        const Done done = calls[index].done;
        const QDBusMessage message = calls[index].message;
        const bool alive = !calls[index].context.isNull();
        if (alive)
            done(message.createReply(arguments));
    }
    void replyError(const QString &name, const QString &text = QStringLiteral("fake"))
    {
        const int index = pending();
        calls[index].answered = true;
        const Done done = calls[index].done;
        const QDBusMessage message = calls[index].message;
        const bool alive = !calls[index].context.isNull();
        if (alive)
            done(message.createErrorReply(name, text));
    }
    void emitSignal(const QString &path, const QString &interface, const QString &name,
                    const QVariantList &arguments)
    {
        QDBusMessage signal = QDBusMessage::createSignal(path, interface, name);
        signal.setArguments(arguments);
        const QList<Subscription> current = subscriptions;
        for (const Subscription &s : current)
            if (s.context && s.path == path && s.interface == interface && s.name == name)
                s.handler(signal);
    }
};

} // namespace strmqt::test
