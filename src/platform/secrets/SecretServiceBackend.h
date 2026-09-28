#pragma once

#include "DBusTransport.h"
#include "SecretBackend.h"

#include <QByteArray>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <memory>

namespace strmqt::secrets {

// org.freedesktop.Secret.Secret: (oayays) — session, parameters, value, content type.
struct DBusSecret
{
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};
QDBusArgument &operator<<(QDBusArgument &arg, const DBusSecret &secret);
const QDBusArgument &operator>>(const QDBusArgument &arg, DBusSecret &secret);

// a{ss}. QMap<QString, QString> already has a metatype; it only needs
// qDBusRegisterMetaType to marshal as a dict of strings.
using StringMap = QMap<QString, QString>;

// Reads a D-Bus reply argument whether it arrived from the bus (a QDBusArgument)
// or from a test's plain QVariant (FakeDBusTransport builds replies directly).
template<class T> T fromDBus(const QVariant &v)
{
    if (v.canConvert<QDBusArgument>())
        return qdbus_cast<T>(v.value<QDBusArgument>());
    return v.value<T>();
}

// The freedesktop Secret Service (org.freedesktop.secrets: gnome-keyring,
// KeePassXC, kwalletd6's own implementation) over QtDBus, with the "plain"
// session (spec 2026-09-27 §6.3). One item per key in the default collection,
// found by the attributes xdg:schema=ca.mikesdev.StrmQt.Secret and
// strmqt-key=<key>; the key is already the hashed per-account one, so the
// server and user never appear in the keyring.
class SecretServiceBackend final : public SecretBackend
{
public:
    SecretServiceBackend(DBusTransport &transport, QObject *context);
    ~SecretServiceBackend() override;

    QString name() const override;
    void prepare(Callback done) override;
    void open(Callback done) override;
    void write(const QString &key, const QString &value, Callback done) override;
    void read(const QString &key, Callback done) override;
    void remove(const QString &key, Callback done) override;

    // How long a prompt may stay unanswered before the step fails;
    // kInteractiveTimeoutMs unless a test shortens it.
    void setPromptTimeoutMs(int ms) { m_promptTimeoutMs = ms; }

private:
    using Handler = std::function<void(const QDBusMessage &reply)>;
    void send(const QString &path, const QString &interface, const QString &method,
              const QVariantList &arguments, Handler handler, int timeoutMs = kDefaultTimeoutMs);
    // `/` completes at once; otherwise shows the prompt and waits for Completed.
    void runPrompt(const QDBusObjectPath &prompt, const QString &step, Callback done);
    // Unlocks `items` (running the prompt if one is needed), then `done`.
    void unlock(const QList<QDBusObjectPath> &items, const QString &step, Callback done);
    // Finds the key's items: `done(reply, unlocked + locked)`, unlocking the locked ones first.
    void search(const QString &key,
                std::function<void(const Reply &, const QList<QDBusObjectPath> &)> done);
    void deleteItems(QList<QDBusObjectPath> items, Callback done);

    DBusTransport &m_transport;
    QPointer<QObject> m_context;
    QDBusObjectPath m_session;
    QDBusObjectPath m_collection;
    int m_promptTimeoutMs = kInteractiveTimeoutMs;
    // Signal subscriptions and timers of prompts still running; deleted with us.
    QList<QPointer<QObject>> m_prompts;
    // Replies can outlive this backend (SecretsStore swaps backends while the
    // context lives on); they check this token before touching members.
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
};

} // namespace strmqt::secrets

Q_DECLARE_METATYPE(strmqt::secrets::DBusSecret)
