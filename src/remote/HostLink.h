#pragma once

#include <QObject>
#include <QString>

#include <functional>

class QLocalServer;

namespace strmqt {

// HostLink — how a StrmLight's code typed into the desktop StrmQt reaches the
// streaming host's StrmQt, which is the one the StrmLight pairs with (its
// remote is on its own port; Settings::setStreamingHostInstance).
//
// A local socket in the user's runtime directory, open to this user only:
// whoever can reach it could as well read the settings the paired remotes are
// kept in. One line each way: "pair 1234" and "ok" or "no".
class HostLink : public QObject
{
    Q_OBJECT

public:
    explicit HostLink(QString socketPath = defaultSocketPath(), QObject *parent = nullptr);
    ~HostLink() override;

    static QString defaultSocketPath();

    // The host's side: `take` arms the code and says whether it did.
    bool listen(std::function<bool(const QString &code)> take);
    bool isListening() const;

    // The desktop's side: whether a host is there and took the code. Waits
    // for at most `timeoutMs`, so not for the host's own event loop: never
    // call it from the process that listens.
    bool pass(const QString &code, int timeoutMs = 1000) const;

private:
    QString m_path;
    QLocalServer *m_server = nullptr;
    std::function<bool(const QString &)> m_take;
};

} // namespace strmqt
