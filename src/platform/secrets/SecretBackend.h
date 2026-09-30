#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QObject;

namespace strmqt::secrets {

class DBusTransport;

enum class Outcome
{
    Ok,
    Unavailable,
    Refused,
    Failed
};

struct Reply
{
    Outcome outcome = Outcome::Failed;
    QString value; // read(): the secret; empty = not stored
    QString error;
};
using Callback = std::function<void(const Reply &)>;

// One keyring, driven by SecretsStore's state machine (spec 2026-09-27 §6.2).
// Unavailable means "try the next keyring"; Refused means the user said no, and
// the store goes to the vault file without asking another keyring.
class SecretBackend
{
public:
    virtual ~SecretBackend() = default;
    virtual QString name() const = 0;        // "KWallet", "KWallet 5", "Secret Service"
    virtual void prepare(Callback done) = 0; // find the wallet / collection
    virtual void open(Callback done) = 0;    // open / unlock it (may prompt)
    virtual void write(const QString &key, const QString &value, Callback done) = 0;
    virtual void read(const QString &key, Callback done) = 0;
    virtual void remove(const QString &key, Callback done) = 0;
    // Whether a write can land without first creating the keyring's store (a
    // Secret Service with no default collection answers false). The legacy
    // vault migration waits for true rather than prompting from a read.
    virtual bool hasStorage() const { return true; }
};

enum class BackendKind
{
    KWallet6,
    KWallet5,
    SecretService
};

// The ordered keyring candidates, from the session bus's owned and activatable
// names and XDG_CURRENT_DESKTOP (a colon list). An activatable KWallet counts
// only on a KDE desktop; the order is always KWallet 6, KWallet 5, Secret Service.
QList<BackendKind> chooseSecretBackends(const QStringList &owned, const QStringList &activatable,
                                        const QString &currentDesktop);

// nullptr when this build has no backend of that kind; callers skip it as unavailable.
// Every transport call the backend makes is guarded by `context`.
std::unique_ptr<SecretBackend> makeSecretBackend(BackendKind kind, DBusTransport &transport,
                                                 QObject *context);

// For the log: "KWallet6", "KWallet5", "SecretService".
QString backendKindName(BackendKind kind);

} // namespace strmqt::secrets
