#include "KWalletBackend.h"

#include "DBusTransport.h"

#include <QDBusMessage>
#include <QMetaType>

#include <utility>

namespace strmqt::secrets {

namespace {

const auto kWalletInterface = QStringLiteral("org.kde.KWallet");
const auto kWalletFolder = QStringLiteral("StrmQt");

QString dbusError(const QDBusMessage &reply)
{
    const QString message = reply.errorMessage();
    return message.isEmpty() ? QStringLiteral("KWallet request failed") : message;
}

// The reply checks SecretsStore has always made: exactly one argument of the
// expected meta type, and no error.
bool singleArgument(const QDBusMessage &reply, int metaType)
{
    const QVariantList arguments = reply.arguments();
    return reply.type() != QDBusMessage::ErrorMessage && arguments.size() == 1 &&
           arguments.first().metaType().id() == metaType;
}

Reply failure(const QDBusMessage &reply)
{
    Reply result;
    result.outcome = isUnavailableError(reply) ? Outcome::Unavailable : Outcome::Failed;
    result.error = dbusError(reply);
    return result;
}

Reply success(const QString &value = {})
{
    Reply result;
    result.outcome = Outcome::Ok;
    result.value = value;
    return result;
}

// open waits on the user's unlock dialog, so a missing reply there means a slow
// user, not a vanished daemon: NoReply / Timeout / TimedOut fail (vault, no second
// keyring prompt). Only a daemon that is really gone moves on to the next keyring.
Reply openFailure(const QDBusMessage &reply)
{
    Reply result = failure(reply);
    const QString name = reply.errorName();
    const bool gone = name == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown") ||
                      name == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner") ||
                      name == QLatin1String("org.freedesktop.DBus.Error.UnknownObject");
    result.outcome = gone ? Outcome::Unavailable : Outcome::Failed;
    return result;
}

// writePassword / removeEntry answer 0 on success.
Reply zeroMeansOk(const QDBusMessage &reply)
{
    if (singleArgument(reply, QMetaType::Int) && reply.arguments().first().toInt() == 0)
        return success();
    return failure(reply);
}

} // namespace

KWalletBackend::KWalletBackend(int generation, DBusTransport &transport, QObject *context)
    : m_generation(generation), m_transport(transport), m_context(context),
      m_service(QStringLiteral("org.kde.kwalletd%1").arg(generation)),
      m_path(QStringLiteral("/modules/kwalletd%1").arg(generation))
{
}

QString KWalletBackend::name() const
{
    return m_generation == 5 ? QStringLiteral("KWallet 5") : QStringLiteral("KWallet");
}

void KWalletBackend::send(const QString &method, const QVariantList &arguments, Handler handler,
                          int timeoutMs)
{
    QDBusMessage message =
        QDBusMessage::createMethodCall(m_service, m_path, kWalletInterface, method);
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

void KWalletBackend::prepare(Callback done)
{
    send(QStringLiteral("networkWallet"), {}, [this, done](const QDBusMessage &reply) {
        if (!singleArgument(reply, QMetaType::QString)) {
            done(failure(reply));
            return;
        }
        m_walletName = reply.arguments().first().toString();
        if (m_walletName.isEmpty()) {
            Reply result;
            result.error = QStringLiteral("KWallet has no network wallet");
            done(result);
            return;
        }
        done(success());
    });
}

void KWalletBackend::open(Callback done)
{
    send(
        QStringLiteral("open"), {m_walletName, QVariant::fromValue(qlonglong(0)), applicationId()},
        [this, done](const QDBusMessage &reply) {
            if (!singleArgument(reply, QMetaType::Int)) {
                done(openFailure(reply));
                return;
            }
            const int handle = reply.arguments().first().toInt();
            if (handle < 0) {
                Reply result;
                result.outcome = Outcome::Refused;
                result.error = QStringLiteral("KWallet open refused");
                done(result);
                return;
            }
            m_handle = handle;
            done(success());
        },
        kInteractiveTimeoutMs);
}

void KWalletBackend::write(const QString &key, const QString &value, Callback done)
{
    send(QStringLiteral("writePassword"), {m_handle, kWalletFolder, key, value, applicationId()},
         [done](const QDBusMessage &reply) { done(zeroMeansOk(reply)); });
}

void KWalletBackend::read(const QString &key, Callback done)
{
    send(QStringLiteral("readPassword"), {m_handle, kWalletFolder, key, applicationId()},
         [done](const QDBusMessage &reply) {
             if (singleArgument(reply, QMetaType::QString))
                 done(success(reply.arguments().first().toString()));
             else
                 done(failure(reply));
         });
}

void KWalletBackend::remove(const QString &key, Callback done)
{
    send(QStringLiteral("removeEntry"), {m_handle, kWalletFolder, key, applicationId()},
         [done](const QDBusMessage &reply) { done(zeroMeansOk(reply)); });
}

} // namespace strmqt::secrets
