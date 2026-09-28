#include "SecretServiceBackend.h"

#include "core/Log.h"

#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <utility>

namespace strmqt::secrets {

QDBusArgument &operator<<(QDBusArgument &arg, const DBusSecret &secret)
{
    arg.beginStructure();
    arg << secret.session << secret.parameters << secret.value << secret.contentType;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, DBusSecret &secret)
{
    arg.beginStructure();
    arg >> secret.session >> secret.parameters >> secret.value >> secret.contentType;
    arg.endStructure();
    return arg;
}

namespace {

const auto kService = QStringLiteral("org.freedesktop.secrets");
const auto kServicePath = QStringLiteral("/org/freedesktop/secrets");
const auto kServiceInterface = QStringLiteral("org.freedesktop.Secret.Service");
const auto kCollectionInterface = QStringLiteral("org.freedesktop.Secret.Collection");
const auto kItemInterface = QStringLiteral("org.freedesktop.Secret.Item");
const auto kPromptInterface = QStringLiteral("org.freedesktop.Secret.Prompt");
const auto kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

void registerTypes()
{
    static const bool registered = [] {
        qDBusRegisterMetaType<DBusSecret>();
        qDBusRegisterMetaType<StringMap>();
        return true;
    }();
    Q_UNUSED(registered)
}

// The only attributes an item carries. The key is already the hashed
// per-account one (emby/<sha256(server+user)>/accessToken).
StringMap attributesFor(const QString &key)
{
    return {{QStringLiteral("xdg:schema"), QStringLiteral("ca.mikesdev.StrmQt.Secret")},
            {QStringLiteral("strmqt-key"), key}};
}

bool isNone(const QDBusObjectPath &path)
{
    return path.path().isEmpty() || path.path() == QLatin1String("/");
}

// Error texts name the backend and the step, never the key or the secret.
Reply outcome(Outcome kind, const QString &step, const QString &what)
{
    Reply result;
    result.outcome = kind;
    result.error = QStringLiteral("Secret Service %1: %2").arg(step, what);
    return result;
}

Reply success(const QString &value = {})
{
    Reply result;
    result.outcome = Outcome::Ok;
    result.value = value;
    return result;
}

QString errorText(const QDBusMessage &reply)
{
    const QString message = reply.errorMessage();
    return message.isEmpty() ? reply.errorName() : message;
}

Reply failure(const QString &step, const QDBusMessage &reply)
{
    return outcome(isUnavailableError(reply) ? Outcome::Unavailable : Outcome::Failed, step,
                   errorText(reply));
}

// For a call that may wait on the user (an unlock, a prompt): a missing reply
// means a slow user, not a vanished daemon, so NoReply / Timeout fail (the vault,
// no second keyring prompt). Only a daemon that is really gone is unavailable.
Reply interactiveFailure(const QString &step, const QDBusMessage &reply)
{
    const QString name = reply.errorName();
    const bool gone = name == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown") ||
                      name == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner") ||
                      name == QLatin1String("org.freedesktop.DBus.Error.UnknownObject");
    return outcome(gone ? Outcome::Unavailable : Outcome::Failed, step, errorText(reply));
}

Reply malformed(const QString &step)
{
    return outcome(Outcome::Failed, step, QStringLiteral("unexpected reply"));
}

bool answered(const QDBusMessage &reply, int arguments)
{
    return reply.type() != QDBusMessage::ErrorMessage && reply.arguments().size() >= arguments;
}

} // namespace

SecretServiceBackend::SecretServiceBackend(DBusTransport &transport, QObject *context)
    : m_transport(transport), m_context(context)
{
    registerTypes();
}

SecretServiceBackend::~SecretServiceBackend()
{
    // A prompt's signal handler may be what is destroying this backend, so the
    // subscriptions go at the next event loop turn, not under its feet.
    for (const QPointer<QObject> &prompt : std::as_const(m_prompts))
        if (prompt)
            prompt->deleteLater();
}

QString SecretServiceBackend::name() const
{
    return QStringLiteral("Secret Service");
}

void SecretServiceBackend::send(const QString &path, const QString &interface,
                                const QString &method, const QVariantList &arguments,
                                Handler handler, int timeoutMs)
{
    QDBusMessage message = QDBusMessage::createMethodCall(kService, path, interface, method);
    message.setArguments(arguments);
    const std::weak_ptr<int> alive = m_alive;
    m_transport.call(
        message, m_context,
        [alive, handler = std::move(handler)](const QDBusMessage &reply) {
            if (!alive.expired())
                handler(reply);
        },
        timeoutMs);
}

void SecretServiceBackend::prepare(Callback done)
{
    const QString step = QStringLiteral("OpenSession");
    send(kServicePath, kServiceInterface, step,
         {QStringLiteral("plain"), QVariant::fromValue(QDBusVariant(QString()))},
         [this, step, done](const QDBusMessage &reply) {
             if (reply.type() == QDBusMessage::ErrorMessage) {
                 done(failure(step, reply));
                 return;
             }
             if (!answered(reply, 2)) {
                 done(malformed(step));
                 return;
             }
             m_session = fromDBus<QDBusObjectPath>(reply.arguments().at(1));
             if (isNone(m_session)) {
                 done(malformed(step));
                 return;
             }
             const QString alias = QStringLiteral("ReadAlias");
             send(kServicePath, kServiceInterface, alias, {QStringLiteral("default")},
                  [this, alias, done](const QDBusMessage &reply) {
                      if (reply.type() == QDBusMessage::ErrorMessage) {
                          done(failure(alias, reply));
                          return;
                      }
                      if (!answered(reply, 1)) {
                          done(malformed(alias));
                          return;
                      }
                      m_collection = fromDBus<QDBusObjectPath>(reply.arguments().first());
                      if (isNone(m_collection)) {
                          done(outcome(Outcome::Unavailable, alias,
                                       QStringLiteral("no default collection")));
                          return;
                      }
                      done(success());
                  });
         });
}

void SecretServiceBackend::open(Callback done)
{
    const QString step = QStringLiteral("Locked");
    send(m_collection.path(), kPropertiesInterface, QStringLiteral("Get"),
         {kCollectionInterface, QStringLiteral("Locked")},
         [this, step, done](const QDBusMessage &reply) {
             if (reply.type() == QDBusMessage::ErrorMessage) {
                 done(failure(step, reply));
                 return;
             }
             if (!answered(reply, 1)) {
                 done(malformed(step));
                 return;
             }
             const QVariant locked = fromDBus<QDBusVariant>(reply.arguments().first()).variant();
             if (locked.metaType() != QMetaType::fromType<bool>()) {
                 done(malformed(step));
                 return;
             }
             if (!locked.toBool()) {
                 done(success());
                 return;
             }
             unlock({m_collection}, QStringLiteral("Unlock"), done);
         });
}

void SecretServiceBackend::unlock(const QList<QDBusObjectPath> &items, const QString &step,
                                  Callback done)
{
    send(
        kServicePath, kServiceInterface, QStringLiteral("Unlock"), {QVariant::fromValue(items)},
        [this, step, done](const QDBusMessage &reply) {
            if (reply.type() == QDBusMessage::ErrorMessage) {
                done(interactiveFailure(step, reply));
                return;
            }
            if (!answered(reply, 2)) {
                done(malformed(step));
                return;
            }
            runPrompt(fromDBus<QDBusObjectPath>(reply.arguments().at(1)), step, done);
        },
        kInteractiveTimeoutMs);
}

void SecretServiceBackend::runPrompt(const QDBusObjectPath &prompt, const QString &step,
                                     Callback done)
{
    if (isNone(prompt)) {
        done(success());
        return;
    }
    qCInfo(logCore).noquote() << "secrets: Secret Service" << step << "is waiting for the user";

    // Owns the Completed subscription and the timeout; goes once the step is done.
    auto *guard = new QObject(m_context.data());
    m_prompts.removeIf([](const QPointer<QObject> &p) { return p.isNull(); });
    m_prompts.append(guard);

    // Whichever comes first finishes the step: Completed, an error from the
    // Prompt call, or the user not answering at all.
    const std::weak_ptr<int> alive = m_alive;
    auto finished = std::make_shared<bool>(false);
    const QPointer<QObject> guardPtr(guard);
    const auto finish = [alive, finished, guardPtr, done](const Reply &reply) {
        if (*finished)
            return;
        *finished = true;
        if (guardPtr)
            guardPtr->deleteLater();
        if (!alive.expired())
            done(reply);
    };

    // Only the Secret Service's own Completed counts: with no sender filter,
    // any session-bus peer could fake the user's answer.
    const bool subscribed = m_transport.connectSignal(
        kService, prompt.path(), kPromptInterface, QStringLiteral("Completed"), guard,
        [finish, step](const QDBusMessage &signal) {
            const QVariantList arguments = signal.arguments();
            if (arguments.isEmpty())
                finish(malformed(step));
            else if (arguments.first().toBool())
                finish(outcome(Outcome::Refused, step, QStringLiteral("prompt dismissed")));
            else
                finish(success());
        });
    if (!subscribed) {
        finish(outcome(Outcome::Failed, step, QStringLiteral("cannot watch the prompt")));
        return;
    }

    auto *timer = new QTimer(guard);
    timer->setSingleShot(true);
    // The keyring's dialog stays up after the step gives in, and a late answer
    // would store the secret there as well as in the vault: take it down.
    QObject::connect(timer, &QTimer::timeout, guard, [this, alive, finish, step, prompt] {
        finish(outcome(Outcome::Failed, step, QStringLiteral("prompt not answered")));
        if (!alive.expired())
            send(prompt.path(), kPromptInterface, QStringLiteral("Dismiss"), {},
                 [](const QDBusMessage &) {});
    });
    timer->start(m_promptTimeoutMs);

    send(
        prompt.path(), kPromptInterface, QStringLiteral("Prompt"), {QString()},
        [finish, step](const QDBusMessage &reply) {
            // Success only means the prompt is showing; Completed ends it.
            if (reply.type() == QDBusMessage::ErrorMessage)
                finish(interactiveFailure(step, reply));
        },
        kInteractiveTimeoutMs);
}

void SecretServiceBackend::write(const QString &key, const QString &value, Callback done)
{
    const QString step = QStringLiteral("CreateItem");
    const QVariantMap properties{
        {QStringLiteral("org.freedesktop.Secret.Item.Label"),
         QStringLiteral("StrmQt access token")},
        {QStringLiteral("org.freedesktop.Secret.Item.Attributes"),
         QVariant::fromValue(attributesFor(key))},
    };
    const DBusSecret secret{
        m_session, {}, value.toUtf8(), QStringLiteral("text/plain; charset=utf8")};
    send(
        m_collection.path(), kCollectionInterface, step,
        {properties, QVariant::fromValue(secret), true},
        [this, step, done](const QDBusMessage &reply) {
            if (reply.type() == QDBusMessage::ErrorMessage) {
                done(failure(step, reply));
                return;
            }
            if (!answered(reply, 2)) {
                done(malformed(step));
                return;
            }
            runPrompt(fromDBus<QDBusObjectPath>(reply.arguments().at(1)), step, done);
        },
        kInteractiveTimeoutMs);
}

void SecretServiceBackend::search(
    const QString &key, std::function<void(const Reply &, const QList<QDBusObjectPath> &)> done)
{
    const QString step = QStringLiteral("SearchItems");
    send(kServicePath, kServiceInterface, step, {QVariant::fromValue(attributesFor(key))},
         [this, step, done](const QDBusMessage &reply) {
             if (reply.type() == QDBusMessage::ErrorMessage) {
                 done(failure(step, reply), {});
                 return;
             }
             if (!answered(reply, 2)) {
                 done(malformed(step), {});
                 return;
             }
             const auto unlocked = fromDBus<QList<QDBusObjectPath>>(reply.arguments().at(0));
             const auto locked = fromDBus<QList<QDBusObjectPath>>(reply.arguments().at(1));
             if (locked.isEmpty()) {
                 done(success(), unlocked);
                 return;
             }
             unlock(locked, QStringLiteral("Unlock"),
                    [done, all = unlocked + locked](const Reply &result) {
                        done(result,
                             result.outcome == Outcome::Ok ? all : QList<QDBusObjectPath>{});
                    });
         });
}

void SecretServiceBackend::read(const QString &key, Callback done)
{
    const QString step = QStringLiteral("GetSecret");
    search(key, [this, step, done](const Reply &found, const QList<QDBusObjectPath> &items) {
        if (found.outcome != Outcome::Ok) {
            done(found);
            return;
        }
        // No item: an empty success, so SecretsStore consults the vault.
        if (items.isEmpty()) {
            done(success());
            return;
        }
        send(
            items.first().path(), kItemInterface, step, {QVariant::fromValue(m_session)},
            [step, done](const QDBusMessage &reply) {
                if (reply.type() == QDBusMessage::ErrorMessage) {
                    done(failure(step, reply));
                    return;
                }
                if (!answered(reply, 1)) {
                    done(malformed(step));
                    return;
                }
                const auto secret = fromDBus<DBusSecret>(reply.arguments().first());
                done(success(QString::fromUtf8(secret.value)));
            },
            kInteractiveTimeoutMs);
    });
}

void SecretServiceBackend::remove(const QString &key, Callback done)
{
    search(key, [this, done](const Reply &found, const QList<QDBusObjectPath> &items) {
        if (found.outcome != Outcome::Ok)
            done(found);
        else
            deleteItems(items, done);
    });
}

void SecretServiceBackend::deleteItems(QList<QDBusObjectPath> items, Callback done)
{
    if (items.isEmpty()) {
        done(success());
        return;
    }
    const QString step = QStringLiteral("Delete");
    const QDBusObjectPath item = items.takeFirst();
    send(
        item.path(), kItemInterface, step, {},
        [this, step, items, done](const QDBusMessage &reply) {
            if (reply.type() == QDBusMessage::ErrorMessage) {
                done(failure(step, reply));
                return;
            }
            if (!answered(reply, 1)) {
                done(malformed(step));
                return;
            }
            runPrompt(fromDBus<QDBusObjectPath>(reply.arguments().first()), step,
                      [this, items, done](const Reply &deleted) {
                          if (deleted.outcome != Outcome::Ok)
                              done(deleted);
                          else
                              deleteItems(items, done);
                      });
        },
        kInteractiveTimeoutMs);
}

} // namespace strmqt::secrets
