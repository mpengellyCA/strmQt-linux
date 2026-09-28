#pragma once

#include "SecretBackend.h"

#include <QPointer>
#include <QVariantList>

class QDBusMessage;

namespace strmqt::secrets {

// KWallet over its own D-Bus protocol (org.kde.KWallet), no KF link dependency.
// Generation 6 is org.kde.kwalletd6 at /modules/kwalletd6 (Plasma 6, the wire
// format StrmQt has always used); generation 5 is the same protocol at
// org.kde.kwalletd5 and /modules/kwalletd5 (Plasma 5.27). Secrets live in the
// wallet's "StrmQt" folder, keyed exactly as SecretsStore is asked.
class KWalletBackend final : public SecretBackend
{
public:
    KWalletBackend(int generation, DBusTransport &transport, QObject *context); // 5 or 6

    QString name() const override;
    void prepare(Callback done) override;
    void open(Callback done) override;
    void write(const QString &key, const QString &value, Callback done) override;
    void read(const QString &key, Callback done) override;
    void remove(const QString &key, Callback done) override;

private:
    using Handler = std::function<void(const QDBusMessage &reply)>;
    void send(const QString &method, const QVariantList &arguments, Handler handler);

    int m_generation;
    DBusTransport &m_transport;
    QPointer<QObject> m_context;
    QString m_service;
    QString m_path;
    QString m_walletName;
    int m_handle = -1;
    // Replies can outlive this backend (SecretsStore swaps backends while the
    // context lives on); they check this token before touching members.
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
};

} // namespace strmqt::secrets
