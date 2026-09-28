#include "SecretBackend.h"

#include "KWalletBackend.h"

namespace strmqt::secrets {

namespace {

const auto kKWallet6Service = QStringLiteral("org.kde.kwalletd6");
const auto kKWallet5Service = QStringLiteral("org.kde.kwalletd5");
const auto kSecretService = QStringLiteral("org.freedesktop.secrets");

} // namespace

QList<BackendKind> chooseSecretBackends(const QStringList &owned, const QStringList &activatable,
                                        const QString &currentDesktop)
{
    bool kde = false;
    for (const QString &part : currentDesktop.split(QLatin1Char(':')))
        kde = kde || part.compare(QLatin1String("KDE"), Qt::CaseInsensitive) == 0;

    // Activating kwalletd on a non-KDE desktop would greet the user with a
    // "create a new wallet" dialog for a wallet they do not use (spec §6.2).
    const auto candidate = [&](const QString &service, bool mayActivate) {
        return owned.contains(service) || (mayActivate && activatable.contains(service));
    };

    QList<BackendKind> kinds;
    if (candidate(kKWallet6Service, kde))
        kinds.append(BackendKind::KWallet6);
    if (candidate(kKWallet5Service, kde))
        kinds.append(BackendKind::KWallet5);
    if (candidate(kSecretService, true))
        kinds.append(BackendKind::SecretService);
    return kinds;
}

std::unique_ptr<SecretBackend> makeSecretBackend(BackendKind kind, DBusTransport &transport,
                                                 QObject *context)
{
    switch (kind) {
    case BackendKind::KWallet6:
        return std::make_unique<KWalletBackend>(6, transport, context);
    case BackendKind::KWallet5:
        return std::make_unique<KWalletBackend>(5, transport, context);
    case BackendKind::SecretService:
        return nullptr; // Task 13
    }
    return nullptr;
}

QString backendKindName(BackendKind kind)
{
    switch (kind) {
    case BackendKind::KWallet6:
        return QStringLiteral("KWallet6");
    case BackendKind::KWallet5:
        return QStringLiteral("KWallet5");
    case BackendKind::SecretService:
        return QStringLiteral("SecretService");
    }
    return {};
}

} // namespace strmqt::secrets
